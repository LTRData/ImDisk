/*
    API library for the ImDisk Virtual Disk Driver for Windows NT/2000/XP.

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
#include <shellapi.h>

#include <stdio.h>

#include "..\inc\ntumapi.h"
#include "..\inc\imdisk.h"
#include "..\inc\imdproxy.h"

#include "drvio.h"

#pragma warning(disable: 4100)

void
DoEvents(HWND hWnd)
{
  MSG msg;
  while (PeekMessage(&msg, hWnd, 0, 0, PM_REMOVE))
    {
      if (!IsDialogMessage(hWnd, &msg))
	TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
}

BOOL
CALLBACK
StatusDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
    ShowWindow(hWnd, SW_SHOW);

  return FALSE;
}

BOOL
CDECL
MsgBoxPrintF(HWND hWnd, UINT uStyle, LPCWSTR lpTitle, LPCWSTR lpMessage, ...)
{
  va_list param_list;
  LPWSTR lpBuf = NULL;

  va_start(param_list, lpMessage);

  if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		     FORMAT_MESSAGE_FROM_STRING, lpMessage, 0, 0,
		     (LPWSTR) &lpBuf, 0, &param_list))
    return FALSE;

  MessageBox(hWnd, lpBuf, lpTitle, uStyle);
  LocalFree(lpBuf);
  return TRUE;
}

VOID
WINAPI
MsgBoxLastError(HWND hWnd, LPCWSTR Prefix)
{
  LPWSTR MsgBuf;

  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, GetLastError(), 0, (LPWSTR) &MsgBuf, 0, NULL);

  MsgBoxPrintF(hWnd, MB_ICONEXCLAMATION,
	       L"ImDisk Virtual Disk Driver",
	       L"%1\r\n\r\n%2", Prefix, MsgBuf);

  LocalFree(MsgBuf);
}

BOOL
WINAPI
ImDiskStartService(LPWSTR ServiceName)
{
  SC_HANDLE hSCManager;
  SC_HANDLE hService;

  hSCManager = OpenSCManager(NULL, NULL, 0);
  if (hSCManager == NULL)
    return FALSE;

  hService = OpenService(hSCManager, ServiceName, SERVICE_START);
  if (hService == NULL)
    {
      DWORD dwLastError = GetLastError();
      CloseServiceHandle(hSCManager);
      SetLastError(dwLastError);
      return FALSE;
    }

  if (!StartService(hService, 0, NULL))
    {
      DWORD dwLastError = GetLastError();
      CloseServiceHandle(hService);
      CloseServiceHandle(hSCManager);
      SetLastError(dwLastError);
      return FALSE;
    }

  CloseServiceHandle(hService);
  CloseServiceHandle(hSCManager);
  return TRUE;
}

BOOL
WINAPI
ImDiskCreateMountPoint(LPCWSTR Directory, LPCWSTR Target)
{
  int iSize = (wcslen(Target) + 1) << 1;
  REPARSE_DATA_JUNCTION ReparseData = { 0 };
  HANDLE hDir;
  DWORD dw;

  hDir = CreateFile(Directory, GENERIC_READ | GENERIC_WRITE,
		    FILE_SHARE_READ, NULL, OPEN_EXISTING,
		    FILE_FLAG_BACKUP_SEMANTICS |
		    FILE_FLAG_OPEN_REPARSE_POINT, NULL);

  if (hDir == INVALID_HANDLE_VALUE)
    return FALSE;

  if ((iSize + 6 > sizeof(ReparseData.Data)) | (iSize == 0))
    {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }

  ReparseData.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  ReparseData.ReparseDataLength = (WORD) (8 + iSize + 2 + iSize + 2);
  ReparseData.NameLength = (WORD) iSize;
  ReparseData.DisplayNameOffset = (WORD) (iSize + 2);
  ReparseData.DisplayNameLength = (WORD) iSize;
  wcscpy((LPWSTR) ReparseData.Data, Target);
  ((LPWSTR) ReparseData.Data)[(iSize >> 1) - 1] = L'\\';
  wcscpy((LPWSTR) (ReparseData.Data + ReparseData.DisplayNameOffset), Target);
  ((LPWSTR) (ReparseData.Data + ReparseData.DisplayNameOffset))
    [(iSize >> 1) - 1] = L'\\';

  if (!DeviceIoControl(hDir, FSCTL_SET_REPARSE_POINT, &ReparseData,
		       16 + iSize + 2 + iSize + 2, NULL, 0, &dw, NULL))
    {
      DWORD last_error = GetLastError();
      CloseHandle(hDir);
      SetLastError(last_error);
      return FALSE;
    }
  else
    {
      CloseHandle(hDir);
      return TRUE;
    }
}

BOOL
WINAPI
ImDiskRemoveMountPoint(LPCWSTR MountPoint)
{
  REPARSE_DATA_JUNCTION ReparseData = { 0 };
  HANDLE hDir;
  DWORD dw;

  hDir = CreateFile(MountPoint, GENERIC_READ | GENERIC_WRITE,
		    FILE_SHARE_READ, NULL, OPEN_EXISTING,
		    FILE_FLAG_BACKUP_SEMANTICS |
		    FILE_FLAG_OPEN_REPARSE_POINT, NULL);

  if (hDir == INVALID_HANDLE_VALUE)
    return FALSE;

  ReparseData.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;

  if (!DeviceIoControl(hDir, FSCTL_DELETE_REPARSE_POINT, &ReparseData,
		       REPARSE_GUID_DATA_BUFFER_HEADER_SIZE, NULL, 0, &dw,
		       NULL))
    {
      DWORD last_error = GetLastError();
      CloseHandle(hDir);
      SetLastError(last_error);
      return FALSE;
    }
  else
    {
      CloseHandle(hDir);
      return TRUE;
    }
}

HANDLE
WINAPI
ImDiskOpenDeviceByName(PUNICODE_STRING FileName, DWORD AccessMode)
{
  NTSTATUS status;
  HANDLE handle;
  OBJECT_ATTRIBUTES object_attrib;
  IO_STATUS_BLOCK io_status;

  InitializeObjectAttributes(&object_attrib,
			     FileName,
			     OBJ_CASE_INSENSITIVE,
			     NULL,
			     NULL);

  status = NtOpenFile(&handle,
		      SYNCHRONIZE | AccessMode,
		      &object_attrib,
		      &io_status,
		      FILE_SHARE_READ | FILE_SHARE_WRITE,
		      FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);

  if (!NT_SUCCESS(status))
    {
      SetLastError(RtlNtStatusToDosError(status));
      return INVALID_HANDLE_VALUE;
    }

  return handle;
}

HANDLE
WINAPI
ImDiskOpenDeviceByNumber(DWORD DeviceNumber, DWORD AccessMode)
{
  WCHAR device_path[MAX_PATH];
  UNICODE_STRING file_name;

  // Build device path, e.g. \Device\ImDisk2
  _snwprintf(device_path, sizeof(device_path) / sizeof(*device_path),
	     IMDISK_DEVICE_BASE_NAME L"%u", DeviceNumber);
  device_path[sizeof(device_path)/sizeof(*device_path) - 1] = 0;

  RtlInitUnicodeString(&file_name, device_path);
  return ImDiskOpenDeviceByName(&file_name, AccessMode);
}

HANDLE
WINAPI
ImDiskOpenDeviceByMountPoint(LPCWSTR MountPoint, DWORD AccessMode)
{
  REPARSE_DATA_JUNCTION ReparseData = { 0 };
  HANDLE hDir;
  DWORD dw;
  UNICODE_STRING DeviceName;

  hDir = CreateFile(MountPoint, GENERIC_READ,
		    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		    FILE_FLAG_BACKUP_SEMANTICS |
		    FILE_FLAG_OPEN_REPARSE_POINT, NULL);

  if (hDir == INVALID_HANDLE_VALUE)
    return INVALID_HANDLE_VALUE;

  if (!DeviceIoControl(hDir, FSCTL_GET_REPARSE_POINT, NULL, 0, &ReparseData,
		       sizeof ReparseData, &dw, NULL))

    {
      DWORD last_error = GetLastError();
      CloseHandle(hDir);
      SetLastError(last_error);
      return INVALID_HANDLE_VALUE;
    }

  CloseHandle(hDir);

  if (ReparseData.ReparseTag != IO_REPARSE_TAG_MOUNT_POINT)
    {
      SetLastError(ERROR_NOT_A_REPARSE_POINT);
      return INVALID_HANDLE_VALUE;
    }

  DeviceName.Length = ReparseData.NameLength;
  DeviceName.Buffer = (PWSTR) ReparseData.Data + ReparseData.NameOffset;
  DeviceName.MaximumLength = DeviceName.Length;

  if (DeviceName.Buffer[(DeviceName.Length >> 1) - 1] == L'\\')
    {
      DeviceName.Buffer[(DeviceName.Length >> 1) - 1] = 0;
      DeviceName.Length -= 2;
    }

  return ImDiskOpenDeviceByName(&DeviceName, AccessMode);
}

BOOL
WINAPI
ImDiskCheckDriverVersion(HANDLE Device)
{
  DWORD VersionCheck;
  DWORD BytesReturned;

  if (!DeviceIoControl(Device,
                       IOCTL_IMDISK_QUERY_VERSION,
                       NULL, 0,
		       &VersionCheck, sizeof VersionCheck,
                       &BytesReturned, NULL))
    return FALSE;

  SetLastError(NO_ERROR);

  if (BytesReturned < sizeof VersionCheck)
    return FALSE;

  if (VersionCheck != IMDISK_VERSION)
    return FALSE;

  return TRUE;
}

BOOL
WINAPI
ImDiskGetVersion(PULONG LibraryVersion,
		 PULONG DriverVersion)
{
  if (LibraryVersion != NULL)
    *LibraryVersion = IMDISK_VERSION;

  if (DriverVersion != NULL)
    {
      UNICODE_STRING file_name;
      HANDLE driver;
      DWORD dw;

      *DriverVersion = 0;

      RtlInitUnicodeString(&file_name, IMDISK_CTL_DEVICE_NAME);

      for (;;)
	{
	  driver = ImDiskOpenDeviceByName(&file_name,
					  GENERIC_READ | GENERIC_WRITE);

	  if (driver != INVALID_HANDLE_VALUE)
	    break;

	  if (GetLastError() != ERROR_FILE_NOT_FOUND)
	    return FALSE;

	  if (!ImDiskStartService(IMDISK_DRIVER_NAME))
	    return FALSE;
	  }

      if (!DeviceIoControl(driver,
			   IOCTL_IMDISK_QUERY_VERSION,
			   NULL, 0,
			   DriverVersion, sizeof(ULONG),
			   &dw, NULL))
	{
	  NtClose(driver);
	  return FALSE;
	}

      NtClose(driver);

      if (dw < sizeof(ULONG))
	return FALSE;
    }

  return TRUE;
}

WCHAR
WINAPI
ImDiskFindFreeDriveLetter()
{
  DWORD logical_drives = GetLogicalDrives();
  WCHAR search;
  for (search = L'D'; search < L'Z'; search++)
    if ((logical_drives & (1 << (search - L'A'))) == 0)
      return search;

  return 0;
}

ULONG
WINAPI
ImDiskGetDeviceList()
{
  UNICODE_STRING file_name;
  HANDLE driver;
  ULONG device_list;
  DWORD dw;

  RtlInitUnicodeString(&file_name, IMDISK_CTL_DEVICE_NAME);

  driver = ImDiskOpenDeviceByName(&file_name, GENERIC_READ);
  if (driver == INVALID_HANDLE_VALUE)
    return 0;

  if (!DeviceIoControl(driver,
		       IOCTL_IMDISK_QUERY_DRIVER,
		       NULL, 0,
		       &device_list, sizeof(device_list),
		       &dw, NULL))
    {
      NtClose(driver);
      return 0;
    }

  NtClose(driver);
  SetLastError(NO_ERROR);
  return device_list;
}

BOOL
WINAPI
ImDiskQueryDevice(DWORD DeviceNumber,
		  PIMDISK_CREATE_DATA CreateData,
		  ULONG CreateDataSize)
{
  HANDLE device = ImDiskOpenDeviceByNumber(DeviceNumber, GENERIC_READ);
  DWORD dw;

  if (device == INVALID_HANDLE_VALUE)
    return FALSE;

  if (!DeviceIoControl(device,
                       IOCTL_IMDISK_QUERY_DEVICE,
                       NULL,
                       0,
                       CreateData,
                       CreateDataSize,
                       &dw, NULL))
    {
      NtClose(device);
      return FALSE;
    }

  NtClose(device);

  if (dw < sizeof(IMDISK_CREATE_DATA) - sizeof(*CreateData->FileName))
    {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }

  return TRUE;
}

BOOL
WINAPI
ImDiskCreateDevice(HWND hWnd,
		   PDISK_GEOMETRY DiskGeometry,
		   DWORD Flags,
		   LPCWSTR FileName,
		   BOOL NativePath,
		   LPWSTR MountPoint)
{
  PIMDISK_CREATE_DATA create_data;
  UNICODE_STRING file_name;
  HANDLE driver;
  DWORD dw;

  RtlInitUnicodeString(&file_name, IMDISK_CTL_DEVICE_NAME);

  for (;;)
    {
      if (hWnd != NULL)
	SetWindowText(hWnd, L"Opening the ImDisk Virtual Disk Driver...");

      driver = ImDiskOpenDeviceByName(&file_name,
				      GENERIC_READ | GENERIC_WRITE);

      if (driver != INVALID_HANDLE_VALUE)
	break;

      if (GetLastError() != ERROR_FILE_NOT_FOUND)
	{
	  if (hWnd != NULL)
	    MsgBoxLastError(hWnd, L"Error controlling the ImDisk driver:");

	  return FALSE;
	}

      if (hWnd != NULL)
	SetWindowText(hWnd, L"Loading the ImDisk Virtual Disk Driver...");

      if (!ImDiskStartService(IMDISK_DRIVER_NAME))
	switch (GetLastError())
	  {
	  case ERROR_SERVICE_DOES_NOT_EXIST:
	    if (hWnd != NULL)
	      MessageBox(hWnd,
			 L"The ImDisk Virtual Disk Driver is not installed. "
			 L"Please re-install ImDisk.",
			 L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
	    return FALSE;

	  case ERROR_PATH_NOT_FOUND:
	  case ERROR_FILE_NOT_FOUND:
	    if (hWnd != NULL)
	      MessageBox(hWnd,
			 L"Cannot load imdisk.sys. "
			 L"Please re-install ImDisk.",
			 L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
	    return FALSE;

	  case ERROR_SERVICE_DISABLED:
	    if (hWnd != NULL)
	      MessageBox(hWnd,
			 L"The ImDisk Virtual Disk Driver is not disabled.",
			 L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
	    return FALSE;

	  default:
	    if (hWnd != NULL)
	      MsgBoxLastError(hWnd,
			      L"Error loading ImDisk Virtual Disk Driver:");
	    return FALSE;
	  }

      DoEvents(NULL);
      if (hWnd != NULL)
	SetWindowText(hWnd, L"The ImDisk Virtual Disk Driver was loaded.");
    }

  if (!ImDiskCheckDriverVersion(driver))
    {
      NtClose(driver);
      if (hWnd != NULL)
	MessageBox(hWnd,
		   L"The version of the ImDisk Virtual Disk Driver "
		   L"(imdisk.sys) installed on this system does not match "
		   L"the version of this Control Panel applet. Please "
		   L"reinstall ImDisk to make sure that all components of it "
		   L"on this system are from the same install package.",
		   L"ImDisk Virtual Disk Driver", MB_ICONSTOP);

      SetLastError(ERROR_INVALID_FUNCTION);
      return FALSE;
    }

  // Proxy reconnection types requires the user mode service.
  if ((IMDISK_TYPE(Flags) == IMDISK_TYPE_PROXY) &
      (IMDISK_PROXY_TYPE(Flags) != IMDISK_PROXY_TYPE_DIRECT))
    {
      if (!WaitNamedPipe(IMDPROXY_SVC_PIPE_DOSDEV_NAME, 0))
	if (GetLastError() == ERROR_FILE_NOT_FOUND)
	  if (ImDiskStartService(IMDPROXY_SVC))
	    {
	      while (!WaitNamedPipe(IMDPROXY_SVC_PIPE_DOSDEV_NAME, 0))
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
		  Sleep(500);
		else
		  break;

	      if (hWnd != NULL)
		SetWindowText
		  (hWnd,
		   L"ImDisk Virtual Disk Driver Helper Service started.");
	    }
	  else
	    {
	      switch (GetLastError())
		{
		case ERROR_SERVICE_DOES_NOT_EXIST:
		  if (hWnd != NULL)
		    MessageBox(hWnd,
			       L"The ImDisk Virtual Disk Driver Helper "
			       L"Service is not installed. Please re-install "
			       L"ImDisk.",
			       L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
		  break;

		case ERROR_PATH_NOT_FOUND:
		case ERROR_FILE_NOT_FOUND:
		  if (hWnd != NULL)
		    MessageBox(hWnd,
			       L"Cannot start the ImDisk Virtual Disk Driver "
			       L"Helper Service. Please re-install ImDisk.",
			       L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
		  break;

		case ERROR_SERVICE_DISABLED:
		  if (hWnd != NULL)
		    MessageBox(hWnd,
			       L"The ImDisk Virtual Disk Driver Helper "
			       L"Service is disabled.",
			       L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
		  break;

		default:
		  if (hWnd != NULL)
		    MsgBoxLastError
		      (hWnd,
		       L"Error starting ImDisk Virtual Disk Driver Helper "
		       L"Service:");
		}

	      NtClose(driver);
	      return FALSE;
	    }
    }

  if (FileName == NULL)
    RtlInitUnicodeString(&file_name, NULL);
  else
    if (NativePath)
      {
	if (!RtlCreateUnicodeString(&file_name, FileName))
	  {
	    NtClose(driver);
	    if (hWnd != NULL)
	      MessageBox(hWnd, L"Memory allocation error.",
			 L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
	    return FALSE;
	  }
      }
    else
      {
	if (!RtlDosPathNameToNtPathName_U(FileName, &file_name, NULL, NULL))
	  {
	    NtClose(driver);
	    if (hWnd != NULL)
	      MessageBox(hWnd, L"Memory allocation error.",
			 L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
	    return FALSE;
	  }
      }

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Creating virtual disk...");

  create_data = _alloca(sizeof(IMDISK_CREATE_DATA) + file_name.Length);
  if (create_data == NULL)
    {
      NtClose(driver);
      RtlFreeUnicodeString(&file_name);
      if (hWnd != NULL)
	MessageBox(hWnd, L"Memory allocation error.",
		   L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
      return FALSE;
    }

  ZeroMemory(create_data, sizeof(IMDISK_CREATE_DATA) + file_name.Length);

  // Check if mount point is a drive letter or junction point
  if (MountPoint != NULL)
    if ((wcslen(MountPoint) == 2) ? MountPoint[1] == L':' : FALSE)
      create_data->DriveLetter = MountPoint[0];

  create_data->DeviceNumber   = IMDISK_AUTO_DEVICE_NUMBER;
  create_data->DiskGeometry   = *DiskGeometry;
  create_data->Flags          = Flags;
  create_data->FileNameLength = file_name.Length;

  if (file_name.Length != 0)
    {
      memcpy(&create_data->FileName, file_name.Buffer, file_name.Length);
      RtlFreeUnicodeString(&file_name);
    }

  if (!DeviceIoControl(driver,
                       IOCTL_IMDISK_CREATE_DEVICE,
                       create_data,
		       sizeof(IMDISK_CREATE_DATA) +
		       create_data->FileNameLength,
                       create_data,
		       sizeof(IMDISK_CREATE_DATA) +
		       create_data->FileNameLength,
                       &dw,
		       NULL))
    {
      if (hWnd != NULL)
	MsgBoxLastError(hWnd, L"Error creating virtual disk:");
      NtClose(driver);
      return FALSE;
    }

  NtClose(driver);

  if (MountPoint != NULL)
    {
      WCHAR device_path[MAX_PATH];

      if (hWnd != NULL)
	SetWindowText(hWnd, L"Creating mount point...");

      // Build device path, e.g. \Device\ImDisk2
      _snwprintf(device_path, sizeof(device_path) / sizeof(*device_path),
		 IMDISK_DEVICE_BASE_NAME L"%u", create_data->DeviceNumber);
      device_path[sizeof(device_path)/sizeof(*device_path) - 1] = 0;

      if (!DefineDosDevice(DDD_RAW_TARGET_PATH, MountPoint, device_path))
	if (hWnd != NULL)
	  MsgBoxLastError(hWnd, L"Error creating mount point:");
    }

  return TRUE;
}

BOOL
WINAPI
ImDiskRemoveDevice(HWND hWnd,
		   DWORD DeviceNumber,
		   LPCWSTR MountPoint)
{
  HANDLE device;
  DWORD dw;

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Opening device...");

  if (MountPoint == NULL)
    {
      device = ImDiskOpenDeviceByNumber(DeviceNumber,
					GENERIC_READ | GENERIC_WRITE);
      if (device == INVALID_HANDLE_VALUE)
	device = ImDiskOpenDeviceByNumber(DeviceNumber,
					  GENERIC_READ);
    }
  else if ((wcslen(MountPoint) == 2) ? MountPoint[1] == ':' : 
	   (wcslen(MountPoint) == 3) ? wcscmp(MountPoint + 1, L":\\") == 0 :
	   FALSE)
    {
      WCHAR drive_letter_path[] = L"\\\\.\\ :";

      drive_letter_path[4] = MountPoint[0];

      device = CreateFile(drive_letter_path,
			  GENERIC_READ | GENERIC_WRITE,
			  FILE_SHARE_READ | FILE_SHARE_WRITE,
			  NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);

      if (device == INVALID_HANDLE_VALUE)
	device = CreateFile(drive_letter_path,
			    GENERIC_READ,
			    FILE_SHARE_READ | FILE_SHARE_WRITE,
			    NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    }
  else
    {
      if (hWnd != NULL)
	MsgBoxPrintF(hWnd, MB_ICONSTOP, L"ImDisk Virtual Disk Driver",
		     L"Unsupported mount point: '%1'", MountPoint);
      SetLastError(ERROR_INVALID_FUNCTION);
      return FALSE;
    }

  if (device == INVALID_HANDLE_VALUE)
    {
      if (hWnd != NULL)
	MsgBoxLastError(hWnd, L"Error opening device:");
      return FALSE;
    }

  if (!ImDiskCheckDriverVersion(device))
    if (GetLastError() != NO_ERROR)
      {
	if (hWnd != NULL)
	  MsgBoxPrintF(hWnd, MB_ICONSTOP, L"ImDisk Virtual Disk Driver",
		       L"Not an ImDisk Virtual Disk: '%1'", MountPoint);

	return FALSE;
      }

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Flushing file buffers...");

  FlushFileBuffers(device);

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Locking volume...");

  if (!DeviceIoControl(device,
                       FSCTL_LOCK_VOLUME,
                       NULL,
		       0,
		       NULL,
		       0,
		       &dw,
		       NULL))
    if (hWnd == NULL)
      return FALSE;
    else
      if (MessageBox(hWnd,
		     L"Cannot lock the device. The device may be in use by "
		     L"another process or you may not have permission to lock "
		     L"it. Do you want do try to force dismount of the "
		     L"volume? (Unsaved data on the volume will be lost.)",
		     L"ImDisk Virtual Disk Driver",
		     MB_ICONEXCLAMATION | MB_YESNO | MB_DEFBUTTON2) == IDNO)
	{
	  NtClose(device);
	  return FALSE;
	}

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Dismounting filesystem...");

  DeviceIoControl(device,
		  FSCTL_DISMOUNT_VOLUME,
		  NULL,
		  0,
		  NULL,
		  0,
		  &dw,
		  NULL);

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Removing device...");

  if (!DeviceIoControl(device,
                       IOCTL_STORAGE_EJECT_MEDIA,
                       NULL,
		       0,
		       NULL,
		       0,
		       &dw,
		       NULL))
    {
      if (hWnd != NULL)
	MsgBoxLastError(hWnd, L"Error removing device:");
      NtClose(device);
      return FALSE;
    }

  DeviceIoControl(device,
		  FSCTL_UNLOCK_VOLUME,
		  NULL,
		  0,
		  NULL,
		  0,
		  &dw,
		  NULL);

  NtClose(device);

  if (MountPoint != NULL)
    {
      if (hWnd != NULL)
	SetWindowText(hWnd, L"Removing drive letter...");

      if (!DefineDosDevice(DDD_REMOVE_DEFINITION, MountPoint, NULL))
	if (hWnd != NULL)
	  MsgBoxLastError(hWnd, L"Error removing drive letter:");
    }

  if (hWnd != NULL)
    SetWindowText(hWnd, L"OK.");

  return TRUE;
}

BOOL
WINAPI
ImDiskExtendDevice(HWND hWnd,
		   DWORD DeviceNumber,
		   CONST PLARGE_INTEGER ExtendSize)
{
  HANDLE device;
  DWORD dw;
  DISK_GROW_PARTITION grow_partition = { 0 };
  GET_LENGTH_INFORMATION length_information;
  DISK_GEOMETRY disk_geometry;
  LONGLONG new_filesystem_size;

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Opening device...");

  device = ImDiskOpenDeviceByNumber(DeviceNumber,
				    GENERIC_READ | GENERIC_WRITE);

  if (device == INVALID_HANDLE_VALUE)
    device = ImDiskOpenDeviceByNumber(DeviceNumber,
				      GENERIC_READ);

  if (device == INVALID_HANDLE_VALUE)
    {
      if (hWnd != NULL)
	MsgBoxLastError(hWnd, L"Error opening device:");
      return FALSE;
    }

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Extending disk size...");

  grow_partition.PartitionNumber = 1;
  grow_partition.BytesToGrow = *ExtendSize;
  if (!DeviceIoControl(device,
                       IOCTL_DISK_GROW_PARTITION,
                       &grow_partition,
                       sizeof(grow_partition),
                       NULL,
                       0,
                       &dw, NULL))
    {
      if (hWnd != NULL)
	if (GetLastError() == ERROR_INVALID_FUNCTION)
	  MessageBox
	    (hWnd,
	     L"It is only possible to extend the size of a virtual disk that "
	     L"is backed by a physical file. It is not possible to extend "
	     L"virtual disks backed by virtual memory.",
	     L"ImDisk Virtual Disk Driver",
	     MB_ICONSTOP);
	else
	  MsgBoxLastError(hWnd, L"Error extending the virtual disk size:");

      NtClose(device);
      return FALSE;
    }

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Extending filesystem size...");

  if (!DeviceIoControl(device,
                       IOCTL_DISK_GET_LENGTH_INFO,
                       NULL,
                       0,
                       &length_information,
                       sizeof(length_information),
                       &dw, NULL))
    {
      if (hWnd != NULL)
	MsgBoxLastError
	  (hWnd,
	   L"An error occured when attempting to check the new total size of "
	   L"the resized virtual disk:");

      NtClose(device);
      return FALSE;
    }

  if (!DeviceIoControl(device,
                       IOCTL_DISK_GET_DRIVE_GEOMETRY,
                       NULL,
                       0,
                       &disk_geometry,
                       sizeof(disk_geometry),
                       &dw, NULL))
    {
      if (hWnd != NULL)
	MsgBoxLastError
	  (hWnd,
	   L"An error occured when attempting to check the new total size of "
	   L"the resized virtual disk:");

      NtClose(device);
      return FALSE;
    }

  new_filesystem_size =
    length_information.Length.QuadPart /
    disk_geometry.BytesPerSector;

  if (!DeviceIoControl(device,
                       FSCTL_EXTEND_VOLUME,
                       &new_filesystem_size,
                       sizeof(new_filesystem_size),
                       NULL,
                       0,
                       &dw, NULL))
    if (hWnd != NULL)
      MsgBoxLastError
	(hWnd,
	 L"The disk size was extended successfully, but it was not possible "
	 L"to extend the current filesystem on it. You will have to reformat "
	 L"the disk to use the full disk size.");

  NtClose(device);
  return TRUE;
}
