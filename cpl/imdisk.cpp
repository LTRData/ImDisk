/*
    Control Panel Applet for the ImDisk Virtual Disk Driver for
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

extern "C" HINSTANCE hInstance = NULL;

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
  _snwprintf(wcBuffer, sizeof(wcBuffer)/sizeof(*wcBuffer),
	     L"%.4g %s",
	     _h(create_data->DiskGeometry.Cylinders.QuadPart),
	     _p(create_data->DiskGeometry.Cylinders.QuadPart));
  wcBuffer[sizeof(wcBuffer)/sizeof(*wcBuffer)] = 0;

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
CplAppletDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM /*lParam*/)
{
  switch (uMsg)
    {
    case WM_INITDIALOG:
      {
	HWND hWndListView = GetDlgItem(hWnd, IDC_LISTVIEW);

	LVCOLUMN lvc = { 0 };
	lvc.mask = LVCF_WIDTH | LVCF_TEXT;
	lvc.cx = 40;
	lvc.pszText = L"Drive";

	ListView_InsertColumn(hWndListView, 0, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 280;
	lvc.pszText = L"Image file";

	ListView_InsertColumn(hWndListView, 1, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 90;
	lvc.pszText = L"Size";

	ListView_InsertColumn(hWndListView, 2, &lvc);

	lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.cx = 130;
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
	
	return TRUE;
      }

    case WM_NOTIFY:
      switch ((int) wParam)
	{
	case IDC_LISTVIEW:
	  if (SendDlgItemMessage(hWnd, IDC_LISTVIEW, LVM_GETNEXTITEM,
				 (WPARAM) -1,
				 MAKELPARAM((UINT) LVNI_SELECTED, 0)) != -1)
	    {
	      EnableWindow(GetDlgItem(hWnd, IDC_BTN_DELETE), TRUE);
	      EnableWindow(GetDlgItem(hWnd, IDC_BTN_EXTEND), TRUE);
	    }
	  else
	    {
	      EnableWindow(GetDlgItem(hWnd, IDC_BTN_DELETE), FALSE);
	      EnableWindow(GetDlgItem(hWnd, IDC_BTN_EXTEND), FALSE);
	    }

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

	case IDC_BTN_ABOUT:
	  MsgBoxPrintF
	    (hWnd,
	     MB_ICONINFORMATION,
	     L"About ImDisk Virtual Disk Driver",
	     L"ImDisk Virtual Disk Driver for Windows NT/2000/XP/2003.\r\n"
	     L"Version %1!i!.%2!i!.%3!i! - (Compiled %4!hs!)\r\n"
	     L"\r\n"
	     L"Copyright (C) 2004-2007 Olof Lagerkvist.\r\n"
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

	case IDC_BTN_EXTEND:
	  if (DialogBox(hInstance, MAKEINTRESOURCE(IDD_DLG_EXTEND), hWnd,
			ExtendDlgProc) == IDOK)
	    RefreshList(GetDlgItem(hWnd, IDC_LISTVIEW));

	  return TRUE;
	}
      
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
