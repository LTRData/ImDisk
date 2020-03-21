#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>

#include <stdio.h>

#include "..\inc\ntumapi.h"
#include "..\inc\imdisk.h"
#include "..\inc\imdproxy.h"

#include "drvio.h"

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
MsgBoxPrintF(HWND hWnd, UINT uStyle, LPCWSTR lpMessage, ...)
{
  va_list param_list;
  LPWSTR lpBuf = NULL;

  va_start(param_list, lpMessage);

  if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		     FORMAT_MESSAGE_FROM_STRING, lpMessage, 0, 0,
		     (LPWSTR) &lpBuf, 0, &param_list))
    return FALSE;

  MessageBox(hWnd, lpBuf, L"ImDisk", uStyle);
  LocalFree(lpBuf);
  return TRUE;
}

void
MsgBoxLastError(HWND hWnd, LPCWSTR Prefix)
{
  LPWSTR MsgBuf;

  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, GetLastError(), 0, (LPWSTR) &MsgBuf, 0, NULL);

  MsgBoxPrintF(hWnd, MB_ICONEXCLAMATION, L"%1\r\n\r\n%2", Prefix, MsgBuf);

  LocalFree(MsgBuf);
}

BOOL
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

HANDLE
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

BOOL
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

  if (BytesReturned < sizeof VersionCheck)
    return FALSE;

  if (VersionCheck != IMDISK_VERSION)
    return FALSE;

  return TRUE;
}

BOOL
ImDiskCreate(HWND hWnd,
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
      SetWindowText(hWnd, L"Opening the ImDisk Virtual Disk Driver...");

      driver = ImDiskOpenDeviceByName(&file_name,
				      GENERIC_READ | GENERIC_WRITE);

      if (driver != INVALID_HANDLE_VALUE)
	break;

      if (GetLastError() != ERROR_FILE_NOT_FOUND)
	{
	  MsgBoxLastError(hWnd, L"Error controlling the ImDisk driver:");
	  return FALSE;
	}

      SetWindowText(hWnd, L"Loading the ImDisk Virtual Disk Driver...");

      if (!ImDiskStartService(IMDISK_DRIVER_NAME))
	switch (GetLastError())
	  {
	  case ERROR_SERVICE_DOES_NOT_EXIST:
	    MessageBox(hWnd,
		       L"The ImDisk Virtual Disk Driver is not installed. "
		       L"Please re-install ImDisk.", L"ImDisk",
		       MB_ICONSTOP);
	    return FALSE;

	  case ERROR_PATH_NOT_FOUND:
	  case ERROR_FILE_NOT_FOUND:
	    MessageBox(hWnd,
		       L"Cannot load imdisk.sys. "
		       L"Please re-install ImDisk.", L"ImDisk",
		       MB_ICONSTOP);
	    return FALSE;

	  case ERROR_SERVICE_DISABLED:
	    MessageBox(hWnd,
		       L"The ImDisk Virtual Disk Driver is not disabled.",
		       L"ImDisk", MB_ICONSTOP);
	    return FALSE;

	  default:
	    MsgBoxLastError(hWnd,
			    L"Error loading ImDisk Virtual Disk Driver:");
	    return FALSE;
	  }

      DoEvents(NULL);
      SetWindowText(hWnd, L"The ImDisk Virtual Disk Driver was loaded.");
    }

  if (!ImDiskCheckDriverVersion(driver))
    {
      NtClose(driver);
      MessageBox(hWnd,
		 L"The version of the ImDisk Virtual Disk Driver "
		 L"(imdisk.sys) installed on this system does not match "
		 L"the version of this control applet. Please "
		 L"reinstall ImDisk to make sure that all components of it "
		 L"on this system are from the same install package.",
		 L"ImDisk", MB_ICONSTOP);
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

	      SetWindowText(hWnd,
			    L"ImDisk I/O Packet Forwarder Service started.");
	    }
	  else
	    {
	      switch (GetLastError())
		{
		case ERROR_SERVICE_DOES_NOT_EXIST:
		  MessageBox(hWnd,
			     L"The ImDisk I/O Packet Forwarder Service is not "
			     L"installed. Please re-install ImDisk.",
			     L"ImDisk", MB_ICONSTOP);
		  break;

		case ERROR_PATH_NOT_FOUND:
		case ERROR_FILE_NOT_FOUND:
		  MessageBox(hWnd,
			     L"Cannot start the ImDisk I/O Packet Forwarder "
			     L"Service. Please re-install ImDisk.",
			     L"ImDisk", MB_ICONSTOP);
		  break;

		case ERROR_SERVICE_DISABLED:
		  MessageBox(hWnd,
			     L"The ImDisk I/O Packet Forwarder Service is "
			     L"disabled.",
			     L"ImDisk", MB_ICONSTOP);
		  break;

		default:
		  MsgBoxLastError
		    (hWnd,
		     L"Error starting ImDisk I/O Packet Forwarder Service:");
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
	    MessageBox(hWnd, L"Memory allocation error.", L"ImDisk",
		       MB_ICONSTOP);
	    return FALSE;
	  }
      }
    else
      {
	if (!RtlDosPathNameToNtPathName_U(FileName, &file_name, NULL, NULL))
	  {
	    NtClose(driver);
	    MessageBox(hWnd, L"Memory allocation error.", L"ImDisk",
		       MB_ICONSTOP);
	    return FALSE;
	  }
      }

  SetWindowText(hWnd, L"Creating virtual disk...");

  create_data = _alloca(sizeof(IMDISK_CREATE_DATA) + file_name.Length);
  if (create_data == NULL)
    {
      NtClose(driver);
      RtlFreeUnicodeString(&file_name);
      MessageBox(hWnd, L"Memory allocation error.", L"ImDisk", MB_ICONSTOP);
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
      MsgBoxLastError(hWnd, L"Error creating virtual disk:");
      NtClose(driver);
      return FALSE;
    }

  NtClose(driver);

  if (MountPoint != NULL)
    {
      WCHAR device_path[MAX_PATH];

      SetWindowText(hWnd, L"Creating mount point...");

      // Build device path, e.g. \Device\ImDisk2
      _snwprintf(device_path, sizeof(device_path) / sizeof(*device_path),
		 IMDISK_DEVICE_BASE_NAME L"%u", create_data->DeviceNumber);
      device_path[sizeof(device_path)/sizeof(*device_path) - 1] = 0;

      if (!DefineDosDevice(DDD_RAW_TARGET_PATH, MountPoint, device_path))
	      MsgBoxLastError(hWnd, L"Error creating mount point:");
    }

  return TRUE;
}

BOOL
ImDiskRemove(HWND hWnd, DWORD DeviceNumber, LPCWSTR MountPoint)
{
  HANDLE device;
  DWORD dw;

  SetWindowText(hWnd, L"Opening device...");

  device = ImDiskOpenDeviceByNumber(DeviceNumber,
				    GENERIC_READ | GENERIC_WRITE);

  if (device == INVALID_HANDLE_VALUE)
    device = ImDiskOpenDeviceByNumber(DeviceNumber,
				      GENERIC_READ);

  if (device == INVALID_HANDLE_VALUE)
    {
      MsgBoxLastError(hWnd, L"Error opening device:");
      return FALSE;
    }

  if (!ImDiskCheckDriverVersion(device))
    {
      MessageBox(hWnd, L"Incompatible version of ImDisk driver.", L"ImDisk",
		 MB_ICONSTOP);
      NtClose(device);
      return FALSE;
    }

  SetWindowText(hWnd, L"Flushing file buffers...");

  FlushFileBuffers(device);

  SetWindowText(hWnd, L"Locking volume...");

  if (!DeviceIoControl(device,
                       FSCTL_LOCK_VOLUME,
                       NULL,
		       0,
		       NULL,
		       0,
		       &dw,
		       NULL))
    {
      MsgBoxLastError(hWnd, L"Cannot lock the device. The device may be in "
		      L"use by another process or you may not have "
		      L"permission to lock it.");
      NtClose(device);
      return FALSE;
    }

  SetWindowText(hWnd, L"Dismounting filesystem...");

  if (!DeviceIoControl(device,
                       FSCTL_DISMOUNT_VOLUME,
                       NULL,
		       0,
		       NULL,
		       0,
		       &dw,
		       NULL))
    {
      MsgBoxLastError(hWnd, L"Error dismounting filesystem on device:");
      NtClose(device);
      return FALSE;
    }

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
      MsgBoxLastError(hWnd, L"Error removing device:");
      NtClose(device);
      return FALSE;
    }

  if (!DeviceIoControl(device,
                       FSCTL_UNLOCK_VOLUME,
                       NULL,
		       0,
		       NULL,
		       0,
		       &dw,
		       NULL))
    {
      MsgBoxLastError(hWnd, L"Error occured while unlocking the volume:");
      NtClose(device);
      return FALSE;
    }

  NtClose(device);

  if (MountPoint != NULL)
    {
      SetWindowText(hWnd, L"Removing drive letter...");

      if (!DefineDosDevice(DDD_REMOVE_DEFINITION, MountPoint, NULL))
	MsgBoxLastError(hWnd, L"Error removing drive letter:");
    }

  SetWindowText(hWnd, L"OK.");

  return TRUE;
}
