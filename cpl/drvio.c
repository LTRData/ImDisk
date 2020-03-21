/*
    API library for the ImDisk Virtual Disk Driver for Windows NT/2000/XP.

    Copyright (C) 2007-2010 Olof Lagerkvist.

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
#include <commdlg.h>
#include <dbt.h>

#include <stdio.h>

#include "..\inc\ntumapi.h"
#include "..\inc\imdisk.h"
#include "..\inc\imdproxy.h"

#include "drvio.h"
#include "mbr.h"

#pragma warning(disable: 4100)

// Known file format offsets
typedef struct _KNOWN_FORMAT
{
  LPCWSTR Extension;
  LONGLONG Offset;
} KNOWN_FORMAT, *PKNOWN_FORMAT;

KNOWN_FORMAT KnownFormats[] = {
  { L"nrg", 600 << 9 },
  { L"sdi", 8 << 9 }
};

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

ULONGLONG APIFlags;

ULONGLONG
WINAPI
ImDiskGetAPIFlags()
{
  return APIFlags;
}

ULONGLONG
WINAPI
ImDiskSetAPIFlags(ULONGLONG Flags)
{
  ULONGLONG old_flags = APIFlags;
  APIFlags = Flags;
  return old_flags;
}

INT_PTR
CALLBACK
StatusDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
    {
    case WM_INITDIALOG:
      ShowWindow(hWnd, SW_SHOW);

      if (lParam != 0)
	{
	  SetProp(hWnd, L"CancelFlag", (HANDLE) lParam);
	  ShowWindow(GetDlgItem(hWnd, IDCANCEL), SW_SHOW);
	}

      return TRUE;

    case WM_COMMAND:
      switch (LOWORD(wParam))
	{
	case IDCANCEL:
	  {
	    LPBOOL cancel_flag = (LPBOOL) GetProp(hWnd, L"CancelFlag");
	    if (cancel_flag != NULL)
	      *cancel_flag = TRUE;

	    return TRUE;
	  }
	}

      return TRUE;

    case WM_NCDESTROY:
      RemoveProp(hWnd, L"CancelFlag");
      return TRUE;
    }

  return FALSE;
}

BOOL
CDECL
MsgBoxPrintF(HWND hWnd, UINT uStyle, LPCWSTR lpTitle, LPCWSTR lpMessage, ...)
{
  va_list param_list;
  LPWSTR lpBuf = NULL;
  int msg_result;

  va_start(param_list, lpMessage);

  if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		     FORMAT_MESSAGE_FROM_STRING, lpMessage, 0, 0,
		     (LPWSTR) &lpBuf, 0, &param_list))
    return 0;

  msg_result = MessageBox(hWnd, lpBuf, lpTitle, uStyle);
  LocalFree(lpBuf);
  return msg_result;
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

VOID
WINAPI
ImDiskGetPartitionTypeName(IN BYTE PartitionType,
			   OUT LPWSTR Name,
			   IN DWORD NameSize)
{
  LPWSTR name;

  // Clear out NTFT signature if present.
  switch (PartitionType & 0x7F)
    {
    case PARTITION_ENTRY_UNUSED:
      name = L"Unused";
      break;

    case PARTITION_EXTENDED: case PARTITION_XINT13_EXTENDED:
      name = L"Extended";
      break;

    case PARTITION_FAT_12:
      name = L"FAT12";
      break;

    case PARTITION_FAT_16: case PARTITION_HUGE: case PARTITION_XINT13:
      name = L"FAT16";
      break;

    case PARTITION_FAT32: case PARTITION_FAT32_XINT13:
      name = L"FAT32";
      break;

    case PARTITION_OS2BOOTMGR:
      name = L"OS/2 Boot Manager";
      break;

    case PARTITION_XENIX_1: case PARTITION_XENIX_2:
      name = L"Xenix";
      break;

    case PARTITION_IFS:
      name = L"NTFS or HPFS";
      break;

    case PARTITION_PREP:
      name = L"PReP";
      break;

    case PARTITION_LDM:
      name = L"LDM";
      break;

    case PARTITION_UNIX:
      name = L"Unix";
      break;

    case 0xA5:
      name = L"BSD";
      break;

    default:
      name = L"Unknown";
      break;
    }

  wcsncpy(Name, name, NameSize - 1);
  Name[NameSize - 1] = 0;
}

BOOL
WINAPI
ImDiskGetOffsetByFileExt(IN LPCWSTR ImageFile,
			 OUT PLARGE_INTEGER Offset)
{
  LPWSTR path_sep;
  PKNOWN_FORMAT known_format;

  path_sep = wcsrchr(ImageFile, L'/');
  if (path_sep != NULL)
    ImageFile = path_sep + 1;
  path_sep = wcsrchr(ImageFile, L'\\');
  if (path_sep != NULL)
    ImageFile = path_sep + 1;

  ImageFile = wcsrchr(ImageFile, L'.');
  if (ImageFile == NULL)
    return FALSE;

  ++ImageFile;

  for (known_format = KnownFormats;
       known_format <
	 KnownFormats + sizeof(KnownFormats)/sizeof(*KnownFormats);
       known_format++)
    if (_wcsicmp(ImageFile, known_format->Extension) == 0)
      {
	Offset->QuadPart = known_format->Offset;
	return TRUE;
      }

  return FALSE;
}

BOOL
WINAPI
ImDiskGetPartitionInformation(IN LPCWSTR ImageFile,
			      IN DWORD SectorSize OPTIONAL,
			      IN PLARGE_INTEGER Offset OPTIONAL,
			      OUT PPARTITION_INFORMATION PartitionInformation)
{
  HANDLE hImageFile;
  BOOL bResult;
  DWORD dwLastError;

  hImageFile = CreateFile(ImageFile, GENERIC_READ, FILE_SHARE_READ |
			  FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			  OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);

  if (hImageFile == INVALID_HANDLE_VALUE)
    return FALSE;

  bResult = ImDiskGetPartitionInfoIndirect(hImageFile,
					   ImDiskReadFileHandle,
					   SectorSize,
					   Offset,
					   PartitionInformation);

  dwLastError = GetLastError();
  CloseHandle(hImageFile);
  SetLastError(dwLastError);

  return bResult;
}

typedef BOOL (WINAPI *ImDiskReadFileProc)(IN HANDLE Handle,
					  IN OUT LPVOID Buffer,
					  IN LARGE_INTEGER Offset,
					  IN DWORD NumberOfBytesToRead,
					  OUT LPDWORD NumberOfBytesRead);

BOOL
WINAPI
ImDiskReadFileHandle(IN HANDLE Handle,
		     IN OUT LPVOID Buffer,
		     IN LARGE_INTEGER Offset,
		     IN DWORD NumberOfBytesToRead,
		     OUT LPDWORD NumberOfBytesRead)
{
  if (SetFilePointer(Handle, Offset.LowPart, &Offset.HighPart,
		     FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    if (GetLastError() != NO_ERROR)
      return FALSE;

  return ReadFile(Handle,
		  Buffer,
		  NumberOfBytesToRead,
		  NumberOfBytesRead,
		  NULL);
}

BOOL
WINAPI
ImDiskGetPartitionInfoIndirect(IN HANDLE Handle,
			       IN ImDiskReadFileProc ReadFileProc,
			       IN DWORD SectorSize OPTIONAL,
			       IN PLARGE_INTEGER Offset OPTIONAL,
			       OUT PPARTITION_INFORMATION PartitionInformation)
{
  LPBYTE buffer = NULL;
  DWORD read_size = 0;
  int i;

  ZeroMemory(PartitionInformation, 8 * sizeof(PARTITION_INFORMATION));

  if (SectorSize == 0)
    SectorSize = 512;

  buffer = _alloca(SectorSize << 1);
  if (buffer == NULL)
    return FALSE;

  if (!ReadFileProc(Handle, buffer, *Offset, SectorSize, &read_size))
    return FALSE;

  if (read_size != SectorSize)
    {
      SetLastError(ERROR_DISK_CORRUPT);
      return FALSE;
    }

  // Check if MBR signature is there, otherwise this is not an MBR
  if ((*(LPWORD)(buffer + 0x01FE) != 0xAA55) |
      (*(LPBYTE)(buffer + 0x01BE) & 0x7F) |
      (*(LPBYTE)(buffer + 0x01CE) & 0x7F) |
      (*(LPBYTE)(buffer + 0x01DE) & 0x7F) |
      (*(LPBYTE)(buffer + 0x01EE) & 0x7F))
    {
      SetLastError(ERROR_DISK_CORRUPT);
      return FALSE;
    }

  for (i = 0; i < 4; i++)
    {
      LPBYTE partition_entry = buffer + 512 - 66 + (i << 4);

      PartitionInformation[i].StartingOffset.QuadPart =
	(LONGLONG)SectorSize * *(LPDWORD)(partition_entry + 8);
      PartitionInformation[i].PartitionLength.QuadPart =
	(LONGLONG)SectorSize * *(LPDWORD)(partition_entry + 12);
      PartitionInformation[i].HiddenSectors = *(LPDWORD)(partition_entry + 8);
      PartitionInformation[i].PartitionNumber = i+1;
      PartitionInformation[i].PartitionType = *(partition_entry + 4);
      PartitionInformation[i].BootIndicator = *partition_entry >> 7;
      PartitionInformation[i].RecognizedPartition = FALSE;
      PartitionInformation[i].RewritePartition = FALSE;

      if (IsContainerPartition(PartitionInformation[i].PartitionType))
	{
	  LARGE_INTEGER offset = *Offset;
	  int j;
	  offset.QuadPart += PartitionInformation[i].StartingOffset.QuadPart;

	  if (!ReadFileProc(Handle, buffer + SectorSize, offset, SectorSize,
			    &read_size))
	    return FALSE;

	  if (read_size != SectorSize)
	    {
	      SetLastError(ERROR_DISK_CORRUPT);
	      return FALSE;
	    }

	  // Check if MBR signature is there, otherwise this is not an MBR
	  if ((*(LPWORD)(buffer + SectorSize + 0x01FE) != 0xAA55) |
	      (*(LPBYTE)(buffer + SectorSize + 0x01BE) & 0x7F) |
	      (*(LPBYTE)(buffer + SectorSize + 0x01CE) & 0x7F) |
	      (*(LPBYTE)(buffer + SectorSize + 0x01DE) & 0x7F) |
	      (*(LPBYTE)(buffer + SectorSize + 0x01EE) & 0x7F))
	    {
	      SetLastError(ERROR_DISK_CORRUPT);
	      return FALSE;
	    }

	  for (j = 0; j < 4; j++)
	    {
	      partition_entry = buffer + (SectorSize << 1) - 66 + (j << 4);

	      PartitionInformation[j+4].StartingOffset.QuadPart =
		PartitionInformation[i].StartingOffset.QuadPart +
		(LONGLONG)SectorSize * *(LPDWORD)(partition_entry + 8);
	      PartitionInformation[j+4].PartitionLength.QuadPart =
		(LONGLONG)SectorSize * *(LPDWORD)(partition_entry + 12);
	      PartitionInformation[j+4].HiddenSectors =
		*(LPDWORD)(partition_entry + 8);
	      PartitionInformation[j+4].PartitionNumber = j+1;
	      PartitionInformation[j+4].PartitionType = *(partition_entry + 4);
	      PartitionInformation[j+4].BootIndicator = *partition_entry >> 7;
	      PartitionInformation[j+4].RecognizedPartition = FALSE;
	      PartitionInformation[j+4].RewritePartition = FALSE;
	    }
	}
    }

  return TRUE;
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

      if (dwLastError == ERROR_SERVICE_ALREADY_RUNNING)
	return TRUE;
      else
	return FALSE;
    }

  CloseServiceHandle(hService);
  CloseServiceHandle(hSCManager);
  return TRUE;
}

BOOL
WINAPI
ImDiskCreateMountPoint(LPCWSTR MountPoint, LPCWSTR Target)
{
  WORD iSize = (WORD) ((wcslen(Target) + 1) << 1);
  REPARSE_DATA_JUNCTION ReparseData = { 0 };
  HANDLE hDir;
  DWORD dw;

  if (wcslen(MountPoint) == 2)
    if (MountPoint[1] == L':')
      {
	WCHAR GlobalMountPoint[] = L"Global\\ :";
	GlobalMountPoint[7] = MountPoint[0];
	DefineDosDevice(DDD_RAW_TARGET_PATH, GlobalMountPoint, Target);
	return DefineDosDevice(DDD_RAW_TARGET_PATH, MountPoint, Target);
      }

  hDir = CreateFile(MountPoint, GENERIC_READ | GENERIC_WRITE,
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

  if (wcslen(MountPoint) == 2)
    if (MountPoint[1] == L':')
      {
	WCHAR GlobalMountPoint[] = L"Global\\ :";
	GlobalMountPoint[7] = MountPoint[0];
	DefineDosDevice(DDD_REMOVE_DEFINITION, GlobalMountPoint, NULL);
	return DefineDosDevice(DDD_REMOVE_DEFINITION, MountPoint, NULL);
      }

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
  UNICODE_STRING DeviceName;
  WCHAR DriveLetterPath[] = L"\\DosDevices\\ :";
  REPARSE_DATA_JUNCTION ReparseData = { 0 };

  if (wcslen(MountPoint) == 2 ? MountPoint[1] == L':' : FALSE)
    {
      DriveLetterPath[12] = MountPoint[0];

      RtlInitUnicodeString(&DeviceName, DriveLetterPath);
    }
  else
    {
      HANDLE hDir;
      DWORD dw;

      hDir = CreateFile(MountPoint, GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS |
			FILE_FLAG_OPEN_REPARSE_POINT, NULL);

      if (hDir == INVALID_HANDLE_VALUE)
	return INVALID_HANDLE_VALUE;

      if (!DeviceIoControl(hDir, FSCTL_GET_REPARSE_POINT,
			   NULL, 0,
			   &ReparseData, sizeof ReparseData,
			   &dw, NULL))
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
    }

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

  if (VersionCheck != IMDISK_DRIVER_VERSION)
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
  for (search = L'D'; search <= L'Z'; search++)
    if ((logical_drives & (1 << (search - L'A'))) == 0)
      return search;

  return 0;
}

ULONGLONG
WINAPI
ImDiskGetDeviceList()
{
  UNICODE_STRING file_name;
  HANDLE driver;
  ULONGLONG device_list;
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
  HANDLE device = ImDiskOpenDeviceByNumber(DeviceNumber, FILE_READ_ATTRIBUTES);
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
		   PLARGE_INTEGER ImageOffset,
		   DWORD Flags,
		   LPCWSTR FileName,
		   BOOL NativePath,
		   LPWSTR MountPoint)
{
  return ImDiskCreateDeviceEx(hWnd,
			      NULL,
			      DiskGeometry,
			      ImageOffset,
			      Flags,
			      FileName,
			      NativePath,
			      MountPoint);
}

BOOL
WINAPI
ImDiskCreateDeviceEx(HWND hWnd,
		     LPDWORD DeviceNumber,
		     PDISK_GEOMETRY DiskGeometry,
		     PLARGE_INTEGER ImageOffset,
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
			 L"The ImDisk Virtual Disk Driver is disabled.",
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
		   L"the version of this control program. Please reinstall "
		   L"ImDisk to make sure that all components of it on this "
		   L"system are from the same install package. You may have "
		   L"to restart your computer if you still see this message "
		   L"after reinstalling.",
		   L"ImDisk Virtual Disk Driver", MB_ICONSTOP);

      SetLastError(ERROR_INVALID_FUNCTION);
      return FALSE;
    }

  // Proxy reconnection types requires the user mode service.
  if ((IMDISK_TYPE(Flags) == IMDISK_TYPE_PROXY) &
      ((IMDISK_PROXY_TYPE(Flags) == IMDISK_PROXY_TYPE_TCP) |
       (IMDISK_PROXY_TYPE(Flags) == IMDISK_PROXY_TYPE_COMM)))
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
  else if (NativePath)
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
  else if ((IMDISK_TYPE(Flags) == IMDISK_TYPE_PROXY) &
	   (IMDISK_PROXY_TYPE(Flags) == IMDISK_PROXY_TYPE_SHM))
    {
      LPWSTR namespace_prefix;
      LPWSTR prefixed_name;
      HANDLE h = CreateFile(L"\\\\?\\Global", 0, FILE_SHARE_READ, NULL,
			    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if ((h == INVALID_HANDLE_VALUE) &
	  (GetLastError() == ERROR_FILE_NOT_FOUND))
	namespace_prefix = L"\\BaseNamedObjects\\";
      else
	namespace_prefix = L"\\BaseNamedObjects\\Global\\";

      if (h != INVALID_HANDLE_VALUE)
	CloseHandle(h);

      prefixed_name = (LPWSTR)
	_alloca(((wcslen(namespace_prefix) + wcslen(FileName)) << 1) + 1);

      if (prefixed_name == NULL)
	{
	  NtClose(driver);
	  if (hWnd != NULL)
	    MessageBox(hWnd, L"Memory allocation error.",
		       L"ImDisk Virtual Disk Driver", MB_ICONSTOP);
	  return FALSE;
	}

      wcscpy(prefixed_name, namespace_prefix);
      wcscat(prefixed_name, FileName);

      if (!RtlCreateUnicodeString(&file_name, prefixed_name))
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

  if (DeviceNumber != NULL)
    create_data->DeviceNumber = *DeviceNumber;

  if (ImageOffset != NULL)
    create_data->ImageOffset = *ImageOffset;

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

  if (DeviceNumber != NULL)
    *DeviceNumber = create_data->DeviceNumber;

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

      // Notify processes that new device has arrived.
      if (((APIFlags & IMDISK_API_NO_BROADCAST_NOTIFY) == 0) &
	  (MountPoint[0] >= L'A') & (MountPoint[0] <= L'Z'))
	{
	  DWORD_PTR dwp;
	  DEV_BROADCAST_VOLUME dev_broadcast_volume = {
	    sizeof(DEV_BROADCAST_VOLUME),
	    DBT_DEVTYP_VOLUME
	  };

	  if (hWnd != NULL)
	    SetWindowText
	      (hWnd,
	       L"Notifying applications that device has been created...");

	  dev_broadcast_volume.dbcv_unitmask = 1 << (MountPoint[0] - L'A');

	  SendMessageTimeout(HWND_BROADCAST,
			     WM_DEVICECHANGE,
			     DBT_DEVICEARRIVAL,
			     (LPARAM)&dev_broadcast_volume,
			     SMTO_BLOCK | SMTO_ABORTIFHUNG,
			     4000,
			     &dwp);

	  dev_broadcast_volume.dbcv_flags = DBTF_MEDIA;

	  SendMessageTimeout(HWND_BROADCAST,
			     WM_DEVICECHANGE,
			     DBT_DEVICEARRIVAL,
			     (LPARAM)&dev_broadcast_volume,
			     SMTO_BLOCK | SMTO_ABORTIFHUNG,
			     4000,
			     &dwp);

	  SendMessageTimeout(HWND_BROADCAST,
			     WM_DEVICECHANGE,
			     DBT_DEVNODES_CHANGED,
			     (LPARAM)0,
			     SMTO_BLOCK | SMTO_ABORTIFHUNG,
			     4000,
			     &dwp);

	  /* Tried PostMessage instead of SendMessageTimeout
	  PostMessage(HWND_BROADCAST,
		      WM_DEVICECHANGE,
		      DBT_DEVICEARRIVAL,
		      (LPARAM)&dev_broadcast_volume);

	  dev_broadcast_volume.dbcv_flags = DBTF_MEDIA;

	  PostMessage(HWND_BROADCAST,
		      WM_DEVICECHANGE,
		      DBT_DEVICEARRIVAL,
		      (LPARAM)&dev_broadcast_volume);
	  */
	}
    }

  return TRUE;
}

BOOL
WINAPI
ImDiskForceRemoveDevice(HANDLE Device,
			DWORD DeviceNumber)
{
  UNICODE_STRING file_name;
  HANDLE driver;
  DWORD dw;

  if (Device != NULL)
    {
      DWORD create_data_size = sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2);
      PIMDISK_CREATE_DATA create_data = (PIMDISK_CREATE_DATA)
	_alloca(create_data_size);

      if (create_data == NULL)
	return FALSE;

      if (!DeviceIoControl(Device,
			   IOCTL_IMDISK_QUERY_DEVICE,
			   NULL,
			   0,
			   create_data,
			   create_data_size,
			   &dw, NULL))
	return FALSE;

      DeviceNumber = create_data->DeviceNumber;
    }

  RtlInitUnicodeString(&file_name, IMDISK_CTL_DEVICE_NAME);
  driver = ImDiskOpenDeviceByName(&file_name,
				  GENERIC_READ | GENERIC_WRITE);
  if (driver == NULL)
    return FALSE;

  if (!DeviceIoControl(driver,
                       IOCTL_IMDISK_REMOVE_DEVICE,
                       &DeviceNumber,
		       sizeof DeviceNumber,
		       NULL,
		       0,
		       &dw,
		       NULL))
    {
      DWORD dwLastError = GetLastError();
      NtClose(driver);
      SetLastError(dwLastError);
      return FALSE;
    }

  NtClose(driver);
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
  BOOL force_dismount = FALSE;

  if (MountPoint == NULL)
    {
      if (hWnd != NULL)
	SetWindowText(hWnd, L"Opening device...");

      device = ImDiskOpenDeviceByNumber(DeviceNumber,
					GENERIC_READ | GENERIC_WRITE);

      if (device == INVALID_HANDLE_VALUE)
	device = ImDiskOpenDeviceByNumber(DeviceNumber,
					  GENERIC_READ);

      if (device == INVALID_HANDLE_VALUE)
	device = ImDiskOpenDeviceByNumber(DeviceNumber,
					  FILE_READ_ATTRIBUTES);
    }
  else if ((wcslen(MountPoint) == 2) ? MountPoint[1] == ':' : 
	   (wcslen(MountPoint) == 3) ? wcscmp(MountPoint + 1, L":\\") == 0 :
	   FALSE)
    {
      WCHAR drive_letter_path[] = L"\\\\.\\ :";
      drive_letter_path[4] = MountPoint[0];

      // Notify processes that this device is about to be removed.
      if (((APIFlags & IMDISK_API_NO_BROADCAST_NOTIFY) == 0) &
	  (MountPoint[0] >= L'A') & (MountPoint[0] <= L'Z'))
	{
	  DEV_BROADCAST_VOLUME dev_broadcast_volume = {
	    sizeof(DEV_BROADCAST_VOLUME),
	    DBT_DEVTYP_VOLUME
	  };
	  DWORD_PTR dwp;

	  if (hWnd != NULL)
	    SetWindowText
	      (hWnd,
	       L"Notifying applications that device is being removed...");

	  dev_broadcast_volume.dbcv_unitmask = 1 << (MountPoint[0] - L'A');

	  SendMessageTimeout(HWND_BROADCAST,
			     WM_DEVICECHANGE,
			     DBT_DEVICEQUERYREMOVE,
			     (LPARAM)&dev_broadcast_volume,
			     SMTO_BLOCK | SMTO_ABORTIFHUNG,
			     4000,
			     &dwp);

	  SendMessageTimeout(HWND_BROADCAST,
			     WM_DEVICECHANGE,
			     DBT_DEVICEREMOVEPENDING,
			     (LPARAM)&dev_broadcast_volume,
			     SMTO_BLOCK | SMTO_ABORTIFHUNG,
			     4000,
			     &dwp);
	}

      if (hWnd != NULL)
	SetWindowText(hWnd, L"Opening device...");

      device = CreateFile(drive_letter_path,
			  GENERIC_READ | GENERIC_WRITE,
			  FILE_SHARE_READ | FILE_SHARE_WRITE,
			  NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);

      if (device == INVALID_HANDLE_VALUE)
	device = CreateFile(drive_letter_path,
			    GENERIC_READ,
			    FILE_SHARE_READ | FILE_SHARE_WRITE,
			    NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);

      if (device == INVALID_HANDLE_VALUE)
	device = CreateFile(drive_letter_path,
			    FILE_READ_ATTRIBUTES,
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
	NtClose(device);
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
    if (APIFlags & IMDISK_API_FORCE_DISMOUNT)
      force_dismount = TRUE;
    else if (hWnd == NULL)
      {
	NtClose(device);
	return FALSE;
      }
    else if (MessageBox(hWnd,
			L"Cannot lock the device. The device may be in use by "
			L"another process or you may not have permission to "
			L"lock it. Do you want do try to force dismount of "
			L"the volume? (Unsaved data on the volume will be "
			L"lost.)",
			L"ImDisk Virtual Disk Driver",
			MB_ICONEXCLAMATION | MB_YESNO | MB_DEFBUTTON2) !=
	     IDYES)
      {
	NtClose(device);
	return FALSE;
      }
    else
      force_dismount = TRUE;

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

  if (force_dismount)
    DeviceIoControl(device,
		    FSCTL_LOCK_VOLUME,
		    NULL,
		    0,
		    NULL,
		    0,
		    &dw,
		    NULL);

  if (hWnd != NULL)
    SetWindowText(hWnd, L"");

  // If interactive mode, check if image has been modified and if so ask user
  // if it should be saved first.
  if (hWnd != NULL)
    {
      DWORD create_data_size = sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2);
      PIMDISK_CREATE_DATA create_data = (PIMDISK_CREATE_DATA)
	_alloca(create_data_size);

      if (create_data == NULL)
	{
	  DWORD last_error = GetLastError();
	  NtClose(device);
	  MessageBox(hWnd, L"Memory allocation error.", L"ImDisk",
		     MB_ICONSTOP);
	  SetLastError(last_error);
	  return FALSE;
	}

      if (!DeviceIoControl(device,
			   IOCTL_IMDISK_QUERY_DEVICE,
			   NULL,
			   0,
			   create_data,
			   create_data_size,
			   &dw, NULL))
	{
	  DWORD last_error = GetLastError();
	  MsgBoxLastError(hWnd, L"Error communicating with device:");
	  NtClose(device);
	  SetLastError(last_error);
	  return FALSE;
	}

      create_data->FileName[create_data->FileNameLength /
			    sizeof(*create_data->FileName)] = 0;

      if ((IMDISK_TYPE(create_data->Flags) == IMDISK_TYPE_VM) &&
	  (create_data->Flags & IMDISK_IMAGE_MODIFIED))
	switch (MessageBox(hWnd,
		       L"The virtual disk has been modified. Do you "
		       L"want to save it as an image file before unmounting "
		       L"it?", L"ImDisk Virtual Disk Driver",
		       MB_ICONINFORMATION | MB_YESNOCANCEL))
	  {
	  case IDYES:
	    {
	      OPENFILENAME_NT4 ofn = { sizeof ofn };
	      HANDLE image = INVALID_HANDLE_VALUE;
	      ULARGE_INTEGER file_size = { 0 };

	      ofn.hwndOwner = hWnd;
	      ofn.lpstrFilter = L"Image files (*.img)\0*.img\0";
	      ofn.lpstrFile = create_data->FileName;
	      ofn.nMaxFile = MAX_PATH;
	      ofn.lpstrTitle = L"Save to image file";
	      ofn.Flags = OFN_EXPLORER | OFN_LONGNAMES | OFN_OVERWRITEPROMPT |
		OFN_PATHMUSTEXIST;
	      ofn.lpstrDefExt = L"img";

	      if (IMDISK_DEVICE_TYPE(create_data->Flags) ==
		  IMDISK_DEVICE_TYPE_CD)
		{
		  ofn.lpstrFilter = L"ISO image (*.iso)\0*.iso\0";
		  ofn.lpstrDefExt = L"iso";
		}

	      if (!GetSaveFileName((LPOPENFILENAMEW) &ofn))
		{
		  DWORD last_error = GetLastError();
		  NtClose(device);
		  SetLastError(last_error);
		  return FALSE;
		}

	      SetWindowText(hWnd, L"Saving image file...");

	      image = CreateFile(ofn.lpstrFile,
				 GENERIC_WRITE,
				 FILE_SHARE_READ | FILE_SHARE_DELETE,
				 NULL,
				 CREATE_ALWAYS,
				 FILE_ATTRIBUTE_NORMAL,
				 NULL);

	      if (image == INVALID_HANDLE_VALUE)
		{
		  MsgBoxLastError(hWnd, L"Cannot create image file:");
		  NtClose(device);
		  return FALSE;
		}

	      if (!ImDiskSaveImageFile(device, image, 0, NULL))
		{
		  MsgBoxLastError(hWnd, L"Error saving image:");

		  file_size.LowPart = GetFileSize(image, &file_size.HighPart);
		  if (file_size.QuadPart == 0)
		    DeleteFile(ofn.lpstrFile);

		  CloseHandle(image);
		  NtClose(device);
		  return FALSE;
		}

	      file_size.LowPart = GetFileSize(image, &file_size.HighPart);
	      if (file_size.QuadPart == 0)
		{
		  DeleteFile(ofn.lpstrFile);
		  CloseHandle(image);

		  MessageBox(hWnd,
			     L"The drive contents could not be saved. "
			     L"Check that the drive contains a supported "
			     L"filesystem.", L"ImDisk Virtual Disk Driver",
			     MB_ICONEXCLAMATION);

		  NtClose(device);
		  return FALSE;
		}

	      CloseHandle(image);
	    }

	  case IDNO:
	    break;

	  default:
	    NtClose(device);
	    return FALSE;
	  }
    }

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
    if (force_dismount ? !ImDiskForceRemoveDevice(device, 0) : FALSE)
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
      DWORD_PTR dwp;

      WCHAR reg_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MountPoints\\ ";
      WCHAR reg_key2[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MountPoints2\\ ";

      if (hWnd != NULL)
	SetWindowText(hWnd, L"Removing drive letter...");

      if (!DefineDosDevice(DDD_REMOVE_DEFINITION, MountPoint, NULL))
	if (hWnd != NULL)
	  MsgBoxLastError(hWnd, L"Error removing drive letter:");

      reg_key[63] = MountPoint[0];
      reg_key2[64] = MountPoint[0];

      RegDeleteKey(HKEY_CURRENT_USER, reg_key);
      RegDeleteKey(HKEY_CURRENT_USER, reg_key2);

      if ((APIFlags & IMDISK_API_NO_BROADCAST_NOTIFY) == 0)
	SendMessageTimeout(HWND_BROADCAST,
			   WM_DEVICECHANGE,
			   DBT_DEVNODES_CHANGED,
			   (LPARAM)0,
			   SMTO_BLOCK | SMTO_ABORTIFHUNG,
			   4000,
			   &dwp);
    }

  if (hWnd != NULL)
    SetWindowText(hWnd, L"OK.");

  return TRUE;
}

BOOL
WINAPI
ImDiskChangeFlags(HWND hWnd,
		  DWORD DeviceNumber,
		  LPCWSTR MountPoint,
		  DWORD FlagsToChange,
		  DWORD Flags)
{
  HANDLE device;
  DWORD dw;
  IMDISK_SET_DEVICE_FLAGS device_flags;

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
	NtClose(device);
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
      {
	NtClose(device);
	return FALSE;
      }
    else
      {
	NtClose(device);

	MessageBox(hWnd,
		   L"Cannot lock the device. The device may be in use by "
		   L"another process or you may not have permission to lock "
		   L"it.",
		   L"ImDisk Virtual Disk Driver",
		   MB_ICONEXCLAMATION);

	return FALSE;
      }

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Dismounting filesystem...");

  if (!DeviceIoControl(device,
		       FSCTL_DISMOUNT_VOLUME,
		       NULL,
		       0,
		       NULL,
		       0,
		       &dw,
		       NULL))
    if (hWnd == NULL)
      {
	NtClose(device);
	return FALSE;
      }
    else
      {
	NtClose(device);

	MessageBox(hWnd,
		   L"Cannot lock dismount the filesystem on the device.",
		   L"ImDisk Virtual Disk Driver",
		   MB_ICONEXCLAMATION);

	return FALSE;
      }

  if (hWnd != NULL)
    SetWindowText(hWnd, L"Setting device flags...");

  device_flags.FlagsToChange = FlagsToChange;
  device_flags.FlagValues = Flags;

  if (!DeviceIoControl(device,
                       IOCTL_IMDISK_SET_DEVICE_FLAGS,
                       &device_flags,
                       sizeof(device_flags),
                       NULL,
                       0,
                       &dw,
		       NULL))
    {
      if (hWnd != NULL)
	MsgBoxLastError(hWnd, L"Error setting device flags:");

      NtClose(device);
      return FALSE;
    }

  NtClose(device);
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

BOOL
WINAPI
ImDiskSaveImageFile(IN HANDLE DeviceHandle,
		    IN HANDLE FileHandle,
		    IN DWORD BufferSize OPTIONAL,
		    IN LPBOOL CancelFlag OPTIONAL)
{
  LPBYTE buffer;
  IMDISK_SET_DEVICE_FLAGS device_flags = { 0 };
  LARGE_INTEGER disk_size;
  LONGLONG saved_size = 0;
  DWORD dwReadSize;
  DWORD dwWriteSize;

  if (!ImDiskGetVolumeSize(DeviceHandle, &disk_size.QuadPart))
    return FALSE;

  if (disk_size.QuadPart == 0)
    return TRUE;

  // Auto-select buffer size based on disk size
  if (BufferSize == 0)
    BufferSize = 1 << 19;

  // Turn on FSCTL_ALLOW_EXTENDED_DASD_IO so that we can make sure that
  // we read the entire drive
  DeviceIoControl(DeviceHandle,
		  FSCTL_ALLOW_EXTENDED_DASD_IO,
		  NULL,
		  0,
		  NULL,
		  0,
		  &dwReadSize,
		  NULL);

  buffer = VirtualAlloc(NULL, BufferSize, MEM_COMMIT, PAGE_READWRITE);
  if (buffer == NULL)
    return FALSE;

  for (;;)
    {
      if (CancelFlag != NULL)
	{
	  DoEvents(NULL);

	  if (*CancelFlag)
	    {
	      VirtualFree(buffer, 0, MEM_RELEASE);
	      SetLastError(ERROR_CANCELLED);
	      return FALSE;
	    }
	}

      if (disk_size.QuadPart - saved_size < BufferSize)
	BufferSize = (DWORD) (disk_size.QuadPart - saved_size);

      if (!ReadFile(DeviceHandle, buffer, BufferSize, &dwReadSize, NULL))
	{
	  DWORD dwLastError = GetLastError();
	  VirtualFree(buffer, 0, MEM_RELEASE);
	  SetLastError(dwLastError);
	  return FALSE;
	}

      if (dwReadSize == 0)
	{
	  VirtualFree(buffer, 0, MEM_RELEASE);
	  break;
	}

      if (!WriteFile(FileHandle, buffer, dwReadSize, &dwWriteSize, NULL))
	{
	  DWORD dwLastError = GetLastError();
	  VirtualFree(buffer, 0, MEM_RELEASE);
	  SetLastError(dwLastError);
	  return FALSE;
	}

      saved_size += dwWriteSize;

      if (dwWriteSize != dwReadSize)
	{
	  DWORD dwLastError = GetLastError();
	  VirtualFree(buffer, 0, MEM_RELEASE);
	  SetLastError(dwLastError);
	  return FALSE;
	}
    }

  device_flags.FlagsToChange = IMDISK_IMAGE_MODIFIED;

  DeviceIoControl(DeviceHandle,
		  IOCTL_IMDISK_SET_DEVICE_FLAGS,
		  &device_flags,
		  sizeof(device_flags),
		  NULL,
		  0,
		  &dwReadSize,
		  NULL);

  return TRUE;
}

BOOL
WINAPI
ImDiskAdjustImageFileSize(IN HANDLE FileHandle,
			  IN PLARGE_INTEGER FileSize)
{
  ULARGE_INTEGER existing_size = { 0 };
  DWORD ptr;

  // This piece of code compares size of created image file with that of the
  // original disk volume and possibly adjusts image file size if it does not
  // exactly match the size of the original disk/partition.
  existing_size.LowPart = GetFileSize(FileHandle, &existing_size.HighPart);
  if (existing_size.LowPart == INVALID_FILE_SIZE)
    if (GetLastError() != NO_ERROR)
      return FALSE;

  if (existing_size.QuadPart < (ULONGLONG)FileSize->QuadPart)
    {
      SetLastError(ERROR_DISK_OPERATION_FAILED);
      return FALSE;
    }

  ptr = SetFilePointer(FileHandle,
		       FileSize->LowPart,
		       (LPLONG) &FileSize->HighPart,
		       FILE_BEGIN);
  if (ptr == INVALID_SET_FILE_POINTER)
    if (GetLastError() != NO_ERROR)
      return FALSE;

  return SetEndOfFile(FileHandle);
}

BOOL
WINAPI
ImDiskGetVolumeSize(IN HANDLE Handle,
		    OUT PLONGLONG Size)
{
  PARTITION_INFORMATION partition_info = { 0 };
  DISK_GEOMETRY disk_geometry = { 0 };
  DWORD dwBytesReturned;

  DeviceIoControl(Handle,
		  IOCTL_DISK_UPDATE_PROPERTIES,
		  NULL,
		  0,
		  NULL,
		  0,
		  &dwBytesReturned,
		  NULL);

  if (DeviceIoControl(Handle,
		      IOCTL_DISK_GET_PARTITION_INFO,
		      NULL,
		      0,
		      &partition_info,
		      sizeof(partition_info),
		      &dwBytesReturned,
		      NULL))
    {
      *Size = partition_info.PartitionLength.QuadPart;
      return TRUE;
    }

  if (DeviceIoControl(Handle,
		      IOCTL_DISK_GET_DRIVE_GEOMETRY,
		      NULL,
		      0,
		      &disk_geometry,
		      sizeof(disk_geometry),
		      &dwBytesReturned,
		      NULL))
    {
      *Size =
	disk_geometry.Cylinders.QuadPart *
	disk_geometry.TracksPerCylinder *
	disk_geometry.SectorsPerTrack *
	disk_geometry.BytesPerSector;
      return TRUE;
    }

  return FALSE;
}

BOOL
WINAPI
ImDiskBuildMBR(IN PDISK_GEOMETRY DiskGeometry OPTIONAL,
	       IN PPARTITION_INFORMATION PartitionInformation OPTIONAL,
	       IN BYTE NumberOfPartitions OPTIONAL,
	       IN OUT LPBYTE MBR,
	       IN DWORD_PTR MBRSize)
{
  BYTE i;

  if (MBRSize < default_mbr_size)
    {
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }

  if (NumberOfPartitions > 4)
    {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }

  memcpy(MBR, default_mbr, default_mbr_size);

  for (i = 0; i < NumberOfPartitions; i++)
    {
      LPBYTE partition_entry = MBR + 512 - 66 + (i << 4);

      ULONGLONG start_sector =
 	PartitionInformation[i].StartingOffset.QuadPart /
	DiskGeometry->BytesPerSector;

      ULONGLONG number_of_sectors =
 	PartitionInformation[i].PartitionLength.QuadPart /
	DiskGeometry->BytesPerSector;

      ULONGLONG end_sector = start_sector + number_of_sectors - 1;

      if ((start_sector & 0xFFFFFFFF00000000) |
	  (number_of_sectors & 0xFFFFFFFF00000000) |
	  (end_sector & 0xFFFFFFFF00000000))
	{
	  SetLastError(ERROR_INVALID_PARAMETER);
	  return FALSE;
	}

      // 0x00 Boot indicator
      if (PartitionInformation[i].BootIndicator)
	*partition_entry = 0x80;

      // 0x01-0x03 CHS start sector
      *(LPDWORD)(partition_entry + 1) =
	ImDiskConvertLBAToCHS(DiskGeometry, (DWORD) start_sector);

      // 0x04 partition type
      *(partition_entry + 4) = PartitionInformation[i].PartitionType;

      // 0x05-0x07 CHS end sector
      *(LPDWORD)(partition_entry + 5) =
	ImDiskConvertLBAToCHS(DiskGeometry, (DWORD) end_sector);

      // 0x08-0x0B LBA start sector
      *(LPDWORD)(partition_entry + 8) = (DWORD) start_sector;

      // 0x0C-0x0F LBA size (sectors)
      *(LPDWORD)(partition_entry + 12) = (DWORD) number_of_sectors;
    }

  return TRUE;
}

DWORD
WINAPI
ImDiskConvertCHSToLBA(IN PDISK_GEOMETRY DiskGeometry,
		      IN LPBYTE CHS)
{
  DWORD cylinder = (((DWORD) CHS[1] & 0xC0) << 2) | CHS[2];
  DWORD heads = CHS[0];
  DWORD sector = CHS[1] & 0x3F;

  return
    ((cylinder * DiskGeometry->TracksPerCylinder + heads) *
     DiskGeometry->SectorsPerTrack) + sector - 1;
}

DWORD
WINAPI
ImDiskConvertLBAToCHS(IN PDISK_GEOMETRY DiskGeometry,
		      IN DWORD LBA)
{
  /*
    cylinder = LBA / (heads_per_cylinder * sectors_per_track)
    temp = LBA % (heads_per_cylinder * sectors_per_track)
    head = temp / sectors_per_track
    sector = temp % sectors_per_track + 1
  */

  DWORD cylinder =
    LBA /
    DiskGeometry->TracksPerCylinder / DiskGeometry->SectorsPerTrack;

  DWORD temp =
    LBA %
    DiskGeometry->TracksPerCylinder / DiskGeometry->SectorsPerTrack;

  DWORD head =
    temp /
    DiskGeometry->SectorsPerTrack;

  DWORD sector =
    temp %
    DiskGeometry->SectorsPerTrack + 1;

  if ((cylinder >= 1024) |
      (head >= 256) |
      (sector >= 64))
    {
      cylinder = 1023;
      head = 254;
      sector = 63;
    }

  return
    head |
    (sector << 8) |
    ((cylinder & 0x0FF) << 16) |
    ((cylinder & 0x300) << 14);
}

VOID
WINAPI
ImDiskNativePathToWin32(IN OUT LPWSTR *Path)
{
  if (wcsncmp(*Path, L"\\??\\", 4) == 0)
    (*Path)[1] = L'\\';
  else if (wcsncmp(*Path, L"\\DosDevices\\", 12) == 0)
    {
      (*Path) += 8;
      (*Path)[0] = L'\\';
      (*Path)[1] = L'\\';
      (*Path)[3] = L'?';
    }
  else
    return;

  if ((*Path)[4] != 0 ? (*Path)[5] == L':' : FALSE)
    (*Path) += 4;
  else if (wcsncmp(*Path, L"\\\\?\\UNC\\", 8) == 0)
    {
      (*Path) += 6;
      (*Path)[0] = L'\\';
    }
}

