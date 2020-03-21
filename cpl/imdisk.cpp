/*
    Control Panel Applet for the ImDisk Virtual Disk Driver for
    Windows NT/2000/XP.

    Copyright (C) 2007 Olof Lagerkvist.

    Some credits:
    - Parts related to floppy emulation based on VFD by Ken Kato.
      http://chitchat.at.infoseek.co.jp/vmware/vfd.html
    - Parts related to CD-ROM emulation and impersonation to support remote
      files based on FileDisk by Bo BrantÅÈn.
      http://www.acc.umu.se/~bosse/
    - Virtual memory image support, usermode storage backend support and some
      code ported to NT from the FreeBSD md driver by Olof Lagerkvist.
      http://www.ltr-data.se

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <windows.h>
#include <winioctl.h>
#include <commctrl.h>
#include <commdlg.h>
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

HINSTANCE hInstance = NULL;

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

  HANDLE device = ImDiskOpenDeviceByNumber(iDeviceNumber, GENERIC_READ);

  if (device == INVALID_HANDLE_VALUE)
    {
      MsgBoxLastError(hWnd, L"Error opening device:");
      return;
    }

  if (!ImDiskCheckDriverVersion(device))
    {
      NtClose(device);
      MsgBoxPrintF(hWnd, MB_ICONSTOP, L"Warning! The device with number %1!i! "
		   L"is created by an incompatible version of ImDisk. Please "
		   L"reinstall ImDisk to make sure that all components of it "
		   L"on this system are from the same install package.",
		   iDeviceNumber);
      return;
    }

  DWORD dw;
  if (!DeviceIoControl(device,
                       IOCTL_IMDISK_QUERY_DEVICE,
                       NULL,
                       0,
                       create_data,
                       sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2),
                       &dw, NULL))
    {
      NtClose(device);
      return;
    }

  NtClose(device);

  if (dw < sizeof(IMDISK_CREATE_DATA) - sizeof(*create_data->FileName))
    {
      MsgBoxPrintF(hWnd, MB_ICONEXCLAMATION, L"Error querying the device with "
		   L"number %1!i!. Is that really an ImDisk device?",
		   iDeviceNumber);
      return;
    }

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
      lvi.pszText = (LPWSTR) _alloca(create_data->FileNameLength +
				     sizeof(*create_data->FileName));
      if (lvi.pszText == NULL)
	return;
      wcsncpy(lvi.pszText, create_data->FileName,
	      create_data->FileNameLength >> 1);
      lvi.pszText[create_data->FileNameLength >> 1] = 0;
      break;

    case IMDISK_TYPE_VM:
      if (create_data->FileNameLength > 0)
	{
	  WCHAR Text[] = L"Virtual memory, from ";
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
      else
	lvi.pszText = L"Virtual memory";
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

  LONGLONG file_size =
    create_data->DiskGeometry.Cylinders.QuadPart *
    create_data->DiskGeometry.TracksPerCylinder *
    create_data->DiskGeometry.SectorsPerTrack *
    create_data->DiskGeometry.BytesPerSector;

  WCHAR wcBuffer[128];
  _snwprintf(wcBuffer, sizeof(wcBuffer)/sizeof(*wcBuffer),
	     L"%.4g %s", _h(file_size), _p(file_size));
  wcBuffer[sizeof(wcBuffer)/sizeof(*wcBuffer)] = 0;

  lvi.iSubItem = 2;
  lvi.pszText = wcBuffer;
  ListView_SetItem(hWnd, &lvi);

  lvi.iSubItem = 3;
  if (IMDISK_READONLY(create_data->Flags))
    lvi.pszText = L"Read-only";
  else
    lvi.pszText = L"Read/write";

  ListView_SetItem(hWnd, &lvi);
}

bool
RefreshList(HWND hWnd)
{
  ListView_DeleteAllItems(hWnd);

  UNICODE_STRING file_name;
  RtlInitUnicodeString(&file_name, IMDISK_CTL_DEVICE_NAME);

  HANDLE driver = ImDiskOpenDeviceByName(&file_name, GENERIC_READ);

  if (driver == INVALID_HANDLE_VALUE)
    {
      if (GetLastError() == ERROR_FILE_NOT_FOUND)
	return true;

      MsgBoxLastError(hWnd, L"Cannot control the ImDisk Virtual Disk Driver:");

      return false;
    }

  DWORD device_list;
  DWORD dw;
  if (!DeviceIoControl(driver,
		       IOCTL_IMDISK_QUERY_DRIVER,
		       NULL, 0,
		       &device_list, sizeof(device_list),
		       &dw, NULL))
    {
      NtClose(driver);
      MsgBoxLastError(hWnd, L"Cannot control the ImDisk Virtual Disk Driver:");
    }

  NtClose(driver);

  for (DWORD counter = 0; device_list != 0; device_list >>= 1, counter++)
    if (device_list & 1)
      LoadDeviceToList(hWnd, counter);

  return true;
}

BOOL CALLBACK
DlgProc(HWND hWnd, UINT uMsg, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
  if (uMsg == WM_INITDIALOG)
    ShowWindow(hWnd, SW_SHOW);

  return FALSE;
}

BOOL CALLBACK
NewDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM /*lParam*/)
{
  switch (uMsg)
    {
    case WM_INITDIALOG:
      SendDlgItemMessage(hWnd, IDC_EDT_IMAGEFILE, EM_SETLIMITTEXT,
			 (WPARAM) MAX_PATH, 0);
      SendDlgItemMessage(hWnd, IDC_EDT_DRIVE, EM_SETLIMITTEXT, (WPARAM) 1, 0);
      CheckDlgButton(hWnd, IDC_UNIT_B, BST_CHECKED);
      CheckDlgButton(hWnd, IDC_DT_AUTO, BST_CHECKED);
      return FALSE;

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

	      OPENFILENAME_NT4 ofn = { 0 };
	      ofn.lStructSize = sizeof ofn;
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
			     DlgProc);					   

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

	      DWORD flags = 0;
	      
	      if (IsDlgButtonChecked(hWnd, IDC_DT_HD))
		flags |= IMDISK_DEVICE_TYPE_HD;
	      else if (IsDlgButtonChecked(hWnd, IDC_DT_FD))
		flags |= IMDISK_DEVICE_TYPE_FD;
	      else if (IsDlgButtonChecked(hWnd, IDC_DT_CD))
		flags |= IMDISK_DEVICE_TYPE_CD;
	      if (IsDlgButtonChecked(hWnd, IDC_CHK_READONLY))
		flags |= IMDISK_OPTION_RO;
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
		ImDiskCreate(GetDlgItem(hWndStatus, IDC_STATUS_MSG),
			     &disk_geometry,
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
		  SetDlgItemText(hWnd, IDC_EDT_SIZE, L"(current)");
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
CplAppletDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM /*lParam*/)
{
  switch (uMsg)
    {
    case WM_INITDIALOG:
      {
	HWND hWndListView = GetDlgItem(hWnd, IDC_LISTVIEW);

	LVCOLUMN lvc = { 0 };
	lvc.mask = LVCF_WIDTH | LVCF_TEXT;
	lvc.cx = 50;
	lvc.pszText = L"Drive";

	ListView_InsertColumn(hWndListView, 0, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 300;
	lvc.pszText = L"Image file";

	ListView_InsertColumn(hWndListView, 1, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 100;
	lvc.pszText = L"Size";

	ListView_InsertColumn(hWndListView, 2, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 90;
	lvc.pszText = L"Properties";

	ListView_InsertColumn(hWndListView, 3, &lvc);

	HIMAGELIST hImageList =
	  ImageList_Create(GetSystemMetrics(SM_CXSMICON),
			   GetSystemMetrics(SM_CYSMICON),
			   ILC_MASK, 1, 1);

	ImageList_AddIcon(hImageList,
			  LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICONHD)));
	ImageList_AddIcon(hImageList,
			  LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICONFD)));
	ImageList_AddIcon(hImageList,
			  LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICONCD)));

	ListView_SetImageList(hWndListView, hImageList, LVSIL_SMALL);

	RefreshList(hWndListView);
	
	return FALSE;
      }

    case WM_NOTIFY:
      switch ((int) wParam)
	{
	case IDC_LISTVIEW:
	  if (SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETNEXTITEM,
				 (WPARAM) -1,
				 MAKELPARAM((UINT) LVNI_SELECTED, 0)) != -1)
	    EnableWindow(GetDlgItem(hWnd, IDC_BTN_DELETE), TRUE);
	  else
	    EnableWindow(GetDlgItem(hWnd, IDC_BTN_DELETE), FALSE);

	  return TRUE;
	}

      return TRUE;

    case WM_COMMAND:
      switch (LOWORD(wParam))
	{
	case IDOK:
	  EndDialog(hWnd, IDOK);
	  return TRUE;

	case IDC_BTN_NEW:
	  if (DialogBox(hInstance, MAKEINTRESOURCE(IDD_NEWDIALOG), hWnd,
			NewDlgProc) == IDOK)
	    RefreshList(GetDlgItem(hWnd, IDC_LISTVIEW));

	  return TRUE;

	case IDC_BTN_DELETE:
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
			   DlgProc);					   

	    ImDiskRemove(GetDlgItem(hWndStatus, IDC_STATUS_MSG),
			 lvi.lParam,
			 lvi.pszText[0] == 0 ? NULL : lvi.pszText);

	    Sleep(100);

	    EnableWindow(hWnd, TRUE);
	    DestroyWindow(hWndStatus);

	    DoEvents(NULL);

	    RefreshList(GetDlgItem(hWnd, IDC_LISTVIEW));
	  }

	  return TRUE;
	}

    case IDC_BTN_ABOUT:
      MessageBox
	(hWnd,
	 L"ImDisk Virtual Disk Driver for Windows NT/2000/XP/2003.\r\n"
	 L"\r\n"
	 L"Copyright (C) 2004-2007 Olof Lagerkvist.\r\n"
	 L"http://www.ltr-data.se     olof@ltr-data.se\r\n"
	 L"\r\n"
	 L"Some credits:\r\n"
	 L"- Parts related to floppy emulation based on VFD by Ken Kato.\r\n"
	 L"  http://chitchat.at.infoseek.co.jp/vmware/vfd.html\r\n"
	 L"- Parts related to CD-ROM emulation and impersonation to\r\n"
	 L"  support remote files based on FileDisk by Bo BrantÈn.\r\n"
	 L"  http://www.acc.umu.se/~bosse/\r\n"
	 L"- Virtual memory image support, usermode storage backend\r\n"
	 L"  support and some code ported to NT from the FreeBSD md driver\r\n"
	 L"  by Olof Lagerkvist.\r\n"
	 L"  http://www.ltr-data.se\r\n"
	 L"\r\n"
	 L"This program is free software; you can redistribute it and/or\r\n"
	 L"modify it under the terms of the GNU General Public License as\r\n"
	 L"published by the Free Software Foundation.\r\n",
	 L"About ImDisk Virtual Disk Driver",
	 MB_ICONINFORMATION);

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
		      CplAppletDlgProc) == -1)
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
DllMain(HINSTANCE hinstDLL,  DWORD, LPVOID)
{
  hInstance = hinstDLL;
  return TRUE;
}
