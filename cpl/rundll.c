/*
rundll32.exe compatible functions for the ImDisk Virtual Disk Driver for
Windows NT/2000/XP.

Copyright (C) 2007-2015 Olof Lagerkvist.

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

#include <windows.h>
#include <winioctl.h>
#include <commdlg.h>

#include <stdlib.h>

#include "..\inc\ntumapi.h"
#include "..\inc\imdisk.h"

#include "drvio.h"

#include "imdisk.rc.h"
#include "resource.h"

#pragma warning(disable: 4100)
#pragma warning(disable: 4204)

extern HINSTANCE hInstance;

IMDISK_API
void
WINAPI
RunDLL_MountFileW(HWND hWnd,
HINSTANCE hInst,
LPWSTR lpszCmdLine,
int nCmdShow)
{
    DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_NEWDIALOG), hWnd, NewDlgProc,
        (LPARAM)lpszCmdLine);
}

IMDISK_API
void
WINAPI
RunDLL_MountFile(HWND hWnd,
HINSTANCE hInst,
LPSTR lpszCmdLine,
int nCmdShow)
{
    int file_name_length;
    LPWSTR file_name;

    file_name_length = (int)(strlen(lpszCmdLine) + 1);
    file_name = (LPWSTR)malloc(((size_t)file_name_length) << 1);

    if (file_name == NULL)
    {
        MessageBox(hWnd,
            L"Memory allocation error.",
            L"ImDisk Virtual Disk Driver",
            MB_ICONSTOP);
        return;
    }

    if (MultiByteToWideChar(CP_ACP, 0,
        lpszCmdLine, -1,
        file_name, file_name_length) == 0)
    {
        MsgBoxLastError(hWnd, L"Invalid filename:");
        return;
    }

    DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_NEWDIALOG), hWnd, NewDlgProc,
        (LPARAM)file_name);

    free(file_name);
}

IMDISK_API
void
WINAPI
RunDLL_RemoveDevice(HWND hWnd,
HINSTANCE hInst,
LPSTR lpszCmdLine,
int nCmdShow)
{
    WCHAR win_dir[MAX_PATH + 1] = L"";
    WCHAR mount_point[3] = L" :";
    HWND hWndStatus;

    // If user right-clicked in Windows Explorer the drive we are dismounting is
    // the current directory in this process. Change to Windows directory.
    if (GetWindowsDirectory(win_dir, _countof(win_dir)))
    {
        win_dir[_countof(win_dir) - 1] = 0;
        SetCurrentDirectory(win_dir);
    }

    if (strlen(lpszCmdLine) < 2 ? TRUE : lpszCmdLine[1] != L':')
    {
        ImDiskMsgBoxPrintF(hWnd, MB_ICONSTOP, L"ImDisk Virtual Disk Driver",
            L"Unsupported mount point: '%1!hs!'", lpszCmdLine);
        return;
    }

    mount_point[0] = lpszCmdLine[0];

    hWndStatus = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_DLG_STATUS), hWnd,
        StatusDlgProc);

    ImDiskRemoveDevice(GetDlgItem(hWndStatus, IDC_STATUS_MSG),
        0,
        mount_point);

    Sleep(100);

    DestroyWindow(hWndStatus);
}

IMDISK_API
void
WINAPI
RunDLL_SaveImageFile(HWND hWnd,
HINSTANCE hInst,
LPSTR lpszCmdLine,
int nCmdShow)
{
    WCHAR file_name[MAX_PATH + 1] = L"";
    WCHAR mount_point[] = L"\\\\.\\ :";
    HANDLE hDev;
    BOOL bIsCdRomType = FALSE;

    switch (GetDriveTypeA(lpszCmdLine))
    {
    case DRIVE_CDROM:
        bIsCdRomType = TRUE;
        break;

    case DRIVE_REMOTE:
        ImDiskMsgBoxPrintF(hWnd, MB_ICONSTOP, L"ImDisk Virtual Disk Driver",
            L"Unsupported drive type: '%1!hs!'", lpszCmdLine);
        return;
    }

    // If user right-clicked in Windows Explorer the drive we are dismounting is
    // the current directory in this process. Change to Windows directory.
    if (GetWindowsDirectory(file_name, _countof(file_name)))
    {
        file_name[_countof(file_name) - 1] = 0;
        SetCurrentDirectory(file_name);
    }
    file_name[0] = 0;

    if (strlen(lpszCmdLine) < 2 ? TRUE : lpszCmdLine[1] != L':')
    {
        ImDiskMsgBoxPrintF(hWnd, MB_ICONSTOP, L"ImDisk Virtual Disk Driver",
            L"Unsupported mount point: '%1!hs!'", lpszCmdLine);
        return;
    }

    mount_point[4] = lpszCmdLine[0];

    hDev = CreateFile(mount_point,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);

    if (hDev == INVALID_HANDLE_VALUE)
    {
        MsgBoxLastError(hWnd, L"Cannot open drive for direct access:");
        return;
    }

    ImDiskSaveImageFileInteractive(hDev, hWnd, 0, bIsCdRomType);

    CloseHandle(hDev);
}
