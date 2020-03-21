/*
    Control Panel Applet for the ImDisk Virtual Disk Driver for
    Windows NT/2000/XP.

    Copyright (C) 2007-2008 Olof Lagerkvist.

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
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cpl.h>

#include <stdio.h>
#include <malloc.h>

#include "..\inc\ntumapi.h"
#include "..\inc\imdisk.h"

#include "drvio.h"

#include "imdisk.rc.h"

/// Macros for "human readable" file sizes.
#define _1KB  (1ui64<<10)
#define _1MB  (1ui64<<20)
#define _1GB  (1ui64<<30)
#define _1TB  (1ui64<<40)

#define _B(n)  ((double)(n))
#define _KB(n) ((double)(n)/_1KB)
#define _MB(n) ((double)(n)/_1MB)
#define _GB(n) ((double)(n)/_1GB)
#define _TB(n) ((double)(n)/_1TB)

#define _h(n) ((n)>=_1TB ? _TB(n) : (n)>=_1GB ? _GB(n) :	\
	       (n)>=_1MB ? _MB(n) : (n)>=_1KB ? _KB(n) : (n))
#define _p(n) ((n)>=_1TB ? L"TB" : (n)>=_1GB ? L"GB" :			\
	       (n)>=_1MB ? L"MB" : (n)>=_1KB ? L"KB": \
	       (n)==1 ? L"byte" : L"bytes")

extern "C" HINSTANCE hInstance = NULL;

// Define DEBUG if you want debug output.
//#define DEBUG

#ifndef DEBUG

#define KdPrint(x)

#else

#define KdPrint(x)          DbgPrintF          x

BOOL
DbgPrintF(LPCSTR Message, ...)
{
  va_list param_list;
  LPSTR lpBuf = NULL;

  va_start(param_list, Message);

  if (!FormatMessageA(FORMAT_MESSAGE_MAX_WIDTH_MASK |
		      FORMAT_MESSAGE_ALLOCATE_BUFFER |
		      FORMAT_MESSAGE_FROM_STRING, Message, 0, 0,
		      (LPSTR) &lpBuf, 0, &param_list))
    return FALSE;

  OutputDebugStringA(lpBuf);
  LocalFree(lpBuf);
  return TRUE;
}
#endif

void
LoadDeviceToList(HWND hWnd, int iDeviceNumber)
{
  PIMDISK_CREATE_DATA create_data = (PIMDISK_CREATE_DATA)
    _alloca(sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2));

  if (create_data == NULL)
    {
      MessageBox(hWnd, L"Memory allocation error.", L"ImDisk", MB_ICONSTOP);
      return;
    }

  if (!ImDiskQueryDevice(iDeviceNumber, create_data,
			 sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2)))
    return;

  WCHAR wcMountPoint[3] = L"";
  if (create_data->DriveLetter != 0)
    {
      wcMountPoint[0] = create_data->DriveLetter;
      wcMountPoint[1] = L':';
    }

  LVITEM lvi;
  lvi.mask = LVIF_IMAGE | LVIF_TEXT | LVIF_PARAM;
  lvi.iItem = 0;
  lvi.iSubItem = 0;
  lvi.pszText = wcMountPoint;

  switch (IMDISK_DEVICE_TYPE(create_data->Flags))
    {
    case IMDISK_DEVICE_TYPE_FD:
      lvi.iImage = 1;
      break;

    case IMDISK_DEVICE_TYPE_CD:
      lvi.iImage = 2;
      break;

    default:
      lvi.iImage = 0;
    }

  lvi.lParam = iDeviceNumber;
  lvi.iItem = ListView_InsertItem(hWnd, &lvi);

  lvi.mask = LVIF_TEXT;
  lvi.iSubItem = 1;
  switch (IMDISK_TYPE(create_data->Flags))
    {
    case IMDISK_TYPE_FILE:
    case IMDISK_TYPE_VM:
      if (create_data->FileNameLength == 0)
	{
	  lvi.pszText = L"Virtual memory";
	  break;
	}

      lvi.pszText = (LPWSTR) _alloca(create_data->FileNameLength +
				     sizeof(*create_data->FileName));
      if (lvi.pszText == NULL)
	return;

      wcsncpy(lvi.pszText, create_data->FileName,
	      create_data->FileNameLength >> 1);
      lvi.pszText[create_data->FileNameLength >> 1] = 0;

      if (wcsncmp(lvi.pszText, L"\\??\\", 4) == 0)
	lvi.pszText += 4;
      else if (wcsncmp(lvi.pszText, L"\\DosDevices\\", 12) == 0)
	lvi.pszText += 12;

      if (wcsncmp(lvi.pszText, L"UNC\\", 4) == 0)
	{
	  lvi.pszText += 2;
	  lvi.pszText[0] = L'\\';
	}

      if (IMDISK_TYPE(create_data->Flags) == IMDISK_TYPE_VM)
	{
	  LPWSTR filename_part = lvi.pszText;

	  WCHAR Text[] = L"Virtual memory, from ";
	  lvi.pszText = (LPWSTR) _alloca(sizeof(Text) +
					 (wcslen(lvi.pszText) << 1) + 2);
	  if (lvi.pszText == NULL)
	    return;

	  wcscpy(lvi.pszText, Text);
	  wcscat(lvi.pszText, filename_part);
	}
      break;

    case IMDISK_TYPE_PROXY:
	{
	  WCHAR Text[] = L"Proxy through ";
	  lvi.pszText = (LPWSTR) _alloca(create_data->FileNameLength +
					 sizeof(*create_data->FileName) +
					 sizeof(Text));
	  if (lvi.pszText == NULL)
	    return;
	  wcsncpy(lvi.pszText, Text, sizeof(Text) >> 1);
	  lvi.pszText[sizeof(Text) >> 1] = 0;
	  wcsncat(lvi.pszText, create_data->FileName,
		  create_data->FileNameLength >> 1);
	  lvi.pszText[(create_data->FileNameLength + sizeof(Text) - 1) >> 1] =
	    0;
	}
      break;

    default:
      lvi.pszText = L"";
    }

  ListView_SetItem(hWnd, &lvi);

  WCHAR wcBuffer[128];
  switch (create_data->DiskGeometry.Cylinders.QuadPart)
    {
    case 2880 << 10:
      wcscpy(wcBuffer, L"2,880 KB");
      break;

    case 1722 << 10:
      wcscpy(wcBuffer, L"1,722 KB");
      break;

    case 1680 << 10:
      wcscpy(wcBuffer, L"1,680 KB");
      break;

    case 1440 << 10:
      wcscpy(wcBuffer, L"1,440 KB");
      break;

    case 1200 << 10:
      wcscpy(wcBuffer, L"1,200 KB");
      break;

    default:
      _snwprintf(wcBuffer, sizeof(wcBuffer)/sizeof(*wcBuffer)-1,
		 L"%.4g %s",
		 _h(create_data->DiskGeometry.Cylinders.QuadPart),
		 _p(create_data->DiskGeometry.Cylinders.QuadPart));
    }

  wcBuffer[sizeof(wcBuffer)/sizeof(*wcBuffer) - 1] = 0;

  lvi.iSubItem = 2;
  lvi.pszText = wcBuffer;
  ListView_SetItem(hWnd, &lvi);

  lvi.iSubItem = 3;
  if (IMDISK_READONLY(create_data->Flags))
    if (IMDISK_REMOVABLE(create_data->Flags))
      lvi.pszText = L"Read-only, Removable";
    else
      lvi.pszText = L"Read-only";
  else
    if (IMDISK_REMOVABLE(create_data->Flags))
      lvi.pszText = L"Read/write, Removable";
    else
      lvi.pszText = L"Read/write";

  ListView_SetItem(hWnd, &lvi);

  lvi.iSubItem = 4;
  if (create_data->DriveLetter != 0)
    if (GetVolumeInformation(wcMountPoint, NULL, 0, NULL, NULL, NULL,
			     wcBuffer, sizeof(wcBuffer)/sizeof(*wcBuffer)))
      lvi.pszText = wcBuffer;
    else
      lvi.pszText = L"N/A";
  else
    lvi.pszText = L"";
  ListView_SetItem(hWnd, &lvi);
}

bool
RefreshList(HWND hWnd)
{
  ListView_DeleteAllItems(hWnd);

  DWORD device_list = ImDiskGetDeviceList();

  if (device_list == 0)
    switch (GetLastError())
      {
      case NO_ERROR:
      case ERROR_FILE_NOT_FOUND:
	return true;

      default:
	MsgBoxLastError(hWnd,
			L"Cannot control the ImDisk Virtual Disk Driver:");

	return false;
      }

  for (DWORD counter = 0; device_list != 0; device_list >>= 1, counter++)
    if (device_list & 1)
      LoadDeviceToList(hWnd, counter);

  return true;
}

BOOL
CALLBACK
AboutDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
    {
    case WM_INITDIALOG:
      {
	LPWSTR *argv = (LPWSTR*) lParam;
	LPWSTR lpBuf = NULL;

	SetWindowText(hWnd, argv[0]);

	if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			   FORMAT_MESSAGE_ARGUMENT_ARRAY |
			   FORMAT_MESSAGE_FROM_STRING, argv[1], 0, 0,
			   (LPWSTR) &lpBuf, 0, (va_list*) (argv+2)))
	  return TRUE;

	SetDlgItemText(hWnd, IDC_EDT_ABOUTTEXT, lpBuf);
	LocalFree(lpBuf);
	return TRUE;
      }

    case WM_COMMAND:
      switch (LOWORD(wParam))
	{
	case IDCANCEL:
	case IDOK:
	  EndDialog(hWnd, IDOK);
	  return TRUE;
	}

      return TRUE;

    case WM_CLOSE:
      EndDialog(hWnd, IDCANCEL);
      return TRUE;
    }

  return FALSE;
}

int
CDECL
DisplayAboutBox(HWND hWnd, LPCWSTR Title, LPCWSTR /*Message*/, ...)
{
  return DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_ABOUT_BOX), hWnd,
			AboutDlgProc, (LPARAM) &Title);
}

void
SaveSelectedDeviceToImageFile(HWND hWnd)
{
  LVITEM lvi = { 0 };
  lvi.mask = LVIF_IMAGE | LVIF_TEXT | LVIF_PARAM;
  lvi.iItem = (int)
    SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETNEXTITEM, (WPARAM) -1,
		       MAKELPARAM((UINT) LVNI_SELECTED, 0));

  if (lvi.iItem == -1)
    return;

  lvi.iSubItem = 0;
  WCHAR wcBuffer[3] = L"";
  lvi.pszText = wcBuffer;
  lvi.cchTextMax = sizeof(wcBuffer)/sizeof(*wcBuffer);
  SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETITEM, 0, (LPARAM) &lvi);

  WCHAR file_name[MAX_PATH + 1] = L"";
  OPENFILENAME_NT4 ofn = { sizeof ofn };
  ofn.hwndOwner = hWnd;
  ofn.lpstrFilter = L"Image files (*.img)\0*.img\0";
  ofn.lpstrFile = file_name;
  ofn.nMaxFile = sizeof(file_name)/sizeof(*file_name);
  ofn.lpstrTitle = L"Save contents of virtual disk to image file";
  ofn.Flags = OFN_EXPLORER | OFN_LONGNAMES | OFN_OVERWRITEPROMPT |
    OFN_PATHMUSTEXIST;
  ofn.lpstrDefExt = L"img";

  if (lvi.iImage == 2)
    {
      ofn.lpstrFilter = L"ISO image (*.iso)\0*.iso\0";
      ofn.lpstrDefExt = L"iso";
    }

  HANDLE hDev = ImDiskOpenDeviceByNumber(lvi.lParam, GENERIC_READ);

  if (hDev == INVALID_HANDLE_VALUE)
    {
      MsgBoxLastError(hWnd, L"Cannot open drive for direct access:");
      return;
    }

  if (!GetSaveFileName((LPOPENFILENAMEW) &ofn))
    {
      CloseHandle(hDev);
      return;
    }

  HANDLE hImage = CreateFile(ofn.lpstrFile,
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

  DWORD dwRet;
  if (DeviceIoControl(hDev, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &dwRet, NULL))
    DeviceIoControl(hDev, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &dwRet,
		    NULL);
  else
    if (MessageBox(hWnd,
		   L"Cannot lock the drive. It may be in use by another "
		   L"program. Do you want to continue anyway?",
		   L"ImDisk Virtual Disk Driver",
		   MB_ICONEXCLAMATION | MB_OKCANCEL | MB_DEFBUTTON2) != IDOK)
      {
	CloseHandle(hDev);
	CloseHandle(hImage);
	return;
      }

  BOOL bCancelFlag = FALSE;

  EnableWindow(hWnd, FALSE);
  HWND hWndStatus =
    CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_DLG_STATUS), hWnd,
		      StatusDlgProc, (LPARAM) &bCancelFlag);

  SetDlgItemText(hWndStatus, IDC_STATUS_MSG, L"Saving image file...");

  if (ImDiskSaveImageFile(hDev, hImage, 2 << 20, &bCancelFlag))
    {
      DestroyWindow(hWndStatus);
      EnableWindow(hWnd, TRUE);
      CloseHandle(hDev);

      if (GetFileSize(hImage, NULL) == 0)
	{
	  DeleteFile(ofn.lpstrFile);
	  CloseHandle(hImage);

	  MsgBoxPrintF(hWnd, MB_ICONEXCLAMATION, L"ImDisk Virtual Disk Driver",
		       L"The contents of drive '%1' could not be saved. Check "
		       L"that the drive contains a supported filesystem.",
		       lvi.pszText);

	  return;
	}

      CloseHandle(hImage);
      MsgBoxPrintF(hWnd, MB_ICONINFORMATION,
		   L"ImDisk Virtual Disk Driver",
		   L"Successfully saved the contents of drive '%1' to "
		   L"image file '%2'.", lvi.pszText, ofn.lpstrFile);
      return;
    }

  MsgBoxLastError(hWnd, L"Error saving image:");

  DestroyWindow(hWndStatus);
  EnableWindow(hWnd, TRUE);
  CloseHandle(hDev);

  if (GetFileSize(hImage, NULL) == 0)
    DeleteFile(ofn.lpstrFile);

  CloseHandle(hImage);
}

BOOL CALLBACK
NewDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
    {
    case WM_INITDIALOG:
      {
	WCHAR free_drive_letter[3] = L" ";
	free_drive_letter[0] = ImDiskFindFreeDriveLetter();
	SendDlgItemMessage(hWnd, IDC_EDT_DRIVE, WM_SETTEXT, 0,
			   (LPARAM)(LPCWSTR) free_drive_letter);

	if (lParam != 0)
	  SetDlgItemText(hWnd, IDC_EDT_IMAGEFILE, (LPCWSTR) lParam);

	SendDlgItemMessage(hWnd, IDC_EDT_IMAGEFILE, EM_SETLIMITTEXT,
			   (WPARAM) MAX_PATH, 0);
	SendDlgItemMessage(hWnd, IDC_EDT_DRIVE, EM_SETLIMITTEXT, (WPARAM) 1,
			   0);

	CheckDlgButton(hWnd, IDC_UNIT_B, BST_CHECKED);
	CheckDlgButton(hWnd, IDC_OFFSET_UNIT_B, BST_CHECKED);
	CheckDlgButton(hWnd, IDC_DT_AUTO, BST_CHECKED);

	return TRUE;
      }

    case WM_COMMAND:
      {
	if ((GetWindowTextLength(GetDlgItem(hWnd, IDC_EDT_IMAGEFILE)) > 0) |
	    (GetDlgItemInt(hWnd, IDC_EDT_SIZE, NULL, FALSE) > 0))
	  EnableWindow(GetDlgItem(hWnd, IDOK), TRUE);
	else
	  EnableWindow(GetDlgItem(hWnd, IDOK), FALSE);

	if (GetWindowTextLength(GetDlgItem(hWnd, IDC_EDT_IMAGEFILE)) > 0)
	  EnableWindow(GetDlgItem(hWnd, IDC_CHK_VM), TRUE);
	else
	  EnableWindow(GetDlgItem(hWnd, IDC_CHK_VM), FALSE);

	switch (LOWORD(wParam))
	  {
	  case IDC_BTN_BROWSE_FILE:
	    {
	      WCHAR file_name[MAX_PATH+1] = L"*";

	      OPENFILENAME_NT4 ofn = { sizeof ofn };
	      ofn.hwndOwner = hWnd;
	      ofn.lpstrFile = file_name;
	      ofn.nMaxFile = sizeof(file_name)/sizeof(*file_name);
	      ofn.lpstrTitle = L"Select image file";
	      ofn.Flags = OFN_CREATEPROMPT | OFN_EXPLORER | OFN_HIDEREADONLY |
		OFN_LONGNAMES | OFN_PATHMUSTEXIST;

	      if (GetOpenFileName((LPOPENFILENAMEW) &ofn))
		SetDlgItemText(hWnd, IDC_EDT_IMAGEFILE, file_name);
	    }
	    return TRUE;

	  case IDOK:
	    {
	      EnableWindow(hWnd, FALSE);
	      HWND hWndStatus =
		CreateDialog(hInstance, MAKEINTRESOURCE(IDD_DLG_STATUS), hWnd,
			     StatusDlgProc);

	      DISK_GEOMETRY disk_geometry = { 0 };
	      disk_geometry.Cylinders.QuadPart =
		GetDlgItemInt(hWnd, IDC_EDT_SIZE, NULL, FALSE);

	      if (IsDlgButtonChecked(hWnd, IDC_UNIT_GB))
		disk_geometry.Cylinders.QuadPart <<= 30;
	      else if (IsDlgButtonChecked(hWnd, IDC_UNIT_MB))
		disk_geometry.Cylinders.QuadPart <<= 20;
	      else if (IsDlgButtonChecked(hWnd, IDC_UNIT_KB))
		disk_geometry.Cylinders.QuadPart <<= 10;
	      else if (IsDlgButtonChecked(hWnd, IDC_UNIT_BLOCKS))
		disk_geometry.Cylinders.QuadPart <<= 9;

	      LARGE_INTEGER image_offset = { 0 };
	      image_offset.QuadPart =
		GetDlgItemInt(hWnd, IDC_EDT_IMAGE_OFFSET, NULL, FALSE);

	      if (IsDlgButtonChecked(hWnd, IDC_OFFSET_UNIT_GB))
		image_offset.QuadPart <<= 30;
	      else if (IsDlgButtonChecked(hWnd, IDC_OFFSET_UNIT_MB))
		image_offset.QuadPart <<= 20;
	      else if (IsDlgButtonChecked(hWnd, IDC_OFFSET_UNIT_KB))
		image_offset.QuadPart <<= 10;
	      else if (IsDlgButtonChecked(hWnd, IDC_OFFSET_UNIT_BLOCKS))
		image_offset.QuadPart <<= 9;

	      DWORD flags = 0;
	      
	      if (IsDlgButtonChecked(hWnd, IDC_DT_HD))
		flags |= IMDISK_DEVICE_TYPE_HD;
	      else if (IsDlgButtonChecked(hWnd, IDC_DT_FD))
		flags |= IMDISK_DEVICE_TYPE_FD;
	      else if (IsDlgButtonChecked(hWnd, IDC_DT_CD))
		flags |= IMDISK_DEVICE_TYPE_CD;

	      if (IsDlgButtonChecked(hWnd, IDC_CHK_READONLY))
		flags |= IMDISK_OPTION_RO;

	      if (IsDlgButtonChecked(hWnd, IDC_CHK_REMOVABLE))
		flags |= IMDISK_OPTION_REMOVABLE;

	      if (IsDlgButtonChecked(hWnd, IDC_CHK_VM))
		flags |= IMDISK_TYPE_VM;
	      
	      WCHAR file_name[MAX_PATH+1] = L"";
	      UINT uLen = GetDlgItemText(hWnd, IDC_EDT_IMAGEFILE, file_name,
					 sizeof(file_name)/sizeof(*file_name));
	      file_name[uLen] = 0;

	      WCHAR drive_letter[3] = L"";
	      GetDlgItemText(hWnd, IDC_EDT_DRIVE, drive_letter, 3);

	      drive_letter[0] = towupper(drive_letter[0]);
	      drive_letter[1] = L':';

	      BOOL status =
		ImDiskCreateDevice(GetDlgItem(hWndStatus, IDC_STATUS_MSG),
				   &disk_geometry,
				   &image_offset,
				   flags,
				   file_name[0] == 0 ? NULL : file_name,
				   FALSE,
				   drive_letter[0] == 0 ? NULL : drive_letter);

	      EnableWindow(hWnd, TRUE);
	      DestroyWindow(hWndStatus);
	      if (status)
		EndDialog(hWnd, IDOK);
	      else
		EndDialog(hWnd, IDCANCEL);
	    }
	    return TRUE;

	  case IDCANCEL:
	    EndDialog(hWnd, IDCANCEL);
	    return TRUE;

	  case IDC_EDT_SIZE:
	    switch (HIWORD(wParam))
	      {
	      case EN_SETFOCUS:
		if (GetDlgItemInt(hWnd, IDC_EDT_SIZE, NULL, FALSE) == 0)
		  SetDlgItemText(hWnd, IDC_EDT_SIZE, L"");
		return TRUE;

	      case EN_KILLFOCUS:
		if (GetDlgItemInt(hWnd, IDC_EDT_SIZE, NULL, FALSE) == 0)
		  SetDlgItemText(hWnd, IDC_EDT_SIZE,
				 L"(current image file size)");
		return TRUE;
	      }
	    return TRUE;

	  case IDC_EDT_IMAGE_OFFSET:
	    switch (HIWORD(wParam))
	      {
	      case EN_KILLFOCUS:
		if (GetDlgItemInt(hWnd, IDC_EDT_IMAGE_OFFSET, NULL, FALSE) ==
		    0)
		  SetDlgItemText(hWnd, IDC_EDT_IMAGE_OFFSET, L"0");
		return TRUE;
	      }
	    return TRUE;
	  }

	return TRUE;
      }

    case WM_CLOSE:
      EndDialog(hWnd, IDCANCEL);
      return TRUE;
    }

  return FALSE;
}

BOOL CALLBACK
ExtendDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM /*lParam*/)
{
  switch (uMsg)
    {
    case WM_INITDIALOG:
      CheckDlgButton(hWnd, IDC_UNIT_B, BST_CHECKED);
      return TRUE;

    case WM_COMMAND:
      {
	if (GetDlgItemInt(hWnd, IDC_EDT_EXTEND_SIZE, NULL, FALSE) > 0)
	  EnableWindow(GetDlgItem(hWnd, IDOK), TRUE);
	else
	  EnableWindow(GetDlgItem(hWnd, IDOK), FALSE);

	switch (LOWORD(wParam))
	  {
	  case IDOK:
	    {
	      EnableWindow(hWnd, FALSE);
	      HWND hWndStatus =
		CreateDialog(hInstance, MAKEINTRESOURCE(IDD_DLG_STATUS), hWnd,
			     StatusDlgProc);

	      LVITEM lvi = { 0 };
	      lvi.iItem = (int)
		SendDlgItemMessage(GetParent(hWnd), IDC_LISTVIEW,
				   LVM_GETNEXTITEM, (WPARAM) -1,
				   MAKELPARAM((UINT) LVNI_SELECTED, 0));

	      if (lvi.iItem == -1)
		{
		  EndDialog(hWnd, IDCANCEL);
		  return TRUE;
		}

	      lvi.iSubItem = 0;
	      lvi.mask = LVIF_PARAM;
	      SendDlgItemMessage(GetParent(hWnd), IDC_LISTVIEW, LVM_GETITEM, 0,
				 (LPARAM) &lvi);

	      LARGE_INTEGER extend_size;
	      extend_size.QuadPart =
		GetDlgItemInt(hWnd, IDC_EDT_EXTEND_SIZE, NULL, FALSE);

	      if (IsDlgButtonChecked(hWnd, IDC_UNIT_GB))
		extend_size.QuadPart <<= 30;
	      else if (IsDlgButtonChecked(hWnd, IDC_UNIT_MB))
		extend_size.QuadPart <<= 20;
	      else if (IsDlgButtonChecked(hWnd, IDC_UNIT_KB))
		extend_size.QuadPart <<= 10;
	      else if (IsDlgButtonChecked(hWnd, IDC_UNIT_BLOCKS))
		extend_size.QuadPart <<= 9;

	      BOOL status =
		ImDiskExtendDevice(GetDlgItem(hWndStatus, IDC_STATUS_MSG),
				   lvi.lParam,
				   &extend_size);

	      Sleep(100);

	      EnableWindow(hWnd, TRUE);
	      DestroyWindow(hWndStatus);

	      if (status)
		EndDialog(hWnd, IDOK);
	      else
		EndDialog(hWnd, IDCANCEL);

	      return TRUE;
	    }

	  case IDCANCEL:
	    EndDialog(hWnd, IDCANCEL);
	    return TRUE;
	  }
	return TRUE;
      }

    case WM_CLOSE:
      EndDialog(hWnd, IDCANCEL);
      return TRUE;
    }

  return FALSE;
}

BOOL CALLBACK
CPlAppletDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
    {
    case WM_INITDIALOG:
      {
	SetClassLong(hWnd, GCL_HICON,
		     (LONG) LoadIcon(hInstance,
				     MAKEINTRESOURCE(IDI_APPICON)));

	HWND hWndListView = GetDlgItem(hWnd, IDC_LISTVIEW);

	LVCOLUMN lvc = { 0 };
	lvc.mask = LVCF_WIDTH | LVCF_TEXT;
	lvc.cx = 40;
	lvc.pszText = L"Drive";

	ListView_InsertColumn(hWndListView, 0, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 255;
	lvc.pszText = L"Image file";

	ListView_InsertColumn(hWndListView, 1, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 60;
	lvc.pszText = L"Size";

	ListView_InsertColumn(hWndListView, 2, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 135;
	lvc.pszText = L"Properties";

	ListView_InsertColumn(hWndListView, 3, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 60;
	lvc.pszText = L"Filesystem";

	ListView_InsertColumn(hWndListView, 4, &lvc);

	HIMAGELIST hImageList =
	  ImageList_Create(GetSystemMetrics(SM_CXSMICON),
			   GetSystemMetrics(SM_CYSMICON),
			   ILC_COLOR8 | ILC_MASK, 1, 1);

	ImageList_AddIcon(hImageList,
			  LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICONHD)));
	ImageList_AddIcon(hImageList,
			  LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICONFD)));
	ImageList_AddIcon(hImageList,
			  LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICONCD)));

	ListView_SetImageList(hWndListView, hImageList, LVSIL_SMALL);

	RefreshList(hWndListView);

	return TRUE;
      }

    case WM_NOTIFY:
      switch ((int) wParam)
	{
	case IDC_LISTVIEW:
	  switch (((LPNMHDR) lParam)->code)
	    {
	    case NM_CUSTOMDRAW:
	      {
		BOOL item_selected =
		  SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETNEXTITEM,
				     (WPARAM) -1,
				     MAKELPARAM((UINT) LVNI_SELECTED, 0)) !=
		  -1;

		EnableMenuItem(GetMenu(hWnd), CM_CPL_APPLET_SELECTED,
			       (item_selected ? MF_ENABLED : MF_GRAYED) |
			       MF_BYCOMMAND);

		EnableWindow(GetDlgItem(hWnd, CM_CPL_APPLET_SELECTED_UNMOUNT),
			     item_selected);
		EnableWindow(GetDlgItem(hWnd,
					CM_CPL_APPLET_SELECTED_EXTEND_SIZE),
			     item_selected);
		EnableWindow(GetDlgItem(hWnd, CM_CPL_APPLET_SELECTED_FORMAT),
			     item_selected);
		EnableWindow(GetDlgItem(hWnd,
					CM_CPL_APPLET_SELECTED_SAVE_IMAGE),
			     item_selected);
	      }

	      DrawMenuBar(hWnd);

	      return TRUE;

	    case NM_RCLICK:
	      {
		int item_selected =
		  SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETNEXTITEM,
				     (WPARAM) -1,
				     MAKELPARAM((UINT) LVNI_SELECTED, 0));
		if (item_selected == -1)
		  return TRUE;

		POINT item_pos = { 0, 0 };
		SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETITEMPOSITION,
				   (WPARAM) item_selected, (LPARAM) &item_pos);

		item_pos.x = 35;

		MapWindowPoints(GetDlgItem(hWnd, IDC_LISTVIEW), NULL,
				&item_pos, 1);
		
		TrackPopupMenu(GetSubMenu(GetMenu(hWnd), 1), TPM_RIGHTBUTTON,
			       item_pos.x, item_pos.y, 0, hWnd, NULL);
	      }
	      return TRUE;

	    case NM_DBLCLK:
	      wParam = IDOK;
	      break;

	    case LVN_KEYDOWN:
	      switch (((LV_KEYDOWN *) lParam)->wVKey)
		{
		  // The Delete and F5 keys in the listview translates into
		  // buttons/menu items and then passed to the WM_COMMAND
		  // case handler following this WM_NOTIFY case handler.
		case VK_DELETE:
		  wParam = CM_CPL_APPLET_SELECTED_UNMOUNT;
		  break;

		case VK_F5:
		  wParam = CM_CPL_APPLET_WINDOW_REFRESH;
		  break;

		default:
		  return FALSE;
		}

	      break;

	    default:
	      return FALSE;
	    }

	  break;

	default:
	  return TRUE;
	}

    case WM_COMMAND:
      switch (LOWORD(wParam))
	{
	case CM_CPL_APPLET_FILE_MOUNT_NEW:
	  if (DialogBox(hInstance, MAKEINTRESOURCE(IDD_NEWDIALOG), hWnd,
			NewDlgProc) == IDOK)
	    RefreshList(GetDlgItem(hWnd, IDC_LISTVIEW));

	  return TRUE;

	case IDCANCEL:
	case CM_CPL_APPLET_FILE_EXIT:
	  EndDialog(hWnd, IDOK);

	  return TRUE;

	case IDOK:
	case CM_CPL_APPLET_SELECTED_OPEN:
	  {
	    WCHAR mount_point[3] = L"";
	    LVITEM lvi = { 0 };
	    lvi.iItem = (int)
	      SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETNEXTITEM,
				 (WPARAM) -1,
				 MAKELPARAM((UINT) LVNI_SELECTED, 0));

	    if (lvi.iItem == -1)
	      return TRUE;
	    
	    lvi.iSubItem = 0;
	    lvi.mask = LVIF_TEXT;
	    lvi.pszText = mount_point;
	    lvi.cchTextMax = sizeof(mount_point) / sizeof(*mount_point);
	    SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETITEM, 0,
			       (LPARAM) &lvi);

	    if (mount_point[0] == 0)
	      return TRUE;
	    
	    if ((int) ShellExecute(hWnd, L"open", mount_point, NULL, NULL,
				   SW_SHOWNORMAL) <= 32)
	      MessageBox(hWnd,
			 L"Cannot open the drive. Check that the drive is "
			 L"formatted with a compatible filesystem and that it "
			 L"is not locked by another process.",
			 L"ImDisk Virtual Disk Driver",
			 MB_ICONEXCLAMATION);
	  }

	  return TRUE;

	case CM_CPL_APPLET_SELECTED_UNMOUNT:
	  {
	    LVITEM lvi = { 0 };
	    lvi.mask = LVIF_TEXT | LVIF_PARAM;
	    lvi.iItem = (int)
	      SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETNEXTITEM,
				 (WPARAM) -1,
				 MAKELPARAM((UINT) LVNI_SELECTED, 0));
	    if (lvi.iItem == -1)
	      return TRUE;

	    lvi.iSubItem = 0;
	    WCHAR wcBuffer[3] = L"";
	    lvi.pszText = wcBuffer;
	    lvi.cchTextMax = sizeof(wcBuffer)/sizeof(*wcBuffer);
	    SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETITEM, 0,
			       (LPARAM) &lvi);

	    EnableWindow(hWnd, FALSE);
	    HWND hWndStatus =
	      CreateDialog(hInstance, MAKEINTRESOURCE(IDD_DLG_STATUS), hWnd,
			   StatusDlgProc);

	    ImDiskRemoveDevice(GetDlgItem(hWndStatus, IDC_STATUS_MSG),
			       lvi.lParam,
			       lvi.pszText[0] == 0 ? NULL : lvi.pszText);

	    Sleep(100);

	    EnableWindow(hWnd, TRUE);
	    DestroyWindow(hWndStatus);

	    DoEvents(NULL);

	    RefreshList(GetDlgItem(hWnd, IDC_LISTVIEW));

	    return TRUE;
	  }

	case CM_CPL_APPLET_SELECTED_SET_RO:
	case CM_CPL_APPLET_SELECTED_SET_RW:
	case CM_CPL_APPLET_SELECTED_SET_REM:
	case CM_CPL_APPLET_SELECTED_SET_FIXED:
	  {
	    LVITEM lvi = { 0 };
	    lvi.mask = LVIF_TEXT | LVIF_PARAM;
	    lvi.iItem = (int)
	      SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETNEXTITEM,
				 (WPARAM) -1,
				 MAKELPARAM((UINT) LVNI_SELECTED, 0));
	    if (lvi.iItem == -1)
	      return TRUE;

	    lvi.iSubItem = 0;
	    WCHAR wcBuffer[3] = L"";
	    lvi.pszText = wcBuffer;
	    lvi.cchTextMax = sizeof(wcBuffer)/sizeof(*wcBuffer);
	    SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETITEM, 0,
			       (LPARAM) &lvi);

	    EnableWindow(hWnd, FALSE);
	    HWND hWndStatus =
	      CreateDialog(hInstance, MAKEINTRESOURCE(IDD_DLG_STATUS), hWnd,
			   StatusDlgProc);

	    DWORD flags_to_change = 0;
	    DWORD flags = 0;

	    switch (LOWORD(wParam))
	      {
	      case CM_CPL_APPLET_SELECTED_SET_RO:
		flags = IMDISK_OPTION_RO;
	      case CM_CPL_APPLET_SELECTED_SET_RW:
		flags_to_change = IMDISK_OPTION_RO;
		break;

	      case CM_CPL_APPLET_SELECTED_SET_REM:
		flags = IMDISK_OPTION_REMOVABLE;
	      case CM_CPL_APPLET_SELECTED_SET_FIXED:
		flags_to_change = IMDISK_OPTION_REMOVABLE;
		break;
	      }

	    ImDiskChangeFlags(GetDlgItem(hWndStatus, IDC_STATUS_MSG),
			      lvi.lParam,
			      lvi.pszText[0] == 0 ? NULL : lvi.pszText,
			      flags_to_change, flags);

	    Sleep(100);

	    EnableWindow(hWnd, TRUE);
	    DestroyWindow(hWndStatus);

	    DoEvents(NULL);

	    RefreshList(GetDlgItem(hWnd, IDC_LISTVIEW));

	    return TRUE;
	  }

	case CM_CPL_APPLET_SELECTED_EXTEND_SIZE:
	  if (DialogBox(hInstance, MAKEINTRESOURCE(IDD_DLG_EXTEND), hWnd,
			ExtendDlgProc) == IDOK)
	    RefreshList(GetDlgItem(hWnd, IDC_LISTVIEW));

	  return TRUE;

	case CM_CPL_APPLET_SELECTED_FORMAT:
	  {
	    WCHAR mount_point[3] = L"";
	    LVITEM lvi = { 0 };
	    lvi.iItem = (int)
	      SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETNEXTITEM,
				 (WPARAM) -1,
				 MAKELPARAM((UINT) LVNI_SELECTED, 0));

	    if (lvi.iItem == -1)
	      return TRUE;
	    
	    lvi.iSubItem = 0;
	    lvi.mask = LVIF_TEXT;
	    lvi.pszText = mount_point;
	    lvi.cchTextMax = sizeof(mount_point) / sizeof(*mount_point);
	    SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETITEM, 0,
			       (LPARAM) &lvi);

	    if ((mount_point[0] < 'A') | (mount_point[0] > 'Z'))
	      {
		MsgBoxPrintF(hWnd, MB_ICONSTOP, L"Unsupported mount point",
			     L"It is only possible to format drives with a "
			     L"drive letter A: - Z:. This mount point, '%1' "
			     L"is not supported.", mount_point);
		return TRUE;
	      }

	    SHFormatDrive(hWnd, (mount_point[0] & 0x1F) - 1, SHFMT_ID_DEFAULT,
			  SHFMT_OPT_FULL);

	    RefreshList(GetDlgItem(hWnd, IDC_LISTVIEW));
	  }

	  return TRUE;

	case CM_CPL_APPLET_SELECTED_SAVE_IMAGE:
	  SaveSelectedDeviceToImageFile(hWnd);

	  return TRUE;

	case CM_CPL_APPLET_WINDOW_REFRESH:
	  RefreshList(GetDlgItem(hWnd, IDC_LISTVIEW));

	  return TRUE;

	case CM_CPL_APPLET_HELP_ABOUT:
	  DisplayAboutBox
	    (hWnd,
	     L"About ImDisk Virtual Disk Driver",
	     L"ImDisk Virtual Disk Driver for Windows NT/2000/XP/2003.\r\n"
	     L"Version %1!i!.%2!i!.%3!i! - (Compiled %4!hs!)\r\n"
	     L"\r\n"
	     L"Copyright (C) 2004-2008 Olof Lagerkvist.\r\n"
	     L"http://www.ltr-data.se     olof@ltr-data.se\r\n"
	     L"\r\n"
	     L"Permission is hereby granted, free of charge, to any person\r\n"
	     L"obtaining a copy of this software and associated documentation\r\n"
	     L"files (the \"Software\"), to deal in the Software without\r\n"
	     L"restriction, including without limitation the rights to use,\r\n"
	     L"copy, modify, merge, publish, distribute, sublicense, and/or\r\n"
	     L"sell copies of the Software, and to permit persons to whom the\r\n"
	     L"Software is furnished to do so, subject to the following\r\n"
	     L"conditions:\r\n"
	     L"\r\n"
	     L"The above copyright notice and this permission notice shall be\r\n"
	     L"included in all copies or substantial portions of the Software.\r\n"
	     L"\r\n"
	     L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,\r\n"
	     L"EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES\r\n"
	     L"OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\r\n"
	     L"NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT\r\n"
	     L"HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,\r\n"
	     L"WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING\r\n"
	     L"FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR\r\n"
	     L"OTHER DEALINGS IN THE SOFTWARE.\r\n"
	     L"\r\n"
	     L"This program contains some GNU GPL licensed code:\r\n"
	     L"- Parts related to floppy emulation based on VFD by Ken Kato.\r\n"
	     L"  http://chitchat.at.infoseek.co.jp/vmware/vfd.html\r\n"
	     L"Copyright (C) Free Software Foundation, Inc.\r\n"
	     L"Read gpl.txt for the full GNU GPL license.\r\n"
	     L"\r\n"
	     L"This program may contain BSD licensed code:\r\n"
	     L"- Some code ported to NT from the FreeBSD md driver by Olof Lagerkvist.\r\n"
	     L"  http://www.ltr-data.se\r\n"
	     L"Copyright (C) The FreeBSD Project.\r\n"
	     L"Copyright (C) The Regents of the University of California.\r\n",
	     (int) (IMDISK_VERSION & 0xFF00) >> 8,
	     (int) (IMDISK_VERSION & 0xF0) >> 4,
	     (int) IMDISK_VERSION & 0xF,
	     __TIMESTAMP__);

	  return TRUE;
	}

      return TRUE;

    case WM_CLOSE:
      EndDialog(hWnd, IDOK);
      return TRUE;
    }

  return FALSE;
}

EXTERN_C LONG APIENTRY
CPlApplet(HWND hwndCPl,	// handle to Control Panel window
	  UINT uMsg,	        // message
	  LONG /*lParam1*/,	// first message parameter
	  LONG lParam2 	// second message parameter
	  )
{
  switch (uMsg)
    {
    case CPL_DBLCLK:
      {
	if (DialogBox(hInstance, MAKEINTRESOURCE(IDD_CPLAPPLET), hwndCPl,
		      CPlAppletDlgProc) == -1)
	  MessageBox(hwndCPl, L"Error loading dialog box.", L"ImDisk",
		     MB_ICONSTOP);
	return 0;
      }

    case CPL_EXIT:
      return 0;

    case CPL_GETCOUNT:
      return 1;

    case CPL_INIT:
      return 1;

    case CPL_INQUIRE:
      {
        LPCPLINFO lpcpli = (LPCPLINFO)lParam2;
        lpcpli->idIcon = IDI_APPICON;
        lpcpli->idName = IDS_CPLAPPLET_TITLE;
        lpcpli->idInfo = IDS_CPLAPPLET_DESCRIPTION;
        lpcpli->lData = 0;
        return 0;
      }

    case CPL_STOP:
      return 0;

    default:
      return 1;
    }
}

EXTERN_C BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD, LPVOID)
{
  hInstance = hinstDLL;
  return TRUE;
}
