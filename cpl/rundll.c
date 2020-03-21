/*
    rundll32.exe compatible functions for the ImDisk Virtual Disk Driver for
    Windows NT/2000/XP.

    Copyright (C) 2007 Olof Lagerkvist.

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

#include <malloc.h>

#include "..\inc\ntumapi.h"
#include "..\inc\imdisk.h"

#include "drvio.h"

#include "imdisk.rc.h"

#pragma warning(disable: 4100)
#pragma warning(disable: 4204)

extern HINSTANCE hInstance;

void
WINAPI
RunDLL_MountFile(HWND hWnd,
		 HINSTANCE hInst,
		 LPSTR lpszCmdLine,
		 int nCmdShow)
{
  int file_name_length;
  LPWSTR file_name;

  file_name_length = (int) (strlen(lpszCmdLine) + 1);
  file_name = (LPWSTR) malloc(file_name_length << 1);

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
		 (LPARAM) file_name);

  free(file_name);
}

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
  if (GetWindowsDirectory(win_dir, sizeof(win_dir) / sizeof(*win_dir)))
    {
      win_dir[(sizeof(win_dir) / sizeof(*win_dir)) - 1] = 0;
      SetCurrentDirectory(win_dir);
    }

  if (strlen(lpszCmdLine) < 2 ? TRUE : lpszCmdLine[1] != L':')
    {
      MsgBoxPrintF(hWnd, MB_ICONSTOP, L"ImDisk Virtual Disk Driver",
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
      MsgBoxPrintF(hWnd, MB_ICONSTOP, L"ImDisk Virtual Disk Driver",
		   L"Unsupported drive type: '%1!hs!'", lpszCmdLine);
      return;
    }

  // If user right-clicked in Windows Explorer the drive we are dismounting is
  // the current directory in this process. Change to Windows directory.
  if (GetWindowsDirectory(file_name, sizeof(file_name) / sizeof(*file_name)))
    {
      file_name[(sizeof(file_name) / sizeof(*file_name)) - 1] = 0;
      SetCurrentDirectory(file_name);
    }
  file_name[0] = 0;

  if (strlen(lpszCmdLine) < 2 ? TRUE : lpszCmdLine[1] != L':')
    {
      MsgBoxPrintF(hWnd, MB_ICONSTOP, L"ImDisk Virtual Disk Driver",
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

/*
  HANDLE hImage;
  DWORD dwRet;
  HWND hWndStatus;
  BOOL bCancelFlag = FALSE;
  OPENFILENAME_NT4 ofn = { sizeof ofn };
  WCHAR dlg_title[] = L"Save drive  : to image file";
  dlg_title[11] = lpszCmdLine[0];

  ofn.hwndOwner = hWnd;
  ofn.lpstrFilter = L"Image files (*.img)\0*.img\0";
  ofn.lpstrFile = file_name;
  ofn.nMaxFile = sizeof(file_name)/sizeof(*file_name);
  ofn.lpstrTitle = dlg_title;
  ofn.Flags = OFN_EXPLORER | OFN_LONGNAMES | OFN_OVERWRITEPROMPT |
    OFN_PATHMUSTEXIST;
  ofn.lpstrDefExt = L"img";

  if (!GetSaveFileName((LPOPENFILENAMEW) &ofn))
    {
      CloseHandle(hDev);
      return;
    }

  hImage = CreateFile(ofn.lpstrFile,
		      GENERIC_WRITE,
		      FILE_SHARE_READ | FILE_SHARE_DELETE,
		      NULL,
		      CREATE_ALWAYS,
		      FILE_ATTRIBUTE_NORMAL,
		      NULL);

  if (hImage == INVALID_HANDLE_VALUE)
    {
      MsgBoxLastError(hWnd, L"Cannot create image file:");
      CloseHandle(hDev);
      return;
    }

  if (DeviceIoControl(hDev, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &dwRet, NULL))
    DeviceIoControl(hDev, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &dwRet,
		    NULL);
  else
    if (MsgBoxPrintF(hWnd, MB_ICONEXCLAMATION | MB_OKCANCEL | MB_DEFBUTTON2,
		     L"ImDisk Virtual Disk Driver",
		     L"Cannot lock drive '%1!hs!'. It may be in use by "
		     L"another program. Do you want to continue anyway?",
		     lpszCmdLine) != IDOK)
      {
	CloseHandle(hDev);
	CloseHandle(hImage);
	return;
      }

  hWndStatus = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_DLG_STATUS),
				 hWnd, StatusDlgProc, (LPARAM) &bCancelFlag);


  SetDlgItemText(hWndStatus, IDC_STATUS_MSG, L"Saving image file...");

  if (ImDiskSaveImageFile(hDev, hImage, 0, &bCancelFlag))
    {
      DestroyWindow(hWndStatus);
      CloseHandle(hDev);

      if (GetFileSize(hImage, NULL) == 0)
	{
	  DeleteFile(ofn.lpstrFile);
	  CloseHandle(hImage);

	  MsgBoxPrintF(hWnd, MB_ICONEXCLAMATION, L"ImDisk Virtual Disk Driver",
		       L"The contents of drive '%1!hs!' could not be saved. "
		       L"Check that the drive contains a supported "
		       L"filesystem.", lpszCmdLine);

	  return;
	}

      CloseHandle(hImage);
      MsgBoxPrintF(hWnd, MB_ICONINFORMATION,
		   L"ImDisk Virtual Disk Driver",
		   L"Successfully saved the contents of drive '%1!hs!' to "
		   L"image file '%2'.", lpszCmdLine, ofn.lpstrFile);
      return;
    }

  MsgBoxLastError(hWnd, L"Error saving image:");

  DestroyWindow(hWndStatus);
  CloseHandle(hDev);

  if (GetFileSize(hImage, NULL) == 0)
    DeleteFile(ofn.lpstrFile);

  CloseHandle(hImage);
}
*/
