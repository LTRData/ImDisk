/*
    ImDisk Virtual Disk Driver for Windows NT/2000/XP.
    This driver emulates harddisk partitions, floppy drives and CD/DVD-ROM
    drives from disk image files, in virtual memory or by redirecting I/O
    requests somewhere else, possibly to another machine, through a
    co-operating user-mode service, ImDskSvc.

    Copyright (C) 2005-2006 Olof Lagerkvist.

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

#include <ntddk.h>
#include <ntdddisk.h>
#include <ntddcdrm.h>
#include <ntverp.h>
#include <stdio.h>

///
/// Definitions and imports are now in the "sources" file and managed by the
/// build utility.
///

#define DEBUG_LEVEL 0

#if DEBUG_LEVEL >= 2
#define KdPrint2(x) DbgPrint x
#else
#define KdPrint2(x)
#endif

#if DEBUG_LEVEL >= 1
#undef KdPrint
#define KdPrint(x)  DbgPrint x
#endif

#include "..\inc\ntkmapi.h"
#include "..\inc\imdisk.h"
#include "..\inc\imdproxy.h"

#define IMDISK_DEFAULT_LOAD_DEVICES      0
#define IMDISK_DEFAULT_MAX_DEVICES       16

///
/// Constants for synthetical geometry of the virtual disks
///

// For hard drive partition-style devices
#define SECTOR_SIZE_HDD                  512

// For CD-ROM/DVD-style devices
#define SECTOR_SIZE_CD_ROM               2048
#define SECTORS_PER_TRACK_CD_ROM         32
#define TRACKS_PER_CYLINDER_CD_ROM       64

// For floppy devices. Based on Virtual Floppy Driver, VFD, by Ken Kato.
#define SECTOR_SIZE_FDD                  512
#define TRACKS_PER_CYLINDER_FDD          12
//
//	Sizes in bytes of different kinds of floppies
//
#define MEDIA_SIZE_2P88MB	(2880 << 10)
#define MEDIA_SIZE_1P44MB	(1440 << 10)
#define	MEDIA_SIZE_720KB 	(720  << 10)
#define MEDIA_SIZE_1P20MB	(1200 << 10)
#define MEDIA_SIZE_640KB	(640  << 10)
#define	MEDIA_SIZE_320KB 	(320  << 10)

//
//      Indexes for the following DISK_GEOMETRY table.
//
#define MEDIA_TYPE_288	0
#define MEDIA_TYPE_144	1
#define MEDIA_TYPE_720	2
#define MEDIA_TYPE_120	3
#define MEDIA_TYPE_640	4
#define MEDIA_TYPE_320	5

DISK_GEOMETRY media_table[] = {
  { { 80 }, F3_2Pt88_512, 2, 36, 512 },
  { { 80 }, F3_1Pt44_512, 2, 18, 512 },
  { { 80 }, F3_720_512,   2,  9, 512 },
  { { 80 }, F5_1Pt2_512,  2, 15, 512 },
  { { 40 }, F5_640_512,   2, 18, 512 },
  { { 40 }, F5_320_512,   2,  9, 512 }
};

//
//	TOC Data Track returned for virtual CD/DVD
//
#define TOC_DATA_TRACK                   0x04

//
//	Fill character for formatting virtual floppy media
//
#define MEDIA_FORMAT_FILL_DATA	0xf6

typedef struct _DEVICE_THREAD_DATA
{
  PDRIVER_OBJECT driver_object;
  PIMDISK_CREATE_DATA create_data;
  PETHREAD client_thread;
  KEVENT created_event;
  NTSTATUS status;
} DEVICE_THREAD_DATA, *PDEVICE_THREAD_DATA;

typedef struct _DEVICE_EXTENSION
{
  LIST_ENTRY list_head;
  KSPIN_LOCK list_lock;
  KEVENT request_event;
  PVOID thread_pointer;
  BOOLEAN terminate_thread;
  ULONG device_number;
  union
  {
    HANDLE file_handle;   // For file or proxy type
    PUCHAR image_buffer;  // For vm type
  };
  UNICODE_STRING file_name;
  WCHAR drive_letter;
  DISK_GEOMETRY disk_geometry;
  ULONG media_change_count;
  BOOLEAN media_in_device;
  BOOLEAN read_only;
  BOOLEAN vm_disk;
  BOOLEAN use_proxy;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

// Our own functions.

NTSTATUS
DriverEntry(IN PDRIVER_OBJECT DriverObject,
	    IN PUNICODE_STRING RegistryPath);

VOID
ImDiskUnload(IN PDRIVER_OBJECT DriverObject);

NTSTATUS
ImDiskAddVirtualDisk(IN PDRIVER_OBJECT DriverObject,
		     IN OUT PIMDISK_CREATE_DATA CreateData,
		     IN PETHREAD ClientThread);

VOID
ImDiskRemoveVirtualDisk(IN PDEVICE_OBJECT DeviceObject);

NTSTATUS
ImDiskCreateClose(IN PDEVICE_OBJECT DeviceObject,
		  IN PIRP Irp);

NTSTATUS
ImDiskReadWrite(IN PDEVICE_OBJECT DeviceObject,
		IN PIRP Irp);

NTSTATUS
ImDiskDeviceControl(IN PDEVICE_OBJECT DeviceObject,
		    IN PIRP Irp);

VOID
ImDiskDeviceThread(IN PVOID Context);

NTSTATUS
ImDiskReadVMDisk(IN PUCHAR VMDisk,
		 OUT PIO_STATUS_BLOCK IoStatusBlock,
		 OUT PVOID Buffer,
		 IN ULONG Length,
		 IN ULONG ByteOffset);

NTSTATUS
ImDiskWriteVMDisk(OUT PUCHAR VMDisk,
		  OUT PIO_STATUS_BLOCK IoStatusBlock,
		  IN PVOID Buffer,
		  IN ULONG Length,
		  IN ULONG ByteOffset);

NTSTATUS
ImDiskConnectProxy(IN HANDLE FileHandle,
		   OUT PIO_STATUS_BLOCK IoStatusBlock,
		   IN ULONG Flags,
		   IN PWSTR ConnectionString,
		   IN ULONG ConnectionStringLength,
		   OUT PIMDPROXY_INFO_RESP ProxyInfoResponse,
		   IN ULONG ProxyInfoResponseLength);

NTSTATUS
ImDiskQueryInformationProxy(IN HANDLE FileHandle,
			    OUT PIO_STATUS_BLOCK IoStatusBlock,
			    OUT PIMDPROXY_INFO_RESP ProxyInfoResponse,
			    IN ULONG ProxyInfoResponseLength);

NTSTATUS
ImDiskReadProxy(IN HANDLE FileHandle,
		OUT PIO_STATUS_BLOCK IoStatusBlock,
		OUT PVOID Buffer,
		IN ULONG Length,
		IN PLARGE_INTEGER ByteOffset);

NTSTATUS
ImDiskWriteProxy(IN HANDLE FileHandle,
		 OUT PIO_STATUS_BLOCK IoStatusBlock,
		 OUT PVOID Buffer,
		 IN ULONG Length,
		 IN PLARGE_INTEGER ByteOffset);

//
// Reads in a loop up to "Length" or until eof reached.
//
NTSTATUS
ImDiskSafeReadFile(IN HANDLE FileHandle,
		   OUT PIO_STATUS_BLOCK IoStatusBlock,
		   OUT PVOID Buffer,
		   IN ULONG Length,
		   IN PLARGE_INTEGER Offset);

NTSTATUS
ImDiskFloppyFormat(IN PDEVICE_EXTENSION Extension,
		   IN PIRP Irp);

//
// Pointer to the controller device object.
//
PDEVICE_OBJECT ImDiskCtlDevice;

//
// Allocation bitmap with currently cnfigured device numbers.
//
volatile ULONG DeviceList = 0;

//
// Max number of devices that can be dynamically created by IOCTL calls
// to the control device.
//
ULONG MaxDevices;

#pragma code_seg("INIT")

//
// This is where it all starts...
//
NTSTATUS
DriverEntry(IN PDRIVER_OBJECT DriverObject,
	    IN PUNICODE_STRING RegistryPath)
{
  UNICODE_STRING parameter_path;
  UNICODE_STRING ctl_device_name;
  UNICODE_STRING sym_link;
  HANDLE key_handle;
  ULONG n_devices;
  NTSTATUS status;
  OBJECT_ATTRIBUTES object_attributes;
  ULONG n;

  // First open and read registry settings to find out if we should load and
  // mount anything automatically.
  parameter_path.Length = 0;

  parameter_path.MaximumLength = RegistryPath->Length +
    sizeof(IMDISK_CFG_PARAMETER_KEY);

  parameter_path.Buffer =
    (PWSTR) ExAllocatePool(PagedPool, parameter_path.MaximumLength);

  if (parameter_path.Buffer == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;

  RtlCopyUnicodeString(&parameter_path, RegistryPath);

  RtlAppendUnicodeToString(&parameter_path, IMDISK_CFG_PARAMETER_KEY);

  InitializeObjectAttributes(&object_attributes, &parameter_path, 0, NULL,
			     NULL);

  status = ZwOpenKey(&key_handle, KEY_READ, &object_attributes);
  if (!NT_SUCCESS(status))
    key_handle = NULL;

  ExFreePool(parameter_path.Buffer);

  if (key_handle != NULL)
    {
      UNICODE_STRING number_of_devices_value;
      PKEY_VALUE_PARTIAL_INFORMATION value_info;
      ULONG required_size;

      value_info = ExAllocatePool(PagedPool,
				  sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
				  sizeof(ULONG));

      if (value_info == NULL)
	{
	  ZwClose(key_handle);
	  return STATUS_INSUFFICIENT_RESOURCES;
	}

      RtlInitUnicodeString(&number_of_devices_value,
			   IMDISK_CFG_LOAD_DEVICES_VALUE);
      
      status = ZwQueryValueKey(key_handle, &number_of_devices_value,
			       KeyValuePartialInformation, value_info,
			       sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
			       sizeof(ULONG), &required_size);

      if (!NT_SUCCESS(status))
	{
	  KdPrint(("ImDisk: Using default value for '%ws'.\n",
		   IMDISK_CFG_LOAD_DEVICES_VALUE));
	  n_devices = IMDISK_DEFAULT_LOAD_DEVICES;
	}
      else if (value_info->Type == REG_DWORD)
	n_devices = *(PULONG) value_info->Data;
      else
	{
	  ExFreePool(value_info);
	  ZwClose(key_handle);
	  return STATUS_INVALID_PARAMETER;
	}

      RtlInitUnicodeString(&number_of_devices_value,
			   IMDISK_CFG_MAX_DEVICES_VALUE);
      
      status = ZwQueryValueKey(key_handle, &number_of_devices_value,
			       KeyValuePartialInformation, value_info,
			       sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
			       sizeof(ULONG), &required_size);

      if (!NT_SUCCESS(status))
	{
	  KdPrint(("ImDisk: Using default value for '%ws'.\n",
		   IMDISK_CFG_MAX_DEVICES_VALUE));
	  MaxDevices = IMDISK_DEFAULT_MAX_DEVICES;
	}
      else if (value_info->Type == REG_DWORD)
	MaxDevices = *(PULONG) value_info->Data;
      else
	{
	  ExFreePool(value_info);
	  ZwClose(key_handle);
	  return STATUS_INVALID_PARAMETER;
	}

      ExFreePool(value_info);
    }
 else
   {
     KdPrint
       (("ImDisk: Cannot open registry key (%#x), using default values.\n",
	 status));

     n_devices = IMDISK_DEFAULT_LOAD_DEVICES;
     MaxDevices = IMDISK_DEFAULT_MAX_DEVICES;
   }

  // Create the control device.
  RtlInitUnicodeString(&ctl_device_name, IMDISK_CTL_DEVICE_NAME);

  status = IoCreateDevice(DriverObject,
			  sizeof(DEVICE_EXTENSION),
			  &ctl_device_name,
			  FILE_DEVICE_IMDISK,
			  0,
			  FALSE,
			  &ImDiskCtlDevice);

  if (!NT_SUCCESS(status))
    {
      KdPrint(("ImDisk: Cannot create control device (%#x).\n", status));
      if (key_handle != NULL)
	ZwClose(key_handle);

      return status;
    }

  // The control device gets a device_number of -1 to make it easily
  // distinguishable from the actual created devices.
  ((PDEVICE_EXTENSION) ImDiskCtlDevice->DeviceExtension)->device_number =
    (ULONG) -1;

  RtlInitUnicodeString(&sym_link, IMDISK_CTL_SYMLINK_NAME);
  IoCreateUnprotectedSymbolicLink(&sym_link, &ctl_device_name);

  // If the registry settings told us to create devices here in the start
  // procedure, do that now.
  for (n = 0; n < n_devices; n++)
    {
      LARGE_INTEGER wait_time;
      PKEY_VALUE_PARTIAL_INFORMATION value_info_image_file;
      PKEY_VALUE_PARTIAL_INFORMATION value_info_size;
      PKEY_VALUE_PARTIAL_INFORMATION value_info_flags;
      ULONG required_size;
      PIMDISK_CREATE_DATA create_data;
      PWSTR value_name_buffer;
      UNICODE_STRING value_name;

      wait_time.QuadPart = -1;
      KeDelayExecutionThread(KernelMode, FALSE, &wait_time);

      value_info_image_file =
	ExAllocatePool(PagedPool,
		       sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
		       (MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR)));

      if (value_info_image_file == NULL)
	{
	  KdPrint(("ImDisk: Error creating device %u. (ExAllocatePool)\n", n));

	  break;
	}

      value_info_size =
	ExAllocatePool(PagedPool,
		       sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
		       sizeof(LARGE_INTEGER));

      if (value_info_size == NULL)
	{
	  KdPrint(("ImDisk: Error creating device %u. (ExAllocatePool)\n", n));

	  ExFreePool(value_info_image_file);
	  break;
	}

      value_info_flags =
	ExAllocatePool(PagedPool,
		       sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG));

      if (value_info_flags == NULL)
	{
	  KdPrint(("ImDisk: Error creating device %u. (ExAllocatePool)\n", n));

	  ExFreePool(value_info_image_file);
	  ExFreePool(value_info_size);
	  break;
	}

      value_name_buffer = ExAllocatePool(PagedPool, MAXIMUM_FILENAME_LENGTH);

      if (value_info_flags == NULL)
	{
	  KdPrint(("ImDisk: Error creating device %u. (ExAllocatePool)\n", n));

	  ExFreePool(value_info_image_file);
	  ExFreePool(value_info_size);
	  ExFreePool(value_info_flags);
	  break;
	}

      _snwprintf(value_name_buffer, MAXIMUM_FILENAME_LENGTH,
		 IMDISK_CFG_IMAGE_FILE_PREFIX L"%u", n);
      value_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

      RtlInitUnicodeString(&value_name, value_name_buffer);

      status = ZwQueryValueKey(key_handle,
			       &value_name,
			       KeyValuePartialInformation,
			       value_info_image_file,
			       sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
			       (MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR)),
			       &required_size);

      if ((!NT_SUCCESS(status)) |
	  (value_info_image_file->Type != REG_SZ))
	{
	  KdPrint(("ImDisk: Missing or bad '%ws' for device %i.\n",
		   value_name_buffer, n));

	  *(PWCHAR) value_info_image_file->Data = 0;
	  value_info_image_file->DataLength = sizeof(WCHAR);
	}

      _snwprintf(value_name_buffer, MAXIMUM_FILENAME_LENGTH,
		 IMDISK_CFG_SIZE_PREFIX L"%u", n);
      value_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

      RtlInitUnicodeString(&value_name, value_name_buffer);

      status = ZwQueryValueKey(key_handle,
			       &value_name,
			       KeyValuePartialInformation,
			       value_info_size,
			       sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
			       sizeof(LARGE_INTEGER),
			       &required_size);

      if ((!NT_SUCCESS(status)) |
	  (value_info_size->Type != REG_BINARY) |
	  (value_info_size->DataLength != sizeof(LARGE_INTEGER)))
	{
	  KdPrint(("ImDisk: Missing or bad '%ws' for device %i.\n",
		   value_name_buffer, n));

	  ((PLARGE_INTEGER) value_info_size->Data)->QuadPart = 0;
	}

      _snwprintf(value_name_buffer, MAXIMUM_FILENAME_LENGTH,
		 IMDISK_CFG_FLAGS_PREFIX L"%u", n);
      value_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

      RtlInitUnicodeString(&value_name, value_name_buffer);

      status = ZwQueryValueKey(key_handle,
			       &value_name,
			       KeyValuePartialInformation,
			       value_info_flags,
			       sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
			       sizeof(ULONG),
			       &required_size);

      if ((!NT_SUCCESS(status)) |
	  (value_info_flags->Type != REG_DWORD))
	{
	  KdPrint(("ImDisk: Missing or bad '%ws' for device %i.\n",
		   value_name_buffer, n));

	  *(PULONG) value_info_flags->Data = 0;
	}

      ExFreePool(value_name_buffer);

      create_data =
	ExAllocatePool(PagedPool,
		       sizeof(IMDISK_CREATE_DATA) +
		       value_info_image_file->DataLength);

      if (create_data == NULL)
	{
	  KdPrint(("ImDisk: Error creating device %u. (ExAllocatePool)\n", n));

	  ExFreePool(value_info_image_file);
	  ExFreePool(value_info_size);
	  ExFreePool(value_info_flags);
	  break;
	}

      RtlZeroMemory(create_data, sizeof(IMDISK_CREATE_DATA));

      wcscpy(create_data->FileName, (PCWSTR) value_info_image_file->Data);

      create_data->FileNameLength = (USHORT)
	value_info_image_file->DataLength - sizeof(WCHAR);

      ExFreePool(value_info_image_file);

      create_data->DiskGeometry.Cylinders.QuadPart =
	((PLARGE_INTEGER) value_info_size->Data)->QuadPart;

      ExFreePool(value_info_size);

      create_data->Flags = *(PULONG) value_info_flags->Data;

      ExFreePool(value_info_flags);

      create_data->DeviceNumber = n;

      ImDiskAddVirtualDisk(DriverObject, create_data, NULL);

      ExFreePool(create_data);
    }

  if (key_handle != NULL)
    ZwClose(key_handle);

  DriverObject->MajorFunction[IRP_MJ_CREATE] = ImDiskCreateClose;
  DriverObject->MajorFunction[IRP_MJ_CLOSE] = ImDiskCreateClose;
  DriverObject->MajorFunction[IRP_MJ_READ] = ImDiskReadWrite;
  DriverObject->MajorFunction[IRP_MJ_WRITE] = ImDiskReadWrite;
  DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ImDiskDeviceControl;

  DriverObject->DriverUnload = ImDiskUnload;

  return STATUS_SUCCESS;
}

#pragma code_seg("PAGE")

NTSTATUS
ImDiskAddVirtualDisk(IN PDRIVER_OBJECT DriverObject,
		     IN OUT PIMDISK_CREATE_DATA CreateData,
		     IN PETHREAD ClientThread)
{
  NTSTATUS status;
  DEVICE_THREAD_DATA device_thread_data;
  HANDLE thread_handle;

  PAGED_CODE();

  ASSERT(DriverObject != NULL);
  ASSERT(CreateData != NULL);

  device_thread_data.driver_object = DriverObject;
  device_thread_data.create_data = CreateData;
  device_thread_data.client_thread = ClientThread;
  KeInitializeEvent(&device_thread_data.created_event,
		    NotificationEvent,
		    FALSE);

  status = PsCreateSystemThread(&thread_handle,
				(ACCESS_MASK) 0L,
				NULL,
				NULL,
				NULL,
				ImDiskDeviceThread,
				&device_thread_data);

  if (!NT_SUCCESS(status))
    {
      KdPrint(("ImDisk: Cannot create device thread. (%#x)\n", status));

      return status;
    }

  ZwClose(thread_handle);

  KeWaitForSingleObject(&device_thread_data.created_event,
			Executive,
			KernelMode,
			FALSE,
			NULL);

  if (!NT_SUCCESS(device_thread_data.status))
    {
      KdPrint(("ImDisk: Device thread failed to initialize. (%#x)\n",
	       device_thread_data.status));

      return device_thread_data.status;
    }

  return STATUS_SUCCESS;
}

NTSTATUS
ImDiskCreateDevice(IN PDRIVER_OBJECT DriverObject,
		   IN OUT PIMDISK_CREATE_DATA CreateData,
		   IN PETHREAD ClientThread,
		   OUT PDEVICE_OBJECT *DeviceObject)
{
  UNICODE_STRING file_name;
  PWCHAR device_name_buffer;
  UNICODE_STRING device_name;
  NTSTATUS status;
  PDEVICE_EXTENSION device_extension;
  DEVICE_TYPE device_type;
  ULONG device_characteristics;
  LARGE_INTEGER file_size;
  HANDLE file_handle = NULL;
  PUCHAR image_buffer = NULL;
  ULONG alignment_requirement;

  PAGED_CODE();

  ASSERT(CreateData != NULL);

  KdPrint
    (("ImDisk: Got request to create a virtual disk. Request data:\n"
      "DeviceNumber   = %#x\n"
      "DiskGeometry\n"
      "  .Cylinders   = %p%p\n"
      "  .MediaType   = %i\n"
      "  .T/C         = %u\n"
      "  .S/T         = %u\n"
      "  .B/S         = %u\n"
      "Flags          = %#x\n"
      "FileNameLength = %u\n"
      "FileName       = '%.*ws'\n",
      CreateData->DeviceNumber,
      CreateData->DiskGeometry.Cylinders.HighPart,
      CreateData->DiskGeometry.Cylinders.LowPart,
      CreateData->DiskGeometry.MediaType,
      CreateData->DiskGeometry.TracksPerCylinder,
      CreateData->DiskGeometry.SectorsPerTrack,
      CreateData->DiskGeometry.BytesPerSector,
      CreateData->Flags,
      CreateData->FileNameLength,
      (int)(CreateData->FileNameLength / sizeof(*CreateData->FileName)),
      CreateData->FileName));

  file_size = CreateData->DiskGeometry.Cylinders;
  if (CreateData->DiskGeometry.TracksPerCylinder != 0)
    file_size.QuadPart *= CreateData->DiskGeometry.TracksPerCylinder;
  if (CreateData->DiskGeometry.SectorsPerTrack != 0)
    file_size.QuadPart *= CreateData->DiskGeometry.SectorsPerTrack;
  if (CreateData->DiskGeometry.BytesPerSector != 0)
    file_size.QuadPart *= CreateData->DiskGeometry.BytesPerSector;

  // Auto-select type if not specified.
  if (IMDISK_TYPE(CreateData->Flags) == 0)
    if (CreateData->FileNameLength == 0)
      CreateData->Flags |= IMDISK_TYPE_VM;
    else
      CreateData->Flags |= IMDISK_TYPE_FILE;

  // Blank filenames only supported for non-zero VM disks.
  if (((CreateData->FileNameLength == 0) &
       (IMDISK_TYPE(CreateData->Flags) != IMDISK_TYPE_VM)) |
      ((CreateData->FileNameLength == 0) &
       (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM) &
       (file_size.QuadPart == 0)))
    {
      KdPrint(("ImDisk: Blank filenames only supported for non-zero length "
	       "vm type disks.\n"));

      return STATUS_INVALID_PARAMETER;
    }

  // Cannot create >= 2 GB VM disk in 32 bit version.
  if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM) &
      ((file_size.QuadPart & 0xFFFFFFFF80000000) != 0))
    {
      KdPrint(("ImDisk: Cannot create >= 2GB vm disks on 32-bit system.\n"));

      return STATUS_INVALID_PARAMETER;
    }

  // Auto-find first free device number
  if ((CreateData->DeviceNumber == IMDISK_AUTO_DEVICE_NUMBER) |
      (CreateData->DeviceNumber & 0xFFFFFFE0))
    {
      for (CreateData->DeviceNumber = 0;
	   CreateData->DeviceNumber < MaxDevices;
	   CreateData->DeviceNumber++)
	if ((~DeviceList) & (1 << CreateData->DeviceNumber))
	  break;

      if (CreateData->DeviceNumber >= MaxDevices)
	return STATUS_INVALID_PARAMETER;
    }

  file_name.Length = CreateData->FileNameLength;
  file_name.MaximumLength = CreateData->FileNameLength;
  file_name.Buffer = NULL;

  // If a file is to be opened or created, allocated name buffer and open that
  // file...
  if (CreateData->FileNameLength > 0)
    {
      IO_STATUS_BLOCK io_status;
      OBJECT_ATTRIBUTES object_attributes;
      UNICODE_STRING real_file_name;

      file_name.Buffer = ExAllocatePool(NonPagedPool,
					file_name.MaximumLength);

      if (file_name.Buffer == NULL)
	{
	  KdPrint(("ImDisk: Error allocating buffer for filename.\n"));
	  return STATUS_INSUFFICIENT_RESOURCES;
	}

      RtlCopyMemory(file_name.Buffer, CreateData->FileName,
		    CreateData->FileNameLength);
      // If no device-type specified, check if filename ends with .iso or .bin.
      // In that case, set device-type automatically to FILE_DEVICE_CDROM
      if ((IMDISK_DEVICE_TYPE(CreateData->Flags) == 0) &
	  (CreateData->FileNameLength >= (4 * sizeof(*CreateData->FileName))))
	{
	  LPWSTR name = CreateData->FileName +
	    (CreateData->FileNameLength / sizeof(*CreateData->FileName)) - 4;
	  if ((_wcsnicmp(name, L".iso", 4) == 0) |
	      (_wcsnicmp(name, L".bin", 4) == 0))
	    CreateData->Flags |= IMDISK_DEVICE_TYPE_CD | IMDISK_OPTION_RO;
	}

      if (IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_CD)
	CreateData->Flags |= IMDISK_OPTION_RO;

      KdPrint(("ImDisk: Done with device type auto-selection by file ext.\n"));

      if (ClientThread != NULL)
	{
	  SECURITY_QUALITY_OF_SERVICE security_quality_of_service;
	  SECURITY_CLIENT_CONTEXT security_client_context;

	  RtlZeroMemory(&security_quality_of_service,
			sizeof(SECURITY_QUALITY_OF_SERVICE));

	  security_quality_of_service.Length =
	    sizeof(SECURITY_QUALITY_OF_SERVICE);
	  security_quality_of_service.ImpersonationLevel =
	    SecurityImpersonation;
	  security_quality_of_service.ContextTrackingMode =
	    SECURITY_STATIC_TRACKING;
	  security_quality_of_service.EffectiveOnly = FALSE;

	  SeCreateClientSecurity(ClientThread,
				 &security_quality_of_service,
				 FALSE,
				 &security_client_context);

	  SeImpersonateClient(&security_client_context, NULL);
	  KdPrint(("ImDisk: Impersonation status: %#x.\n", status));

	  SeDeleteClientSecurity(&security_client_context);
	}
      else
	KdPrint(("ImDisk: No impersonation information.\n"));

      if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY) &
	  (IMDISK_PROXY_TYPE(CreateData->Flags) != IMDISK_PROXY_TYPE_DIRECT))
	RtlInitUnicodeString(&real_file_name, IMDPROXY_SVC_PIPE_NATIVE_NAME);
      else
	real_file_name = file_name;

      InitializeObjectAttributes(&object_attributes,
				 &real_file_name,
				 OBJ_CASE_INSENSITIVE, NULL, NULL);

      KdPrint(("ImDisk: Passing WriteMode=%#x and WriteShare=%#x\n",
	       (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY) |
	       !((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM) |
		 IMDISK_READONLY(CreateData->Flags)),
	       IMDISK_READONLY(CreateData->Flags) |
	       (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM)));

      status =
	ZwCreateFile(&file_handle,
		     GENERIC_READ |
		     ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY) |
		      !((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM) |
			IMDISK_READONLY(CreateData->Flags)) ?
		      GENERIC_WRITE : 0),
		     &object_attributes,
		     &io_status,
		     NULL,
		     FILE_ATTRIBUTE_NORMAL,
		     FILE_SHARE_READ |
		     FILE_SHARE_DELETE |
		     (IMDISK_READONLY(CreateData->Flags) |
		      (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM) ?
		      FILE_SHARE_WRITE : 0),
		     FILE_OPEN,
		     IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY ?
		     FILE_NON_DIRECTORY_FILE |
		     FILE_SEQUENTIAL_ONLY |
		     FILE_NO_INTERMEDIATE_BUFFERING |
		     FILE_SYNCHRONOUS_IO_NONALERT :
		     FILE_NON_DIRECTORY_FILE |
		     FILE_RANDOM_ACCESS |
		     FILE_NO_INTERMEDIATE_BUFFERING |
		     FILE_SYNCHRONOUS_IO_NONALERT,
		     NULL,
		     0);

      // If not found we will create the file if a new non-zero size is
      // specified, read-only virtual disk is not specified and we are
      // creating a type 'file' virtual disk.
      if ((status == STATUS_OBJECT_NAME_NOT_FOUND) |
	  (status == STATUS_NO_SUCH_FILE))
	{
	  if ((file_size.QuadPart == 0) |
	      IMDISK_READONLY(CreateData->Flags) |
	      (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM))
	    {
	      ExFreePool(file_name.Buffer);

	      KdPrint(("ImDisk: File '%.*ws' not found. (%#x)\n",
		       (int) real_file_name.Length / sizeof(WCHAR),
		       real_file_name.Buffer,
		       status));

	      return status;
	    }

	  status =
	    ZwCreateFile(&file_handle,
			 GENERIC_READ |
			 GENERIC_WRITE,
			 &object_attributes,
			 &io_status,
			 &file_size,
			 FILE_ATTRIBUTE_NORMAL,
			 FILE_SHARE_READ |
			 FILE_SHARE_DELETE,
			 FILE_OPEN_IF,
			 FILE_NON_DIRECTORY_FILE |
			 FILE_RANDOM_ACCESS |
			 FILE_NO_INTERMEDIATE_BUFFERING |
			 FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
      
	  if (!NT_SUCCESS(status))
	    {
	      ExFreePool(file_name.Buffer);

	      KdPrint(("ImDisk: Cannot create '%.*ws'. (%#x)\n",
		       (int)(CreateData->FileNameLength /
			     sizeof(*CreateData->FileName)),
		       CreateData->FileName,
		       status));
	      
	      return status;
	    }
	}
      else if (!NT_SUCCESS(status))
	{
	  ExFreePool(file_name.Buffer);
	  
	  KdPrint(("ImDisk: Cannot open file '%.*ws'. Status: %#x\n",
		   (int)(real_file_name.Length / sizeof(WCHAR)),
		   real_file_name.Buffer,
		   status));

	  return status;
	}

      KdPrint(("ImDisk: File '%.*ws' opened successfully.\n",
	       (int)(real_file_name.Length / sizeof(WCHAR)),
	       real_file_name.Buffer));

      // Adjust the file length to the requested virtual disk size.
      if ((file_size.QuadPart != 0) &
	  (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &
	  !IMDISK_READONLY(CreateData->Flags))
	{
	  status = ZwSetInformationFile(file_handle,
					&io_status,
					&file_size,
					sizeof
					(FILE_END_OF_FILE_INFORMATION),
					FileEndOfFileInformation);

	  if (!NT_SUCCESS(status))
	    {
	      ZwClose(file_handle);
	      ExFreePool(file_name.Buffer);
	      KdPrint(("ImDisk: Error setting eof (%#x).\n", status));
	      return status;
	    }
	}

      /*
      status = ZwQueryInformationFile(file_handle,
				      IoStatus,
				      &file_basic,
				      sizeof(FILE_BASIC_INFORMATION),
				      FileBasicInformation);

      if (!NT_SUCCESS(status))
	{
	  ZwClose(file_handle);
	  return status;
	}

  //
  // The NT cache manager can deadlock if a filesystem that is using the cache
  // manager is used in a virtual disk that stores its file on a filesystem
  // that is also using the cache manager, this is why we open the file with
  // FILE_NO_INTERMEDIATE_BUFFERING above, however if the file is compressed
  // or encrypted NT will not honor this request and cache it anyway since it
  // need to store the decompressed/unencrypted data somewhere, therefor we put
  // an extra check here and don't alow disk images to be compressed/encrypted.
  //
      if (file_basic.FileAttributes &
	  (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ENCRYPTED))
	{
	  ZwClose(file_handle);
	  return status_not_supported;
	}
      */

      // Get the file size of the disk file.
      if (IMDISK_TYPE(CreateData->Flags) != IMDISK_TYPE_PROXY)
	{
	  FILE_STANDARD_INFORMATION file_standard;

	  status = ZwQueryInformationFile(file_handle,
					  &io_status,
					  &file_standard,
					  sizeof(FILE_STANDARD_INFORMATION),
					  FileStandardInformation);

	  if (!NT_SUCCESS(status))
	    {
	      ZwClose(file_handle);
	      ExFreePool(file_name.Buffer);

	      KdPrint
		(("ImDisk: Error getting FILE_STANDARD_INFORMATION (%#x).\n",
		  status));

	      return status;
	    }

	  // Allocate virtual memory for 'vm' type.
	  if (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM)
	    {
	      LARGE_INTEGER byte_offset;
	      ULONG max_size;

	      // Check that file size < 2 GB.
	      if (file_size.QuadPart == 0)
		if (file_standard.EndOfFile.QuadPart & 0xFFFFFFFF80000000)
		  {
		    ZwClose(file_handle);
		    ExFreePool(file_name.Buffer);

		    KdPrint(("ImDisk: VM disk >= 2GB not supported.\n"));

		    return STATUS_INSUFFICIENT_RESOURCES;
		  }
		else
		  file_size = file_standard.EndOfFile;

	      max_size = file_size.LowPart;
	      image_buffer = NULL;
	      status =
		ZwAllocateVirtualMemory(NtCurrentProcess(),
					&image_buffer,
					0,
					&max_size,
					MEM_COMMIT,
					PAGE_READWRITE);
	      if (!NT_SUCCESS(status))
		{
		  ZwClose(file_handle);
		  ExFreePool(file_name.Buffer);

		  KdPrint(("ImDisk: Error allocating vm for image. (%#x)\n",
			   status));
		  
		  return STATUS_NO_MEMORY;
		}

	      alignment_requirement = FILE_BYTE_ALIGNMENT;

	      KdPrint(("ImDisk: Reading image file into vm disk buffer.\n"));

	      // Failure to read pre-load image is now considered a fatal error
	      byte_offset.QuadPart = 0;
	      status = ImDiskSafeReadFile(file_handle,
					  &io_status,
					  image_buffer,
					  file_size.LowPart,
					  &byte_offset);

	      ZwClose(file_handle);

	      if (!NT_SUCCESS(status))
		{
		  ULONG free_size = 0;

		  ZwFreeVirtualMemory(NtCurrentProcess(),
				      &image_buffer,
				      &free_size, MEM_RELEASE);

		  ExFreePool(file_name.Buffer);

		  KdPrint(("ImDisk: Failed to read image file (%#x).\n",
			   status));

		  return status;
		}

	      KdPrint(("ImDisk: Image loaded successfully.\n"));
	    }
	  else
	    {
	      FILE_ALIGNMENT_INFORMATION file_alignment;

	      status = ZwQueryInformationFile(file_handle,
					      &io_status,
					      &file_alignment,
					      sizeof
					      (FILE_ALIGNMENT_INFORMATION),
					      FileAlignmentInformation);
	  
	      if (!NT_SUCCESS(status))
		{
		  ZwClose(file_handle);
		  ExFreePool(file_name.Buffer);

		  KdPrint(("ImDisk: Error querying file alignment (%#x).\n",
			   status));

		  return status;
		}

	      file_size = file_standard.EndOfFile;

	      alignment_requirement = file_alignment.AlignmentRequirement;
	    }
	}
      else
	// If proxy is used the file size is queried to the proxy instead.
	{
	  IMDPROXY_INFO_RESP proxy_info;

	  if (IMDISK_PROXY_TYPE(CreateData->Flags) != IMDISK_PROXY_TYPE_DIRECT)
	    status = ImDiskConnectProxy(file_handle,
					&io_status,
					CreateData->Flags,
					CreateData->FileName,
					CreateData->FileNameLength,
					&proxy_info,
					sizeof(IMDPROXY_INFO_RESP));
	  else
	    status = ImDiskQueryInformationProxy(file_handle,
						 &io_status,
						 &proxy_info,
						 sizeof(IMDPROXY_INFO_RESP));

	  if (!NT_SUCCESS(status))
	    {
	      ZwClose(file_handle);
	      ExFreePool(file_name.Buffer);

	      KdPrint(("ImDisk: Error querying proxy (%#x).\n", status));

	      return status;
	    }

	  if (file_size.QuadPart == 0)
	    file_size.QuadPart = proxy_info.file_size;

	  if ((proxy_info.req_alignment - 1 > FILE_512_BYTE_ALIGNMENT) |
	      (file_size.QuadPart == 0))
	    {
	      ZwClose(file_handle);
	      ExFreePool(file_name.Buffer);

	      KdPrint(("ImDisk: Unsupported sizes. "
		       "Got %p-%p size and %p-%p alignment.\n",
		       proxy_info.file_size,
		       proxy_info.req_alignment));

	      return STATUS_INVALID_PARAMETER;
	    }

	  alignment_requirement = (ULONG) proxy_info.req_alignment - 1;

	  if (proxy_info.flags & IMDPROXY_FLAG_RO)
	    CreateData->Flags |= IMDISK_OPTION_RO;

	  KdPrint(("ImDisk: Got from proxy: Siz=%p%p Flg=%#x Alg=%#x.\n",
		   file_size.HighPart, file_size.LowPart,
		   (ULONG) proxy_info.flags,
		   (ULONG) proxy_info.req_alignment));
	}

      // The file_size variable may have been adjusted by routines above.
      // Adjust the geometry to reflect this.
      CreateData->DiskGeometry.Cylinders = file_size;
      if (CreateData->DiskGeometry.TracksPerCylinder != 0)
	CreateData->DiskGeometry.Cylinders.QuadPart /=
	  CreateData->DiskGeometry.TracksPerCylinder;
      if (CreateData->DiskGeometry.SectorsPerTrack != 0)
	CreateData->DiskGeometry.Cylinders.QuadPart /=
	  CreateData->DiskGeometry.SectorsPerTrack;
      if (CreateData->DiskGeometry.BytesPerSector != 0)
	CreateData->DiskGeometry.Cylinders.QuadPart /=
	  CreateData->DiskGeometry.BytesPerSector;

      if (CreateData->DiskGeometry.Cylinders.QuadPart == 0)
	{
	  ULONG free_size = 0;
      
	  KdPrint(("ImDisk: Fatal error: Number of cylinders equals zero.\n"));

	  if (file_handle != NULL)
	    ZwClose(file_handle);
	  if (file_name.Buffer != NULL)
	    ExFreePool(file_name.Buffer);
	  if (image_buffer != NULL)
	    ZwFreeVirtualMemory(NtCurrentProcess(),
				&image_buffer,
				&free_size, MEM_RELEASE);

	  return STATUS_INVALID_PARAMETER;
	}
    }
  // Blank vm-disk, just allocate...
  else
    {
      ULONG max_size;
      max_size = file_size.LowPart;

      image_buffer = NULL;
      status =
	ZwAllocateVirtualMemory(NtCurrentProcess(),
				&image_buffer,
				0,
				&max_size,
				MEM_COMMIT,
				PAGE_READWRITE);
      if (!NT_SUCCESS(status))
	{
	  KdPrint
	    (("ImDisk: Error allocating virtual memory for vm disk (%#x).\n",
	      status));

	  return STATUS_NO_MEMORY;
	}

      alignment_requirement = FILE_BYTE_ALIGNMENT;
    }

  KdPrint(("ImDisk: Done with file/memory checks.\n"));

  // If no device-type specified and size matches common floppy sizes,
  // auto-select FILE_DEVICE_DISK with FILE_FLOPPY_DISKETTE and
  // FILE_REMOVABLE_MEDIA.
  // If still no device-type specified, specify FILE_DEVICE_DISK with no
  // particular characteristics. This will emulate a hard disk partition.
  switch (file_size.QuadPart)
    {
    case MEDIA_SIZE_2P88MB:
    case MEDIA_SIZE_1P44MB:
    case MEDIA_SIZE_720KB:
    case MEDIA_SIZE_1P20MB:
    case MEDIA_SIZE_640KB:
    case MEDIA_SIZE_320KB:
      CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
      break;

    default:
      CreateData->Flags |= IMDISK_DEVICE_TYPE_HD;
    }
	  
  KdPrint(("ImDisk: Done with device type selection for floppy sizes.\n"));

  // If some parts of the DISK_GEOMETRY structure are zero, auto-fill with
  // typical values for this type of disk.
  if (IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_CD)
    {
      if (CreateData->DiskGeometry.BytesPerSector == 0)
	{
	  if (CreateData->DiskGeometry.Cylinders.QuadPart /
	      SECTOR_SIZE_CD_ROM * SECTOR_SIZE_CD_ROM ==
	      CreateData->DiskGeometry.Cylinders.QuadPart)
	    CreateData->DiskGeometry.BytesPerSector = SECTOR_SIZE_CD_ROM;
	  else
	    CreateData->DiskGeometry.BytesPerSector = 1;

	  CreateData->DiskGeometry.Cylinders.QuadPart /=
	    CreateData->DiskGeometry.BytesPerSector;
	}

      if (CreateData->DiskGeometry.SectorsPerTrack == 0)
	{
	  if (CreateData->DiskGeometry.Cylinders.QuadPart /
	      SECTORS_PER_TRACK_CD_ROM * SECTORS_PER_TRACK_CD_ROM ==
	      CreateData->DiskGeometry.Cylinders.QuadPart)
	    CreateData->DiskGeometry.SectorsPerTrack =
	      SECTORS_PER_TRACK_CD_ROM;
	  else
	    CreateData->DiskGeometry.SectorsPerTrack = 1;

	  CreateData->DiskGeometry.Cylinders.QuadPart /=
	    CreateData->DiskGeometry.SectorsPerTrack;
	}

      if (CreateData->DiskGeometry.TracksPerCylinder == 0)
	{
	  if (CreateData->DiskGeometry.Cylinders.QuadPart /
	      TRACKS_PER_CYLINDER_CD_ROM * TRACKS_PER_CYLINDER_CD_ROM ==
	      CreateData->DiskGeometry.Cylinders.QuadPart)
	    CreateData->DiskGeometry.TracksPerCylinder =
	      TRACKS_PER_CYLINDER_CD_ROM;
	  else
	    CreateData->DiskGeometry.TracksPerCylinder = 1;

	  CreateData->DiskGeometry.Cylinders.QuadPart /=
	    CreateData->DiskGeometry.TracksPerCylinder;
	}

      if (CreateData->DiskGeometry.MediaType == Unknown)
	CreateData->DiskGeometry.MediaType = RemovableMedia;
    }
  // Common floppy sizes geometries.
  else
    {
      if ((IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_FD) &
	  (CreateData->DiskGeometry.BytesPerSector == 0) &
	  (CreateData->DiskGeometry.SectorsPerTrack == 0) &
	  (CreateData->DiskGeometry.TracksPerCylinder == 0) &
	  (CreateData->DiskGeometry.MediaType == Unknown))
	switch (file_size.QuadPart)
	  {
	  case MEDIA_SIZE_2P88MB:
	    CreateData->DiskGeometry = media_table[MEDIA_TYPE_288];
	    break;

	  case MEDIA_SIZE_1P44MB:
	    CreateData->DiskGeometry = media_table[MEDIA_TYPE_144];
	    break;

	  case MEDIA_SIZE_720KB:
	    CreateData->DiskGeometry = media_table[MEDIA_TYPE_720];
	    break;

	  case MEDIA_SIZE_1P20MB:
	    CreateData->DiskGeometry = media_table[MEDIA_TYPE_120];
	    break;

	  case MEDIA_SIZE_640KB:
	    CreateData->DiskGeometry = media_table[MEDIA_TYPE_640];
	    break;

	  case MEDIA_SIZE_320KB:
	    CreateData->DiskGeometry = media_table[MEDIA_TYPE_320];
	    break;
	  }

      if (CreateData->DiskGeometry.BytesPerSector == 0)
	{
	  if (CreateData->DiskGeometry.Cylinders.QuadPart /
	      SECTOR_SIZE_HDD * SECTOR_SIZE_HDD ==
	      CreateData->DiskGeometry.Cylinders.QuadPart)
	    CreateData->DiskGeometry.BytesPerSector = SECTOR_SIZE_HDD;
	  else
	    CreateData->DiskGeometry.BytesPerSector = 1;

	  CreateData->DiskGeometry.Cylinders.QuadPart /=
	    CreateData->DiskGeometry.BytesPerSector;
	}

      if (CreateData->DiskGeometry.SectorsPerTrack == 0)
	{
	  CreateData->DiskGeometry.SectorsPerTrack = 1;

	  if ((CreateData->DiskGeometry.Cylinders.QuadPart / 7 * 7 ==
	       CreateData->DiskGeometry.Cylinders.QuadPart) &
	      (CreateData->DiskGeometry.SectorsPerTrack * 7 < 64))
	    {
	      CreateData->DiskGeometry.SectorsPerTrack *= 7;
	      CreateData->DiskGeometry.Cylinders.QuadPart /= 7;
	    }

	  if ((CreateData->DiskGeometry.Cylinders.QuadPart / 3 * 3 ==
	       CreateData->DiskGeometry.Cylinders.QuadPart) &
	      (CreateData->DiskGeometry.SectorsPerTrack * 3 < 64))
	    {
	      CreateData->DiskGeometry.SectorsPerTrack *= 3;
	      CreateData->DiskGeometry.Cylinders.QuadPart /= 3;
	    }

	  if ((CreateData->DiskGeometry.Cylinders.QuadPart / 3 * 3 ==
	       CreateData->DiskGeometry.Cylinders.QuadPart) &
	      (CreateData->DiskGeometry.SectorsPerTrack * 3 < 64))
	    {
	      CreateData->DiskGeometry.SectorsPerTrack *= 3;
	      CreateData->DiskGeometry.Cylinders.QuadPart /= 3;
	    }

	  while (((CreateData->DiskGeometry.Cylinders.QuadPart & 1) == 0) &
		 (CreateData->DiskGeometry.SectorsPerTrack <= 16))
	    {
	      CreateData->DiskGeometry.SectorsPerTrack <<= 1;
	      CreateData->DiskGeometry.Cylinders.QuadPart >>= 1;
	    }
	}

      if (CreateData->DiskGeometry.TracksPerCylinder == 0)
	{
	  CreateData->DiskGeometry.TracksPerCylinder = 1;

	  if (CreateData->DiskGeometry.Cylinders.QuadPart / 17 * 17 ==
	      CreateData->DiskGeometry.Cylinders.QuadPart)
	    {
	      CreateData->DiskGeometry.TracksPerCylinder *= 17;
	      CreateData->DiskGeometry.Cylinders.QuadPart /= 17;
	    }

	  if (CreateData->DiskGeometry.Cylinders.QuadPart / 5 * 5 ==
	      CreateData->DiskGeometry.Cylinders.QuadPart)
	    {
	      CreateData->DiskGeometry.TracksPerCylinder *= 5;
	      CreateData->DiskGeometry.Cylinders.QuadPart /= 5;
	    }

	  if (CreateData->DiskGeometry.Cylinders.QuadPart / 3 * 3 ==
	      CreateData->DiskGeometry.Cylinders.QuadPart)
	    {
	      CreateData->DiskGeometry.TracksPerCylinder *= 3;
	      CreateData->DiskGeometry.Cylinders.QuadPart /= 3;
	    }

	  while (((CreateData->DiskGeometry.Cylinders.QuadPart & 1) == 0) &
		 (CreateData->DiskGeometry.TracksPerCylinder <= 64))
	    {
	      CreateData->DiskGeometry.TracksPerCylinder <<= 1;
	      CreateData->DiskGeometry.Cylinders.QuadPart >>= 1;
	    }
	}

      if (CreateData->DiskGeometry.MediaType == Unknown)
	CreateData->DiskGeometry.MediaType = FixedMedia;
    }

  KdPrint(("ImDisk: Done with disk geometry setup.\n"));

  // Ensure upper-case driveletter.
  CreateData->DriveLetter &= ~0x20;

  KdPrint
    (("ImDisk: After checks and translations we got this create data:\n"
      "DeviceNumber   = %#x\n"
      "DiskGeometry\n"
      "  .Cylinders   = %p%p\n"
      "  .MediaType   = %i\n"
      "  .T/C         = %u\n"
      "  .S/T         = %u\n"
      "  .B/S         = %u\n"
      "Flags          = %#x\n"
      "FileNameLength = %u\n"
      "FileName       = '%.*ws'\n",
      CreateData->DeviceNumber,
      CreateData->DiskGeometry.Cylinders.HighPart,
      CreateData->DiskGeometry.Cylinders.LowPart,
      CreateData->DiskGeometry.MediaType,
      CreateData->DiskGeometry.TracksPerCylinder,
      CreateData->DiskGeometry.SectorsPerTrack,
      CreateData->DiskGeometry.BytesPerSector,
      CreateData->Flags,
      CreateData->FileNameLength,
      (int)(CreateData->FileNameLength / sizeof(*CreateData->FileName)),
      CreateData->FileName));

  // Now build real DeviceType and DeviceCharacteristics parameters.
  if (IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_CD)
    {
      device_type = FILE_DEVICE_CD_ROM;
      device_characteristics = FILE_READ_ONLY_DEVICE | FILE_REMOVABLE_MEDIA;
    }
  else
    {
      device_type = FILE_DEVICE_DISK;

      if (IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_FD)
	device_characteristics = FILE_FLOPPY_DISKETTE | FILE_REMOVABLE_MEDIA;
      else
	device_characteristics = 0;
    }

  if (IMDISK_READONLY(CreateData->Flags))
    device_characteristics |= FILE_READ_ONLY_DEVICE;

  // Buffer for device name
  device_name_buffer = ExAllocatePool(PagedPool,
				      MAXIMUM_FILENAME_LENGTH *
				      sizeof(*device_name_buffer));

  if (device_name_buffer == NULL)
    {
      ULONG free_size = 0;
      if (file_handle != NULL)
	ZwClose(file_handle);
      if (file_name.Buffer != NULL)
	ExFreePool(file_name.Buffer);
      if (image_buffer != NULL)
	ZwFreeVirtualMemory(NtCurrentProcess(),
			    &image_buffer,
			    &free_size, MEM_RELEASE);

      return STATUS_INSUFFICIENT_RESOURCES;
    }

  _snwprintf(device_name_buffer, MAXIMUM_FILENAME_LENGTH,
	     IMDISK_DEVICE_BASE_NAME L"%u", CreateData->DeviceNumber);
  device_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

  KdPrint
    (("ImDisk: Creating device '%ws'. Device type %#x, characteristics %#x.\n",
      device_name_buffer, device_type, device_characteristics));

  RtlInitUnicodeString(&device_name, device_name_buffer);

  status = IoCreateDevice(DriverObject,
			  sizeof(DEVICE_EXTENSION),
			  &device_name,
			  device_type,
			  device_characteristics,
			  FALSE,
			  DeviceObject);

  if (!NT_SUCCESS(status))
    {
      ULONG free_size = 0;

      ExFreePool(device_name_buffer);
      if (file_handle != NULL)
	ZwClose(file_handle);
      if (file_name.Buffer != NULL)
	ExFreePool(file_name.Buffer);
      if (image_buffer != NULL)
	ZwFreeVirtualMemory(NtCurrentProcess(),
			    &image_buffer,
			    &free_size, MEM_RELEASE);

      KdPrint(("ImDisk: Cannot create device. (%#x)\n", status));

      return status;
    }

  (*DeviceObject)->Flags |= DO_DIRECT_IO;
  (*DeviceObject)->AlignmentRequirement = alignment_requirement;

  device_extension = (PDEVICE_EXTENSION) (*DeviceObject)->DeviceExtension;

  device_extension->media_in_device = FALSE;

  // Auto-set our own read-only flag if the characteristics of the device
  // object is set to read-only.
  if ((*DeviceObject)->Characteristics & FILE_READ_ONLY_DEVICE)
    device_extension->read_only = TRUE;

  InitializeListHead(&device_extension->list_head);

  KeInitializeSpinLock(&device_extension->list_lock);

  KeInitializeEvent(&device_extension->request_event,
		    SynchronizationEvent, FALSE);

  device_extension->terminate_thread = FALSE;
  device_extension->device_number = CreateData->DeviceNumber;

  DeviceList |= 1 << CreateData->DeviceNumber;

  device_extension->file_name = file_name;

  device_extension->disk_geometry = CreateData->DiskGeometry;

  // VM disk.
  if (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM)
    {
      device_extension->image_buffer = image_buffer;
      device_extension->vm_disk = TRUE;
    }
  else
    {
      device_extension->file_handle = file_handle;
      device_extension->vm_disk = FALSE;
    }

  // Use proxy service.
  if (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY)
    device_extension->use_proxy = TRUE;
  else
    device_extension->use_proxy = FALSE;

  if (((*DeviceObject)->DeviceType == FILE_DEVICE_CD_ROM) |
      IMDISK_READONLY(CreateData->Flags))
    device_extension->read_only = TRUE;
  else
    device_extension->read_only = FALSE;
  
  if (device_extension->read_only)
    (*DeviceObject)->Characteristics |= FILE_READ_ONLY_DEVICE;
  else
    (*DeviceObject)->Characteristics &= ~FILE_READ_ONLY_DEVICE;

  device_extension->media_in_device = TRUE;

  device_extension->media_change_count++;

  if (CreateData->DriveLetter != 0)
    {
      WCHAR sym_link_global_wchar[] = L"\\DosDevices\\Global\\ :";
      UNICODE_STRING sym_link;

      sym_link_global_wchar[19] = CreateData->DriveLetter;

      KdPrint(("ImDisk: Creating symlink '%ws' -> '%ws'.\n",
	       sym_link_global_wchar, device_name_buffer));

      RtlInitUnicodeString(&sym_link, sym_link_global_wchar);
      status = IoCreateUnprotectedSymbolicLink(&sym_link, &device_name);

      if (!NT_SUCCESS(status))
	{
	  KdPrint(("ImDisk: Cannot symlink '%ws' to '%ws'. (%#x)\n",
		   sym_link_global_wchar, device_name_buffer, status));
	}

      device_extension->drive_letter = CreateData->DriveLetter;
    }

  (*DeviceObject)->Flags &= ~DO_DEVICE_INITIALIZING;

  KdPrint(("ImDisk: Device '%ws' created.\n", device_name_buffer));

  ExFreePool(device_name_buffer);

  return STATUS_SUCCESS;
}

VOID
ImDiskUnload(IN PDRIVER_OBJECT DriverObject)
{
  PDEVICE_OBJECT device_object;

  PAGED_CODE();

  device_object = DriverObject->DeviceObject;

  KdPrint(("ImDisk: Entering ImDiskUnload for driver %#x. "
	   "Current device objects chain dump for this driver:\n",
	   DriverObject));

  while (device_object != NULL)
    {
      KdPrint(("%#x -> ", device_object));
      device_object = device_object->NextDevice;
    }

  KdPrint(("(null)\n"));

  device_object = DriverObject->DeviceObject;

  for (;;)
    {
      PDEVICE_OBJECT next_device;
      PDEVICE_EXTENSION device_extension;

      if (device_object == NULL)
	{
	  KdPrint
	    (("ImDisk: No more devices to delete. Leaving ImDiskUnload.\n"));
	  return;
	}
  
      next_device = device_object->NextDevice;
      device_extension = (PDEVICE_EXTENSION) device_object->DeviceExtension;

      KdPrint(("ImDisk: Now deleting device %i.\n",
	       device_extension->device_number));

      if (device_object == ImDiskCtlDevice)
	{
	  UNICODE_STRING sym_link;
	  LARGE_INTEGER time_out;
	  time_out.QuadPart = -1000000;

	  while (device_object->ReferenceCount != 0)
	    {
	      KdPrint(("ImDisk: Ctl device is busy. Waiting.\n"));

	      KeDelayExecutionThread(KernelMode, FALSE, &time_out);

	      time_out.LowPart <<= 2;
	    }

	  KdPrint(("ImDisk: Deleting ctl device.\n"));
	  RtlInitUnicodeString(&sym_link, IMDISK_CTL_SYMLINK_NAME);
	  IoDeleteSymbolicLink(&sym_link);
	  IoDeleteDevice(device_object);
	}
      else
	{
	  KdPrint(("ImDisk: Shutting down device %i.\n",
		   device_extension->device_number));
	  ImDiskRemoveVirtualDisk(device_object);
	}

      device_object = next_device;
    }
}

VOID
ImDiskRemoveVirtualDisk(IN PDEVICE_OBJECT DeviceObject)
{
  PDEVICE_EXTENSION device_extension;

  PAGED_CODE();

  ASSERT(DeviceObject != NULL);

  device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

  KdPrint(("ImDisk: Request to shutdown device %i.\n",
	   device_extension->device_number));

  device_extension->media_in_device = FALSE;

  device_extension->terminate_thread = TRUE;

  KeSetEvent(&device_extension->request_event, (KPRIORITY) 0, FALSE);
}

#pragma code_seg()

NTSTATUS
ImDiskCreateClose(IN PDEVICE_OBJECT DeviceObject,
		  IN PIRP Irp)
{
  PIO_STACK_LOCATION io_stack;
  PDEVICE_EXTENSION device_extension;

  ASSERT(DeviceObject != NULL);
  ASSERT(Irp != NULL);

  KdPrint(("ImDisk: Entering ImDiskCreateClose.\n"));

  io_stack = IoGetCurrentIrpStackLocation(Irp);
  device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

  if (io_stack->FileObject->FileName.Length != 0)
    {
      KdPrint(("ImDisk: Attempt to open '%.*ws' on device %i.\n",
	       (int)(io_stack->FileObject->FileName.Length / sizeof(WCHAR)),
	       io_stack->FileObject->FileName.Buffer,
	       device_extension->device_number));

      Irp->IoStatus.Status = STATUS_OBJECT_NAME_NOT_FOUND;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return Irp->IoStatus.Status;
    }

  if ((io_stack->MajorFunction == IRP_MJ_CREATE) &
      (device_extension->terminate_thread == TRUE))
    {
      KdPrint(("ImDisk: Attempt to open device %i when shut down.\n",
	       device_extension->device_number));

      Irp->IoStatus.Status = STATUS_DELETE_PENDING;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return Irp->IoStatus.Status;
    }

  KdPrint(("ImDisk: Successfully created/closed a handle for device %i.\n",
	   device_extension->device_number));

  Irp->IoStatus.Status = STATUS_SUCCESS;
  Irp->IoStatus.Information = FILE_OPENED;

  IoCompleteRequest(Irp, IO_NO_INCREMENT);

  return STATUS_SUCCESS;
}

NTSTATUS
ImDiskReadWrite(IN PDEVICE_OBJECT DeviceObject,
		IN PIRP Irp)
{
  PDEVICE_EXTENSION device_extension;
  PIO_STACK_LOCATION io_stack;

  ASSERT(DeviceObject != NULL);
  ASSERT(Irp != NULL);

  device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

  if (DeviceObject == ImDiskCtlDevice)
    {
      KdPrint(("ImDisk: Read/write attempt on ctl device.\n"));

      Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return Irp->IoStatus.Status;
    }

  if (!device_extension->media_in_device)
    {
      KdPrint(("ImDisk: Read/write attempt on device %i with no media.\n",
	       device_extension->device_number));

      Irp->IoStatus.Status = STATUS_SUCCESS;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return Irp->IoStatus.Status;
    }

  io_stack = IoGetCurrentIrpStackLocation(Irp);

  if ((io_stack->MajorFunction == IRP_MJ_WRITE) &&
      device_extension->read_only)
    {
      KdPrint(("ImDisk: Attempt to write to write-protected device %i.\n",
	       device_extension->device_number));

      Irp->IoStatus.Status = STATUS_MEDIA_WRITE_PROTECTED;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_MEDIA_WRITE_PROTECTED;
    }

  if ((io_stack->Parameters.Read.ByteOffset.QuadPart +
       io_stack->Parameters.Read.Length) >
      (device_extension->disk_geometry.Cylinders.QuadPart *
       device_extension->disk_geometry.TracksPerCylinder *
       device_extension->disk_geometry.SectorsPerTrack *
       device_extension->disk_geometry.BytesPerSector))
    {
      KdPrint(("ImDisk: Read/write beyond eof on device %i.\n",
	       device_extension->device_number));

      Irp->IoStatus.Status = STATUS_SUCCESS;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_SUCCESS;
    }

  if (io_stack->Parameters.Read.Length == 0)
    {
      KdPrint(("ImDisk: Read/write zero bytes on device %i.\n",
	       device_extension->device_number));

      Irp->IoStatus.Status = STATUS_SUCCESS;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_SUCCESS;
    }

  IoMarkIrpPending(Irp);

  ExInterlockedInsertTailList(&device_extension->list_head,
			      &Irp->Tail.Overlay.ListEntry,
			      &device_extension->list_lock);
  
  KeSetEvent(&device_extension->request_event, (KPRIORITY) 0, FALSE);

  return STATUS_PENDING;
}
  
NTSTATUS
ImDiskDeviceControl(IN PDEVICE_OBJECT DeviceObject,
		    IN PIRP Irp)
{
  PDEVICE_EXTENSION device_extension;
  PIO_STACK_LOCATION io_stack;
  NTSTATUS status;

  ASSERT(DeviceObject != NULL);
  ASSERT(Irp != NULL);

  device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

  io_stack = IoGetCurrentIrpStackLocation(Irp);

  KdPrint(("ImDisk: Device %i received IOCTL %#x IRP %#x.\n",
	   device_extension->device_number,
	   io_stack->Parameters.DeviceIoControl.IoControlCode,
	   Irp));

  // The control device can only receive version queries, enumeration queries
  // or device create requests.
  if (DeviceObject == ImDiskCtlDevice)
    switch (io_stack->Parameters.DeviceIoControl.IoControlCode)
      {
      case IOCTL_IMDISK_QUERY_VERSION:
      case IOCTL_IMDISK_CREATE_DEVICE:
      case IOCTL_IMDISK_QUERY_DRIVER:
	break;

      default:
	KdPrint(("ImDisk: Invalid IOCTL %#x for control device.\n",
		 io_stack->Parameters.DeviceIoControl.IoControlCode));
	
	Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;
      }
  else
    switch (io_stack->Parameters.DeviceIoControl.IoControlCode)
      {
	// Invalid IOCTL codes for this driver's disk devices.
      case IOCTL_IMDISK_CREATE_DEVICE:
      case IOCTL_IMDISK_QUERY_DRIVER:
	KdPrint(("ImDisk: Invalid IOCTL %#x for disk device.\n",
		 io_stack->Parameters.DeviceIoControl.IoControlCode));
	
	Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;

	// The only IOCTL codes available without mounted image file.
      case IOCTL_IMDISK_QUERY_VERSION:
      case IOCTL_DISK_EJECT_MEDIA:
      case IOCTL_STORAGE_EJECT_MEDIA:
	break;

      default:
	if (!device_extension->media_in_device)
	  {
	    KdPrint(("ImDisk: Invalid IOCTL %#x when no file mounted.\n",
		     io_stack->Parameters.DeviceIoControl.IoControlCode));

	    Irp->IoStatus.Status = STATUS_NO_MEDIA_IN_DEVICE;
	    Irp->IoStatus.Information = 0;
	    
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);

	    return Irp->IoStatus.Status;
	  }
      }

  switch (io_stack->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_IMDISK_CREATE_DEVICE:
      {
	PIMDISK_CREATE_DATA create_data;

	KdPrint(("ImDisk: IOCTL_IMDISK_CREATE_DEVICE for device %i.\n",
		 device_extension->device_number));

	if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
	  {
	    status = STATUS_ACCESS_DENIED;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
	    sizeof(IMDISK_CREATE_DATA) - sizeof(*create_data->FileName))
	  {
	    KdPrint(("ImDisk: Invalid input buffer size (1). "
		     "Got: %u Expected at least: %u.\n",
		     io_stack->Parameters.DeviceIoControl.InputBufferLength,
		     sizeof(IMDISK_CREATE_DATA) -
		     sizeof(*create_data->FileName)));

	    status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	create_data = (PIMDISK_CREATE_DATA) Irp->AssociatedIrp.SystemBuffer;

	if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
	    sizeof(IMDISK_CREATE_DATA) +
	    create_data->FileNameLength -
	    sizeof(*create_data->FileName))
	  {
	    KdPrint(("ImDisk: Invalid input buffer size (2). "
		     "Got: %u Expected at least: %u.\n",
		     io_stack->Parameters.DeviceIoControl.InputBufferLength,
		     sizeof(IMDISK_CREATE_DATA) +
		     create_data->FileNameLength -
		     sizeof(*create_data->FileName)));

	    status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	status = ImDiskAddVirtualDisk(DeviceObject->DriverObject,
				      (PIMDISK_CREATE_DATA)
				      Irp->AssociatedIrp.SystemBuffer,
				      Irp->Tail.Overlay.Thread);

	if (NT_SUCCESS(status) &&
	    (io_stack->Parameters.DeviceIoControl.OutputBufferLength >=
	     io_stack->Parameters.DeviceIoControl.InputBufferLength))
	  Irp->IoStatus.Information =
	    io_stack->Parameters.DeviceIoControl.OutputBufferLength;
	else
	  Irp->IoStatus.Information = 0;
	break;
      }

    case IOCTL_DISK_EJECT_MEDIA:
    case IOCTL_STORAGE_EJECT_MEDIA:
      KdPrint(("ImDisk: IOCTL_DISK/STORAGE_EJECT_MEDIA for device %i.\n",
	       device_extension->device_number));

      if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
	{
	  status = STATUS_ACCESS_DENIED;
	  Irp->IoStatus.Information = 0;
	  break;
	}

      KdPrint(("ImDisk: Shutting down device %i.\n",
	       device_extension->device_number));

      ImDiskRemoveVirtualDisk(DeviceObject);

      status = STATUS_SUCCESS;
      Irp->IoStatus.Information = 0;
      break;

    case IOCTL_IMDISK_QUERY_DRIVER:
      {
	KdPrint(("ImDisk: IOCTL_IMDISK_QUERY_DRIVER for device %i.\n",
		 device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(ULONG))
	  Irp->IoStatus.Information = 0;
	else
	  {
	    *(PULONG) Irp->AssociatedIrp.SystemBuffer = DeviceList;
	    Irp->IoStatus.Information = sizeof(ULONG);
	  }

	status = STATUS_SUCCESS;
	break;
      }

    case IOCTL_IMDISK_QUERY_DEVICE:
      {
	PIMDISK_CREATE_DATA create_data;

	KdPrint(("ImDisk: IOCTL_IMDISK_QUERY_DEVICE for device %i.\n",
		 device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(IMDISK_CREATE_DATA) +
	    device_extension->file_name.Length +
	    sizeof(*create_data->FileName))
	  {
	    status = STATUS_BUFFER_TOO_SMALL;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	create_data = (PIMDISK_CREATE_DATA) Irp->AssociatedIrp.SystemBuffer;

	create_data->DeviceNumber = device_extension->device_number;
	create_data->DiskGeometry = device_extension->disk_geometry;

	create_data->Flags = 0;
	if (device_extension->read_only)
	  create_data->Flags |= IMDISK_OPTION_RO;

	if (DeviceObject->DeviceType == FILE_DEVICE_CD_ROM)
	  create_data->Flags |= IMDISK_DEVICE_TYPE_CD | IMDISK_OPTION_RO;
	else if (DeviceObject->Characteristics & FILE_FLOPPY_DISKETTE)
	  create_data->Flags |= IMDISK_DEVICE_TYPE_FD;
	else
	  create_data->Flags |= IMDISK_DEVICE_TYPE_HD;

	if (device_extension->vm_disk)
	  create_data->Flags |= IMDISK_TYPE_VM;
	else if (device_extension->use_proxy)
	  create_data->Flags |= IMDISK_TYPE_PROXY;
	else
	  create_data->Flags |= IMDISK_TYPE_FILE;

	create_data->DriveLetter = device_extension->drive_letter;

	create_data->FileNameLength = device_extension->file_name.Length;

	if (device_extension->file_name.Length > 0)
	  RtlCopyMemory(create_data->FileName,
			device_extension->file_name.Buffer,
			device_extension->file_name.Length);

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(IMDISK_CREATE_DATA) +
	  create_data->FileNameLength -
	  sizeof(*create_data->FileName);

	break;
      }

    case IOCTL_DISK_CHECK_VERIFY:
    case IOCTL_CDROM_CHECK_VERIFY:
    case IOCTL_STORAGE_CHECK_VERIFY:
    case IOCTL_STORAGE_CHECK_VERIFY2:
      {
	KdPrint(("ImDisk: IOCTL_DISK/CDROM/STORAGE_CHECK_VERIFY/2 for "
		 "device %i.\n", device_extension->device_number));

	if (device_extension->vm_disk)
	  {
	    KdPrint(("ImDisk: Faked verify ok on vm device %i.\n",
		     device_extension->device_number));

	    if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
		sizeof(ULONG))
	      Irp->IoStatus.Information = 0;
	    else
	      {
		*(PULONG) Irp->AssociatedIrp.SystemBuffer =
		  device_extension->media_change_count;
		
		Irp->IoStatus.Information = sizeof(ULONG);
	      }
	    
	    status = STATUS_SUCCESS;
	  }
	else
	  status = STATUS_PENDING;

	break;
      }

    case IOCTL_IMDISK_QUERY_VERSION:
      {
	KdPrint(("ImDisk: IOCTL_IMDISK_QUERY_VERSION for device %i.\n",
		 device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(ULONG))
	  status = STATUS_INVALID_PARAMETER;
	else
	  {
	    *(PULONG) Irp->AssociatedIrp.SystemBuffer = IMDISK_VERSION;
	    Irp->IoStatus.Information = sizeof(ULONG);
	    status = STATUS_SUCCESS;
	  }

	break;
      }

    case IOCTL_DISK_FORMAT_TRACKS:
    case IOCTL_DISK_FORMAT_TRACKS_EX:
      //	Only several checks are done here
      //	Actual operation is done by the device thread
      {
	PFORMAT_PARAMETERS param;
	PDISK_GEOMETRY geometry;
	LONGLONG file_size;

	KdPrint(("ImDisk: IOCTL_DISK_FORMAT_TRACKS for device %i.\n",
		 device_extension->device_number));

	/*
	if (~DeviceObject->Characteristics & FILE_FLOPPY_DISKETTE)
	  {
	    Irp->IoStatus.Information = 0;
	    status = STATUS_INVALID_DEVICE_REQUEST;
	    break;
	  }
	*/

	//	Media is writable?

	if (device_extension->read_only)
	  {
	    KdPrint(("ImDisk: Attempt to format write-protected image.\n"));

	    Irp->IoStatus.Information = 0;
	    status = STATUS_MEDIA_WRITE_PROTECTED;
	    break;
	  }

	//	Check input parameter size

	if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
	    sizeof(FORMAT_PARAMETERS))
	  {
	    Irp->IoStatus.Information = 0;
	    status = STATUS_INVALID_PARAMETER;
	    break;
	  }

	//	Input parameter sanity check

	param = (PFORMAT_PARAMETERS) Irp->AssociatedIrp.SystemBuffer;
	geometry = &device_extension->disk_geometry;

	if ((param->StartHeadNumber > geometry->TracksPerCylinder - 1) ||
	    (param->EndHeadNumber   > geometry->TracksPerCylinder - 1) ||
	    ((LONGLONG)param->StartCylinderNumber >
	     geometry->Cylinders.QuadPart) ||
	    ((LONGLONG)param->EndCylinderNumber >
	     geometry->Cylinders.QuadPart) ||
	    (param->EndCylinderNumber	< param->StartCylinderNumber))
	  {
	    Irp->IoStatus.Information = 0;
	    status = STATUS_INVALID_PARAMETER;
	    break;
	  }

	file_size =
	  geometry->Cylinders.QuadPart * geometry->TracksPerCylinder *
	  geometry->SectorsPerTrack * geometry->BytesPerSector;

	if ((param->StartCylinderNumber * geometry->TracksPerCylinder *
	     geometry->BytesPerSector * geometry->SectorsPerTrack +
	     param->StartHeadNumber * geometry->BytesPerSector *
	     geometry->SectorsPerTrack >= file_size) |
	    (param->EndCylinderNumber * geometry->TracksPerCylinder *
	     geometry->BytesPerSector * geometry->SectorsPerTrack +
	     param->EndHeadNumber * geometry->BytesPerSector *
	     geometry->SectorsPerTrack >= file_size))
	  {
	    Irp->IoStatus.Information = 0;
	    status = STATUS_INVALID_PARAMETER;
	    break;
	  }

	//	If this is an EX request then make a couple of extra checks

	if (io_stack->Parameters.DeviceIoControl.IoControlCode ==
	    IOCTL_DISK_FORMAT_TRACKS_EX)
	  {
	    PFORMAT_EX_PARAMETERS exparam;
	    ULONG paramsize;

	    KdPrint(("ImDisk: IOCTL_DISK_FORMAT_TRACKS_EX for device %i.\n",
		     device_extension->device_number));

	    if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
		sizeof(FORMAT_EX_PARAMETERS))
	      {
		Irp->IoStatus.Information = 0;
		status = STATUS_INVALID_PARAMETER;
		break;
	      }

	    exparam = (PFORMAT_EX_PARAMETERS)Irp->AssociatedIrp.SystemBuffer;

	    paramsize = sizeof(FORMAT_EX_PARAMETERS)
	      + exparam->SectorsPerTrack * sizeof(USHORT)
	      - sizeof(USHORT);

	    if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
		paramsize ||
		exparam->FormatGapLength > geometry->SectorsPerTrack ||
		exparam->SectorsPerTrack != geometry->SectorsPerTrack)
	      {
		Irp->IoStatus.Information = 0;
		status = STATUS_INVALID_PARAMETER;
		break;
	      }
	  }

	status = STATUS_PENDING;
	break;
      }

    case IOCTL_DISK_GET_MEDIA_TYPES:
    case IOCTL_STORAGE_GET_MEDIA_TYPES:
    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    case IOCTL_CDROM_GET_DRIVE_GEOMETRY:
      {
	KdPrint(("ImDisk: IOCTL_DISK/STORAGE_GET_MEDIA_TYPES/DRIVE_GEOMETRY "
		 "for device %i.\n", device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(device_extension->disk_geometry))
	  {
	    status = STATUS_BUFFER_TOO_SMALL;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	*(PDISK_GEOMETRY) Irp->AssociatedIrp.SystemBuffer =
	  device_extension->disk_geometry;

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(device_extension->disk_geometry);
	break;
      }

    case IOCTL_DISK_GET_LENGTH_INFO:
      {
	KdPrint(("ImDisk: IOCTL_DISK_GET_LENGTH_INFO for device %i.\n",
		 device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(GET_LENGTH_INFORMATION))
	  {
	    status = STATUS_BUFFER_TOO_SMALL;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	((PGET_LENGTH_INFORMATION) Irp->AssociatedIrp.SystemBuffer)->
	  Length.QuadPart =
	  device_extension->disk_geometry.Cylinders.QuadPart *
	  device_extension->disk_geometry.TracksPerCylinder *
	  device_extension->disk_geometry.SectorsPerTrack *
	  device_extension->disk_geometry.BytesPerSector;

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(GET_LENGTH_INFORMATION);

	break;
      }

    case IOCTL_DISK_GET_PARTITION_INFO:
      {
	PPARTITION_INFORMATION partition_information;
	LONGLONG length;

	KdPrint(("ImDisk: IOCTL_DISK_GET_PARTITION_INFO for device %i.\n",
		 device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(PARTITION_INFORMATION))
	  {
	    status = STATUS_BUFFER_TOO_SMALL;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	partition_information =
	  (PPARTITION_INFORMATION) Irp->AssociatedIrp.SystemBuffer;

	length =
	  device_extension->disk_geometry.Cylinders.QuadPart *
	  device_extension->disk_geometry.TracksPerCylinder *
	  device_extension->disk_geometry.SectorsPerTrack *
	  device_extension->disk_geometry.BytesPerSector;

	partition_information->StartingOffset.QuadPart = 0;
	partition_information->PartitionLength.QuadPart = length;
	partition_information->HiddenSectors =
	  device_extension->disk_geometry.SectorsPerTrack;
	partition_information->PartitionNumber = 0;
	partition_information->PartitionType = PARTITION_HUGE;
	partition_information->BootIndicator = FALSE;
	partition_information->RecognizedPartition = FALSE;
	partition_information->RewritePartition = FALSE;

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(PARTITION_INFORMATION);

	break;
      }

    case IOCTL_DISK_GET_PARTITION_INFO_EX:
      {
	PPARTITION_INFORMATION_EX partition_information_ex;
	LONGLONG length;

	KdPrint(("ImDisk: IOCTL_DISK_GET_PARTITION_INFO_EX for device %i.\n",
		 device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(PARTITION_INFORMATION_EX))
	  {
	    status = STATUS_BUFFER_TOO_SMALL;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	partition_information_ex =
	  (PPARTITION_INFORMATION_EX) Irp->AssociatedIrp.SystemBuffer;

	length =
	  device_extension->disk_geometry.Cylinders.QuadPart *
	  device_extension->disk_geometry.TracksPerCylinder *
	  device_extension->disk_geometry.SectorsPerTrack *
	  device_extension->disk_geometry.BytesPerSector;

	partition_information_ex->PartitionStyle = PARTITION_STYLE_MBR;
	partition_information_ex->StartingOffset.QuadPart = 0;
	partition_information_ex->PartitionLength.QuadPart = length;
	partition_information_ex->PartitionNumber = 0;
	partition_information_ex->RewritePartition = FALSE;
	partition_information_ex->Mbr.PartitionType = PARTITION_HUGE;
	partition_information_ex->Mbr.BootIndicator = FALSE;
	partition_information_ex->Mbr.RecognizedPartition = FALSE;
	partition_information_ex->Mbr.HiddenSectors =
	  device_extension->disk_geometry.SectorsPerTrack;

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(PARTITION_INFORMATION_EX);

	break;
      }

    case IOCTL_DISK_IS_WRITABLE:
      {
	KdPrint(("ImDisk: IOCTL_DISK_IS_WRITABLE for device %i.\n",
		 device_extension->device_number));

	if (!device_extension->read_only)
	  status = STATUS_SUCCESS;
	else
	  status = STATUS_MEDIA_WRITE_PROTECTED;

	Irp->IoStatus.Information = 0;

	break;
      }

    case IOCTL_DISK_MEDIA_REMOVAL:
    case IOCTL_STORAGE_MEDIA_REMOVAL:
      {
	KdPrint(("ImDisk: IOCTL_DISK/STORAGE_MEDIA_REMOVAL for device %i.\n",
		 device_extension->device_number));

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	break;
      }

    case IOCTL_CDROM_READ_TOC:
      {
	PCDROM_TOC cdrom_toc;

	KdPrint(("ImDisk: IOCTL_CDROM_READ_TOC for device %i.\n",
		 device_extension->device_number));

	if (DeviceObject->DeviceType != FILE_DEVICE_CD_ROM)
	  {
	    Irp->IoStatus.Information = 0;
	    status = STATUS_INVALID_DEVICE_REQUEST;
	    break;
	  }

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(CDROM_TOC))
	  {
	    status = STATUS_BUFFER_TOO_SMALL;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	cdrom_toc = (PCDROM_TOC) Irp->AssociatedIrp.SystemBuffer;

	RtlZeroMemory(cdrom_toc, sizeof(CDROM_TOC));

	cdrom_toc->FirstTrack = 1;
	cdrom_toc->LastTrack = 1;
	cdrom_toc->TrackData[0].Control = TOC_DATA_TRACK;

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(CDROM_TOC);

	break;
      }

    case IOCTL_DISK_SET_PARTITION_INFO:
      {
	KdPrint(("ImDisk: IOCTL_DISK_SET_PARTITION_INFO for device %i.\n",
		 device_extension->device_number));

	if (device_extension->read_only)
	  {
	    KdPrint(("ImDisk: Attempt to partition read-only image.\n"));

	    status = STATUS_MEDIA_WRITE_PROTECTED;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
	    sizeof(SET_PARTITION_INFORMATION))
	  {
	    status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	break;
      }

    case IOCTL_DISK_SET_PARTITION_INFO_EX:
      {
	PSET_PARTITION_INFORMATION_EX partition_information_ex;

	KdPrint(("ImDisk: IOCTL_DISK_SET_PARTITION_INFO_EX for device %i.\n",
		 device_extension->device_number));

	if (device_extension->read_only)
	  {
	    KdPrint(("ImDisk: Attempt to partition read-only image.\n"));

	    status = STATUS_MEDIA_WRITE_PROTECTED;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
	    sizeof(SET_PARTITION_INFORMATION_EX))
	  {
	    status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	partition_information_ex = (PSET_PARTITION_INFORMATION_EX)
	  Irp->AssociatedIrp.SystemBuffer;

	if (partition_information_ex->PartitionStyle != PARTITION_STYLE_MBR)
	  {
	    status = STATUS_UNSUCCESSFUL;
	    Irp->IoStatus.Information = 0;
	  }
	else
	  {
	    status = STATUS_SUCCESS;
	    Irp->IoStatus.Information = 0;
	  }

	break;
      }

    case IOCTL_DISK_VERIFY:
      {
	PVERIFY_INFORMATION verify_information;

	KdPrint(("ImDisk: IOCTL_DISK_VERIFY for device %i.\n",
		 device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
	    sizeof(VERIFY_INFORMATION))
	  {
	    status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	verify_information = (PVERIFY_INFORMATION)
	  Irp->AssociatedIrp.SystemBuffer;

	if (device_extension->read_only)
	  {
	    KdPrint(("ImDisk: Attempt to verify read-only media.\n"));

	    status = STATUS_MEDIA_WRITE_PROTECTED;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	if (verify_information->StartingOffset.QuadPart +
	    verify_information->Length >
	    device_extension->disk_geometry.Cylinders.QuadPart *
	    device_extension->disk_geometry.TracksPerCylinder *
	    device_extension->disk_geometry.SectorsPerTrack *
	    device_extension->disk_geometry.BytesPerSector)
	  {
	    KdPrint(("ImDisk: Attempt to verify beyond image size.\n"));

	    status = STATUS_NONEXISTENT_SECTOR;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	break;
      }

    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
      {
	PSTORAGE_DEVICE_NUMBER device_number;

	KdPrint(("ImDisk: IOCTL_STORAGE_GET_DEVICE_NUMBER for device %i.\n",
		 device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(STORAGE_DEVICE_NUMBER))
	  {
	    status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	device_number = (PSTORAGE_DEVICE_NUMBER)
	  Irp->AssociatedIrp.SystemBuffer;

	device_number->DeviceType = DeviceObject->DeviceType;
	device_number->DeviceNumber = (ULONG) DeviceObject;
	device_number->PartitionNumber = (ULONG) -1;

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(STORAGE_DEVICE_NUMBER);

	break;
      }

    case IOCTL_STORAGE_GET_HOTPLUG_INFO:
      {
	PSTORAGE_HOTPLUG_INFO hotplug_info;

	KdPrint(("ImDisk: IOCTL_STORAGE_GET_HOTPLUG_INFO for device %i.\n",
		 device_extension->device_number));

	if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
	    sizeof(STORAGE_HOTPLUG_INFO))
	  {
	    status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    break;
	  }

	hotplug_info = (PSTORAGE_HOTPLUG_INFO)
	  Irp->AssociatedIrp.SystemBuffer;

	hotplug_info->Size = sizeof(STORAGE_HOTPLUG_INFO);
	hotplug_info->MediaRemovable = FALSE;
	hotplug_info->MediaHotplug = FALSE;
	hotplug_info->DeviceHotplug = FALSE;
	hotplug_info->WriteCacheEnableOverride = FALSE;

	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(STORAGE_HOTPLUG_INFO);

	break;
      }

    case IOCTL_CDROM_GET_LAST_SESSION:
      {
	status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	break;
      }

    default:
      {
	KdPrint(("ImDisk: Unknown IOCTL %#x.\n",
		 io_stack->Parameters.DeviceIoControl.IoControlCode));

	status = STATUS_INVALID_DEVICE_REQUEST;
	Irp->IoStatus.Information = 0;
      }
    }

  if (status == STATUS_PENDING)
    {
      IoMarkIrpPending(Irp);

      ExInterlockedInsertTailList(&device_extension->list_head,
				  &Irp->Tail.Overlay.ListEntry,
				  &device_extension->list_lock);

      KeSetEvent(&device_extension->request_event, (KPRIORITY) 0, FALSE);
    }
  else
    {
      Irp->IoStatus.Status = status;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

  return status;
}

VOID
ImDiskDeviceThread(IN PVOID Context)
{
  PDEVICE_THREAD_DATA device_thread_data;
  PDEVICE_OBJECT device_object;
  PDEVICE_EXTENSION device_extension;
  PLIST_ENTRY request;
  PIRP irp;
  PIO_STACK_LOCATION io_stack;
  LARGE_INTEGER time_out;

  PAGED_CODE();

  ASSERT(Context != NULL);

  KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

  device_thread_data = (PDEVICE_THREAD_DATA) Context;

  device_thread_data->status =
    ImDiskCreateDevice(device_thread_data->driver_object,
		       device_thread_data->create_data,
		       device_thread_data->client_thread,
		       &device_object);

  if (!NT_SUCCESS(device_thread_data->status))
    {
      KeSetEvent(&device_thread_data->created_event, (KPRIORITY) 0, FALSE);
      PsTerminateSystemThread(STATUS_SUCCESS);
    }

  KeSetEvent(&device_thread_data->created_event, (KPRIORITY) 0, FALSE);

  KdPrint(("ImDisk: Device thread initialized. (flags=%#x)\n",
	   device_object->Flags));

  device_extension = (PDEVICE_EXTENSION) device_object->DeviceExtension;

  time_out.QuadPart = -1000000;

  for (;;)
    {
      request =
	ExInterlockedRemoveHeadList(&device_extension->list_head,
				    &device_extension->list_lock);

      if (request == NULL)
	{
	  if (!device_extension->terminate_thread)
	    {
	      KeWaitForSingleObject(&device_extension->request_event,
				    Executive, KernelMode, FALSE, NULL);
	      continue;
	    }

	  KdPrint(("ImDisk: Device %i thread is shutting down.\n",
		   device_extension->device_number));

	  if (device_extension->drive_letter != 0)
	    {
	      NTSTATUS status;
	      WCHAR sym_link_global_wchar[] = L"\\DosDevices\\Global\\ :";
	      UNICODE_STRING sym_link;

	      sym_link_global_wchar[19] = device_extension->drive_letter;

	      KdPrint(("ImDisk: Removing symlink '%ws'.\n",
		       sym_link_global_wchar));

	      RtlInitUnicodeString(&sym_link, sym_link_global_wchar);
	      status = IoDeleteSymbolicLink(&sym_link);

	      if (!NT_SUCCESS(status))
		{
		  KdPrint
		    (("ImDisk: Cannot remove symlink '%ws'. (%#x)\n",
		      sym_link_global_wchar, status));
		}

	      device_extension->drive_letter = 0;
	    }

	  if (device_object->ReferenceCount != 0)
	    {
	      KdPrint(("ImDisk: Device %i is busy. Waiting.\n",
		       device_extension->device_number));

	      KeDelayExecutionThread(KernelMode, FALSE, &time_out);

	      time_out.LowPart <<= 4;
	      continue;
	    }

	  KdPrint(("ImDisk: Freeing resources for device %i.\n",
		   device_extension->device_number));

	  if (device_extension->vm_disk)
	    {
	      ULONG free_size = 0;
	      if (device_extension->image_buffer != NULL)
		ZwFreeVirtualMemory(NtCurrentProcess(),
				    &device_extension->image_buffer,
				    &free_size, MEM_RELEASE);

	      device_extension->image_buffer = NULL;
	    }
	  else
	    {
	      if (device_extension->file_handle != NULL)
		ZwClose(device_extension->file_handle);

	      device_extension->file_handle = NULL;
	    }

	  if (device_extension->file_name.Buffer != NULL)
	    {
	      ExFreePool(device_extension->file_name.Buffer);
	      device_extension->file_name.Buffer = NULL;
	      device_extension->file_name.Length = 0;
	      device_extension->file_name.MaximumLength = 0;
	    }

	  DeviceList &= ~(1 << device_extension->device_number);

	  KdPrint(("ImDisk: Deleting device object %i.\n",
		   device_extension->device_number));

	  IoDeleteDevice(device_object);

	  PsTerminateSystemThread(STATUS_SUCCESS);
	}

      irp = CONTAINING_RECORD(request, IRP, Tail.Overlay.ListEntry);

      io_stack = IoGetCurrentIrpStackLocation(irp);

      switch (io_stack->MajorFunction)
	{
	case IRP_MJ_READ:
	  {
	    PUCHAR buffer;
	    PUCHAR system_buffer =
	      (PUCHAR) MmGetSystemAddressForMdlSafe(irp->MdlAddress,
						    NormalPagePriority);
	    if (system_buffer == NULL)
	      {
		irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		irp->IoStatus.Information = 0;
		break;
	      }

	    if (device_extension->vm_disk)
	      {
		irp->IoStatus.Status =
		  ImDiskReadVMDisk(device_extension->image_buffer,
				   &irp->IoStatus,
				   system_buffer,
				   io_stack->Parameters.Read.Length,
				   io_stack->Parameters.
				   Read.ByteOffset.LowPart);
		break;
	      }

	    buffer = (PUCHAR)
	      ExAllocatePool(NonPagedPool, io_stack->Parameters.Read.Length);

	    if (buffer == NULL)
	      {
		irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		irp->IoStatus.Information = 0;
		break;
	      }

	    if (device_extension->use_proxy)
	      {
		irp->IoStatus.Status =
		  ImDiskReadProxy(device_extension->file_handle,
				  &irp->IoStatus,
				  buffer,
				  io_stack->Parameters.Read.Length,
				  &io_stack->Parameters.Read.ByteOffset);

		if (!NT_SUCCESS(irp->IoStatus.Status))
		  {
		    KdPrint(("ImDisk: Read failed on device %i.\n",
			     device_extension->device_number));

		    ImDiskRemoveVirtualDisk(device_object);
		    irp->IoStatus.Status = STATUS_NO_MEDIA_IN_DEVICE;
		    irp->IoStatus.Information = 0;
		  }
	      }
	    else				      
	      irp->IoStatus.Status =
		ZwReadFile(device_extension->file_handle,
			   NULL,
			   NULL,
			   NULL,
			   &irp->IoStatus,
			   buffer,
			   io_stack->Parameters.Read.Length,
			   &io_stack->Parameters.Read.ByteOffset,
			   NULL);

	    RtlCopyMemory(system_buffer, buffer,
			  irp->IoStatus.Information);

	    ExFreePool(buffer);

	    break;
	  }

	case IRP_MJ_WRITE:
	  {
	    PUCHAR buffer;
	    PUCHAR system_buffer =
	      (PUCHAR) MmGetSystemAddressForMdlSafe(irp->MdlAddress,
						    NormalPagePriority);
	    if (system_buffer == NULL)
	      {
		irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		irp->IoStatus.Information = 0;
		break;
	      }

	    if (device_extension->vm_disk)
	      {
		irp->IoStatus.Status =
		  ImDiskWriteVMDisk(device_extension->image_buffer,
				    &irp->IoStatus,
				    system_buffer,
				    io_stack->Parameters.Write.Length,
				    io_stack->Parameters.
				    Write.ByteOffset.LowPart);
		break;
	      }
		
	    buffer = (PUCHAR)
	      ExAllocatePool(NonPagedPool, io_stack->Parameters.Write.Length);

	    if (buffer == NULL)
	      {
		irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		irp->IoStatus.Information = 0;
		break;
	      }

	    RtlCopyMemory(buffer, system_buffer,
			  io_stack->Parameters.Write.Length);

	    if (device_extension->use_proxy)
	      {
		irp->IoStatus.Status =
		  ImDiskWriteProxy(device_extension->file_handle,
				   &irp->IoStatus,
				   buffer,
				   io_stack->Parameters.Write.Length,
				   &io_stack->
				   Parameters.Write.ByteOffset);

		if (!NT_SUCCESS(irp->IoStatus.Status))
		  {
		    KdPrint(("ImDisk: Write failed on device %i.\n",
			     device_extension->device_number));

		    ImDiskRemoveVirtualDisk(device_object);
		    irp->IoStatus.Status = STATUS_NO_MEDIA_IN_DEVICE;
		    irp->IoStatus.Information = 0;
		  }
	      }
	    else				      
	      irp->IoStatus.Status =
		ZwWriteFile(device_extension->file_handle,
			    NULL,
			    NULL,
			    NULL,
			    &irp->IoStatus,
			    buffer,
			    io_stack->Parameters.Write.Length,
			    &io_stack->Parameters.Write.ByteOffset,
			    NULL);

	    ExFreePool(buffer);

	    break;
	  }

	case IRP_MJ_DEVICE_CONTROL:
	  switch (io_stack->Parameters.DeviceIoControl.IoControlCode)
	    {
	    case IOCTL_DISK_CHECK_VERIFY:
	    case IOCTL_CDROM_CHECK_VERIFY:
	    case IOCTL_STORAGE_CHECK_VERIFY:
	    case IOCTL_STORAGE_CHECK_VERIFY2:
	      {
		PUCHAR buffer;
		LARGE_INTEGER byte_offset;
		
		buffer = (PUCHAR)
		  ExAllocatePool(NonPagedPool, + 1);

		if (buffer == NULL)
		  {
		    irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		    irp->IoStatus.Information = 0;
		    break;
		  }

		byte_offset.QuadPart = 0;

		if (device_extension->use_proxy)
		  irp->IoStatus.Status =
		    ImDiskReadProxy(device_extension->file_handle,
				    &irp->IoStatus,
				    buffer,
				    0,
				    &byte_offset);
		else				      
		  irp->IoStatus.Status =
		    ZwReadFile(device_extension->file_handle,
			       NULL,
			       NULL,
			       NULL,
			       &irp->IoStatus,
			       buffer,
			       0,
			       &byte_offset,
			       NULL);

		ExFreePool(buffer);

		if (!NT_SUCCESS(irp->IoStatus.Status))
		  {
		    KdPrint(("ImDisk: Verify failed on device %i.\n",
			     device_extension->device_number));

		    ImDiskRemoveVirtualDisk(device_object);
		    irp->IoStatus.Status = STATUS_NO_MEDIA_IN_DEVICE;
		    irp->IoStatus.Information = 0;
		    break;
		  }

		KdPrint(("ImDisk: Verify ok on device %i.\n",
			 device_extension->device_number));

		if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
		    sizeof(ULONG))
		  irp->IoStatus.Information = 0;
		else
		  {
		    *(PULONG) irp->AssociatedIrp.SystemBuffer =
		      device_extension->media_change_count;

		    irp->IoStatus.Information = sizeof(ULONG);
		  }

		irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	      }

	    case IOCTL_DISK_FORMAT_TRACKS:
	    case IOCTL_DISK_FORMAT_TRACKS_EX:
	      {
		NTSTATUS status =
		  ImDiskFloppyFormat(device_extension, irp);

		if (!NT_SUCCESS(status))
		  {
		    ImDiskRemoveVirtualDisk(device_object);
		    irp->IoStatus.Status = STATUS_NO_MEDIA_IN_DEVICE;
		    irp->IoStatus.Information = 0;
		    break;
		  }

		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = status;
		break;
	      }

	    default:
	      irp->IoStatus.Status = STATUS_DRIVER_INTERNAL_ERROR;
	    }
	  break;

	default:
	  irp->IoStatus.Status = STATUS_DRIVER_INTERNAL_ERROR;
	}

      IoCompleteRequest(irp,
			NT_SUCCESS(irp->IoStatus.Status) ?
			IO_DISK_INCREMENT : IO_NO_INCREMENT);
    }
}

#pragma code_seg("PAGE")

NTSTATUS
ImDiskReadVMDisk(IN PUCHAR VMDisk,
		 OUT PIO_STATUS_BLOCK IoStatusBlock,
		 OUT PVOID Buffer,
		 IN ULONG Length,
		 IN ULONG ByteOffset)
{
  LARGE_INTEGER wait_time;

  PAGED_CODE();

  ASSERT(VMDisk != NULL);
  ASSERT(IoStatusBlock != NULL);
  ASSERT(Buffer != NULL);

  RtlCopyMemory(Buffer,	VMDisk + ByteOffset, Length);

  wait_time.QuadPart = -1;
  KeDelayExecutionThread(KernelMode, FALSE, &wait_time);

  IoStatusBlock->Status = STATUS_SUCCESS;
  IoStatusBlock->Information = Length;

  return STATUS_SUCCESS;
}

NTSTATUS
ImDiskWriteVMDisk(IN OUT PUCHAR VMDisk,
		  OUT PIO_STATUS_BLOCK IoStatusBlock,
		  OUT PVOID Buffer,
		  IN ULONG Length,
		  IN ULONG ByteOffset)
{
  LARGE_INTEGER wait_time;

  PAGED_CODE();

  ASSERT(VMDisk != NULL);
  ASSERT(IoStatusBlock != NULL);
  ASSERT(Buffer != NULL);

  RtlCopyMemory(VMDisk + ByteOffset, Buffer, Length);

  wait_time.QuadPart = -1;
  KeDelayExecutionThread(KernelMode, FALSE, &wait_time);

  IoStatusBlock->Status = STATUS_SUCCESS;
  IoStatusBlock->Information = Length;

  return STATUS_SUCCESS;
}

NTSTATUS
ImDiskSafeReadFile(IN HANDLE FileHandle,
		   OUT PIO_STATUS_BLOCK IoStatusBlock,
		   OUT PVOID Buffer,
		   IN ULONG Length,
		   IN PLARGE_INTEGER Offset)
{
  NTSTATUS status;
  ULONG LengthDone = 0;

  PAGED_CODE();

  ASSERT(FileHandle != NULL);
  ASSERT(IoStatusBlock != NULL);
  ASSERT(Buffer != NULL);

  while (LengthDone < Length)
    {
      ULONG RequestLength = Length - LengthDone;

      for (;;)
	{
	  LARGE_INTEGER RequestOffset;
	  PUCHAR InterBuffer = ExAllocatePool(PagedPool, RequestLength);

	  if (InterBuffer == NULL)
	    {
	      KdPrint(("ImDisk: Insufficient paged pool to allocate "
		       "intermediate buffer for ImDiskSafeReadFile() "
		       "(%u bytes).\n", RequestLength));

	      RequestLength >>= 2;
	      continue;
	    }

	  RequestOffset.QuadPart = Offset->QuadPart + LengthDone;

	  status = ZwReadFile(FileHandle,
			      NULL,
			      NULL,
			      NULL,
			      IoStatusBlock,
			      InterBuffer,
			      RequestLength,
			      &RequestOffset,
			      NULL);

	  if ((status == STATUS_INSUFFICIENT_RESOURCES) |
	      (status == STATUS_INVALID_BUFFER_SIZE) |
	      (status == STATUS_INVALID_PARAMETER))
	    {
	      ExFreePool(InterBuffer);

	      RequestLength >>= 2;
	      continue;
	    }

	  if (!NT_SUCCESS(status))
	    {
	      ExFreePool(InterBuffer);
	      break;
	    }

	  RtlCopyMemory((PUCHAR) Buffer + LengthDone, InterBuffer,
			IoStatusBlock->Information);

	  ExFreePool(InterBuffer);
	  break;
	}

      if (!NT_SUCCESS(status))
	{
	  IoStatusBlock->Status = status;
	  IoStatusBlock->Information = LengthDone;
	  return IoStatusBlock->Status;
	}

      if (IoStatusBlock->Information == 0)
	{
	  IoStatusBlock->Status = STATUS_END_OF_MEDIA;
	  IoStatusBlock->Information = LengthDone;
	  return IoStatusBlock->Status;
	}

      LengthDone += IoStatusBlock->Information;
    }

  IoStatusBlock->Status = STATUS_SUCCESS;
  IoStatusBlock->Information = LengthDone;
  return IoStatusBlock->Status;
}

NTSTATUS
ImDiskSafeReadStream(IN HANDLE FileHandle,
		     OUT PIO_STATUS_BLOCK IoStatusBlock,
		     OUT PVOID Buffer,
		     IN ULONG Length)
{
  NTSTATUS status;
  ULONG LengthDone = 0;

  PAGED_CODE();

  ASSERT(FileHandle != NULL);
  ASSERT(IoStatusBlock != NULL);
  ASSERT(Buffer != NULL);

  while (LengthDone < Length)
    {
      ULONG RequestLength = Length - LengthDone;

      do
	{
	  status = ZwReadFile(FileHandle,
			      NULL,
			      NULL,
			      NULL,
			      IoStatusBlock,
			      (PUCHAR) Buffer + LengthDone,
			      RequestLength,
			      NULL,
			      NULL);
	  RequestLength >>= 1;
	}
      while ((status == STATUS_INVALID_BUFFER_SIZE) |
	     (status == STATUS_INVALID_PARAMETER));

      if (!NT_SUCCESS(status))
	{
	  IoStatusBlock->Status = status;
	  IoStatusBlock->Information = 0;
	  return IoStatusBlock->Status;
	}

      if (IoStatusBlock->Information == 0)
	{
	  IoStatusBlock->Status = STATUS_END_OF_MEDIA;
	  IoStatusBlock->Information = 0;
	  return IoStatusBlock->Status;
	}

      LengthDone += IoStatusBlock->Information;
    }

  IoStatusBlock->Status = STATUS_SUCCESS;
  IoStatusBlock->Information = LengthDone;
  return IoStatusBlock->Status;
}

NTSTATUS
ImDiskSafeWriteStream(IN HANDLE FileHandle,
			OUT PIO_STATUS_BLOCK IoStatusBlock,
			IN PVOID Buffer,
			IN ULONG Length)
{
  NTSTATUS status;
  ULONG LengthDone = 0;

  PAGED_CODE();

  ASSERT(FileHandle != NULL);
  ASSERT(IoStatusBlock != NULL);
  ASSERT(Buffer != NULL);

  while (LengthDone < Length)
    {
      ULONG RequestLength = Length - LengthDone;

      do
	{
	  status = ZwWriteFile(FileHandle,
			       NULL,
			       NULL,
			       NULL,
			       IoStatusBlock,
			       (PUCHAR) Buffer + LengthDone,
			       RequestLength,
			       NULL,
			       NULL);
	  RequestLength >>= 1;
	}
      while ((status == STATUS_INVALID_BUFFER_SIZE) |
	     (status == STATUS_INVALID_PARAMETER));

      if (!NT_SUCCESS(status))
	{
	  IoStatusBlock->Status = status;
	  IoStatusBlock->Information = LengthDone;
	  return IoStatusBlock->Status;
	}

      if (IoStatusBlock->Information == 0)
	{
	  IoStatusBlock->Status = STATUS_END_OF_MEDIA;
	  IoStatusBlock->Information = LengthDone;
	  return IoStatusBlock->Status;
	}

      LengthDone += IoStatusBlock->Information;
    }

  IoStatusBlock->Status = STATUS_SUCCESS;
  IoStatusBlock->Information = LengthDone;
  return IoStatusBlock->Status;
}

NTSTATUS
ImDiskConnectProxy(IN HANDLE FileHandle,
		   OUT PIO_STATUS_BLOCK IoStatusBlock,
		   IN ULONG Flags,
		   IN PWSTR ConnectionString,
		   IN ULONG ConnectionStringLength,
		   OUT PIMDPROXY_INFO_RESP ProxyInfoResponse,
		   IN ULONG ProxyInfoResponseLength)
{
  IMDPROXY_CONNECT_REQ connect_req;
  NTSTATUS status;

  PAGED_CODE();

  ASSERT(FileHandle != NULL);
  ASSERT(IoStatusBlock != NULL);

  if ((ProxyInfoResponse == NULL) |
      (ProxyInfoResponseLength < sizeof(IMDPROXY_INFO_RESP)))
    {
      IoStatusBlock->Status = STATUS_BUFFER_OVERFLOW;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  connect_req.request_code = IMDPROXY_REQ_CONNECT;
  connect_req.flags = Flags;
  connect_req.length = ConnectionStringLength;

  KdPrint(("ImDisk Proxy Client: Sending IMDPROXY_CONNECT_REQ.\n"));

  status = ImDiskSafeWriteStream(FileHandle,
				 IoStatusBlock,
				 &connect_req,
				 sizeof(connect_req));

  if (!NT_SUCCESS(status))
    {
      IoStatusBlock->Status = status;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  KdPrint(("ImDisk Proxy Client: Sending connection string.\n"));

  status = ImDiskSafeWriteStream(FileHandle,
				 IoStatusBlock,
				 ConnectionString,
				 ConnectionStringLength);

  if (!NT_SUCCESS(status))
    {
      IoStatusBlock->Status = status;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  KdPrint(("ImDisk Proxy Client: Sent all connect data.\n"));

  status = ImDiskSafeReadStream(FileHandle,
				IoStatusBlock,
				ProxyInfoResponse,
				sizeof(IMDPROXY_INFO_RESP));

  if (!NT_SUCCESS(status))
    {
      IoStatusBlock->Status = status;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  KdPrint(("ImDisk Proxy Client: Got ok response IMDPROXY_INFO_RESP.\n"));

  IoStatusBlock->Status = STATUS_SUCCESS;
  IoStatusBlock->Information = 0;
  return IoStatusBlock->Status;
}

NTSTATUS
ImDiskQueryInformationProxy(IN HANDLE FileHandle,
			    OUT PIO_STATUS_BLOCK IoStatusBlock,
			    OUT PIMDPROXY_INFO_RESP ProxyInfoResponse,
			    IN ULONG ProxyInfoResponseLength)
{
  ULONGLONG proxy_req = IMDPROXY_REQ_INFO;
  NTSTATUS status;

  PAGED_CODE();

  ASSERT(FileHandle != NULL);
  ASSERT(IoStatusBlock != NULL);

  if ((ProxyInfoResponse == NULL) |
      (ProxyInfoResponseLength < sizeof(IMDPROXY_INFO_RESP)))
    {
      IoStatusBlock->Status = STATUS_BUFFER_OVERFLOW;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  KdPrint(("ImDisk Proxy Client: Sending IMDPROXY_REQ_INFO.\n"));

  status = ImDiskSafeWriteStream(FileHandle,
				 IoStatusBlock,
				 &proxy_req,
				 sizeof(proxy_req));

  if (!NT_SUCCESS(status))
    {
      IoStatusBlock->Status = status;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  KdPrint(("ImDisk Proxy Client: Sent IMDPROXY_REQ_INFO.\n"));

  status = ImDiskSafeReadStream(FileHandle,
				IoStatusBlock,
				ProxyInfoResponse,
				sizeof(IMDPROXY_INFO_RESP));

  if (!NT_SUCCESS(status))
    {
      IoStatusBlock->Status = status;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  KdPrint(("ImDisk Proxy Client: Got ok response IMDPROXY_INFO_RESP.\n"));

  if (ProxyInfoResponse->req_alignment - 1 > FILE_512_BYTE_ALIGNMENT)
    {
      KdPrint(("ImDisk IMDPROXY_INFO_RESP: Unsupported sizes. "
	       "Got %p-%p size and %p-%p alignment.\n",
	       ProxyInfoResponse->file_size,
	       ProxyInfoResponse->req_alignment));

      IoStatusBlock->Status = STATUS_INVALID_PARAMETER;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  IoStatusBlock->Status = STATUS_SUCCESS;
  IoStatusBlock->Information = 0;
  return IoStatusBlock->Status;
}

NTSTATUS
ImDiskReadProxy(IN HANDLE FileHandle,
		  OUT PIO_STATUS_BLOCK IoStatusBlock,
		  OUT PVOID Buffer,
		  IN ULONG Length,
		  IN PLARGE_INTEGER ByteOffset)
{
  IMDPROXY_READ_REQ read_req;
  IMDPROXY_READ_RESP read_resp;
  NTSTATUS status;

  PAGED_CODE();

  ASSERT(FileHandle != NULL);
  ASSERT(IoStatusBlock != NULL);
  ASSERT(Buffer != NULL);
  ASSERT(ByteOffset != NULL);

  read_req.request_code = IMDPROXY_REQ_READ;
  read_req.offset = ByteOffset->QuadPart;
  read_req.length = Length;

  KdPrint2(("ImDisk Proxy Client: IMDPROXY_REQ_READ %u bytes at %u.\n",
	    (ULONG) read_req.length, (ULONG) read_req.offset));

  status = ImDiskSafeWriteStream(FileHandle,
				   IoStatusBlock,
				   &read_req,
				   sizeof(read_req));

  if (!NT_SUCCESS(status))
    {
      IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  KdPrint2(("ImDisk Proxy Client: IMDPROXY_REQ_READ sent. Waiting IMDPROXY_REQ_RESP.\n"));

  status = ImDiskSafeReadStream(FileHandle,
				IoStatusBlock,
				&read_resp,
				sizeof(read_resp));

  if (!NT_SUCCESS(status))
    {
      IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  if (read_resp.errorno != 0)
    {
      KdPrint(("ImDisk Proxy Client: Server returned error %p-%p.\n",
	       read_resp.errorno));
      IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  if (read_resp.length != Length)
    {
      KdPrint(("ImDisk Proxy Client: IMDPROXY_REQ_READ %u bytes, "
	       "IMDPROXY_RESP_READ %u bytes.\n",
	       Length, (ULONG) read_resp.length));
      IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }
  
  KdPrint2
    (("ImDisk Proxy Client: Got ok response. Waiting for data stream.\n"));

  status = ImDiskSafeReadStream(FileHandle,
				IoStatusBlock,
				Buffer,
				(ULONG) read_resp.length);

  if (!NT_SUCCESS(status))
    KdPrint(("ImDisk Proxy Client: Data stream of %u bytes received with I/O "
	     "status %#x. Status returned by stream reader is %#x.\n",
	     IoStatusBlock->Information, IoStatusBlock->Status, status));

  KdPrint2
    (("ImDisk Proxy Client: Received %u byte data stream.\n",
      IoStatusBlock->Information));

  return status;
}

NTSTATUS
ImDiskWriteProxy(IN HANDLE FileHandle,
		   OUT PIO_STATUS_BLOCK IoStatusBlock,
		   IN PVOID Buffer,
		   IN ULONG Length,
		   IN PLARGE_INTEGER ByteOffset)
{
  IMDPROXY_READ_REQ write_req;
  IMDPROXY_READ_RESP write_resp;
  NTSTATUS status;

  PAGED_CODE();

  ASSERT(FileHandle != NULL);
  ASSERT(IoStatusBlock != NULL);
  ASSERT(Buffer != NULL);
  ASSERT(ByteOffset != NULL);

  write_req.request_code = IMDPROXY_REQ_WRITE;
  write_req.offset = ByteOffset->QuadPart;
  write_req.length = Length;

  KdPrint2(("ImDisk Proxy Client: IMDPROXY_REQ_WRITE %u bytes at %u.\n",
	    (ULONG) write_req.length, (ULONG) write_req.offset));

  status = ImDiskSafeWriteStream(FileHandle,
				   IoStatusBlock,
				   &write_req,
				   sizeof(write_req));

  if (!NT_SUCCESS(status))
    {
      IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  KdPrint2
    (("ImDisk Proxy Client: IMDPROXY_REQ_WRITE sent. Sending data stream.\n"));

  status = ImDiskSafeWriteStream(FileHandle,
				   IoStatusBlock,
				   Buffer,
				   (ULONG) write_req.length);

  if (!NT_SUCCESS(status))
    {
      KdPrint(("ImDisk Proxy Client: Data stream send failed. "
	       "Sent %u bytes with I/O status %#x.\n",
	       IoStatusBlock->Information, IoStatusBlock->Status));

      IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  KdPrint2
    (("ImDisk Proxy Client: Data stream of %u bytes sent with I/O status %#x. "
      "Status returned by stream writer is %#x. "
      "Waiting for IMDPROXY_RESP_WRITE.\n",
      IoStatusBlock->Information, IoStatusBlock->Status, status));

  status = ImDiskSafeReadStream(FileHandle,
				  IoStatusBlock,
				  &write_resp,
				  sizeof(write_resp));

  if (!NT_SUCCESS(status))
    {
      IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  if (write_resp.errorno != 0)
    {
      KdPrint(("ImDisk Proxy Client: Server returned error %p-%p.\n",
	       write_resp.errorno));
      IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }

  if (write_resp.length != Length)
    {
      KdPrint(("ImDisk Proxy Client: IMDPROXY_REQ_WRITE %u bytes, "
	       "IMDPROXY_RESP_WRITE %u bytes.\n",
	       Length, (ULONG) write_resp.length));
      IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
      IoStatusBlock->Information = 0;
      return IoStatusBlock->Status;
    }
  
  KdPrint2(("ImDisk Proxy Client: Got ok response. "
	    "Resetting IoStatusBlock fields.\n"));

  IoStatusBlock->Status = STATUS_SUCCESS;
  IoStatusBlock->Information = Length;
  return IoStatusBlock->Status;
}

//
//	Format tracks
//	Actually, just fills specified range of tracks with fill characters
//
NTSTATUS
ImDiskFloppyFormat(IN PDEVICE_EXTENSION Extension,
		   IN PIRP Irp)
{
  PFORMAT_PARAMETERS	param;
  DISK_GEOMETRY	        geometry;
  ULONG			track_length;
  PUCHAR		format_buffer;
  LARGE_INTEGER		start_offset;
  LARGE_INTEGER		end_offset;
  NTSTATUS		status;

  PAGED_CODE();

  ASSERT(Extension != NULL);
  ASSERT(Irp != NULL);

  param = (PFORMAT_PARAMETERS) Irp->AssociatedIrp.SystemBuffer;

  geometry = Extension->disk_geometry;

  track_length = geometry.BytesPerSector * geometry.SectorsPerTrack;

  start_offset.QuadPart =
    param->StartCylinderNumber * geometry.TracksPerCylinder * track_length +
    param->StartHeadNumber * track_length;

  end_offset.QuadPart =
    param->EndCylinderNumber * geometry.TracksPerCylinder * track_length +
    param->EndHeadNumber * track_length;

  if (Extension->vm_disk)
    {
      LARGE_INTEGER wait_time;

      RtlFillMemory(((PUCHAR) Extension->image_buffer) + start_offset.LowPart,
		    end_offset.LowPart - start_offset.LowPart + track_length,
		    MEDIA_FORMAT_FILL_DATA);

      wait_time.QuadPart = -1;
      KeDelayExecutionThread(KernelMode, FALSE, &wait_time);

      Irp->IoStatus.Information = 0;
      return STATUS_SUCCESS;
    }

  format_buffer = ExAllocatePool(NonPagedPool, track_length);

  if (format_buffer == NULL)
    {
      Irp->IoStatus.Information = 0;
      return STATUS_INSUFFICIENT_RESOURCES;
    }

  RtlFillMemory(format_buffer, track_length, MEDIA_FORMAT_FILL_DATA);

  do
    {
      if (Extension->use_proxy)
	status = ImDiskWriteProxy(Extension->file_handle,
				  &Irp->IoStatus,
				  format_buffer,
				  track_length,
				  &start_offset);
      else
	status = ZwWriteFile(Extension->file_handle,
			     NULL,
			     NULL,
			     NULL,
			     &Irp->IoStatus,
			     format_buffer,
			     track_length,
			     &start_offset,
			     NULL);

      if (!NT_SUCCESS(status))
	{
	  KdPrint(("ImDisk Format failed: Write failed with status %#x.\n",
		   status));

	  break;
	}

      start_offset.QuadPart += track_length;
    }
  while (start_offset.QuadPart <= end_offset.QuadPart);

  ExFreePool(format_buffer);

  Irp->IoStatus.Information = 0;

  return status;
}
