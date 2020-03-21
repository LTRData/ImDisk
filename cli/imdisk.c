/*
    Control program for the ImDisk Virtual Disk Driver for Windows NT/2000/XP.

    Copyright (C) 2005-2007 Olof Lagerkvist.

    Some credits:
    - Parts related to floppy emulation based on VFD by Ken Kato.
      http://chitchat.at.infoseek.co.jp/vmware/vfd.html
    - Parts related to CD-ROM emulation and impersonation to support remote
      files based on FileDisk by Bo Brantén.
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
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>

#include "..\inc\ntumapi.h"
#include "..\inc\imdisk.h"
#include "..\inc\imdproxy.h"

//#define DbgOemPrintF(x) ImDiskOemPrintF x
#define DbgOemPrintF(x)

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
#define _p(n) ((n)>=_1TB ? "TB" : (n)>=_1GB ? "GB" :			\
	       (n)>=_1MB ? "MB" : (n)>=_1KB ? "KB": (n)==1 ? "byte" : "bytes")

void __declspec(noreturn)
ImDiskSyntaxHelp()
{
  fputs
    ("Control program for the ImDisk Virtual Disk Driver.\r\n"
     "For copyrights and credits, type imdisk --version\r\n"
     "\n"
     "Syntax:\r\n"
     "imdisk -a -t type -m mountpoint [-n] [-o opt1[,opt2 ...]] [-f|-F file]\r\n"
     "       [-s size] [-S sectorsize] [-u unit] [-x sectors/track]\r\n"
     "       [-y tracks/cylinder]\r\n"
     "imdisk -d [-u unit | -m mountpoint]\r\n"
     "imdisk -l [-n] [-u unit | -m mountpoint]\r\n"
     "\n"
     "-a      Attach a virtual disk. This will configure and attach a virtual disk\r\n"
     "        with the parameters specified and attach it to the system.\r\n"
     "\n"
     "-d      Detach a virtual disk from the system and release all resources.\r\n"
     "\n"
     "-t type\r\n"
     "        Select the backingstore for the virtual disk.\r\n"
     "\n"
     "vm      Storage for this type of virtual disk is allocated from virtual memory\r\n"
     "        in the system process. If a file is specified with -f that file is\r\n"
     "        is loaded into the memory allocated for the disk image.\r\n"
     "\n"
     "file    A file specified with -f file becomes the backingstore for this\r\n"
     "        virtual disk.\r\n"
     "\n"
     "proxy   The actual backingstore for this type of virtual disk is controlled by\r\n"
     "        an ImDisk storage server aaccessed by the driver on this machine by\r\n"
     "        sending storage I/O request through a named pipe specified with -f.\r\n"
     "\n"
     "-f file or -F file\r\n"
     "        Filename to use as backingstore for the file type virtual disk, to\r\n"
     "        initialize a vm type virtual disk or name of a named pipe for I/O\r\n"
     "        client/server communication for proxy type virtual disks. For proxy\r\n"
     "        type virtual disks \"file\" may be a COM port or a remote server\r\n"
     "        address if the -o options includes \"ip\" or \"comm\".\r\n"
     "\n"
     "        Instead of using -f to specify 'DOS-style' paths, such as\r\n"
     "        C:\\dir\\image.bin or \\\\server\\share\\image.bin, you can use -F to\r\n"
     "        specify 'NT-style' native paths, such as\r\n"
     "        \\Device\\Harddisk0\\Partition1\\image.bin. This makes it possible to\r\n"
     "        specify files on disks or communication devices that currently have no\r\n"
     "        drive letters assigned.\r\n"
     "\n"
     "-l      List configured devices. If given with -u or -m, display details about\r\n"
     "        that particular device.\r\n"
     "\n"
     "-n      When printing ImDisk device names, print only the unit number without\r\n"
     "        the \\Device\\ImDisk prefix.\r\n"
     "\n"
     "-s size\r\n"
     "        Size of the virtual disk. Size is number of bytes unless suffixed with\r\n"
     "        a b, k, m, g, t, K, M, G or T which denotes number of 512-byte blocks,\r\n"
     "        thousand bytes, million bytes, billion bytes, trillion bytes,\r\n"
     "        kilobytes megabytes, gigabytes and terabytes respectively. Size is\r\n"
     "        optional the file used by a file type virtual disk does not already\r\n"
     "        exists or when a vm type virtual disk is created without specifying an\r\n"
     "        initialization file using the -f or -F options. If size is specified\r\n"
     "        file type virtual disks, the size of the file used as backingstore for\r\n"
     "        the virtual disk is adjusted to the new size specified with this size\r\n"
     "        option.\r\n"
     "\n"
     "-S sectorsize\r\n"
     "        Sectorsize to use for the virtual disk device. Default value is 512\r\n"
     "        bytes except for CD-ROM/DVD-ROM style devices where 2048 bytes is used\r\n"
     "        by default.\r\n"
     "\n"
     "-x sectors/track\r\n"
     "        See the description of the -y option below.\r\n"
     "\n"
     "-y tracks/cylinder\r\n"
     "        The -x and -y options can be used to specify a synthetic geometry.\r\n"
     "        This is useful for constructing bootable images for later download to\r\n"
     "        physical devices. Default values depends on the device-type specified\r\n"
     "        with the -o option. If the 'fd' option is specified the default values\r\n"
     "        are based on the virtual disk size, e.g. a 1440K image gets 2\r\n"
     "        tracks/cylinder and 18 sectors/track.\r\n"
     "\n"
     "-o option\r\n"
     "        Set or reset options.\r\n"
     "\n"
     "ro      Creates a read-only virtual disk. For vm type virtual disks, this\r\n"
     "        option can only be used if the -f option is also specified.\r\n"
     "\n"
     "cd      Creates a virtual CD-ROM/DVD-ROM. This is the default if the file\r\n"
     "        name specified with the -f option ends with the .iso extension.\r\n"
     "\n"
     "fd      Creates a virtual floppy disk. This is the default if the size of the\r\n"
     "        virtual disk is 320K, 640K, 720K, 1200K, 1440K or 2880K.\r\n"
     "\n"
     "hd      Creates a virtual fixed disk partition. This is the default unless\r\n"
     "        file extension or size match the criterias for defaulting to the cd or\r\n"
     "        fd options.\r\n"
     "\n"
     "ip      Can only be used with proxy-type virtual disks. With this option, the\r\n"
     "        user-mode service component is initialized to connect to an ImDisk\r\n"
     "        storage server using TCP/IP. With this option, the -f switch specifies\r\n"
     "        the remote host optionally followed by a colon and a port number to\r\n"
     "        connect to.\r\n"
     "\n"
     "comm    Can only be used with proxy-type virtual disks. With this option, the\r\n"
     "        user-mode service component is initialized to connect to an ImDisk\r\n"
     "        storage server through a COM port. With this option, the -f switch\r\n"
     "        specifies the COM port to connect to, optionally followed by a colon,\r\n"
     "        a space, and then a device settings string with the same syntax as the\r\n"
     "        MODE command.\r\n"
     "\n"
     "-u unit\r\n"
     "        Along with -a, request a specific unit number for the ImDisk device\r\n"
     "        instead of automatic allocation. Along with -d or -l specifies the\r\n"
     "        unit number of the virtual disk to remove or query.\r\n"
     "\n"
     "-m mountpoint\r\n"
     "        Specifies a drive letter or mount point for the new virtual disk, the\r\n"
     "        virtual disk to query or the virtual disk to remove.\r\n",
     stderr);

  exit(1);
}

BOOL
ImDiskOemPrintF(FILE *Stream, LPCSTR Message, ...)
{
  va_list param_list;
  LPSTR lpBuf = NULL;

  va_start(param_list, Message);

  if (!FormatMessageA(78 |
		      FORMAT_MESSAGE_ALLOCATE_BUFFER |
		      FORMAT_MESSAGE_FROM_STRING, Message, 0, 0,
		      (LPSTR) &lpBuf, 0, &param_list))
    return FALSE;

  CharToOemA(lpBuf, lpBuf);
  fprintf(Stream, "%s\n", lpBuf);
  LocalFree(lpBuf);
  return TRUE;
}

void
PrintLastError(LPCWSTR Prefix)
{
  LPWSTR MsgBuf;

  FormatMessageA(FORMAT_MESSAGE_MAX_WIDTH_MASK |
		 FORMAT_MESSAGE_ALLOCATE_BUFFER |
		 FORMAT_MESSAGE_FROM_SYSTEM |
		 FORMAT_MESSAGE_IGNORE_INSERTS,
		 NULL, GetLastError(), 0, (LPSTR) &MsgBuf, 0, NULL);

  ImDiskOemPrintF(stderr, "%1!ws! %2", Prefix, MsgBuf);

  LocalFree(MsgBuf);
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
    {
      if (GetLastError() == ERROR_INVALID_FUNCTION)
	fputs("Error: Not an ImDisk device.\r\n", stderr);
      else
	PrintLastError(L"Cannot control the ImDisk Virtual Disk Driver:");

      return FALSE;
    }

  if (BytesReturned < sizeof VersionCheck)
    {
      fprintf(stderr,
	      "Wrong version of ImDisk Virtual Disk Driver.\n"
	      "No current driver version information, expected: %u.%u.\n",
	      HIBYTE(IMDISK_VERSION), LOBYTE(IMDISK_VERSION),
	      BytesReturned);
      return FALSE;
    }

  if (VersionCheck != IMDISK_VERSION)
    {
      fprintf(stderr,
	      "Wrong version of ImDisk Virtual Disk Driver.\n"
	      "Expected: %u.%u Installed: %u.%u\n",
	      HIBYTE(IMDISK_VERSION), LOBYTE(IMDISK_VERSION),
	      HIBYTE(VersionCheck), LOBYTE(VersionCheck));
      return FALSE;
    }

  return TRUE;
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

BOOL
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
      ImDiskOemPrintF(stderr, "Name is too long: '%1!ws!'\n", Target);
      return 4;
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
ImDiskOpenDeviceByName(PUNICODE_STRING FileName, DWORD AccessMode)
{
  NTSTATUS status;
  HANDLE handle;
  OBJECT_ATTRIBUTES object_attrib;
  IO_STATUS_BLOCK io_status;

  DbgOemPrintF((stdout, "Opening device '%1!.*ws!'...\n",
		FileName->Length / sizeof(WCHAR), FileName->Buffer));

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

HANDLE
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

int
ImDiskCreate(LPDWORD DeviceNumber,
	     PDISK_GEOMETRY DiskGeometry,
	     DWORD Flags,
	     LPCWSTR FileName,
	     BOOL NativePath,
	     LPWSTR MountPoint)
{
  PIMDISK_CREATE_DATA create_data;
  HANDLE driver;
  UNICODE_STRING file_name;
  DWORD dw;

  RtlInitUnicodeString(&file_name, IMDISK_CTL_DEVICE_NAME);

  for (;;)
    {
      driver = ImDiskOpenDeviceByName(&file_name,
				      GENERIC_READ | GENERIC_WRITE);

      if (driver != INVALID_HANDLE_VALUE)
	break;

      if (GetLastError() != ERROR_FILE_NOT_FOUND)
	{
	  PrintLastError(L"Error controlling the ImDisk Virtual Disk Driver:");
	  return -1;
	}

      if (!ImDiskStartService(IMDISK_DRIVER_NAME))
	switch (GetLastError())
	  {
	  case ERROR_SERVICE_DOES_NOT_EXIST:
	    fputs("The ImDisk Virtual Disk Driver is not installed. "
		  "Please re-install ImDisk.\r\n", stderr);
	    return -1;

	  case ERROR_PATH_NOT_FOUND:
	  case ERROR_FILE_NOT_FOUND:
	    fputs("Cannot load imdisk.sys. "
		  "Please re-install ImDisk.\r\n", stderr);
	    return -1;

	  case ERROR_SERVICE_DISABLED:
	    fputs("The ImDisk Virtual Disk Driver is disabled.\r\n", stderr);
	    return -1;

	  default:
	    PrintLastError(L"Error loading ImDisk Virtual Disk Driver:");
	    return -1;
	  }

      Sleep(0);
      puts("The ImDisk Virtual Disk Driver was loaded into the kernel.");
    }

  if (!ImDiskCheckDriverVersion(driver))
    {
      CloseHandle(driver);
      return -1;
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

	      puts("The ImDisk I/O Packet Forwarder Service was started.");
	    }
	  else
	    {
	      switch (GetLastError())
		{
		case ERROR_SERVICE_DOES_NOT_EXIST:
		  fputs("The ImDisk I/O Packet Forwarder Service is not "
			"installed.\r\n"
			"Please re-install ImDisk.\r\n", stderr);
		  break;

		case ERROR_PATH_NOT_FOUND:
		case ERROR_FILE_NOT_FOUND:
		  fputs("Cannot start ImDisk I/O Packet Forwarder Service.\r\n"
			"Please re-install ImDisk.\r\n", stderr);
		  break;

		case ERROR_SERVICE_DISABLED:
		  fputs("The ImDisk I/O Packet Forwarder Service is "
			"disabled.\r\n", stderr);
		  break;

		default:
		  PrintLastError
		    (L"Error starting ImDisk I/O Packet Forwarder Service:");
		}

	      CloseHandle(driver);
	      return -1;
	    }
    }

  if (FileName == NULL)
    RtlInitUnicodeString(&file_name, NULL);
  else
    if (NativePath)
      {
	if (!RtlCreateUnicodeString(&file_name, FileName))
	  {
	    CloseHandle(driver);
	    fputs("Memory allocation error.\r\n", stderr);
	    return -1;
	  }
      }
    else
      {
	if (!RtlDosPathNameToNtPathName_U(FileName, &file_name, NULL, NULL))
	  {
	    CloseHandle(driver);
	    fputs("Memory allocation error.\r\n", stderr);
	    return -1;
	  }
      }

  create_data = _alloca(sizeof(IMDISK_CREATE_DATA) + file_name.Length);
  if (create_data == NULL)
    {
      perror("Memory allocation error");
      CloseHandle(driver);
      RtlFreeUnicodeString(&file_name);
      return -1;
    }

  ZeroMemory(create_data, sizeof(IMDISK_CREATE_DATA) + file_name.Length);

  // Check if mount point is a drive letter or junction point
  if (MountPoint != NULL)
    if ((wcslen(MountPoint) == 2) ? MountPoint[1] == L':' : FALSE)
      create_data->DriveLetter = MountPoint[0];

  create_data->DeviceNumber   = *DeviceNumber;
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
      PrintLastError(L"Error creating virtual disk:");
      CloseHandle(driver);
      return -1;
    }

  CloseHandle(driver);

  if (MountPoint != NULL)
    {
      WCHAR device_path[MAX_PATH];

      // Build device path, e.g. \Device\ImDisk2
      _snwprintf(device_path, sizeof(device_path) / sizeof(*device_path),
		 IMDISK_DEVICE_BASE_NAME L"%u", create_data->DeviceNumber);
      device_path[sizeof(device_path)/sizeof(*device_path) - 1] = 0;

      if (create_data->DriveLetter == 0)
	{
	  if (!ImDiskCreateMountPoint(MountPoint, device_path))
	    {
	      switch (GetLastError())
		{
		case ERROR_INVALID_REPARSE_DATA:
		  ImDiskOemPrintF(stderr,
				  "Invalid mount point path: '%1!ws!'\n",
				  MountPoint);
		  break;

		case ERROR_INVALID_PARAMETER:
		  fputs("This version of Windows only supports drive letters "
			"as mount points.\r\n"
			"Windows 2000 or higher is required to support "
			"subdirectory mount points.\r\n",
			stderr);
		  break;

		case ERROR_INVALID_FUNCTION:
		case ERROR_NOT_A_REPARSE_POINT:
		  fputs("Mount points are only supported on NTFS volumes.\r\n",
			stderr);
		  break;

		case ERROR_DIRECTORY:
		case ERROR_DIR_NOT_EMPTY:
		  fputs("Mount points can only created on empty "
			"directories.\r\n", stderr);
		  break;

		default:
		  PrintLastError(L"Error creating mount point:");
		}

	      fputs
		("Warning: The device is created without a mount point.\r\n",
		 stderr);

	      MountPoint[0] = 0;
	    }
	}
      else
	if (!DefineDosDevice(DDD_RAW_TARGET_PATH, MountPoint, device_path))
	  PrintLastError(L"Error creating mount point:");
    }

  *DeviceNumber = create_data->DeviceNumber;

  return 0;
}

int
ImDiskRemove(DWORD DeviceNumber, LPCWSTR MountPoint)
{
  PIMDISK_CREATE_DATA create_data = (PIMDISK_CREATE_DATA)
    _alloca(sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2));
  WCHAR drive_letter_mount_point[] = L" :";
  HANDLE device;
  DWORD dw;

  if (MountPoint == NULL)
    {
      device = ImDiskOpenDeviceByNumber(DeviceNumber,
					GENERIC_READ | GENERIC_WRITE);
      if (device == INVALID_HANDLE_VALUE)
	device = ImDiskOpenDeviceByNumber(DeviceNumber,
					  GENERIC_READ);
    }
  else if ((wcslen(MountPoint) == 2) ? MountPoint[1] == ':' : FALSE)
    {
      WCHAR drive_letter_path[] = L"\\\\.\\ :";

      drive_letter_path[4] = MountPoint[0];

      DbgOemPrintF((stdout, "Opening %1!ws!...\n", MountPoint));

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
      device = ImDiskOpenDeviceByMountPoint(MountPoint,
					    GENERIC_READ | GENERIC_WRITE);
      if (device == INVALID_HANDLE_VALUE)
	device = ImDiskOpenDeviceByMountPoint(MountPoint,
					      GENERIC_READ);

      if (device == INVALID_HANDLE_VALUE)
	switch (GetLastError())
	  {
	  case ERROR_INVALID_PARAMETER:
	    fputs("This version of Windows only supports drive letters as "
		  "mount points.\r\n"
		  "Windows 2000 or higher is required to support "
		  "subdirectory mount points.\r\n",
		  stderr);
	    return -1;

	  case ERROR_INVALID_FUNCTION:
	    fputs("Mount points are only supported on NTFS volumes.\r\n",
		  stderr);
	    return -1;

	  case ERROR_NOT_A_REPARSE_POINT:
	  case ERROR_DIRECTORY:
	  case ERROR_DIR_NOT_EMPTY:
	    ImDiskOemPrintF(stderr, "Not a mount point: '%1!ws!'\n",
			    MountPoint);
	    return -1;

	  default:
	    PrintLastError(MountPoint);
	    return -1;
	}
    }

  if (device == INVALID_HANDLE_VALUE)
    if (GetLastError() == ERROR_FILE_NOT_FOUND)
      {
	fputs("No such device.\r\n", stderr);
	return -1;
      }
    else
      {
	PrintLastError(L"Error opening device:");
	return -1;
      }

  if (!ImDiskCheckDriverVersion(device))
    {
      CloseHandle(device);
      return -1;
    }

  if (!DeviceIoControl(device,
                       IOCTL_IMDISK_QUERY_DEVICE,
                       NULL,
                       0,
                       create_data,
                       sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2),
                       &dw, NULL))
    {
      PrintLastError(MountPoint);
      ImDiskOemPrintF(stderr,
		      "%1!ws!: Is that drive really an ImDisk drive?",
		      MountPoint);
      return -1;
    }

  if (dw < sizeof(IMDISK_CREATE_DATA) - sizeof(*create_data->FileName))
    {
      ImDiskOemPrintF(stderr,
		      "%1!ws!: Is that drive really an ImDisk drive?",
		      MountPoint);
      return -1;
    }

  if (MountPoint == NULL)
    {
      drive_letter_mount_point[0] = create_data->DriveLetter;
      MountPoint = drive_letter_mount_point;
    }

  puts("Flushing file buffers...");

  FlushFileBuffers(device);

  puts("Locking volume...");

  if (!DeviceIoControl(device,
                       FSCTL_LOCK_VOLUME,
                       NULL,
		       0,
		       NULL,
		       0,
		       &dw,
		       NULL))
    {
      PrintLastError(MountPoint == NULL ? L"Error" : MountPoint);
      return -1;
    }

  puts("Dismounting filesystem...");

  if (!DeviceIoControl(device,
                       FSCTL_DISMOUNT_VOLUME,
                       NULL,
		       0,
		       NULL,
		       0,
		       &dw,
		       NULL))
    {
      PrintLastError(MountPoint == NULL ? L"Error" : MountPoint);
      return -1;
    }

  puts("Removing device...");

  if (!DeviceIoControl(device,
                       IOCTL_STORAGE_EJECT_MEDIA,
                       NULL,
		       0,
		       NULL,
		       0,
		       &dw,
		       NULL))
    {
      PrintLastError(MountPoint == NULL ? L"Error" : MountPoint);
      return -1;
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
      PrintLastError(MountPoint == NULL ? L"Error" : MountPoint);
      return -1;
    }

  CloseHandle(device);

  if (MountPoint != NULL)
    {
      puts("Removing mountpoint...");

      if ((wcslen(MountPoint) == 2) ? MountPoint[1] != ':' : TRUE)
	{
	  if (!ImDiskRemoveMountPoint(MountPoint))
	    switch (GetLastError())
	      {
	      case ERROR_INVALID_PARAMETER:
		fputs("This version of Windows only supports drive letters as "
		      "mount points.\r\n"
		      "Windows 2000 or higher is required to support "
		      "subdirectory mount points.\r\n",
		      stderr);
		break;

	      case ERROR_INVALID_FUNCTION:
		fputs("Mount points are only supported on empty directories "
		      "on NTFS volumes.\r\n",
		      stderr);
		break;

	      case ERROR_NOT_A_REPARSE_POINT:
	      case ERROR_DIRECTORY:
	      case ERROR_DIR_NOT_EMPTY:
		ImDiskOemPrintF(stderr,
				"Not a mount point: '%1!ws!'\n", MountPoint);
		break;

	      default:
		PrintLastError(MountPoint);
	      }
	}
      else
	if (!DefineDosDevice(DDD_REMOVE_DEFINITION, MountPoint, NULL))
	  PrintLastError(MountPoint);
    }

  puts("OK.");

  return 0;
}

int
ImDiskQueryStatusDriver()
{
  DWORD device_list;
  DWORD dw;
  DWORD counter;
  UNICODE_STRING file_name;
  HANDLE driver;

  RtlInitUnicodeString(&file_name, IMDISK_CTL_DEVICE_NAME);

  driver = ImDiskOpenDeviceByName(&file_name,
				  GENERIC_READ);

  if (driver == INVALID_HANDLE_VALUE)
    {
      if (GetLastError() == ERROR_FILE_NOT_FOUND)
	puts("The ImDisk Virtual Disk Driver is not loaded.");
      else
	PrintLastError(L"Cannot control the ImDisk Virtual Disk Driver:");

      return -1;
    }

  if (!DeviceIoControl(driver,
		       IOCTL_IMDISK_QUERY_DRIVER,
		       NULL, 0,
		       &device_list, sizeof(device_list),
		       &dw, NULL))
    {
      CloseHandle(driver);
      PrintLastError(L"Cannot control the ImDisk Virtual Disk Driver:");
    }

  CloseHandle(driver);

  if (device_list == 0)
    {
      puts("No virtual disks.");
      return 0;
    }

  for (counter = 0; device_list != 0; device_list >>= 1, counter++)
    if (device_list & 1)
      printf("%u\n", counter);

  return 0;
}

int
ImDiskQueryStatusDevice(DWORD DeviceNumber, LPWSTR MountPoint)
{
  HANDLE device;
  DWORD dw;
  PIMDISK_CREATE_DATA create_data = (PIMDISK_CREATE_DATA)
    _alloca(sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2));
  LONGLONG file_size;
  char message_buffer[MAX_PATH];

  if (create_data == NULL)
    {
      perror("Memory allocation error");
      return -1;
    }

  if (MountPoint == NULL)
    {
      device = ImDiskOpenDeviceByNumber(DeviceNumber,
					GENERIC_READ);
    }
  else if ((wcslen(MountPoint) == 2) ? MountPoint[1] == ':' : FALSE)
    {
      WCHAR drive_letter_path[] = L"\\\\.\\ :";

      drive_letter_path[4] = MountPoint[0];

      DbgOemPrintF((stdout, "Opening %1!ws!...\n", MountPoint));

      device = CreateFile(drive_letter_path,
			  GENERIC_READ,
			  FILE_SHARE_READ | FILE_SHARE_WRITE,
			  NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    }
  else
    {
      device = ImDiskOpenDeviceByMountPoint(MountPoint,
					    GENERIC_READ);
      if (device == INVALID_HANDLE_VALUE)
	{
	  switch (GetLastError())
	    {
	    case ERROR_INVALID_PARAMETER:
	      fputs("This version of Windows only supports drive letters as "
		    "mount points.\r\n"
		    "Windows 2000 or higher is required to support "
		    "subdirectory mount points.\r\n",
		    stderr);
	      break;

	    case ERROR_INVALID_FUNCTION:
	      fputs("Mount points are only supported on empty directories on "
		    "NTFS volumes.\r\n",
		    stderr);
	      break;

	    case ERROR_NOT_A_REPARSE_POINT:
	    case ERROR_DIRECTORY:
	    case ERROR_DIR_NOT_EMPTY:
	      ImDiskOemPrintF(stderr,
			      "Not a mount point: '%1!ws!'\n", MountPoint);
	      break;

	    default:
	      PrintLastError(MountPoint);
	    }

	  return -1;
	}
    }

  if (device == INVALID_HANDLE_VALUE)
    if (GetLastError() == ERROR_FILE_NOT_FOUND)
      {
	fputs("No such device.\r\n", stderr);
	return -1;
      }
    else
      {
	PrintLastError(L"Error opening device:");
	return -1;
      }

  if (!ImDiskCheckDriverVersion(device))
    {
      CloseHandle(device);
      return -1;
    }

  if (!DeviceIoControl(device,
                       IOCTL_IMDISK_QUERY_DEVICE,
                       NULL,
                       0,
                       create_data,
                       sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2),
                       &dw, NULL))
    {
      PrintLastError(MountPoint);
      ImDiskOemPrintF(stderr,
		      "%1!ws!: Is that drive really an ImDisk drive?",
		      MountPoint);
      return -1;
    }

  if (dw < sizeof(IMDISK_CREATE_DATA) - sizeof(*create_data->FileName))
    {
      ImDiskOemPrintF(stderr,
		      "%1!ws!: Is that drive really an ImDisk drive?",
		      MountPoint);
      return -1;
    }

  file_size =
    create_data->DiskGeometry.Cylinders.QuadPart *
    create_data->DiskGeometry.TracksPerCylinder *
    create_data->DiskGeometry.SectorsPerTrack *
    create_data->DiskGeometry.BytesPerSector;

  _snprintf(message_buffer, sizeof message_buffer,
	    "%wc%ws%s%.*ws\nSize: %I64u bytes (%.4g %s)%s%s%s.",
	    create_data->DriveLetter == 0 ?
	    L'>' : create_data->DriveLetter,
	    (MountPoint == NULL) | (create_data->DriveLetter != 0) ?
	    L":" : MountPoint,
	    create_data->FileNameLength > 0 ?
	    " = " : "",
	    (int)(create_data->FileNameLength /
		  sizeof(*create_data->FileName)),
	    create_data->FileName,
	    file_size,
	    _h(file_size), _p(file_size),
	    IMDISK_READONLY(create_data->Flags) ?
	    ", ReadOnly" : "",
	    IMDISK_TYPE(create_data->Flags) == IMDISK_TYPE_VM ?
	    ", Virtual Memory Disk" :
	    IMDISK_TYPE(create_data->Flags) == IMDISK_TYPE_PROXY ?
	    ", Proxy Virtual Disk" : ", File Type Virtual Disk",
	    IMDISK_DEVICE_TYPE(create_data->Flags) ==
	    IMDISK_DEVICE_TYPE_CD ? ", CD-ROM" :
	    IMDISK_DEVICE_TYPE(create_data->Flags) ==
	    IMDISK_DEVICE_TYPE_FD ? ", Floppy" : ", HDD");

  message_buffer[sizeof(message_buffer)-1] = 0;

  CharToOemA(message_buffer, message_buffer);
  puts(message_buffer);

  return 0;
}

int __cdecl
wmain(int argc, LPWSTR argv[])
{
  enum
    {
      OP_MODE_NONE,
      OP_MODE_CREATE,
      OP_MODE_REMOVE,
      OP_MODE_QUERY
    } op_mode = OP_MODE_NONE;
  DWORD flags = 0;
  BOOL native_path = FALSE;
  BOOL numeric_print = FALSE;
  LPWSTR file_name = NULL;
  LPWSTR mount_point = NULL;
  DWORD device_number = IMDISK_AUTO_DEVICE_NUMBER;
  DISK_GEOMETRY disk_geometry = { 0 };

  if (argc == 2)
    if (wcscmp(argv[1], L"--version") == 0)
      {
	puts
	  ("Control program for the ImDisk Virtual Disk Driver for Windows NT/2000/XP.\r\n"
	   "\n"
	   "Copyright (C) 2004-2007 Olof Lagerkvist.\r\n"
"\n"
	   "Copyright to parts of this progam belongs to:\r\n"
	   "- Parts related to floppy emulation based on VFD by Ken Kato.\r\n"
	   "  http://chitchat.at.infoseek.co.jp/vmware/vfd.html\r\n"
	   "- Parts related to CD-ROM emulation and impersonation to support remote\r\n"
	   "  files based on FileDisk by Bo Brant‚n.\r\n"
	   "  http://www.acc.umu.se/~bosse/\r\n"
	   "- Virtual memory image support, usermode storage backend support and some\r\n"
	   "  code ported to NT from the FreeBSD md driver by Olof Lagerkvist.\r\n"
	   "  http://www.ltr-data.se\r\n"
	   "\n"
	   "This program is free software; you can redistribute it and/or modify\r\n"
	   "it under the terms of the GNU General Public License as published by\r\n"
	   "the Free Software Foundation; either version 2 of the License, or\r\n"
	   "(at your option) any later version.\r\n"
	   "This program is distributed in the hope that it will be useful,\r\n"
	   "but WITHOUT ANY WARRANTY; without even the implied warranty of\r\n"
	   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\r\n"
	   "GNU General Public License for more details.\r\n"
	   "You should have received a copy of the GNU General Public License\r\n"
	   "along with this program; if not, write to the Free Software\r\n"
	   "Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA");

	return 0;
      }

  while (argc-- > 1)
    {
      argv++;

      if (wcslen(argv[0]) == 2 ? argv[0][0] == L'-' : FALSE)
	switch (argv[0][1])
	  {
	  case L'a':
	    if (op_mode != OP_MODE_NONE)
	      ImDiskSyntaxHelp();

	    op_mode = OP_MODE_CREATE;
	    break;

	  case L'd':
	    if (op_mode != OP_MODE_NONE)
	      ImDiskSyntaxHelp();

	    op_mode = OP_MODE_REMOVE;
	    break;

	  case L'l':
	    if (op_mode != OP_MODE_NONE)
	      ImDiskSyntaxHelp();

	    op_mode = OP_MODE_QUERY;
	    break;

	  case L't':
	    if ((op_mode != OP_MODE_CREATE) |
		(argc < 2) |
		(IMDISK_TYPE(flags) != 0))
	      ImDiskSyntaxHelp();

	    if (wcscmp(argv[1], L"file") == 0)
	      flags |= IMDISK_TYPE_FILE;
	    else if (wcscmp(argv[1], L"vm") == 0)
	      flags |= IMDISK_TYPE_VM;
	    else if (wcscmp(argv[1], L"proxy") == 0)
	      flags |= IMDISK_TYPE_PROXY;
	    else
	      ImDiskSyntaxHelp();

	    argc--;
	    argv++;
	    break;

	  case L'n':
	    numeric_print = TRUE;
	    break;

	  case L'o':
	    if ((op_mode != OP_MODE_CREATE) |
		(argc < 2))
	      ImDiskSyntaxHelp();

	    {
	      LPWSTR opt;

	      for (opt = wcstok(argv[1], L",");
		   opt != NULL;
		   opt = wcstok(NULL, L","))
		if (wcscmp(opt, L"ro") == 0)
		  flags |= IMDISK_OPTION_RO;
		else if (wcscmp(opt, L"ip") == 0)
		  {
		    if ((IMDISK_TYPE(flags) != IMDISK_TYPE_PROXY) |
			(IMDISK_PROXY_TYPE(flags) != IMDISK_PROXY_TYPE_DIRECT))
		      ImDiskSyntaxHelp();

		    native_path = TRUE;
		    flags |= IMDISK_PROXY_TYPE_TCP;
		  }
		else if (wcscmp(opt, L"comm") == 0)
		  {
		    if ((IMDISK_TYPE(flags) != IMDISK_TYPE_PROXY) |
			(IMDISK_PROXY_TYPE(flags) != IMDISK_PROXY_TYPE_DIRECT))
		      ImDiskSyntaxHelp();

		    native_path = TRUE;
		    flags |= IMDISK_PROXY_TYPE_COMM;
		  }
		else if (IMDISK_DEVICE_TYPE(flags) != 0)
		  ImDiskSyntaxHelp();
		else if (wcscmp(opt, L"hd") == 0)
		  flags |= IMDISK_DEVICE_TYPE_HD;
		else if (wcscmp(opt, L"fd") == 0)
		  flags |= IMDISK_DEVICE_TYPE_FD;
		else if (wcscmp(opt, L"cd") == 0)
		  flags |= IMDISK_DEVICE_TYPE_CD;
		else
		  ImDiskSyntaxHelp();
	    }

	    argc--;
	    argv++;
	    break;

	  case L'f':
	  case L'F':
	    if ((op_mode != OP_MODE_CREATE) |
		(argc < 2) |
		(file_name != NULL))
	      ImDiskSyntaxHelp();

	    if (argv[0][1] == L'F')
	      native_path = TRUE;

	    file_name = argv[1];

	    argc--;
	    argv++;
	    break;

	  case L's':
	    if ((op_mode != OP_MODE_CREATE) |
		(argc < 2) |
		(disk_geometry.Cylinders.QuadPart != 0) |
		(disk_geometry.BytesPerSector != 0) |
		(disk_geometry.SectorsPerTrack != 0) |
		(disk_geometry.TracksPerCylinder != 0))
	      ImDiskSyntaxHelp();

	    {
              WCHAR suffix = 0;

	      swscanf(argv[1], L"%I64u%c",
		      &disk_geometry.Cylinders, &suffix);

              switch (suffix)
                {
		case 0:
		  break;
                case 'T':
                  disk_geometry.Cylinders.QuadPart <<= 10;
                case 'G':
                  disk_geometry.Cylinders.QuadPart <<= 10;
                case 'M':
                  disk_geometry.Cylinders.QuadPart <<= 10;
                case 'K':
                  disk_geometry.Cylinders.QuadPart <<= 10;
                  break;
                case 'b':
                  disk_geometry.Cylinders.QuadPart <<= 9;
                  break;
                case 't':
                  disk_geometry.Cylinders.QuadPart *= 1000;
                case 'g':
                  disk_geometry.Cylinders.QuadPart *= 1000;
                case 'm':
                  disk_geometry.Cylinders.QuadPart *= 1000;
                case 'k':
                  disk_geometry.Cylinders.QuadPart *= 1000;
                  break;
                default:
                  fprintf(stderr, "ImDisk: Unsupported size suffix: '%wc'\n",
                          suffix);
                  return 1;
                }
	    }

	    argc--;
	    argv++;
	    break;

	  case L'S':
	    if ((op_mode != OP_MODE_CREATE) |
		(argc < 2) |
		(disk_geometry.BytesPerSector != 0))
	      ImDiskSyntaxHelp();

	    disk_geometry.BytesPerSector = wcstoul(argv[1], NULL, 0);
	    disk_geometry.Cylinders.QuadPart /= disk_geometry.BytesPerSector;

	    argc--;
	    argv++;
	    break;
	    
	  case L'x':
	    if ((op_mode != OP_MODE_CREATE) |
		(argc < 2) |
		(disk_geometry.SectorsPerTrack != 0))
	      ImDiskSyntaxHelp();

	    disk_geometry.SectorsPerTrack = wcstoul(argv[1], NULL, 0);
	    disk_geometry.Cylinders.QuadPart /= disk_geometry.SectorsPerTrack;

	    argc--;
	    argv++;
	    break;
	    
	  case L'y':
	    if ((op_mode != OP_MODE_CREATE) |
		(argc < 2) |
		(disk_geometry.TracksPerCylinder != 0))
	      ImDiskSyntaxHelp();

	    disk_geometry.TracksPerCylinder = wcstoul(argv[1], NULL, 0);
	    disk_geometry.Cylinders.QuadPart /=
	      disk_geometry.TracksPerCylinder;

	    argc--;
	    argv++;
	    break;
	    
	  case L'u':
	    if ((argc < 2) |
		((mount_point != NULL) & (op_mode != OP_MODE_CREATE)) |
		(device_number != IMDISK_AUTO_DEVICE_NUMBER))
	      ImDiskSyntaxHelp();

	    device_number = wcstoul(argv[1], NULL, 0);

	    argc--;
	    argv++;
	    break;
	    
	  case L'm':
	    if ((argc < 2) |
		(mount_point != NULL) |
		((device_number != IMDISK_AUTO_DEVICE_NUMBER) &
		 (op_mode != OP_MODE_CREATE)))
	      ImDiskSyntaxHelp();

	    mount_point = argv[1];

	    argc--;
	    argv++;
	    break;

	  default:
	    ImDiskSyntaxHelp();
	  }
      else
	ImDiskSyntaxHelp();
    }

  switch (op_mode)
    {
    case OP_MODE_CREATE:
      {
	int ret = ImDiskCreate(&device_number, &disk_geometry, flags,
			       file_name, native_path, mount_point);

	if (ret == 0)
	  if (numeric_print)
	    printf("%u\n", device_number);
	  else
	    ImDiskOemPrintF
	      (stdout,
	       "Created device %1!u!: %2!ws! -> %3!ws!",
	       device_number,
	       mount_point == NULL ? L"No mountpoint" : mount_point,
	       file_name == NULL ? L"VM image" : file_name);

	return ret;
      }

    case OP_MODE_REMOVE:
      if ((device_number == IMDISK_AUTO_DEVICE_NUMBER) &
	  (mount_point == NULL))
	ImDiskSyntaxHelp();

      return ImDiskRemove(device_number, mount_point);

    case OP_MODE_QUERY:
      if ((device_number == IMDISK_AUTO_DEVICE_NUMBER) &
	  (mount_point == NULL))
	return !ImDiskQueryStatusDriver();
      
      return ImDiskQueryStatusDevice(device_number, mount_point);
    }

  ImDiskSyntaxHelp();
}

__declspec(noreturn)
void
__cdecl
wmainCRTStartup()
{
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLine(), &argc);

  if (argv == NULL)
    {
      MessageBoxA(NULL, "This program requires Windows NT/2000/XP.", "ImDisk",
		  MB_ICONSTOP);
      ExitProcess((UINT)-1);
    }

  exit(wmain(argc, argv));
}
