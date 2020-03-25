/*
ImDisk Virtual Disk Driver for Windows NT/2000/XP.
This driver emulates harddisk partitions, floppy drives and CD/DVD-ROM
drives from disk image files, in virtual memory or by redirecting I/O
requests somewhere else, possibly to another machine, through a
co-operating user-mode service, ImDskSvc.

Copyright (C) 2005-2015 Olof Lagerkvist.

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

This source file contains some GNU GPL licensed code:
- Parts related to floppy emulation based on VFD by Ken Kato.
http://chitchat.at.infoseek.co.jp/vmware/vfd.html
Copyright (C) Free Software Foundation, Inc.
Read gpl.txt for the full GNU GPL license.

This source file may contain BSD licensed code:
- Some code ported to NT from the FreeBSD md driver by Olof Lagerkvist.
http://www.ltr-data.se
Copyright (C) The FreeBSD Project.
Copyright (C) The Regents of the University of California.
*/

#include <ntifs.h>
#include <wdm.h>
#include <ntddk.h>
#include <ntdddisk.h>
#include <ntddcdrm.h>
#include <ntverp.h>
#include <mountmgr.h>
#include <stdio.h>

///
/// Definitions and imports are now in the "sources" file and managed by the
/// build utility.
///

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

#if DEBUG_LEVEL >= 2
#define KdPrint2(x) DbgPrint x
#else
#define KdPrint2(x)
#endif

#if DEBUG_LEVEL >= 1
#undef KdPrint
#define KdPrint(x)  DbgPrint x
#define ImDiskLogError(x) ImDiskLogDbgError x
#else
#define ImDiskLogError(x)
#endif

#include "..\inc\ntkmapi.h"
#include "..\inc\imdisk.h"
#include "..\inc\imdproxy.h"

#pragma warning(disable: 28719)

#define POOL_TAG                         'iDmI'

#define IMDISK_DEFAULT_LOAD_DEVICES      0
#define IMDISK_DEFAULT_MAX_DEVICES       64000

///
/// Constants for synthetic geometry of the virtual disks
///

// For hard drive partition-style devices
#define SECTOR_SIZE_HDD                  512
#define HEAD_SIZE_HDD                    63

// For CD-ROM/DVD-style devices
#define SECTOR_SIZE_CD_ROM               2048
#define SECTORS_PER_TRACK_CD_ROM         32
#define TRACKS_PER_CYLINDER_CD_ROM       64

#ifdef INCLUDE_VFD_ORIGIN

// For floppy devices. Based on Virtual Floppy Driver, VFD, by Ken Kato.
#define SECTOR_SIZE_FDD                  512
#define TRACKS_PER_CYLINDER_FDD          12
//
//	Sizes in bytes of floppies
//
// 3.5" UHD
#define MEDIA_SIZE_240MB    (234752 << 10)
#define MEDIA_SIZE_120MB    (123264 << 10)
// 3.5"
#define MEDIA_SIZE_2800KB	(2880 << 10)
#define MEDIA_SIZE_1722KB   (1722 << 10)
#define MEDIA_SIZE_1680KB   (1680 << 10)
#define MEDIA_SIZE_1440KB	(1440 << 10)
#define	MEDIA_SIZE_820KB 	(820  << 10)
#define	MEDIA_SIZE_720KB 	(720  << 10)
// 5.25"
#define MEDIA_SIZE_1200KB	(1200 << 10)
#define MEDIA_SIZE_640KB    (640  << 10)
#define	MEDIA_SIZE_360KB	(360  << 10)
#define	MEDIA_SIZE_320KB 	(320  << 10)
#define MEDIA_SIZE_180KB	(180  << 10)
#define	MEDIA_SIZE_160KB 	(160  << 10)

//
//      Indexes for the following DISK_GEOMETRY table.
//
enum {
    // 3.5" UHD
    MEDIA_TYPE_240M,
    MEDIA_TYPE_120M,
    // 3.5"
    MEDIA_TYPE_2880K,
    MEDIA_TYPE_1722K,
    MEDIA_TYPE_1680K,
    MEDIA_TYPE_1440K,
    MEDIA_TYPE_820K,
    MEDIA_TYPE_720K,
    // 5.12"
    MEDIA_TYPE_1200K,
    MEDIA_TYPE_640K,
    MEDIA_TYPE_360K,
    MEDIA_TYPE_320K,
    MEDIA_TYPE_180K,
    MEDIA_TYPE_160K
};

DISK_GEOMETRY media_table[] = {
    // 3.5" UHD
    { { 963 }, F3_120M_512, 8, 32, 512 },
    { { 262 }, F3_120M_512, 32, 56, 512 },
    // 3.5"
    { { 80 }, F3_2Pt88_512, 2, 36, 512 },
    { { 82 }, F3_1Pt44_512, 2, 21, 512 },
    { { 80 }, F3_1Pt44_512, 2, 21, 512 },
    { { 80 }, F3_1Pt44_512, 2, 18, 512 },
    { { 82 }, F3_720_512, 2, 10, 512 },
    { { 80 }, F3_720_512, 2, 9, 512 },
    // 5.25"
    { { 80 }, F5_1Pt2_512, 2, 15, 512 },
    { { 40 }, F5_640_512, 2, 18, 512 },
    { { 40 }, F5_360_512, 2, 9, 512 },
    { { 40 }, F5_320_512, 2, 8, 512 },
    { { 40 }, F5_180_512, 1, 9, 512 },
    { { 40 }, F5_160_512, 1, 8, 512 }
};

#define SET_MEDIA_TYPE(geometry, media_index) \
  (geometry.MediaType = media_table[media_index].MediaType)

#endif // INCLUDE_VFD_ORIGIN

//
//	TOC Data Track returned for virtual CD/DVD
//
#define TOC_DATA_TRACK                   0x04

//
//	Fill character for formatting virtual floppy media
//
#define MEDIA_FORMAT_FILL_DATA	0xf6

// This structure is used when a new device is about to be created. It is sent
// to the created device dispatch thread which also creates the device object.
typedef struct _DEVICE_THREAD_DATA
{
    PDRIVER_OBJECT driver_object;
    PIMDISK_CREATE_DATA create_data;
    PETHREAD client_thread;   // The client thread that device should impersonate
    KEVENT created_event;     // Set when device is created (or creation failed)
    BOOLEAN caller_waiting;   // If there is a caller waiting to free this data
    NTSTATUS status;          // Set after device creation attempt
} DEVICE_THREAD_DATA, *PDEVICE_THREAD_DATA;

typedef struct _PROXY_CONNECTION
{
    enum PROXY_CONNECTION_TYPE
    {
        PROXY_CONNECTION_DEVICE,
        PROXY_CONNECTION_SHM
    } connection_type;       // Connection type

    union
    {
        // Valid if connection_type is PROXY_CONNECTION_DEVICE
        PFILE_OBJECT device;     // Pointer to proxy communication object

                                 // Valid if connection_type is PROXY_CONNECTION_SHM
        struct
        {
            HANDLE request_event_handle;
            PKEVENT request_event;
            HANDLE response_event_handle;
            PKEVENT response_event;
            PUCHAR shared_memory;
            ULONG_PTR shared_memory_size;
        };
    };
} PROXY_CONNECTION, *PPROXY_CONNECTION;

typedef struct _DEVICE_EXTENSION
{
    LIST_ENTRY list_head;
    KSPIN_LOCK list_lock;
    KEVENT request_event;
    KEVENT terminate_thread;

    ULONG device_number;

    HANDLE file_handle;          // For file or proxy type
    PDEVICE_OBJECT dev_object;   // Pointer to image I/O DEVICE_OBJECT
    PFILE_OBJECT file_object;    // Pointer to image I/O FILE_OBJECT
    BOOLEAN parallel_io;         // TRUE if image I/O is done in dispatcher thread
    PUCHAR image_buffer;         // For vm type
    BOOLEAN byte_swap;           // If image I/O should swap each pair of bytes
    BOOLEAN shared_image;        // Image opened for shared writing
    PROXY_CONNECTION proxy;      // Proxy connection data
    UNICODE_STRING file_name;    // Name of image file, if any
    WCHAR drive_letter;          // Drive letter if maintained by the driver
    DISK_GEOMETRY disk_geometry; // Virtual C/H/S geometry (Cylinders=Total size)
    LARGE_INTEGER image_offset;  // Offset in bytes in the image file
    ULONG media_change_count;
    BOOLEAN read_only;
    BOOLEAN vm_disk;             // TRUE if this device is a virtual memory disk
    BOOLEAN awealloc_disk;       // TRUE if this device is a physical memory disk
                                 // through AWEAlloc driver
    BOOLEAN use_proxy;           // TRUE if this device uses proxy device for I/O
    BOOLEAN proxy_unmap;         // TRUE if proxy supports UNMAP operations
    BOOLEAN proxy_zero;          // TRUE if proxy supports ZERO operations
    BOOLEAN image_modified;      // TRUE if this device has been written to
    LONG special_file_count;     // Number of swapfiles/hiberfiles on device
    BOOLEAN use_set_zero_data;   // TRUE if FSCTL_SET_ZERO_DATA is used to write
                                 // all zeros blocks
    BOOLEAN no_file_level_trim;  // TRUE if last file level trim failed

    PKTHREAD device_thread;      // Pointer to the worker thread object

    KSPIN_LOCK last_io_lock;     // Last I/O buffer for fast re-reads
    PUCHAR last_io_data;
    LONGLONG last_io_offset;
    ULONG last_io_length;

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

typedef struct _REFERENCED_OBJECT
{
    LIST_ENTRY list_entry;
    PFILE_OBJECT file_object;
} REFERENCED_OBJECT, *PREFERENCED_OBJECT;

// Prototypes for functions defined in this driver

EXTERN_C DRIVER_INITIALIZE DriverEntry;

DRIVER_UNLOAD ImDiskUnload;

VOID
ImDiskFindFreeDeviceNumber(PDRIVER_OBJECT DriverObject,
    PULONG DeviceNumber);

VOID
ImDiskLogDbgError(IN PVOID Object,
    IN UCHAR MajorFunctionCode,
    IN UCHAR RetryCount,
    IN PULONG DumpData,
    IN USHORT DumpDataSize,
    IN USHORT EventCategory,
    IN NTSTATUS ErrorCode,
    IN ULONG UniqueErrorValue,
    IN NTSTATUS FinalStatus,
    IN ULONG SequenceNumber,
    IN ULONG IoControlCode,
    IN PLARGE_INTEGER DeviceOffset,
    IN PWCHAR Message);

NTSTATUS
ImDiskAddVirtualDisk(IN PDRIVER_OBJECT DriverObject,
    IN OUT PIMDISK_CREATE_DATA CreateData,
    IN PETHREAD ClientThread);

NTSTATUS
ImDiskAddVirtualDiskAfterInitialization(IN PDRIVER_OBJECT DriverObject,
    IN HANDLE ParameterKey,
    IN ULONG DeviceNumber);

NTSTATUS
ImDiskCreateDriveLetter(IN WCHAR DriveLetter,
    IN ULONG DeviceNumber);

NTSTATUS
ImDiskRemoveDriveLetter(IN WCHAR DriveLetter);

VOID
ImDiskRemoveVirtualDisk(IN PDEVICE_OBJECT DeviceObject);

__drv_dispatchType(IRP_MJ_CREATE)
__drv_dispatchType(IRP_MJ_CLOSE)
DRIVER_DISPATCH ImDiskCreateClose;

__drv_dispatchType(IRP_MJ_READ)
__drv_dispatchType(IRP_MJ_WRITE)
DRIVER_DISPATCH ImDiskReadWrite;

__drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
__drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL)
DRIVER_DISPATCH ImDiskDeviceControl;

__drv_dispatchType(IRP_MJ_PNP)
DRIVER_DISPATCH ImDiskDispatchPnP;

__drv_dispatchType(IRP_MJ_QUERY_INFORMATION)
DRIVER_DISPATCH ImDiskQueryInformation;

__drv_dispatchType(IRP_MJ_SET_INFORMATION)
DRIVER_DISPATCH ImDiskSetInformation;

__drv_dispatchType(IRP_MJ_FLUSH_BUFFERS)
DRIVER_DISPATCH ImDiskFlushBuffers;

KSTART_ROUTINE ImDiskDeviceThread;

IO_COMPLETION_ROUTINE ImDiskReadWriteLowerDeviceCompletion;

NTSTATUS
ImDiskConnectProxy(IN OUT PPROXY_CONNECTION Proxy,
    IN OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent OPTIONAL,
    IN ULONG Flags,
    IN PWSTR ConnectionString,
    IN USHORT ConnectionStringLength);

VOID
ImDiskCloseProxy(IN PPROXY_CONNECTION Proxy);

NTSTATUS
ImDiskQueryInformationProxy(IN PPROXY_CONNECTION Proxy,
    IN OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent OPTIONAL,
    OUT PIMDPROXY_INFO_RESP ProxyInfoResponse,
    IN ULONG ProxyInfoResponseLength);

NTSTATUS
ImDiskReadProxy(IN PPROXY_CONNECTION Proxy,
    IN OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent OPTIONAL,
    OUT PVOID Buffer,
    IN ULONG Length,
    IN PLARGE_INTEGER ByteOffset);

NTSTATUS
ImDiskWriteProxy(IN PPROXY_CONNECTION Proxy,
    IN OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent OPTIONAL,
    IN PVOID Buffer,
    IN ULONG Length,
    IN PLARGE_INTEGER ByteOffset);

NTSTATUS
ImDiskUnmapOrZeroProxy(IN PPROXY_CONNECTION Proxy,
    IN ULONGLONG RequestCode,
    IN OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent OPTIONAL,
    IN ULONG Items,
    IN PDEVICE_DATA_SET_RANGE Ranges);

//
// Reads in a loop up to "Length" or until eof reached.
//
NTSTATUS
ImDiskSafeReadFile(IN HANDLE FileHandle,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    OUT PVOID Buffer,
    IN SIZE_T Length,
    IN PLARGE_INTEGER Offset);

NTSTATUS
ImDiskReadWriteLowerDevice(PIRP Irp, PDEVICE_EXTENSION DeviceExtension);

#ifdef INCLUDE_VFD_ORIGIN

NTSTATUS
ImDiskFloppyFormat(IN PDEVICE_EXTENSION Extension,
    IN PIRP Irp);

#endif // INCLUDE_VFD_ORIGIN

#ifdef _AMD64_

#define ImDiskAcquireLock KeAcquireInStackQueuedSpinLock

#define ImDiskReleaseLock KeReleaseInStackQueuedSpinLock

FORCEINLINE
VOID
ImDiskInterlockedInsertTailList(
    PLIST_ENTRY ListHead,
    PLIST_ENTRY ListEntry,
    PKSPIN_LOCK SpinLock)
{
    KLOCK_QUEUE_HANDLE lock_handle;

    KeAcquireInStackQueuedSpinLock(SpinLock, &lock_handle);

    InsertTailList(ListHead, ListEntry);

    KeReleaseInStackQueuedSpinLock(&lock_handle);
}

FORCEINLINE
PLIST_ENTRY
ImDiskInterlockedRemoveHeadList(
    PLIST_ENTRY ListHead,
    PKSPIN_LOCK SpinLock)
{
    KLOCK_QUEUE_HANDLE lock_handle;
    PLIST_ENTRY item;

    KeAcquireInStackQueuedSpinLock(SpinLock, &lock_handle);

    item = RemoveHeadList(ListHead);

    if (item == ListHead)
    {
        item = NULL;
    }

    KeReleaseInStackQueuedSpinLock(&lock_handle);

    return item;
}

#else

#define ImDiskAcquireLock(SpinLock, LockHandle) \
    { \
        (LockHandle)->LockQueue.Lock = (SpinLock); \
        KeAcquireSpinLock((LockHandle)->LockQueue.Lock, &(LockHandle)->OldIrql); \
    }

#define ImDiskReleaseLock(LockHandle) \
    { \
        KeReleaseSpinLock((LockHandle)->LockQueue.Lock, (LockHandle)->OldIrql); \
    }

#define ImDiskInterlockedInsertTailList (VOID)ExInterlockedInsertTailList

#define ImDiskInterlockedRemoveHeadList ExInterlockedRemoveHeadList

#endif

FORCEINLINE
BOOLEAN
ImDiskIsBufferZero(PVOID Buffer, ULONG Length)
{
    PULONGLONG ptr;

    if (Length < sizeof(ULONGLONG))
        return FALSE;

    for (ptr = (PULONGLONG)Buffer;
    (ptr <= (PULONGLONG)((PUCHAR)Buffer + Length - sizeof(ULONGLONG))) &&
        (*ptr == 0); ptr++);

        return (BOOLEAN)(ptr == (PULONGLONG)((PUCHAR)Buffer + Length));
}

FORCEINLINE
VOID
ImDiskByteSwapBuffer(IN OUT PUCHAR Buffer,
    IN ULONG_PTR Length)
{
    PUCHAR ptr;

    for (ptr = Buffer;
    (ULONG_PTR)(ptr - Buffer) < Length;
        ptr += 2)
    {
        UCHAR b1 = ptr[1];
        ptr[1] = ptr[0];
        ptr[0] = b1;
    }
}

//
// Pointer to the controller device object.
//
PDEVICE_OBJECT ImDiskCtlDevice;

//
// Allocation bitmap with currently configured device numbers.
// (No device list bit field is maintained anymore. Device list is always
// enumerated "live" directly from current device objects.)
//
//volatile ULONGLONG DeviceList = 0;

//
// Max number of devices that can be dynamically created by IOCTL calls
// to the control device.
//
ULONG MaxDevices;

//
// An array of boolean values for each drive letter where TRUE means a
// drive letter disallowed for use by ImDisk devices.
//
BOOLEAN DisallowedDriveLetters[L'Z' - L'A' + 1] = { FALSE };

//
// Device list lock
//
KSPIN_LOCK DeviceListLock;

//
// Device list lock
//
KSPIN_LOCK ReferencedObjectsListLock;

//
// List of objects referenced using
// IOCTL_IMDISK_REFERENCE_HANDLE
//
LIST_ENTRY ReferencedObjects;

//
// Handle to global refresh event
//
PKEVENT RefreshEvent = NULL;

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
    UNICODE_STRING refresh_event_name;
    PSECURITY_DESCRIPTOR event_security_descriptor;
    HANDLE key_handle;
    ULONG n_devices;
    NTSTATUS status;
    OBJECT_ATTRIBUTES object_attributes;
    ULONG n;

    KdBreakPoint();

    KeInitializeSpinLock(&DeviceListLock);

    KeInitializeSpinLock(&ReferencedObjectsListLock);

    InitializeListHead(&ReferencedObjects);

    // First open and read registry settings to find out if we should load and
    // mount anything automatically.
    parameter_path.Length = 0;

    parameter_path.MaximumLength = RegistryPath->Length +
        sizeof(IMDISK_CFG_PARAMETER_KEY);

    parameter_path.Buffer =
        (PWSTR)ExAllocatePoolWithTag(PagedPool, parameter_path.MaximumLength,
            POOL_TAG);

    if (parameter_path.Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyUnicodeString(&parameter_path, RegistryPath);

    RtlAppendUnicodeToString(&parameter_path, IMDISK_CFG_PARAMETER_KEY);

    InitializeObjectAttributes(&object_attributes, &parameter_path, 0, NULL,
        NULL);

    status = ZwOpenKey(&key_handle, KEY_READ, &object_attributes);
    if (!NT_SUCCESS(status))
        key_handle = NULL;

    ExFreePoolWithTag(parameter_path.Buffer, POOL_TAG);

    if (key_handle != NULL)
    {
        UNICODE_STRING value_name;
        PKEY_VALUE_PARTIAL_INFORMATION value_info;
        ULONG required_size;

        value_info = (PKEY_VALUE_PARTIAL_INFORMATION)
            ExAllocatePoolWithTag(PagedPool,
                sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
                sizeof(ULONG),
                POOL_TAG);

        if (value_info == NULL)
        {
            ZwClose(key_handle);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlInitUnicodeString(&value_name,
            IMDISK_CFG_DISALLOWED_DRIVE_LETTERS_VALUE);

        status = ZwQueryValueKey(key_handle, &value_name,
            KeyValuePartialInformation, value_info,
            sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
            sizeof(ULONG), &required_size);

        if (!NT_SUCCESS(status))
            KdPrint(("ImDisk: Using default value for '%ws'.\n",
                IMDISK_CFG_DISALLOWED_DRIVE_LETTERS_VALUE));
        else if (value_info->Type == REG_SZ)
        {
            PWSTR str = (PWSTR)value_info->Data;

            for (; *str != 0; str++)
            {
                // Ensure upper-case drive letter.
                *str &= ~0x20;

                if ((*str >= L'A') & (*str <= L'Z'))
                    DisallowedDriveLetters[*str - L'A'] = TRUE;
            }
        }
        else
        {
            ExFreePoolWithTag(value_info, POOL_TAG);
            ZwClose(key_handle);
            return STATUS_INVALID_PARAMETER;
        }

        RtlInitUnicodeString(&value_name,
            IMDISK_CFG_LOAD_DEVICES_VALUE);

        status = ZwQueryValueKey(key_handle, &value_name,
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
            n_devices = *(PULONG)value_info->Data;
        else
        {
            ExFreePoolWithTag(value_info, POOL_TAG);
            ZwClose(key_handle);
            return STATUS_INVALID_PARAMETER;
        }

        RtlInitUnicodeString(&value_name,
            IMDISK_CFG_MAX_DEVICES_VALUE);

        status = ZwQueryValueKey(key_handle, &value_name,
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
        {
            MaxDevices = *(PULONG)value_info->Data;
            if (MaxDevices > IMDISK_DEFAULT_MAX_DEVICES)
                MaxDevices = IMDISK_DEFAULT_MAX_DEVICES;
        }
        else
        {
            ExFreePoolWithTag(value_info, POOL_TAG);
            ZwClose(key_handle);
            return STATUS_INVALID_PARAMETER;
        }

        ExFreePoolWithTag(value_info, POOL_TAG);
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

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            status,
            101,
            status,
            0,
            0,
            NULL,
            L"Error creating device '"
            IMDISK_CTL_DEVICE_NAME
            L"'"));

        return status;
    }

    // The control device gets a device_number of -1 to make it easily
    // distinguishable from the actual created devices.
    ((PDEVICE_EXTENSION)ImDiskCtlDevice->DeviceExtension)->device_number =
        (ULONG)-1;

    RtlInitUnicodeString(&sym_link, IMDISK_CTL_SYMLINK_NAME);
    IoCreateUnprotectedSymbolicLink(&sym_link, &ctl_device_name);

    // If the registry settings told us to create devices here in the start
    // procedure, do that now.
    for (n = 0; n < n_devices; n++)
        ImDiskAddVirtualDiskAfterInitialization(DriverObject, key_handle, n);

    if (key_handle != NULL)
        ZwClose(key_handle);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = ImDiskCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = ImDiskCreateClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = ImDiskReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = ImDiskReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ImDiskDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = ImDiskDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = ImDiskDispatchPnP;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = ImDiskQueryInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION] = ImDiskSetInformation;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = ImDiskFlushBuffers;

    DriverObject->DriverUnload = ImDiskUnload;

    KdPrint(("ImDisk: Creating refresh event.\n"));

    RtlInitUnicodeString(&refresh_event_name, L"\\BaseNamedObjects\\Global\\"
        IMDISK_REFRESH_EVENT_NAME);

    event_security_descriptor =
        ExAllocatePoolWithTag(PagedPool, SECURITY_DESCRIPTOR_MIN_LENGTH, POOL_TAG);

    if (event_security_descriptor == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        KdPrint(("ImDisk: Memory allocation error for security descriptor.\n"));
    }

    if (NT_SUCCESS(status))
    {
        status = RtlCreateSecurityDescriptor(
            event_security_descriptor,
            SECURITY_DESCRIPTOR_REVISION);
    }

    if (NT_SUCCESS(status))
    {
#pragma warning(suppress: 6248)
        status = RtlSetDaclSecurityDescriptor(
            event_security_descriptor,
            TRUE,
            NULL,
            FALSE);
    }

    if (NT_SUCCESS(status))
    {
        HANDLE refresh_event_handle;

        InitializeObjectAttributes(&object_attributes,
            &refresh_event_name,
            OBJ_CASE_INSENSITIVE | OBJ_PERMANENT | OBJ_OPENIF,
            NULL,
            event_security_descriptor);

        status = ZwCreateEvent(&refresh_event_handle,
            EVENT_ALL_ACCESS,
            &object_attributes,
            SynchronizationEvent,
            FALSE);

        if (status == STATUS_OBJECT_PATH_NOT_FOUND)
        {
            KdPrint(("ImDisk: Error creating global refresh event (%#X)\n", status));

            RtlInitUnicodeString(&refresh_event_name, L"\\BaseNamedObjects\\"
                IMDISK_REFRESH_EVENT_NAME);

            status = ZwCreateEvent(&refresh_event_handle,
                EVENT_ALL_ACCESS,
                &object_attributes,
                SynchronizationEvent,
                FALSE);
        }

        if (NT_SUCCESS(status))
        {
            status = ObReferenceObjectByHandle(
                refresh_event_handle,
                EVENT_ALL_ACCESS,
                *ExEventObjectType,
                KernelMode,
                (PVOID*)&RefreshEvent,
                NULL);

            ZwClose(refresh_event_handle);
        }

        if (!NT_SUCCESS(status))
        {
            RefreshEvent = NULL;
            KdPrint(("ImDisk: Error creating refresh event (%#X)\n", status));
        }
    }

    if (event_security_descriptor != NULL)
    {
        ExFreePoolWithTag(event_security_descriptor, POOL_TAG);
    }

    KdPrint(("ImDisk: Initialization done. Leaving DriverEntry.\n"));

    return STATUS_SUCCESS;
}

NTSTATUS
ImDiskAddVirtualDiskAfterInitialization(IN PDRIVER_OBJECT DriverObject,
    IN HANDLE ParameterKey,
    IN ULONG DeviceNumber)
{
    NTSTATUS status;
    PDEVICE_THREAD_DATA device_thread_data;
    HANDLE thread_handle;
    LARGE_INTEGER wait_time;
    PKEY_VALUE_PARTIAL_INFORMATION value_info_image_file;
    PKEY_VALUE_PARTIAL_INFORMATION value_info_size;
    PKEY_VALUE_PARTIAL_INFORMATION value_info_flags;
    PKEY_VALUE_PARTIAL_INFORMATION value_info_drive_letter;
    PKEY_VALUE_PARTIAL_INFORMATION value_info_image_offset;
    ULONG required_size;
    PIMDISK_CREATE_DATA create_data;
    PWSTR value_name_buffer;
    UNICODE_STRING value_name;

    PAGED_CODE();

    ASSERT(DriverObject != NULL);
    ASSERT(ParameterKey != NULL);

    wait_time.QuadPart = -1;
    KeDelayExecutionThread(KernelMode, FALSE, &wait_time);

    // First, allocate all necessary value_info structures

    value_info_image_file = (PKEY_VALUE_PARTIAL_INFORMATION)
        ExAllocatePoolWithTag(PagedPool,
            sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
            (MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR)),
            POOL_TAG);

    if (value_info_image_file == NULL)
    {
        KdPrint(("ImDisk: Error creating device %u. (ExAllocatePoolWithTag)\n",
            DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INSUFFICIENT_RESOURCES,
            101,
            STATUS_INSUFFICIENT_RESOURCES,
            0,
            0,
            NULL,
            L"Error creating disk device."));

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    value_info_size = (PKEY_VALUE_PARTIAL_INFORMATION)
        ExAllocatePoolWithTag(PagedPool,
            sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
            sizeof(LARGE_INTEGER),
            POOL_TAG);

    if (value_info_size == NULL)
    {
        ExFreePoolWithTag(value_info_image_file, POOL_TAG);

        KdPrint(("ImDisk: Error creating device %u. (ExAllocatePoolWithTag)\n",
            DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INSUFFICIENT_RESOURCES,
            101,
            STATUS_INSUFFICIENT_RESOURCES,
            0,
            0,
            NULL,
            L"Error creating disk device."));

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    value_info_flags = (PKEY_VALUE_PARTIAL_INFORMATION)
        ExAllocatePoolWithTag(PagedPool,
            sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
            sizeof(ULONG),
            POOL_TAG);

    if (value_info_flags == NULL)
    {
        ExFreePoolWithTag(value_info_image_file, POOL_TAG);
        ExFreePoolWithTag(value_info_size, POOL_TAG);

        KdPrint(("ImDisk: Error creating device %u. (ExAllocatePoolWithTag)\n",
            DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INSUFFICIENT_RESOURCES,
            101,
            STATUS_INSUFFICIENT_RESOURCES,
            0,
            0,
            NULL,
            L"Error creating disk device."));

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    value_info_drive_letter = (PKEY_VALUE_PARTIAL_INFORMATION)
        ExAllocatePoolWithTag(PagedPool,
            sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
            sizeof(WCHAR),
            POOL_TAG);

    if (value_info_drive_letter == NULL)
    {
        ExFreePoolWithTag(value_info_image_file, POOL_TAG);
        ExFreePoolWithTag(value_info_size, POOL_TAG);
        ExFreePoolWithTag(value_info_flags, POOL_TAG);

        KdPrint(("ImDisk: Error creating device %u. (ExAllocatePoolWithTag)\n",
            DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INSUFFICIENT_RESOURCES,
            101,
            STATUS_INSUFFICIENT_RESOURCES,
            0,
            0,
            NULL,
            L"Error creating disk device."));

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    value_info_image_offset = (PKEY_VALUE_PARTIAL_INFORMATION)
        ExAllocatePoolWithTag(PagedPool,
            sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
            sizeof(LARGE_INTEGER),
            POOL_TAG);

    if (value_info_image_offset == NULL)
    {
        ExFreePoolWithTag(value_info_image_file, POOL_TAG);
        ExFreePoolWithTag(value_info_size, POOL_TAG);
        ExFreePoolWithTag(value_info_flags, POOL_TAG);
        ExFreePoolWithTag(value_info_drive_letter, POOL_TAG);

        KdPrint(("ImDisk: Error creating device %u. (ExAllocatePoolWithTag)\n",
            DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INSUFFICIENT_RESOURCES,
            101,
            STATUS_INSUFFICIENT_RESOURCES,
            0,
            0,
            NULL,
            L"Error creating disk device."));

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Allocate a buffer for used for value names

    value_name_buffer = (PWCHAR)
        ExAllocatePoolWithTag(PagedPool,
            MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR),
            POOL_TAG);

    if (value_name_buffer == NULL)
    {
        ExFreePoolWithTag(value_info_image_file, POOL_TAG);
        ExFreePoolWithTag(value_info_size, POOL_TAG);
        ExFreePoolWithTag(value_info_flags, POOL_TAG);
        ExFreePoolWithTag(value_info_drive_letter, POOL_TAG);
        ExFreePoolWithTag(value_info_image_offset, POOL_TAG);

        KdPrint(("ImDisk: Error creating device %u. (ExAllocatePoolWithTag)\n",
            DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INSUFFICIENT_RESOURCES,
            101,
            STATUS_INSUFFICIENT_RESOURCES,
            0,
            0,
            NULL,
            L"Error creating disk device."));

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Query each value

    _snwprintf(value_name_buffer, MAXIMUM_FILENAME_LENGTH - 1,
        IMDISK_CFG_IMAGE_FILE_PREFIX L"%u", DeviceNumber);
    value_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

    RtlInitUnicodeString(&value_name, value_name_buffer);

    status = ZwQueryValueKey(ParameterKey,
        &value_name,
        KeyValuePartialInformation,
        value_info_image_file,
        sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
        (MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR)),
        &required_size);

    if ((!NT_SUCCESS(status)) ||
        (value_info_image_file->Type != REG_SZ))
    {
        KdPrint(("ImDisk: Missing or bad '%ws' for device %i.\n",
            value_name_buffer, DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            status,
            101,
            status,
            0,
            0,
            NULL,
            L"Missing or bad '"
            IMDISK_CFG_IMAGE_FILE_PREFIX
            L"'"));

        *(PWCHAR)value_info_image_file->Data = 0;
        value_info_image_file->DataLength = sizeof(WCHAR);
    }

    _snwprintf(value_name_buffer, MAXIMUM_FILENAME_LENGTH - 1,
        IMDISK_CFG_SIZE_PREFIX L"%u", DeviceNumber);
    value_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

    RtlInitUnicodeString(&value_name, value_name_buffer);

    status = ZwQueryValueKey(ParameterKey,
        &value_name,
        KeyValuePartialInformation,
        value_info_size,
        sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
        sizeof(LARGE_INTEGER),
        &required_size);

    if ((!NT_SUCCESS(status)) ||
        ((value_info_size->Type != REG_BINARY) &
            (value_info_size->Type != REG_QWORD)) |
        (value_info_size->DataLength != sizeof(LARGE_INTEGER)))
    {
        KdPrint(("ImDisk: Missing or bad '%ws' for device %i.\n",
            value_name_buffer, DeviceNumber));

        ((PLARGE_INTEGER)value_info_size->Data)->QuadPart = 0;
    }

    _snwprintf(value_name_buffer, MAXIMUM_FILENAME_LENGTH - 1,
        IMDISK_CFG_FLAGS_PREFIX L"%u", DeviceNumber);
    value_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

    RtlInitUnicodeString(&value_name, value_name_buffer);

    status = ZwQueryValueKey(ParameterKey,
        &value_name,
        KeyValuePartialInformation,
        value_info_flags,
        sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
        sizeof(ULONG),
        &required_size);

    if ((!NT_SUCCESS(status)) ||
        (value_info_flags->Type != REG_DWORD))
    {
        KdPrint(("ImDisk: Missing or bad '%ws' for device %i.\n",
            value_name_buffer, DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            status,
            101,
            status,
            0,
            0,
            NULL,
            L"Missing or bad '"
            IMDISK_CFG_FLAGS_PREFIX
            L"'"));

        *(PULONG)value_info_flags->Data = 0;
    }

    _snwprintf(value_name_buffer, MAXIMUM_FILENAME_LENGTH - 1,
        IMDISK_CFG_DRIVE_LETTER_PREFIX L"%u", DeviceNumber);
    value_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

    RtlInitUnicodeString(&value_name, value_name_buffer);

    status = ZwQueryValueKey(ParameterKey,
        &value_name,
        KeyValuePartialInformation,
        value_info_drive_letter,
        sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
        sizeof(WCHAR),
        &required_size);

    if (!NT_SUCCESS(status))
    {
        KdPrint(("ImDisk: Missing or bad '%ws' for device %i.\n",
            value_name_buffer, DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            status,
            101,
            status,
            0,
            0,
            NULL,
            L"Missing or bad '"
            IMDISK_CFG_DRIVE_LETTER_PREFIX
            L"'"));

        *(PWCHAR)value_info_drive_letter->Data = 0;
    }

    _snwprintf(value_name_buffer, MAXIMUM_FILENAME_LENGTH - 1,
        IMDISK_CFG_OFFSET_PREFIX L"%u", DeviceNumber);
    value_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

    RtlInitUnicodeString(&value_name, value_name_buffer);

    status = ZwQueryValueKey(ParameterKey,
        &value_name,
        KeyValuePartialInformation,
        value_info_image_offset,
        sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
        sizeof(LARGE_INTEGER),
        &required_size);

    if ((!NT_SUCCESS(status)) ||
        ((value_info_image_offset->Type != REG_BINARY) &
            (value_info_image_offset->Type != REG_QWORD)) ||
        (value_info_image_offset->DataLength != sizeof(LARGE_INTEGER)))
    {
        KdPrint(("ImDisk: Missing or bad '%ws' for device %i.\n",
            value_name_buffer, DeviceNumber));

        ((PLARGE_INTEGER)value_info_image_offset->Data)->QuadPart = 0;
    }

    // Free the value name buffer

    ExFreePoolWithTag(value_name_buffer, POOL_TAG);

    // Allocate IMDISK_CREATE_DATA structure to use in later calls to create
    // functions

    create_data = (PIMDISK_CREATE_DATA)
        ExAllocatePoolWithTag(PagedPool,
            sizeof(IMDISK_CREATE_DATA) +
            value_info_image_file->DataLength,
            POOL_TAG);

    if (create_data == NULL)
    {
        ExFreePoolWithTag(value_info_image_file, POOL_TAG);
        ExFreePoolWithTag(value_info_size, POOL_TAG);
        ExFreePoolWithTag(value_info_flags, POOL_TAG);
        ExFreePoolWithTag(value_info_drive_letter, POOL_TAG);
        ExFreePoolWithTag(value_info_image_offset, POOL_TAG);

        KdPrint(("ImDisk: Error creating device %u. (ExAllocatePoolWithTag)\n",
            DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INSUFFICIENT_RESOURCES,
            101,
            STATUS_INSUFFICIENT_RESOURCES,
            0,
            0,
            NULL,
            L"Error creating disk device."));

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(create_data, sizeof(IMDISK_CREATE_DATA));

    create_data->FileNameLength = (USHORT)
        value_info_image_file->DataLength - sizeof(WCHAR);

    memcpy(create_data->FileName,
        value_info_image_file->Data,
        create_data->FileNameLength);

    ExFreePoolWithTag(value_info_image_file, POOL_TAG);

    create_data->DiskGeometry.Cylinders =
        *(PLARGE_INTEGER)value_info_size->Data;

    ExFreePoolWithTag(value_info_size, POOL_TAG);

    create_data->Flags = *(PULONG)value_info_flags->Data;

    ExFreePoolWithTag(value_info_flags, POOL_TAG);

    create_data->DriveLetter = *(PWCHAR)value_info_drive_letter->Data;

    ExFreePoolWithTag(value_info_drive_letter, POOL_TAG);

    create_data->ImageOffset = *(PLARGE_INTEGER)value_info_image_offset->Data;

    ExFreePoolWithTag(value_info_image_offset, POOL_TAG);

    create_data->DeviceNumber = DeviceNumber;

    device_thread_data = (PDEVICE_THREAD_DATA)
        ExAllocatePoolWithTag(PagedPool,
            sizeof(DEVICE_THREAD_DATA),
            POOL_TAG);

    if (device_thread_data == NULL)
    {
        ExFreePoolWithTag(create_data, POOL_TAG);

        KdPrint(("ImDisk: Error creating device %u. (ExAllocatePoolWithTag)\n",
            DeviceNumber));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INSUFFICIENT_RESOURCES,
            101,
            STATUS_INSUFFICIENT_RESOURCES,
            0,
            0,
            NULL,
            L"Error creating disk device."));

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    device_thread_data->driver_object = DriverObject;
    device_thread_data->create_data = create_data;
    device_thread_data->client_thread = NULL;
    device_thread_data->caller_waiting = FALSE;

    status = PsCreateSystemThread(&thread_handle,
        (ACCESS_MASK)0L,
        NULL,
        NULL,
        NULL,
        ImDiskDeviceThread,
        device_thread_data);

    if (!NT_SUCCESS(status))
    {
        ExFreePoolWithTag(device_thread_data, POOL_TAG);
        ExFreePoolWithTag(create_data, POOL_TAG);

        KdPrint(("ImDisk: Cannot create device thread. (%#x)\n", status));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            status,
            101,
            status,
            0,
            0,
            NULL,
            L"Error creating service thread"));

        return status;
    }

    ZwClose(thread_handle);

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

    // Check if drive letter is disallowed by registry setting
    if (CreateData->DriveLetter != 0)
    {
        // Ensure upper-case drive letter.
        WCHAR ucase_drive_letter = CreateData->DriveLetter & ~0x20;

        if ((ucase_drive_letter >= L'A') & (ucase_drive_letter <= L'Z'))
            if (DisallowedDriveLetters[ucase_drive_letter - L'A'])
                return STATUS_ACCESS_DENIED;
    }

    device_thread_data.driver_object = DriverObject;
    device_thread_data.create_data = CreateData;
    device_thread_data.client_thread = ClientThread;
    device_thread_data.caller_waiting = TRUE;
    KeInitializeEvent(&device_thread_data.created_event,
        NotificationEvent,
        FALSE);

    status = PsCreateSystemThread(&thread_handle,
        (ACCESS_MASK)0L,
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

    if (CreateData->DriveLetter != 0)
        if (KeGetCurrentIrql() == PASSIVE_LEVEL)
            ImDiskCreateDriveLetter(CreateData->DriveLetter,
                CreateData->DeviceNumber);

    return STATUS_SUCCESS;
}

NTSTATUS
ImDiskCreateDriveLetter(IN WCHAR DriveLetter,
    IN ULONG DeviceNumber)
{
    WCHAR sym_link_global_wchar[] = L"\\DosDevices\\Global\\ :";
#ifndef _WIN64
    WCHAR sym_link_wchar[] = L"\\DosDevices\\ :";
#endif
    UNICODE_STRING sym_link;
    PWCHAR device_name_buffer;
    UNICODE_STRING device_name;
    NTSTATUS status;

    PAGED_CODE();

    // Buffer for device name
    device_name_buffer = (PWCHAR)
        ExAllocatePoolWithTag(PagedPool,
            MAXIMUM_FILENAME_LENGTH *
            sizeof(*device_name_buffer),
            POOL_TAG);

    if (device_name_buffer == NULL)
    {
        KdPrint(("ImDisk: Insufficient pool memory.\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _snwprintf(device_name_buffer, MAXIMUM_FILENAME_LENGTH - 1,
        IMDISK_DEVICE_BASE_NAME L"%u",
        DeviceNumber);
    device_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;
    RtlInitUnicodeString(&device_name, device_name_buffer);

#ifndef _WIN64
    sym_link_wchar[12] = DriveLetter;

    KdPrint(("ImDisk: Creating symlink '%ws' -> '%ws'.\n",
        sym_link_wchar, device_name_buffer));

    RtlInitUnicodeString(&sym_link, sym_link_wchar);
    status = IoCreateUnprotectedSymbolicLink(&sym_link, &device_name);

    if (!NT_SUCCESS(status))
    {
        KdPrint(("ImDisk: Cannot symlink '%ws' to '%ws'. (%#x)\n",
            sym_link_global_wchar, device_name_buffer, status));
    }
#endif

    sym_link_global_wchar[19] = DriveLetter;

    KdPrint(("ImDisk: Creating symlink '%ws' -> '%ws'.\n",
        sym_link_global_wchar, device_name_buffer));

    RtlInitUnicodeString(&sym_link, sym_link_global_wchar);
    status = IoCreateUnprotectedSymbolicLink(&sym_link, &device_name);

    if (!NT_SUCCESS(status))
    {
        KdPrint(("ImDisk: Cannot symlink '%ws' to '%ws'. (%#x)\n",
            sym_link_global_wchar, device_name_buffer, status));
    }

    ExFreePoolWithTag(device_name_buffer, POOL_TAG);

    return status;
}

// Parses BPB formatted geometry to a DISK_GEOMETRY structure.
VOID
ImDiskReadFormattedGeometry(IN OUT PIMDISK_CREATE_DATA CreateData,
    IN PFAT_BPB BPB)
{
    USHORT tmp;

    PAGED_CODE();

    KdPrint
        (("ImDisk: Detected BPB values:\n"
            "Bytes per sect: %u\n"
            "Sectors per cl: %u\n"
            "Reserved sect : %u\n"
            "FAT count     : %u\n"
            "FAT root entr : %u\n"
            "Total sectors : %u\n"
            "Media descript: 0x%.2X\n"
            "Sectors pr FAT: %u\n"
            "Sect per track: %u\n"
            "Number of head: %u\n",
            (ULONG)BPB->BytesPerSector,
            (ULONG)BPB->SectorsPerCluster,
            (ULONG)BPB->ReservedSectors,
            (ULONG)BPB->NumberOfFileAllocationTables,
            (ULONG)BPB->NumberOfRootEntries,
            (ULONG)BPB->NumberOfSectors,
            (LONG)BPB->MediaDescriptor,
            (ULONG)BPB->SectorsPerFileAllocationTable,
            (ULONG)BPB->SectorsPerTrack,
            (ULONG)BPB->NumberOfHeads));

    // Some sanity checks. Could this really be valid BPB values? Bytes per
    // sector is multiple of 2 etc?
    if (BPB->BytesPerSector == 0)
        return;

    if (BPB->SectorsPerTrack >= 64)
        return;

    if (BPB->NumberOfHeads >= 256)
        return;

    tmp = BPB->BytesPerSector;
    while ((tmp & 0x0001) == 0)
        tmp >>= 1;
    if ((tmp ^ 0x0001) != 0)
        return;

    if (CreateData->DiskGeometry.SectorsPerTrack == 0)
        CreateData->DiskGeometry.SectorsPerTrack = BPB->SectorsPerTrack;

    if (CreateData->DiskGeometry.TracksPerCylinder == 0)
        CreateData->DiskGeometry.TracksPerCylinder = BPB->NumberOfHeads;

    if (CreateData->DiskGeometry.BytesPerSector == 0)
        CreateData->DiskGeometry.BytesPerSector = BPB->BytesPerSector;

#ifdef INCLUDE_VFD_ORIGIN

    if (((IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_FD) |
        (IMDISK_DEVICE_TYPE(CreateData->Flags) == 0)) &
        (CreateData->DiskGeometry.MediaType == Unknown))
        switch (CreateData->DiskGeometry.Cylinders.QuadPart)
        {
            // 3.5" formats
        case MEDIA_SIZE_240MB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_240M);
            break;

        case MEDIA_SIZE_120MB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_120M);
            break;

        case MEDIA_SIZE_2800KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_2880K);
            break;

        case MEDIA_SIZE_1722KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_1722K);
            break;

        case MEDIA_SIZE_1680KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_1680K);
            break;

        case MEDIA_SIZE_1440KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_1440K);
            break;

        case MEDIA_SIZE_820KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_820K);
            break;

        case MEDIA_SIZE_720KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_720K);
            break;

            // 5.25" formats
        case MEDIA_SIZE_1200KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_1200K);
            break;

        case MEDIA_SIZE_640KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_640K);
            break;

        case MEDIA_SIZE_360KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_360K);
            break;

        case MEDIA_SIZE_320KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_320K);
            break;

        case MEDIA_SIZE_180KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_180K);
            break;

        case MEDIA_SIZE_160KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            SET_MEDIA_TYPE(CreateData->DiskGeometry, MEDIA_TYPE_160K);
            break;
        }

#endif // INCLUDE_VFD_ORIGIN

    KdPrint
        (("ImDisk: Values after BPB geometry detection:\n"
            "DeviceNumber   = %#x\n"
            "DiskGeometry\n"
            "  .Cylinders   = 0x%.8x%.8x\n"
            "  .MediaType   = %i\n"
            "  .T/C         = %u\n"
            "  .S/T         = %u\n"
            "  .B/S         = %u\n"
            "Offset         = 0x%.8x%.8x\n"
            "Flags          = %#x\n"
            "FileNameLength = %u\n"
            "FileName       = '%.*ws'\n"
            "DriveLetter    = %wc\n",
            CreateData->DeviceNumber,
            CreateData->DiskGeometry.Cylinders.HighPart,
            CreateData->DiskGeometry.Cylinders.LowPart,
            CreateData->DiskGeometry.MediaType,
            CreateData->DiskGeometry.TracksPerCylinder,
            CreateData->DiskGeometry.SectorsPerTrack,
            CreateData->DiskGeometry.BytesPerSector,
            CreateData->ImageOffset.HighPart,
            CreateData->ImageOffset.LowPart,
            CreateData->Flags,
            CreateData->FileNameLength,
            (int)(CreateData->FileNameLength / sizeof(*CreateData->FileName)),
            CreateData->FileName,
            CreateData->DriveLetter ? CreateData->DriveLetter : L' '));
}

NTSTATUS
ImDiskGetDiskSize(IN HANDLE FileHandle,
    IN OUT PIO_STATUS_BLOCK IoStatus,
    IN OUT PLARGE_INTEGER DiskSize)
{
    NTSTATUS status;

    PAGED_CODE();

    {
        FILE_STANDARD_INFORMATION file_standard;

        status = ZwQueryInformationFile(FileHandle,
            IoStatus,
            &file_standard,
            sizeof(FILE_STANDARD_INFORMATION),
            FileStandardInformation);

        if (NT_SUCCESS(status))
        {
            *DiskSize = file_standard.EndOfFile;
            return status;
        }

        KdPrint(("ImDisk: FileStandardInformation not supported for "
            "target device. %#x\n", status));
    }

    // Retry with IOCTL_DISK_GET_LENGTH_INFO instead
    {
        GET_LENGTH_INFORMATION part_info = { 0 };

        status =
            ZwDeviceIoControlFile(FileHandle,
                NULL,
                NULL,
                NULL,
                IoStatus,
                IOCTL_DISK_GET_LENGTH_INFO,
                NULL,
                0,
                &part_info,
                sizeof(part_info));

        if (status == STATUS_PENDING)
        {
            ZwWaitForSingleObject(FileHandle, FALSE, NULL);
            status = IoStatus->Status;
        }

        if (NT_SUCCESS(status))
        {
            *DiskSize = part_info.Length;
            return status;
        }

        KdPrint(("ImDisk: IOCTL_DISK_GET_LENGTH_INFO not supported "
            "for target device. %#x\n", status));
    }

    // Retry with IOCTL_DISK_GET_PARTITION_INFO instead
    {
        PARTITION_INFORMATION part_info = { 0 };

        status =
            ZwDeviceIoControlFile(FileHandle,
                NULL,
                NULL,
                NULL,
                IoStatus,
                IOCTL_DISK_GET_PARTITION_INFO,
                NULL,
                0,
                &part_info,
                sizeof(part_info));

        if (status == STATUS_PENDING)
        {
            ZwWaitForSingleObject(FileHandle, FALSE, NULL);
            status = IoStatus->Status;
        }

        if (NT_SUCCESS(status))
        {
            *DiskSize = part_info.PartitionLength;
            return status;
        }

        KdPrint(("ImDisk: IOCTL_DISK_GET_PARTITION_INFO not supported "
            "for target device. %#x\n", status));
    }

    return status;
}

NTSTATUS
ImDiskCreateDevice(IN PDRIVER_OBJECT DriverObject,
    IN OUT PIMDISK_CREATE_DATA CreateData,
    IN PETHREAD ClientThread,
    OUT PDEVICE_OBJECT *DeviceObject)
{
    UNICODE_STRING file_name;
    PWCHAR device_name_buffer = NULL;
    UNICODE_STRING device_name;
    NTSTATUS status;
    PDEVICE_EXTENSION device_extension;
    BOOLEAN proxy_supports_unmap = FALSE;
    BOOLEAN proxy_supports_zero = FALSE;
    DEVICE_TYPE device_type;
    ULONG device_characteristics;
    HANDLE file_handle = NULL;
    PFILE_OBJECT file_object = NULL;
    PDEVICE_OBJECT dev_object = NULL;
    BOOLEAN parallel_io = FALSE;
    PUCHAR image_buffer = NULL;
    PROXY_CONNECTION proxy = { (PROXY_CONNECTION::PROXY_CONNECTION_TYPE)0 };
    ULONG alignment_requirement;
    CCHAR extra_stack_locations = 0;

    PAGED_CODE();

    ASSERT(CreateData != NULL);

    KdPrint
        (("ImDisk: Got request to create a virtual disk. Request data:\n"
            "DeviceNumber   = %#x\n"
            "DiskGeometry\n"
            "  .Cylinders   = 0x%.8x%.8x\n"
            "  .MediaType   = %i\n"
            "  .T/C         = %u\n"
            "  .S/T         = %u\n"
            "  .B/S         = %u\n"
            "Offset         = 0x%.8x%.8x\n"
            "Flags          = %#x\n"
            "FileNameLength = %u\n"
            "FileName       = '%.*ws'\n"
            "DriveLetter    = %wc\n",
            CreateData->DeviceNumber,
            CreateData->DiskGeometry.Cylinders.HighPart,
            CreateData->DiskGeometry.Cylinders.LowPart,
            CreateData->DiskGeometry.MediaType,
            CreateData->DiskGeometry.TracksPerCylinder,
            CreateData->DiskGeometry.SectorsPerTrack,
            CreateData->DiskGeometry.BytesPerSector,
            CreateData->ImageOffset.HighPart,
            CreateData->ImageOffset.LowPart,
            CreateData->Flags,
            CreateData->FileNameLength,
            (int)(CreateData->FileNameLength / sizeof(*CreateData->FileName)),
            CreateData->FileName,
            CreateData->DriveLetter ? CreateData->DriveLetter : L' '));

    // Auto-select type if not specified.
    if (IMDISK_TYPE(CreateData->Flags) == 0)
        if (CreateData->FileNameLength == 0)
            CreateData->Flags |= IMDISK_TYPE_VM;
        else
            CreateData->Flags |= IMDISK_TYPE_FILE;

    // Blank filenames only supported for memory disks where size is specified.
    if ((CreateData->FileNameLength == 0) &&
        !(((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM) &
            (CreateData->DiskGeometry.Cylinders.QuadPart > 65536)) |
            ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &
                (IMDISK_FILE_TYPE(CreateData->Flags) == IMDISK_FILE_TYPE_AWEALLOC) &
                (CreateData->DiskGeometry.Cylinders.QuadPart > 65536))))
    {
        KdPrint(("ImDisk: Blank filenames only supported for memory disks where "
            "size is specified..\n"));

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INVALID_PARAMETER,
            102,
            STATUS_INVALID_PARAMETER,
            0,
            0,
            NULL,
            L"Blank filenames only supported for non-zero length "
            L"vm type disks."));

        return STATUS_INVALID_PARAMETER;
    }

#ifndef _WIN64
    // Cannot create >= 2 GB VM disk in 32 bit version.
    if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM) &
        ((CreateData->DiskGeometry.Cylinders.QuadPart & 0xFFFFFFFF80000000) !=
            0))
    {
        KdPrint(("ImDisk: Cannot create >= 2GB vm disks on 32-bit system.\n"));

        return STATUS_INVALID_PARAMETER;
    }
#endif

    // Auto-find first free device number
    if (CreateData->DeviceNumber == IMDISK_AUTO_DEVICE_NUMBER)
    {
        ImDiskFindFreeDeviceNumber(DriverObject, &CreateData->DeviceNumber);

        KdPrint(("ImDisk: Free device number %i.\n", CreateData->DeviceNumber));
    }

    /*     for (CreateData->DeviceNumber = 0; */
    /* 	 CreateData->DeviceNumber < MaxDevices; */
    /* 	 CreateData->DeviceNumber++) */
    /*       if ((~DeviceList) & (1ULL << CreateData->DeviceNumber)) */
    /* 	break; */

    if (CreateData->DeviceNumber >= MaxDevices)
    {
        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INVALID_PARAMETER,
            102,
            STATUS_INVALID_PARAMETER,
            0,
            0,
            NULL,
            L"Device number too high."));

        return STATUS_INVALID_PARAMETER;
    }

    if (IMDISK_BYTE_SWAP(CreateData->Flags) &&
        (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &&
        (IMDISK_FILE_TYPE(CreateData->Flags) == IMDISK_FILE_TYPE_PARALLEL_IO))
    {
        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            STATUS_INVALID_PARAMETER,
            102,
            STATUS_INVALID_PARAMETER,
            0,
            0,
            NULL,
            L"Byte swapping not supported together with parallel I/O."));

        return STATUS_INVALID_PARAMETER;
    }

    file_name.Length = CreateData->FileNameLength;
    file_name.MaximumLength = CreateData->FileNameLength;
    file_name.Buffer = NULL;

    // If a file is to be opened or created, allocate name buffer and open that
    // file...
    if ((CreateData->FileNameLength > 0) |
        ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &
            (IMDISK_FILE_TYPE(CreateData->Flags) == IMDISK_FILE_TYPE_AWEALLOC)))
    {
        IO_STATUS_BLOCK io_status;
        OBJECT_ATTRIBUTES object_attributes;
        UNICODE_STRING real_file_name;
        ACCESS_MASK desired_access = 0;
        ULONG share_access = 0;
        ULONG create_options = 0;

        if (file_name.MaximumLength > 0)
        {
            file_name.Buffer = (PWCHAR)
                ExAllocatePoolWithTag(NonPagedPool,
                    file_name.MaximumLength,
                    POOL_TAG);

            if (file_name.Buffer == NULL)
            {
                KdPrint(("ImDisk: Error allocating buffer for filename.\n"));

                ImDiskLogError((DriverObject,
                    0,
                    0,
                    NULL,
                    0,
                    1000,
                    STATUS_INSUFFICIENT_RESOURCES,
                    102,
                    STATUS_INSUFFICIENT_RESOURCES,
                    0,
                    0,
                    NULL,
                    L"Memory allocation error."));

                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlCopyMemory(file_name.Buffer, CreateData->FileName,
                CreateData->FileNameLength);
        }

        // If no device-type specified, check if filename ends with .iso, .nrg or
        // .bin. In that case, set device-type automatically to FILE_DEVICE_CDROM
        if ((IMDISK_DEVICE_TYPE(CreateData->Flags) == 0) &
            (CreateData->FileNameLength >= (4 * sizeof(*CreateData->FileName))))
        {
            LPWSTR name = CreateData->FileName +
                (CreateData->FileNameLength / sizeof(*CreateData->FileName)) - 4;
            if ((_wcsnicmp(name, L".iso", 4) == 0) |
                (_wcsnicmp(name, L".nrg", 4) == 0) |
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

            SeDeleteClientSecurity(&security_client_context);
        }
        else
            KdPrint(("ImDisk: No impersonation information.\n"));

        if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &&
            (IMDISK_FILE_TYPE(CreateData->Flags) == IMDISK_FILE_TYPE_AWEALLOC))
        {
            real_file_name.MaximumLength = file_name.Length +
                sizeof(AWEALLOC_DEVICE_NAME);

            real_file_name.Buffer = (PWCHAR)
                ExAllocatePoolWithTag(PagedPool,
                    real_file_name.MaximumLength,
                    POOL_TAG);

            if (real_file_name.Buffer == NULL)
            {
                KdPrint(("ImDisk: Out of memory while allocating %#x bytes\n",
                    real_file_name.MaximumLength));

                ExFreePoolWithTag(file_name.Buffer, POOL_TAG);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            real_file_name.Length = 0;

            status =
                RtlAppendUnicodeToString(&real_file_name,
                    AWEALLOC_DEVICE_NAME);

            if (NT_SUCCESS(status))
                status =
                RtlAppendUnicodeStringToString(&real_file_name,
                    &file_name);

            if (!NT_SUCCESS(status))
            {
                KdPrint(("ImDisk: Internal error: "
                    "RtlAppendUnicodeStringToString failed with "
                    "pre-allocated buffers.\n"));

                ExFreePoolWithTag(file_name.Buffer, POOL_TAG);
                ExFreePoolWithTag(real_file_name.Buffer, POOL_TAG);
                return STATUS_DRIVER_INTERNAL_ERROR;
            }

            InitializeObjectAttributes(&object_attributes,
                &real_file_name,
                OBJ_CASE_INSENSITIVE |
                OBJ_FORCE_ACCESS_CHECK,
                NULL,
                NULL);
        }
        else if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY) &
            ((IMDISK_PROXY_TYPE(CreateData->Flags) ==
                IMDISK_PROXY_TYPE_TCP) |
                (IMDISK_PROXY_TYPE(CreateData->Flags) ==
                    IMDISK_PROXY_TYPE_COMM)))
        {
            RtlInitUnicodeString(&real_file_name, IMDPROXY_SVC_PIPE_NATIVE_NAME);

            InitializeObjectAttributes(&object_attributes,
                &real_file_name,
                OBJ_CASE_INSENSITIVE,
                NULL,
                NULL);
        }
        else
        {
            real_file_name = file_name;

            InitializeObjectAttributes(&object_attributes,
                &real_file_name,
                OBJ_CASE_INSENSITIVE |
                OBJ_FORCE_ACCESS_CHECK,
                NULL,
                NULL);
        }

        if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY) &&
            (IMDISK_PROXY_TYPE(CreateData->Flags) == IMDISK_PROXY_TYPE_SHM))
        {
            proxy.connection_type = PROXY_CONNECTION::PROXY_CONNECTION_SHM;

            status =
                ZwOpenSection(&file_handle,
                    GENERIC_READ | GENERIC_WRITE,
                    &object_attributes);
        }
        else
        {
            desired_access = GENERIC_READ;

            if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY) ||
                ((IMDISK_TYPE(CreateData->Flags) != IMDISK_TYPE_VM) &&
                    !IMDISK_READONLY(CreateData->Flags)))
            {
                desired_access |= GENERIC_WRITE;
            }

            share_access = FILE_SHARE_READ | FILE_SHARE_DELETE;

            if (IMDISK_READONLY(CreateData->Flags) ||
                (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM) ||
                IMDISK_SHARED_IMAGE(CreateData->Flags))
            {
                share_access |= FILE_SHARE_WRITE;
            }

            create_options = FILE_NON_DIRECTORY_FILE |
                FILE_NO_INTERMEDIATE_BUFFERING |
                FILE_SYNCHRONOUS_IO_NONALERT;

            if (IMDISK_SPARSE_FILE(CreateData->Flags))
            {
                create_options |= FILE_OPEN_FOR_BACKUP_INTENT;
            }

            if (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY)
            {
                create_options |= FILE_SEQUENTIAL_ONLY;
            }
            else
            {
                create_options |= FILE_RANDOM_ACCESS;
            }

            KdPrint(("ImDisk: Passing DesiredAccess=%#x ShareAccess=%#x CreateOptions=%#x\n",
                desired_access, share_access, create_options));

            status = ZwCreateFile(
                &file_handle,
                desired_access,
                &object_attributes,
                &io_status,
                NULL,
                FILE_ATTRIBUTE_NORMAL,
                share_access,
                FILE_OPEN,
                create_options,
                NULL,
                0);
        }

        // For 32 bit driver running on Windows 2000 and earlier, the above
        // call will fail because OBJ_FORCE_ACCESS_CHECK is not supported. If so,
        // STATUS_INVALID_PARAMETER is returned and we go on without any access
        // checks in that case.
#ifndef _WIN64
        if (status == STATUS_INVALID_PARAMETER)
        {
            InitializeObjectAttributes(&object_attributes,
                &real_file_name,
                OBJ_CASE_INSENSITIVE,
                NULL,
                NULL);

            if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY) &&
                (IMDISK_PROXY_TYPE(CreateData->Flags) == IMDISK_PROXY_TYPE_SHM))
            {
                proxy.connection_type = PROXY_CONNECTION::PROXY_CONNECTION_SHM;

                status =
                    ZwOpenSection(&file_handle,
                        GENERIC_READ | GENERIC_WRITE,
                        &object_attributes);
            }
            else
            {
                status = ZwCreateFile(
                    &file_handle,
                    desired_access,
                    &object_attributes,
                    &io_status,
                    NULL,
                    FILE_ATTRIBUTE_NORMAL,
                    share_access,
                    FILE_OPEN,
                    create_options,
                    NULL,
                    0);
            }
        }
#endif

        if (!NT_SUCCESS(status))
        {
            KdPrint(("ImDisk: Error opening file '%wZ'. "
                "Status: %#x "
                "SpecSize: %i "
                "WritableFile: %i "
                "DevTypeFile: %i "
                "Flags: %#x\n"
                "FileNameLength: %#x\n",
                &real_file_name,
                status,
                CreateData->DiskGeometry.Cylinders.QuadPart != 0,
                !IMDISK_READONLY(CreateData->Flags),
                IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE,
                CreateData->Flags,
                (int)real_file_name.Length));
        }

        // If not found we will create the file if a new non-zero size is
        // specified, read-only virtual disk is not specified and we are
        // creating a type 'file' virtual disk.
        if (((status == STATUS_OBJECT_NAME_NOT_FOUND) |
            (status == STATUS_NO_SUCH_FILE)) &
            (CreateData->DiskGeometry.Cylinders.QuadPart != 0) &
            (!IMDISK_READONLY(CreateData->Flags)) &
            (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE))
        {
            KdPrint(("ImDisk: Creating new image file ShareAccess=%#x CreateOptions=%#x\n",
                desired_access, share_access, create_options));

            status =
                ZwCreateFile(&file_handle,
                    GENERIC_READ |
                    GENERIC_WRITE,
                    &object_attributes,
                    &io_status,
                    NULL,
                    FILE_ATTRIBUTE_NORMAL,
                    share_access,
                    FILE_OPEN_IF,
                    create_options, NULL, 0);

            if (!NT_SUCCESS(status))
            {
                ImDiskLogError((DriverObject,
                    0,
                    0,
                    NULL,
                    0,
                    1000,
                    status,
                    102,
                    status,
                    0,
                    0,
                    NULL,
                    L"Cannot create image file."));

                KdPrint(("ImDisk: Cannot create '%.*ws'. (%#x)\n",
                    (int)(CreateData->FileNameLength /
                        sizeof(*CreateData->FileName)),
                    CreateData->FileName,
                    status));

                if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &
                    (IMDISK_FILE_TYPE(CreateData->Flags) ==
                        IMDISK_FILE_TYPE_AWEALLOC))
                    ExFreePoolWithTag(real_file_name.Buffer, POOL_TAG);

                ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                return status;
            }
        }
        else if (!NT_SUCCESS(status))
        {
            ImDiskLogError((DriverObject,
                0,
                0,
                NULL,
                0,
                1000,
                status,
                102,
                status,
                0,
                0,
                NULL,
                L"Cannot open image file."));

            KdPrint(("ImDisk: Cannot open file '%wZ'. Status: %#x\n",
                &real_file_name,
                status));

            if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &
                (IMDISK_FILE_TYPE(CreateData->Flags) ==
                    IMDISK_FILE_TYPE_AWEALLOC))
                ExFreePoolWithTag(real_file_name.Buffer, POOL_TAG);

            ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

            return status;
        }

        KdPrint(("ImDisk: File '%wZ' opened successfully.\n",
            &real_file_name));

        if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &
            (IMDISK_FILE_TYPE(CreateData->Flags) == IMDISK_FILE_TYPE_AWEALLOC))
            ExFreePoolWithTag(real_file_name.Buffer, POOL_TAG);

        if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &
            (!IMDISK_READONLY(CreateData->Flags)))
        {
            // If creating a sparse image file
            if (IMDISK_SPARSE_FILE(CreateData->Flags))
            {
                status = ZwFsControlFile(file_handle,
                    NULL,
                    NULL,
                    NULL,
                    &io_status,
                    FSCTL_SET_SPARSE,
                    NULL,
                    0,
                    NULL,
                    0);

                if (NT_SUCCESS(status))
                    KdPrint(("ImDisk: Sparse attribute set on image file.\n"));
                else
                {
                    DbgPrint("ImDisk: Cannot set sparse attribute "
                        "on image file: %X\n", status);

                    CreateData->Flags &= ~IMDISK_OPTION_SPARSE_FILE;
                }
            }

            // Adjust the file length to the requested virtual disk size.
            if ((CreateData->DiskGeometry.Cylinders.QuadPart != 0) &
                (CreateData->ImageOffset.QuadPart == 0))
            {
                status = ZwSetInformationFile(
                    file_handle,
                    &io_status,
                    &CreateData->DiskGeometry.Cylinders,
                    sizeof
                    (FILE_END_OF_FILE_INFORMATION),
                    FileEndOfFileInformation);

                if (NT_SUCCESS(status))
                {
                    KdPrint(("ImDisk: Image file size adjusted.\n"));
                }
                else
                {
                    ZwClose(file_handle);
                    ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                    ImDiskLogError((DriverObject,
                        0,
                        0,
                        NULL,
                        0,
                        1000,
                        status,
                        102,
                        status,
                        0,
                        0,
                        NULL,
                        L"Error setting file size."));

                    KdPrint(("ImDisk: Error setting eof (%#x).\n", status));
                    return status;
                }
            }
        }

        if (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY)
        {
            if (IMDISK_PROXY_TYPE(CreateData->Flags) == IMDISK_PROXY_TYPE_SHM)
            {
                status = ZwMapViewOfSection(
                    file_handle,
                    NtCurrentProcess(),
                    (PVOID*)&proxy.shared_memory,
                    0,
                    0,
                    NULL,
                    &proxy.shared_memory_size,
                    ViewUnmap,
                    0,
                    PAGE_READWRITE);
            }
            else
            {
                status = ObReferenceObjectByHandle(
                    file_handle,
                    FILE_READ_ATTRIBUTES |
                    FILE_READ_DATA |
                    FILE_WRITE_DATA,
                    *IoFileObjectType,
                    KernelMode,
                    (PVOID*)&proxy.device,
                    NULL);
            }

            if (!NT_SUCCESS(status))
            {
                ZwClose(file_handle);
                ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                ImDiskLogError((DriverObject,
                    0,
                    0,
                    NULL,
                    0,
                    1000,
                    status,
                    102,
                    status,
                    0,
                    0,
                    NULL,
                    L"Error referencing proxy device."));

                KdPrint(("ImDisk: Error referencing proxy device (%#x).\n",
                    status));

                return status;
            }

            KdPrint(("ImDisk: Got reference to proxy object %p.\n",
                proxy.connection_type == PROXY_CONNECTION::
                PROXY_CONNECTION_TYPE::PROXY_CONNECTION_DEVICE ?
                (PVOID)proxy.device :
                (PVOID)proxy.shared_memory));

            if (IMDISK_PROXY_TYPE(CreateData->Flags) != IMDISK_PROXY_TYPE_DIRECT)
            {
                status = ImDiskConnectProxy(&proxy,
                    &io_status,
                    NULL,
                    CreateData->Flags,
                    CreateData->FileName,
                    CreateData->FileNameLength);
            }

            if (!NT_SUCCESS(status))
            {
                ImDiskCloseProxy(&proxy);
                ZwClose(file_handle);
                ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                ImDiskLogError((DriverObject,
                    0,
                    0,
                    NULL,
                    0,
                    1000,
                    status,
                    102,
                    status,
                    0,
                    0,
                    NULL,
                    L"Error connecting proxy."));

                KdPrint(("ImDisk: Error connecting proxy (%#x).\n", status));

                return status;
            }
        }

        // Get the file size of the disk file.
        if (IMDISK_TYPE(CreateData->Flags) != IMDISK_TYPE_PROXY)
        {
            LARGE_INTEGER disk_size;

            status =
                ImDiskGetDiskSize(file_handle,
                    &io_status,
                    &disk_size);

            if (!NT_SUCCESS(status))
            {
                ZwClose(file_handle);
                ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                ImDiskLogError((DriverObject,
                    0,
                    0,
                    NULL,
                    0,
                    1000,
                    status,
                    102,
                    status,
                    0,
                    0,
                    NULL,
                    L"Error getting image size."));

                KdPrint
                    (("ImDisk: Error getting image size (%#x).\n",
                        status));

                return status;
            }

            // Allocate virtual memory for 'vm' type.
            if (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM)
            {
                SIZE_T max_size;

                // If no size given for VM disk, use size of pre-load image file.
                // This code is somewhat easier for 64 bit architectures.

#ifdef _WIN64
                if (CreateData->DiskGeometry.Cylinders.QuadPart == 0)
                    CreateData->DiskGeometry.Cylinders.QuadPart =
                    disk_size.QuadPart -
                    CreateData->ImageOffset.QuadPart;

                max_size = CreateData->DiskGeometry.Cylinders.QuadPart;
#else
                if (CreateData->DiskGeometry.Cylinders.QuadPart == 0)
                    // Check that file size < 2 GB.
                    if ((disk_size.QuadPart -
                        CreateData->ImageOffset.QuadPart) & 0xFFFFFFFF80000000)
                    {
                        ZwClose(file_handle);
                        ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                        KdPrint(("ImDisk: VM disk >= 2GB not supported.\n"));

                        return STATUS_INSUFFICIENT_RESOURCES;
                    }
                    else
                        CreateData->DiskGeometry.Cylinders.QuadPart =
                        disk_size.QuadPart -
                        CreateData->ImageOffset.QuadPart;

                max_size = CreateData->DiskGeometry.Cylinders.LowPart;
#endif

                status =
                    ZwAllocateVirtualMemory(NtCurrentProcess(),
                        (PVOID*)&image_buffer,
                        0,
                        &max_size,
                        MEM_COMMIT,
                        PAGE_READWRITE);
                if (!NT_SUCCESS(status))
                {
                    ZwClose(file_handle);
                    ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                    ImDiskLogError((DriverObject,
                        0,
                        0,
                        NULL,
                        0,
                        1000,
                        status,
                        102,
                        status,
                        0,
                        0,
                        NULL,
                        L"Not enough memory for VM disk."));

                    KdPrint(("ImDisk: Error allocating vm for image. (%#x)\n",
                        status));

                    return STATUS_NO_MEMORY;
                }

                alignment_requirement = FILE_BYTE_ALIGNMENT;

                // Loading of image file has been moved to be done just before
                // the service loop.
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
                    ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                    ImDiskLogError((DriverObject,
                        0,
                        0,
                        NULL,
                        0,
                        1000,
                        status,
                        102,
                        status,
                        0,
                        0,
                        NULL,
                        L"Error getting alignment information."));

                    KdPrint(("ImDisk: Error querying file alignment (%#x).\n",
                        status));

                    return status;
                }

                if (CreateData->DiskGeometry.Cylinders.QuadPart == 0)
                    CreateData->DiskGeometry.Cylinders.QuadPart =
                    disk_size.QuadPart -
                    CreateData->ImageOffset.QuadPart;

                alignment_requirement = file_alignment.AlignmentRequirement;
            }

            if ((CreateData->DiskGeometry.TracksPerCylinder == 0) |
                (CreateData->DiskGeometry.SectorsPerTrack == 0) |
                (CreateData->DiskGeometry.BytesPerSector == 0))
            {
                SIZE_T free_size = 0;
                PFAT_VBR fat_vbr = (PFAT_VBR)
                    ExAllocatePoolWithTag(PagedPool,
                        sizeof(FAT_VBR),
                        POOL_TAG);

                if (fat_vbr == NULL)
                {
                    if (file_handle != NULL)
                        ZwClose(file_handle);
                    if (file_name.Buffer != NULL)
                        ExFreePoolWithTag(file_name.Buffer, POOL_TAG);
                    if (image_buffer != NULL)
                        ZwFreeVirtualMemory(NtCurrentProcess(),
                            (PVOID*)&image_buffer,
                            &free_size, MEM_RELEASE);

                    ImDiskLogError((DriverObject,
                        0,
                        0,
                        NULL,
                        0,
                        1000,
                        status,
                        102,
                        status,
                        0,
                        0,
                        NULL,
                        L"Insufficient memory."));

                    KdPrint(("ImDisk: Error allocating memory.\n"));

                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                status =
                    ZwReadFile(file_handle,
                        NULL,
                        NULL,
                        NULL,
                        &io_status,
                        fat_vbr,
                        sizeof(FAT_VBR),
                        &CreateData->ImageOffset,
                        NULL);

                if (NT_SUCCESS(status))
                {
                    ImDiskReadFormattedGeometry(CreateData, &fat_vbr->BPB);
                }
                else
                {
                    ImDiskLogError((DriverObject,
                        0,
                        0,
                        NULL,
                        0,
                        1000,
                        status,
                        102,
                        status,
                        0,
                        0,
                        NULL,
                        L"Error reading first sector."));

                    KdPrint(("ImDisk: Error reading first sector (%#x).\n",
                        status));
                }

                ExFreePoolWithTag(fat_vbr, POOL_TAG);
            }
        }
        else
            // If proxy is used, get the image file size from the proxy instead.
        {
            IMDPROXY_INFO_RESP proxy_info;

            status = ImDiskQueryInformationProxy(&proxy,
                &io_status,
                NULL,
                &proxy_info,
                sizeof(IMDPROXY_INFO_RESP));

            if (!NT_SUCCESS(status))
            {
                ImDiskCloseProxy(&proxy);
                ZwClose(file_handle);
                ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                ImDiskLogError((DriverObject,
                    0,
                    0,
                    NULL,
                    0,
                    1000,
                    status,
                    102,
                    status,
                    0,
                    0,
                    NULL,
                    L"Error querying proxy."));

                KdPrint(("ImDisk: Error querying proxy (%#x).\n", status));

                return status;
            }

            if (CreateData->DiskGeometry.Cylinders.QuadPart == 0)
                CreateData->DiskGeometry.Cylinders.QuadPart = proxy_info.file_size;

            if ((CreateData->DiskGeometry.TracksPerCylinder == 0) |
                (CreateData->DiskGeometry.SectorsPerTrack == 0) |
                (CreateData->DiskGeometry.BytesPerSector == 0))
            {
                PFAT_VBR fat_vbr = (PFAT_VBR)
                    ExAllocatePoolWithTag(PagedPool,
                        sizeof(FAT_VBR),
                        POOL_TAG);

                if (fat_vbr == NULL)
                {
                    ImDiskCloseProxy(&proxy);
                    ZwClose(file_handle);
                    ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                    ImDiskLogError((DriverObject,
                        0,
                        0,
                        NULL,
                        0,
                        1000,
                        status,
                        102,
                        status,
                        0,
                        0,
                        NULL,
                        L"Insufficient memory."));

                    KdPrint(("ImDisk: Error allocating memory.\n"));

                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                status = ImDiskReadProxy(&proxy,
                    &io_status,
                    NULL,
                    fat_vbr,
                    sizeof(FAT_VBR),
                    &CreateData->ImageOffset);

                if (!NT_SUCCESS(status))
                {
                    ImDiskCloseProxy(&proxy);
                    ZwClose(file_handle);
                    ExFreePoolWithTag(file_name.Buffer, POOL_TAG);
                    ExFreePoolWithTag(fat_vbr, POOL_TAG);

                    ImDiskLogError((DriverObject,
                        0,
                        0,
                        NULL,
                        0,
                        1000,
                        status,
                        102,
                        status,
                        0,
                        0,
                        NULL,
                        L"Error reading first sector."));

                    KdPrint(("ImDisk: Error reading first sector (%#x).\n",
                        status));

                    return status;
                }

                ImDiskReadFormattedGeometry(CreateData, &fat_vbr->BPB);

                ExFreePoolWithTag(fat_vbr, POOL_TAG);
            }

            if ((proxy_info.req_alignment - 1 > FILE_512_BYTE_ALIGNMENT) |
                (CreateData->DiskGeometry.Cylinders.QuadPart == 0))
            {
                ImDiskCloseProxy(&proxy);
                ZwClose(file_handle);
                ExFreePoolWithTag(file_name.Buffer, POOL_TAG);

                ImDiskLogError((DriverObject,
                    0,
                    0,
                    NULL,
                    0,
                    1000,
                    status,
                    102,
                    status,
                    0,
                    0,
                    NULL,
                    L"Unsupported sizes."));

#pragma warning(suppress: 6064)
#pragma warning(suppress: 6328)
                KdPrint(("ImDisk: Unsupported sizes. "
                    "Got 0x%.8x%.8x size and 0x%.8x%.8x alignment.\n",
                    proxy_info.file_size,
                    proxy_info.req_alignment));

                return STATUS_INVALID_PARAMETER;
            }

            alignment_requirement = (ULONG)proxy_info.req_alignment - 1;

            if (proxy_info.flags & IMDPROXY_FLAG_RO)
                CreateData->Flags |= IMDISK_OPTION_RO;

            if (proxy_info.flags & IMDPROXY_FLAG_SUPPORTS_UNMAP)
                proxy_supports_unmap = TRUE;

            if (proxy_info.flags & IMDPROXY_FLAG_SUPPORTS_ZERO)
                proxy_supports_zero = TRUE;

            KdPrint(("ImDisk: Got from proxy: Siz=0x%.8x%.8x Flg=%#x Alg=%#x.\n",
                CreateData->DiskGeometry.Cylinders.HighPart,
                CreateData->DiskGeometry.Cylinders.LowPart,
                (ULONG)proxy_info.flags,
                (ULONG)proxy_info.req_alignment));
        }

        if (CreateData->DiskGeometry.Cylinders.QuadPart <= 65536)
        {
            SIZE_T free_size = 0;

            ImDiskLogError((DriverObject,
                0,
                0,
                NULL,
                0,
                1000,
                status,
                102,
                status,
                0,
                0,
                NULL,
                L"Number of cylinders equals zero."));

            KdPrint(("ImDisk: Fatal error: Number of cylinders equals zero.\n"));

            ImDiskCloseProxy(&proxy);
            if (file_handle != NULL)
                ZwClose(file_handle);
            if (file_name.Buffer != NULL)
                ExFreePoolWithTag(file_name.Buffer, POOL_TAG);
            if (image_buffer != NULL)
                ZwFreeVirtualMemory(NtCurrentProcess(),
                    (PVOID*)&image_buffer,
                    &free_size, MEM_RELEASE);

            return STATUS_INVALID_PARAMETER;
        }
    }
    // Blank vm-disk, just allocate...
    else
    {
        SIZE_T max_size;
#ifdef _WIN64
        max_size = CreateData->DiskGeometry.Cylinders.QuadPart;
#else
        max_size = CreateData->DiskGeometry.Cylinders.LowPart;
#endif

        image_buffer = NULL;
        status =
            ZwAllocateVirtualMemory(NtCurrentProcess(),
                (PVOID*)&image_buffer,
                0,
                &max_size,
                MEM_COMMIT,
                PAGE_READWRITE);
        if (!NT_SUCCESS(status))
        {
            KdPrint
                (("ImDisk: Error allocating virtual memory for vm disk (%#x).\n",
                    status));

            ImDiskLogError((DriverObject,
                0,
                0,
                NULL,
                0,
                1000,
                status,
                102,
                status,
                0,
                0,
                NULL,
                L"Not enough free memory for VM disk."));

            return STATUS_NO_MEMORY;
        }

        alignment_requirement = FILE_BYTE_ALIGNMENT;
    }

    KdPrint(("ImDisk: Done with file/memory checks.\n"));

#ifdef INCLUDE_VFD_ORIGIN

    // If no device-type specified and size matches common floppy sizes,
    // auto-select FILE_DEVICE_DISK with FILE_FLOPPY_DISKETTE and
    // FILE_REMOVABLE_MEDIA.
    // If still no device-type specified, specify FILE_DEVICE_DISK with no
    // particular characteristics. This will emulate a hard disk partition.
    if (IMDISK_DEVICE_TYPE(CreateData->Flags) == 0)
        switch (CreateData->DiskGeometry.Cylinders.QuadPart)
        {
        case MEDIA_SIZE_240MB:
        case MEDIA_SIZE_120MB:
        case MEDIA_SIZE_2800KB:
        case MEDIA_SIZE_1722KB:
        case MEDIA_SIZE_1680KB:
        case MEDIA_SIZE_1440KB:
        case MEDIA_SIZE_820KB:
        case MEDIA_SIZE_720KB:
        case MEDIA_SIZE_1200KB:
        case MEDIA_SIZE_640KB:
        case MEDIA_SIZE_360KB:
        case MEDIA_SIZE_320KB:
        case MEDIA_SIZE_180KB:
        case MEDIA_SIZE_160KB:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_FD;
            break;

        default:
            CreateData->Flags |= IMDISK_DEVICE_TYPE_HD;
        }

    KdPrint(("ImDisk: Done with device type selection for floppy sizes.\n"));

#else // INCLUDE_VFD_ORIGIN

    if (IMDISK_DEVICE_TYPE(CreateData->Flags) == 0)
        CreateData->Flags |= IMDISK_DEVICE_TYPE_HD;

#endif // INCLUDE_VFD_ORIGIN

    // If some parts of the DISK_GEOMETRY structure are zero, auto-fill with
    // typical values for this type of disk.
    if (IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_CD)
    {
        LONGLONG calccyl = CreateData->DiskGeometry.Cylinders.QuadPart;

        if (CreateData->DiskGeometry.BytesPerSector == 0)
            CreateData->DiskGeometry.BytesPerSector = SECTOR_SIZE_CD_ROM;

        calccyl /= CreateData->DiskGeometry.BytesPerSector;

        if (CreateData->DiskGeometry.SectorsPerTrack == 0)
        {
            if (calccyl / SECTORS_PER_TRACK_CD_ROM * SECTORS_PER_TRACK_CD_ROM ==
                calccyl)
                CreateData->DiskGeometry.SectorsPerTrack =
                SECTORS_PER_TRACK_CD_ROM;
            else
                CreateData->DiskGeometry.SectorsPerTrack = 1;
        }

        calccyl /= CreateData->DiskGeometry.SectorsPerTrack;

        if (CreateData->DiskGeometry.TracksPerCylinder == 0)
        {
            if (calccyl /
                TRACKS_PER_CYLINDER_CD_ROM * TRACKS_PER_CYLINDER_CD_ROM ==
                calccyl)
                CreateData->DiskGeometry.TracksPerCylinder =
                TRACKS_PER_CYLINDER_CD_ROM;
            else
                CreateData->DiskGeometry.TracksPerCylinder = 1;
        }

        if (CreateData->DiskGeometry.MediaType == Unknown)
            CreateData->DiskGeometry.MediaType = RemovableMedia;
    }
    // Common floppy sizes geometries.
    else
    {
        LONGLONG calccyl = CreateData->DiskGeometry.Cylinders.QuadPart;

#ifdef INCLUDE_VFD_ORIGIN

        if ((IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_FD) &
            (CreateData->DiskGeometry.BytesPerSector == 0) &
            (CreateData->DiskGeometry.SectorsPerTrack == 0) &
            (CreateData->DiskGeometry.TracksPerCylinder == 0) &
            (CreateData->DiskGeometry.MediaType == Unknown))
            switch (calccyl)
            {
                // 3.5" formats
            case MEDIA_SIZE_240MB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_240M];
                break;

            case MEDIA_SIZE_120MB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_120M];
                break;

            case MEDIA_SIZE_2800KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_2880K];
                break;

            case MEDIA_SIZE_1722KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_1722K];
                break;

            case MEDIA_SIZE_1680KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_1680K];
                break;

            case MEDIA_SIZE_1440KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_1440K];
                break;

            case MEDIA_SIZE_820KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_820K];
                break;

            case MEDIA_SIZE_720KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_720K];
                break;

                // 5.25" formats
            case MEDIA_SIZE_1200KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_1200K];
                break;

            case MEDIA_SIZE_640KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_640K];
                break;

            case MEDIA_SIZE_360KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_360K];
                break;

            case MEDIA_SIZE_320KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_320K];
                break;

            case MEDIA_SIZE_180KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_180K];
                break;

            case MEDIA_SIZE_160KB:
                CreateData->DiskGeometry = media_table[MEDIA_TYPE_160K];
                break;
            }

        // In this case the Cylinders member actually specifies the total size of
        // the virtual disk so restore that in case overwritten by the pre-
        // defined floppy geometries above.
        CreateData->DiskGeometry.Cylinders.QuadPart = calccyl;

#endif // INCLUDE_VFD_ORIGIN

        if (CreateData->DiskGeometry.BytesPerSector == 0)
            CreateData->DiskGeometry.BytesPerSector = SECTOR_SIZE_HDD;

        calccyl /= CreateData->DiskGeometry.BytesPerSector;

        if (CreateData->DiskGeometry.SectorsPerTrack == 0)
            CreateData->DiskGeometry.SectorsPerTrack = HEAD_SIZE_HDD;

        calccyl /= CreateData->DiskGeometry.SectorsPerTrack;

        // Former auto-selection of HDD head size
        /*
        if (CreateData->DiskGeometry.SectorsPerTrack == 0)
        {
        CreateData->DiskGeometry.SectorsPerTrack = 1;

        if ((calccyl / 7 * 7 == calccyl) &
        (CreateData->DiskGeometry.SectorsPerTrack * 7 < 64))
        {
        CreateData->DiskGeometry.SectorsPerTrack *= 7;
        calccyl /= 7;
        }

        if ((calccyl / 3 * 3 == calccyl) &
        (CreateData->DiskGeometry.SectorsPerTrack * 3 < 64))
        {
        CreateData->DiskGeometry.SectorsPerTrack *= 3;
        calccyl /= 3;
        }

        if ((calccyl / 3 * 3 == calccyl) &
        (CreateData->DiskGeometry.SectorsPerTrack * 3 < 64))
        {
        CreateData->DiskGeometry.SectorsPerTrack *= 3;
        calccyl /= 3;
        }

        while (((calccyl & 1) == 0) &
        (CreateData->DiskGeometry.SectorsPerTrack <= 16))
        {
        CreateData->DiskGeometry.SectorsPerTrack <<= 1;
        calccyl >>= 1;
        }
        }
        else
        calccyl /= CreateData->DiskGeometry.SectorsPerTrack;
        */

        if (CreateData->DiskGeometry.TracksPerCylinder == 0)
        {
            CreateData->DiskGeometry.TracksPerCylinder = 1;

            if (calccyl >= 130560)
            {
                CreateData->DiskGeometry.TracksPerCylinder = 255;
                calccyl /= 255;
            }
            else
                while ((calccyl > 128) &
                    (CreateData->DiskGeometry.TracksPerCylinder < 128))
                {
                    CreateData->DiskGeometry.TracksPerCylinder <<= 1;
                    calccyl >>= 1;
                }

            /*
            if (calccyl % 17 == 0)
            {
            CreateData->DiskGeometry.TracksPerCylinder *= 17;
            calccyl /= 17;
            }

            if (calccyl % 5 == 0)
            {
            CreateData->DiskGeometry.TracksPerCylinder *= 5;
            calccyl /= 5;
            }

            if (calccyl % 3 == 0)
            {
            CreateData->DiskGeometry.TracksPerCylinder *= 3;
            calccyl /= 3;
            }

            while (((calccyl & 1) == 0) &
            (CreateData->DiskGeometry.TracksPerCylinder <= 64))
            {
            CreateData->DiskGeometry.TracksPerCylinder <<= 1;
            calccyl >>= 1;
            }
            */
        }

        if (CreateData->DiskGeometry.MediaType == Unknown)
            CreateData->DiskGeometry.MediaType = FixedMedia;
    }

    KdPrint(("ImDisk: Done with disk geometry setup.\n"));

    // Ensure upper-case drive letter.
    CreateData->DriveLetter &= ~0x20;

    // Now build real DeviceType and DeviceCharacteristics parameters.
    if (IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_CD)
    {
        device_type = FILE_DEVICE_CD_ROM;
        device_characteristics = FILE_READ_ONLY_DEVICE | FILE_REMOVABLE_MEDIA;
    }
    else if (IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_RAW)
    {
        device_type = FILE_DEVICE_UNKNOWN;
        device_characteristics = 0;
    }
    else
    {
        device_type = FILE_DEVICE_DISK;

        if (IMDISK_DEVICE_TYPE(CreateData->Flags) == IMDISK_DEVICE_TYPE_FD)
            device_characteristics = FILE_FLOPPY_DISKETTE | FILE_REMOVABLE_MEDIA;
        else
            device_characteristics = 0;
    }

    if (IMDISK_REMOVABLE(CreateData->Flags))
        device_characteristics |= FILE_REMOVABLE_MEDIA;

    if (IMDISK_READONLY(CreateData->Flags))
        device_characteristics |= FILE_READ_ONLY_DEVICE;

    KdPrint
        (("ImDisk: After checks and translations we got this create data:\n"
            "DeviceNumber   = %#x\n"
            "DiskGeometry\n"
            "  .Cylinders   = 0x%.8x%.8x\n"
            "  .MediaType   = %i\n"
            "  .T/C         = %u\n"
            "  .S/T         = %u\n"
            "  .B/S         = %u\n"
            "Offset         = 0x%.8x%.8x\n"
            "Flags          = %#x\n"
            "FileNameLength = %u\n"
            "FileName       = '%.*ws'\n"
            "DriveLetter    = %wc\n",
            CreateData->DeviceNumber,
            CreateData->DiskGeometry.Cylinders.HighPart,
            CreateData->DiskGeometry.Cylinders.LowPart,
            CreateData->DiskGeometry.MediaType,
            CreateData->DiskGeometry.TracksPerCylinder,
            CreateData->DiskGeometry.SectorsPerTrack,
            CreateData->DiskGeometry.BytesPerSector,
            CreateData->ImageOffset.HighPart,
            CreateData->ImageOffset.LowPart,
            CreateData->Flags,
            CreateData->FileNameLength,
            (int)(CreateData->FileNameLength / sizeof(*CreateData->FileName)),
            CreateData->FileName,
            CreateData->DriveLetter ? CreateData->DriveLetter : L' '));

    status = STATUS_SUCCESS;

    // Get FILE_OBJECT if we will need that later
    if ((file_handle != NULL) &&
        (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &&
        ((IMDISK_FILE_TYPE(CreateData->Flags) == IMDISK_FILE_TYPE_AWEALLOC) ||
            (IMDISK_FILE_TYPE(CreateData->Flags) == IMDISK_FILE_TYPE_PARALLEL_IO)))
    {
        ACCESS_MASK file_access_mask =
            SYNCHRONIZE | FILE_READ_ATTRIBUTES | FILE_READ_DATA;

        if (device_characteristics & FILE_READ_ONLY_DEVICE)
            file_access_mask |= FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES;

        status = ObReferenceObjectByHandle(file_handle,
            file_access_mask,
            *IoFileObjectType,
            KernelMode, (PVOID*)&file_object, NULL);

        if (!NT_SUCCESS(status))
        {
            file_object = NULL;

            DbgPrint("ImDisk: Error referencing image file handle: %#x\n",
                status);
        }
        else
        {
            parallel_io = TRUE;

            dev_object = IoGetRelatedDeviceObject(file_object);

            if ((dev_object->Flags & DO_DIRECT_IO) == DO_DIRECT_IO)
            {
                extra_stack_locations = dev_object->StackSize;
            }
        }
    }

    if (NT_SUCCESS(status))
    {
        // Buffer for device name
        device_name_buffer = (PWCHAR)
            ExAllocatePoolWithTag(PagedPool,
                MAXIMUM_FILENAME_LENGTH *
                sizeof(*device_name_buffer),
                POOL_TAG);

        if (device_name_buffer == NULL)
        {
            ImDiskLogError((DriverObject,
                0,
                0,
                NULL,
                0,
                1000,
                STATUS_INSUFFICIENT_RESOURCES,
                102,
                STATUS_INSUFFICIENT_RESOURCES,
                0,
                0,
                NULL,
                L"Memory allocation error."));

            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (!NT_SUCCESS(status))
    {
        SIZE_T free_size = 0;
        ImDiskCloseProxy(&proxy);
        if (file_handle != NULL)
            ZwClose(file_handle);
        if (file_object != NULL)
            ObDereferenceObject(file_object);
        if (file_name.Buffer != NULL)
            ExFreePoolWithTag(file_name.Buffer, POOL_TAG);
        if (image_buffer != NULL)
            ZwFreeVirtualMemory(NtCurrentProcess(),
                (PVOID*)&image_buffer,
                &free_size, MEM_RELEASE);

        return status;
    }

    _snwprintf(device_name_buffer, MAXIMUM_FILENAME_LENGTH - 1,
        IMDISK_DEVICE_BASE_NAME L"%u", CreateData->DeviceNumber);
    device_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

    RtlInitUnicodeString(&device_name, device_name_buffer);

    KdPrint
        (("ImDisk: Creating device '%ws'. Device type %#x, characteristics %#x.\n",
            device_name_buffer, device_type, device_characteristics));

    status = IoCreateDevice(DriverObject,
        sizeof(DEVICE_EXTENSION),
        &device_name,
        device_type,
        device_characteristics,
        FALSE,
        DeviceObject);

    if (NT_SUCCESS(status))
    {
        PWCHAR symlink_name_buffer = (PWCHAR)
            ExAllocatePoolWithTag(PagedPool,
                MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR), POOL_TAG);

        if (symlink_name_buffer == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            UNICODE_STRING symlink_name;

            _snwprintf(symlink_name_buffer, MAXIMUM_FILENAME_LENGTH - 1,
                IMDISK_SYMLNK_NATIVE_BASE_NAME L"%u", CreateData->DeviceNumber);
            symlink_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

            RtlInitUnicodeString(&symlink_name, symlink_name_buffer);

            KdPrint(("ImDisk: Creating symlink '%ws'.\n", symlink_name_buffer));

            status = IoCreateUnprotectedSymbolicLink(&symlink_name, &device_name);

            ExFreePoolWithTag(symlink_name_buffer, POOL_TAG);
        }

        if (!NT_SUCCESS(status))
        {
            KdPrint(("ImDisk: Cannot create symlink. (%#x)\n", status));
            IoDeleteDevice(*DeviceObject);
        }
    }

    if (!NT_SUCCESS(status))
    {
        SIZE_T free_size = 0;

        ExFreePoolWithTag(device_name_buffer, POOL_TAG);
        ImDiskCloseProxy(&proxy);
        if (file_handle != NULL)
            ZwClose(file_handle);
        if (file_object != NULL)
            ObDereferenceObject(file_object);
        if (file_name.Buffer != NULL)
            ExFreePoolWithTag(file_name.Buffer, POOL_TAG);
        if (image_buffer != NULL)
            ZwFreeVirtualMemory(NtCurrentProcess(),
                (PVOID*)&image_buffer,
                &free_size, MEM_RELEASE);

        ImDiskLogError((DriverObject,
            0,
            0,
            NULL,
            0,
            1000,
            status,
            102,
            status,
            0,
            0,
            NULL,
            L"Error creating device object."));

        KdPrint(("ImDisk: Cannot create device. (%#x)\n", status));

        return status;
    }

    KdPrint
        (("ImDisk: Setting the AlignmentRequirement field to %#x.\n",
            alignment_requirement));

    (*DeviceObject)->Flags |= DO_DIRECT_IO;

    (*DeviceObject)->AlignmentRequirement = alignment_requirement;

#pragma warning(suppress: 4244)
    (*DeviceObject)->StackSize += extra_stack_locations;

    device_extension = (PDEVICE_EXTENSION)(*DeviceObject)->DeviceExtension;

    RtlZeroMemory(device_extension, sizeof(*device_extension));

    // Auto-set our own read-only flag if the characteristics of the device
    // object is set to read-only.
    if ((*DeviceObject)->Characteristics & FILE_READ_ONLY_DEVICE)
        device_extension->read_only = TRUE;

    InitializeListHead(&device_extension->list_head);

    KeInitializeSpinLock(&device_extension->list_lock);

    KeInitializeSpinLock(&device_extension->last_io_lock);

    KeInitializeEvent(&device_extension->request_event,
        NotificationEvent, FALSE);

    KeInitializeEvent(&device_extension->terminate_thread,
        NotificationEvent, FALSE);

    device_extension->device_number = CreateData->DeviceNumber;

    device_extension->file_name = file_name;

    device_extension->disk_geometry = CreateData->DiskGeometry;

    device_extension->image_offset = CreateData->ImageOffset;

    if (IMDISK_SPARSE_FILE(CreateData->Flags))
        device_extension->use_set_zero_data = TRUE;

    // VM disk.
    if (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_VM)
        device_extension->vm_disk = TRUE;
    else
        device_extension->vm_disk = FALSE;

    // AWEAlloc disk.
    if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &
        (IMDISK_FILE_TYPE(CreateData->Flags) == IMDISK_FILE_TYPE_AWEALLOC))
        device_extension->awealloc_disk = TRUE;
    else
        device_extension->awealloc_disk = FALSE;

    // Byte-swap
    if (IMDISK_BYTE_SWAP(CreateData->Flags))
        device_extension->byte_swap = TRUE;
    else
        device_extension->byte_swap = FALSE;

    // Image opened for shared writing
    if (IMDISK_SHARED_IMAGE(CreateData->Flags))
        device_extension->shared_image = TRUE;
    else
        device_extension->shared_image = FALSE;

    device_extension->image_buffer = image_buffer;
    device_extension->file_handle = file_handle;

    // Use proxy service.
    if (IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_PROXY)
    {
        device_extension->proxy = proxy;
        device_extension->use_proxy = TRUE;
    }
    else
        device_extension->use_proxy = FALSE;

    device_extension->proxy_unmap = proxy_supports_unmap;

    device_extension->proxy_zero = proxy_supports_zero;

    device_extension->media_change_count++;

    device_extension->drive_letter = CreateData->DriveLetter;

    device_extension->device_thread = KeGetCurrentThread();

    device_extension->parallel_io = parallel_io;

    device_extension->file_object = file_object;

    device_extension->dev_object = dev_object;

    (*DeviceObject)->Flags &= ~DO_DEVICE_INITIALIZING;

    KdPrint(("ImDisk: Device '%ws' created.\n", device_name_buffer));

    ExFreePoolWithTag(device_name_buffer, POOL_TAG);

    return STATUS_SUCCESS;
}

VOID
ImDiskUnload(IN PDRIVER_OBJECT DriverObject)
{
    PDEVICE_OBJECT device_object;
    PLIST_ENTRY list_entry;

    PAGED_CODE();

    DbgPrint("ImDisk: Entering ImDiskUnload for driver %p.\n",
        DriverObject);

    if (RefreshEvent != NULL)
    {
        KdPrint(("ImDisk: Pulsing and closing refresh event.\n"));

        KePulseEvent(RefreshEvent, 0, FALSE);
        ObDereferenceObject(RefreshEvent);
        RefreshEvent = NULL;
    }

    KdPrint(("ImDisk: Current device objects chain dump for this driver:\n"));

    for (device_object = DriverObject->DeviceObject;
    device_object != NULL;)
    {
        KdPrint(("%p -> ", device_object));
        device_object = device_object->NextDevice;
    }

    KdPrint(("End of device chain.\n"));

    while ((list_entry = RemoveHeadList(&ReferencedObjects)) !=
        &ReferencedObjects)
    {
        PREFERENCED_OBJECT record =
            CONTAINING_RECORD(list_entry, REFERENCED_OBJECT, list_entry);

        DbgPrint("ImDisk: Freeing unclaimed referenced object: %p\n",
            record->file_object);

        ExFreePoolWithTag(record, POOL_TAG);
    }

    for (device_object = DriverObject->DeviceObject;
    device_object != NULL; )
    {
        PDEVICE_OBJECT next_device = device_object->NextDevice;
        PDEVICE_EXTENSION device_extension = (PDEVICE_EXTENSION)
            device_object->DeviceExtension;

        KdPrint(("ImDisk: Now deleting device %i.\n",
            device_extension->device_number));

        if (device_object == ImDiskCtlDevice)
        {
            UNICODE_STRING sym_link;
            LARGE_INTEGER time_out;
            time_out.QuadPart = -1000000;

#pragma warning(suppress: 28175)
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
            PKTHREAD device_thread;

            KdPrint(("ImDisk: Shutting down device %i.\n",
                device_extension->device_number));

            device_thread = device_extension->device_thread;
            ObReferenceObjectByPointer(device_thread, SYNCHRONIZE, NULL,
                KernelMode);

            ImDiskRemoveVirtualDisk(device_object);

            KdPrint(("ImDisk: Waiting for device thread %i to terminate.\n",
                device_extension->device_number));

            KeWaitForSingleObject(device_thread,
                Executive,
                KernelMode,
                FALSE,
                NULL);

            ObDereferenceObject(device_thread);
        }

        device_object = next_device;
    }

    KdPrint
        (("ImDisk: No more devices to delete. Leaving ImDiskUnload.\n"));
}

#pragma code_seg()

VOID
ImDiskFindFreeDeviceNumber(PDRIVER_OBJECT DriverObject,
    PULONG DeviceNumber)
{
    KLOCK_QUEUE_HANDLE lock_handle;
    PDEVICE_OBJECT device_object;

    ImDiskAcquireLock(&DeviceListLock, &lock_handle);

    *DeviceNumber = 0;
    for (device_object = DriverObject->DeviceObject;
    device_object != NULL;
#pragma warning(suppress: 28175)
        device_object = device_object->NextDevice)
    {
        PDEVICE_EXTENSION device_extension = (PDEVICE_EXTENSION)
            device_object->DeviceExtension;

        // Skip over control device
        if (device_extension->device_number == -1)
            continue;

        KdPrint2(("ImDisk: Found device %i.\n",
            device_extension->device_number));

        // Result will be one number above highest existing
        if (device_extension->device_number >= *DeviceNumber)
            *DeviceNumber = device_extension->device_number + 1;
    }

    ImDiskReleaseLock(&lock_handle);
}

#if DEBUG_LEVEL >= 1
VOID
ImDiskLogDbgError(IN PVOID Object,
    IN UCHAR MajorFunctionCode,
    IN UCHAR RetryCount,
    IN PULONG DumpData,
    IN USHORT DumpDataSize,
    IN USHORT EventCategory,
    IN NTSTATUS ErrorCode,
    IN ULONG UniqueErrorValue,
    IN NTSTATUS FinalStatus,
    IN ULONG SequenceNumber,
    IN ULONG IoControlCode,
    IN PLARGE_INTEGER DeviceOffset,
    IN PWCHAR Message)
{
    ULONG_PTR string_byte_size;
    ULONG_PTR packet_size;
    PIO_ERROR_LOG_PACKET error_log_packet;

    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
        return;

    string_byte_size = (wcslen(Message) + 1) << 1;

    packet_size =
        sizeof(IO_ERROR_LOG_PACKET) + DumpDataSize + string_byte_size;

    if (packet_size > ERROR_LOG_MAXIMUM_SIZE)
    {
        KdPrint(("ImDisk: Warning: Too large error log packet.\n"));
        return;
    }

    error_log_packet =
        (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(Object,
            (UCHAR)packet_size);

    if (error_log_packet == NULL)
    {
        KdPrint(("ImDisk: Warning: IoAllocateErrorLogEntry() returned NULL.\n"));
        return;
    }

    error_log_packet->MajorFunctionCode = MajorFunctionCode;
    error_log_packet->RetryCount = RetryCount;
    error_log_packet->StringOffset = sizeof(IO_ERROR_LOG_PACKET) + DumpDataSize;
    error_log_packet->EventCategory = EventCategory;
    error_log_packet->ErrorCode = ErrorCode;
    error_log_packet->UniqueErrorValue = UniqueErrorValue;
    error_log_packet->FinalStatus = FinalStatus;
    error_log_packet->SequenceNumber = SequenceNumber;
    error_log_packet->IoControlCode = IoControlCode;
    if (DeviceOffset != NULL)
        error_log_packet->DeviceOffset = *DeviceOffset;
    error_log_packet->DumpDataSize = DumpDataSize;

    if (DumpDataSize != 0)
        memcpy(error_log_packet->DumpData, DumpData, DumpDataSize);

    if (Message == NULL)
        error_log_packet->NumberOfStrings = 0;
    else
    {
        error_log_packet->NumberOfStrings = 1;
        memcpy((PUCHAR)error_log_packet + error_log_packet->StringOffset,
            Message,
            string_byte_size);
    }

    IoWriteErrorLogEntry(error_log_packet);
}
#endif

VOID
ImDiskRemoveVirtualDisk(IN PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_EXTENSION device_extension;

    ASSERT(DeviceObject != NULL);

    device_extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    KdPrint(("ImDisk: Request to shutdown device %i.\n",
        device_extension->device_number));

    if (device_extension->drive_letter != 0)
        if (KeGetCurrentIrql() == PASSIVE_LEVEL)
            ImDiskRemoveDriveLetter(device_extension->drive_letter);

    KeSetEvent(&device_extension->terminate_thread, (KPRIORITY)0, FALSE);
}

#pragma code_seg("PAGE")

NTSTATUS
ImDiskRemoveDriveLetter(IN WCHAR DriveLetter)
{
    NTSTATUS status;
    WCHAR sym_link_global_wchar[] = L"\\DosDevices\\Global\\ :";
    WCHAR sym_link_wchar[] = L"\\DosDevices\\ :";
    UNICODE_STRING sym_link;

    PAGED_CODE();

    sym_link_global_wchar[19] = DriveLetter;

    KdPrint(("ImDisk: Removing symlink '%ws'.\n", sym_link_global_wchar));

    RtlInitUnicodeString(&sym_link, sym_link_global_wchar);
    status = IoDeleteSymbolicLink(&sym_link);

    if (!NT_SUCCESS(status))
    {
        KdPrint
            (("ImDisk: Cannot remove symlink '%ws'. (%#x)\n",
                sym_link_global_wchar, status));
    }

    sym_link_wchar[12] = DriveLetter;

    KdPrint(("ImDisk: Removing symlink '%ws'.\n", sym_link_wchar));

    RtlInitUnicodeString(&sym_link, sym_link_wchar);
    status = IoDeleteSymbolicLink(&sym_link);

    if (!NT_SUCCESS(status))
    {
        KdPrint(("ImDisk: Cannot remove symlink '%ws'. (%#x)\n",
            sym_link_wchar, status));
    }

    return status;
}

#pragma code_seg()

NTSTATUS
ImDiskCreateClose(IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION io_stack;
    PDEVICE_EXTENSION device_extension;
    NTSTATUS status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Irp != NULL);

    KdPrint(("ImDisk: Entering ImDiskCreateClose.\n"));

    io_stack = IoGetCurrentIrpStackLocation(Irp);
    device_extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (io_stack->FileObject->FileName.Length != 0)
    {
        KdPrint(("ImDisk: Attempt to open '%wZ' on device %i.\n",
            &io_stack->FileObject->FileName,
            device_extension->device_number));

        status = STATUS_OBJECT_NAME_NOT_FOUND;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    if ((io_stack->MajorFunction == IRP_MJ_CREATE) &&
        KeReadStateEvent(&device_extension->terminate_thread))
    {
        KdPrint(("ImDisk: Attempt to open device %i when shut down.\n",
            device_extension->device_number));

        status = STATUS_DEVICE_REMOVED;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    KdPrint(("ImDisk: Successfully created/closed a handle for device %i.\n",
        device_extension->device_number));

    status = STATUS_SUCCESS;

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = FILE_OPENED;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

typedef struct _LOWER_DEVICE_WORK_ITEM
{
    PIRP OriginalIrp;
    PDEVICE_EXTENSION DeviceExtension;
    LONGLONG OriginalOffset;
    PUCHAR AllocatedBuffer;
    PUCHAR SystemBuffer;
    BOOLEAN CopyBack;
} LOWER_DEVICE_WORK_ITEM, *PLOWER_DEVICE_WORK_ITEM;

VOID
ImDiskFreeIrpWithMdls(PIRP Irp)
{
    PMDL mdl;
    PMDL nextMdl;
    for (mdl = Irp->MdlAddress; mdl != NULL; mdl = nextMdl)
    {
        nextMdl = mdl->Next;

        if (mdl->MdlFlags & MDL_PAGES_LOCKED)
        {
            MmUnlockPages(mdl);
        }

        IoFreeMdl(mdl);
    }

    Irp->MdlAddress = NULL;

    IoFreeIrp(Irp);
}

NTSTATUS
ImDiskReadWriteLowerDeviceCompletion(PDEVICE_OBJECT DeviceObject,
    PIRP Irp, PVOID Context)
{
    PLOWER_DEVICE_WORK_ITEM item = (PLOWER_DEVICE_WORK_ITEM)Context;

    ASSERT(item != NULL);

    __analysis_assume(item != NULL);

    UNREFERENCED_PARAMETER(DeviceObject);

    item->OriginalIrp->IoStatus = Irp->IoStatus;

    if (!NT_SUCCESS(Irp->IoStatus.Status))
    {
        KdPrint(("ImDiskReadWriteLowerDeviceCompletion: Parallel I/O failed with status %#x\n",
            Irp->IoStatus.Status));
    }
    else
    {
        if (item->CopyBack)
        {
            RtlCopyMemory(item->SystemBuffer, item->AllocatedBuffer,
                Irp->IoStatus.Information);
        }

        if (item->AllocatedBuffer != NULL)
        {
            KLOCK_QUEUE_HANDLE lock_handle;

            ImDiskAcquireLock(&item->DeviceExtension->last_io_lock, &lock_handle);

            if (item->DeviceExtension->last_io_data != NULL)
            {
                ExFreePoolWithTag(item->DeviceExtension->last_io_data,
                    POOL_TAG);
            }

            item->DeviceExtension->last_io_data = item->AllocatedBuffer;

            item->DeviceExtension->last_io_offset = item->OriginalOffset;
            item->DeviceExtension->last_io_length =
                (ULONG)Irp->IoStatus.Information;

            ImDiskReleaseLock(&lock_handle);
        }
    }

    if (Irp->MdlAddress != item->OriginalIrp->MdlAddress)
    {
        ImDiskFreeIrpWithMdls(Irp);
    }
    else
    {
        IoFreeIrp(Irp);
    }

    IoCompleteRequest(item->OriginalIrp, IO_NO_INCREMENT);

    ExFreePoolWithTag(item, POOL_TAG);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
ImDiskDeviceControlLowerDevice(PIRP Irp, PDEVICE_EXTENSION DeviceExtension)
{
    PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
    PIO_STACK_LOCATION lower_io_stack = IoGetNextIrpStackLocation(Irp);

    IoCopyCurrentIrpStackLocationToNext(Irp);

    if (io_stack->Parameters.DeviceIoControl.IoControlCode ==
        IOCTL_IMDISK_FSCTL_PASS_THROUGH)
    {
        lower_io_stack->MajorFunction = IRP_MJ_FILE_SYSTEM_CONTROL;
    }

    lower_io_stack->Parameters.DeviceIoControl.IoControlCode =
        *(PULONG)Irp->AssociatedIrp.SystemBuffer;

    lower_io_stack->Parameters.DeviceIoControl.InputBufferLength -=
        sizeof(ULONG);

    RtlMoveMemory(Irp->AssociatedIrp.SystemBuffer,
        (PUCHAR)Irp->AssociatedIrp.SystemBuffer + sizeof(ULONG),
        lower_io_stack->Parameters.DeviceIoControl.InputBufferLength);

    lower_io_stack->FileObject = DeviceExtension->file_object;

    return IoCallDriver(DeviceExtension->dev_object, Irp);
}

NTSTATUS
ImDiskReadWriteLowerDevice(PIRP Irp, PDEVICE_EXTENSION DeviceExtension)
{
    PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
    PIO_STACK_LOCATION lower_io_stack;
    PIRP lower_irp;
    PLOWER_DEVICE_WORK_ITEM item;

    // If image file is a direct I/O device, we simply forward the IRP with
    // correct FILE_OBJECT and adjusted offset if needed.
    if ((DeviceExtension->dev_object->Flags & DO_DIRECT_IO) == DO_DIRECT_IO)
    {
        IoCopyCurrentIrpStackLocationToNext(Irp);

        lower_io_stack = IoGetNextIrpStackLocation(Irp);

        lower_io_stack->Parameters.Read.ByteOffset.QuadPart +=
            DeviceExtension->image_offset.QuadPart;

        lower_io_stack->FileObject = DeviceExtension->file_object;

        if ((io_stack->MajorFunction == IRP_MJ_WRITE) &&
            !DeviceExtension->image_modified)
        {
            DeviceExtension->image_modified = TRUE;

            // Fire refresh event
            if (RefreshEvent != NULL)
                KePulseEvent(RefreshEvent, 0, FALSE);
        }

        return IoCallDriver(DeviceExtension->dev_object, Irp);
    }

    // This goes for image files with DO_BUFFERED_IO or DO_NEITHER_IO.
    // We allocate NP pool as buffer for a request to send down. A completion
    // routine takes care of copying read operation data back to original IRP.

    item = (PLOWER_DEVICE_WORK_ITEM)
        ExAllocatePoolWithTag(NonPagedPool,
            sizeof(*item), POOL_TAG);

    if (item == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(item, sizeof(*item));

    item->OriginalIrp = Irp;
    item->DeviceExtension = DeviceExtension;
    item->OriginalOffset = io_stack->Parameters.Read.ByteOffset.QuadPart;

    if ((io_stack->MajorFunction == IRP_MJ_READ) ||
        (io_stack->MajorFunction == IRP_MJ_WRITE))
    {
        item->SystemBuffer = (PUCHAR)
            MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
                NormalPagePriority);

        if (item->SystemBuffer == NULL)
        {
            ExFreePoolWithTag(item, POOL_TAG);
            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    lower_irp = IoAllocateIrp(DeviceExtension->dev_object->StackSize, FALSE);

    if (lower_irp == NULL)
    {
        ExFreePoolWithTag(item, POOL_TAG);
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    lower_io_stack = IoGetNextIrpStackLocation(lower_irp);

    lower_io_stack->MajorFunction = io_stack->MajorFunction;
    lower_io_stack->Parameters = io_stack->Parameters;

    if ((io_stack->MajorFunction == IRP_MJ_READ) ||
        (io_stack->MajorFunction == IRP_MJ_WRITE))
    {
        lower_irp->AssociatedIrp.SystemBuffer =
            lower_irp->UserBuffer =
            item->AllocatedBuffer = (PUCHAR)
            ExAllocatePoolWithTag(NonPagedPool,
                io_stack->Parameters.Read.Length, POOL_TAG);

        if (item->AllocatedBuffer == NULL)
        {
            ImDiskFreeIrpWithMdls(lower_irp);
            ExFreePoolWithTag(item, POOL_TAG);

            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (io_stack->MajorFunction == IRP_MJ_WRITE)
        {
            RtlCopyMemory(item->AllocatedBuffer, item->SystemBuffer,
                io_stack->Parameters.Write.Length);
        }
        else if (io_stack->MajorFunction == IRP_MJ_READ)
        {
            item->CopyBack = TRUE;
        }
    }

    lower_irp->Tail.Overlay.Thread = Irp->Tail.Overlay.Thread;

    if (io_stack->MajorFunction == IRP_MJ_READ)
    {
        lower_irp->Flags |= IRP_READ_OPERATION;
    }
    else if (io_stack->MajorFunction == IRP_MJ_WRITE)
    {
        lower_irp->Flags |= IRP_WRITE_OPERATION;
        lower_io_stack->Flags |= SL_WRITE_THROUGH;
    }

    lower_irp->Flags |= IRP_NOCACHE;

    lower_io_stack->Parameters.Read = io_stack->Parameters.Read;
    lower_io_stack->Parameters.Read.ByteOffset.QuadPart +=
        DeviceExtension->image_offset.QuadPart;

    lower_io_stack->FileObject = DeviceExtension->file_object;

    if ((io_stack->MajorFunction == IRP_MJ_WRITE) &&
        (!DeviceExtension->image_modified))
    {
        DeviceExtension->image_modified = TRUE;

        // Fire refresh event
        if (RefreshEvent != NULL)
            KePulseEvent(RefreshEvent, 0, FALSE);
    }

    IoSetCompletionRoutine(lower_irp, ImDiskReadWriteLowerDeviceCompletion,
        item, TRUE, TRUE, TRUE);

    IoMarkIrpPending(Irp);

    (void)IoCallDriver(DeviceExtension->dev_object, lower_irp);

    return STATUS_PENDING;
}

NTSTATUS
ImDiskReadWrite(IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PDEVICE_EXTENSION device_extension;
    PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Irp != NULL);

    if ((io_stack->Parameters.Read.ByteOffset.QuadPart < 0) ||
        ((io_stack->Parameters.Read.ByteOffset.QuadPart +
            io_stack->Parameters.Read.Length) < 0))
    {
        KdPrint(("ImDisk: Read/write attempt on negative offset.\n"));

        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return STATUS_SUCCESS;
    }

    device_extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (DeviceObject == ImDiskCtlDevice)
    {
        KdPrint(("ImDisk: Read/write attempt on ctl device.\n"));

        status = STATUS_INVALID_DEVICE_REQUEST;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    if (KeReadStateEvent(&device_extension->terminate_thread) != 0)
    {
        KdPrint(("ImDisk: Read/write attempt on device %i while removing.\n",
            device_extension->device_number));

        status = STATUS_DEVICE_REMOVED;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    if ((io_stack->MajorFunction == IRP_MJ_WRITE) &&
        device_extension->read_only)
    {
        KdPrint(("ImDisk: Attempt to write to write-protected device %i.\n",
            device_extension->device_number));

        status = STATUS_MEDIA_WRITE_PROTECTED;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    if ((io_stack->Parameters.Read.ByteOffset.QuadPart +
        io_stack->Parameters.Read.Length) >
        (device_extension->disk_geometry.Cylinders.QuadPart))
    {
        KdPrint(("ImDisk: Read/write beyond eof on device %i.\n",
            device_extension->device_number));

        status = STATUS_SUCCESS;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    if (io_stack->Parameters.Read.Length == 0)
    {
        KdPrint(("ImDisk: Read/write zero bytes on device %i.\n",
            device_extension->device_number));

        status = STATUS_SUCCESS;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    KdPrint2(("ImDisk: Device %i got r/w request Offset=0x%.8x%.8x Len=%#x.\n",
        device_extension->device_number,
        io_stack->Parameters.Read.ByteOffset.HighPart,
        io_stack->Parameters.Read.ByteOffset.LowPart,
        io_stack->Parameters.Read.Length));

    status = STATUS_PENDING;

    if (io_stack->MajorFunction == IRP_MJ_READ)
    {
        KLOCK_QUEUE_HANDLE lock_handle;

        ImDiskAcquireLock(&device_extension->last_io_lock, &lock_handle);

        if (device_extension->last_io_data != NULL)
        {
            if ((io_stack->Parameters.Read.ByteOffset.QuadPart >=
                device_extension->last_io_offset) &
                ((io_stack->Parameters.Read.ByteOffset.QuadPart +
                    io_stack->Parameters.Read.Length) <=
                    (device_extension->last_io_offset +
                        device_extension->last_io_length)))
            {
                PUCHAR system_buffer =
                    (PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
                        NormalPagePriority);
                if (system_buffer == NULL)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    Irp->IoStatus.Information = 0;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                }
                else
                {
                    KdPrint(("ImDisk: Device %i read Offset=0x%.8x%.8x Len=%#x, "
                        "intermediate cache hit.\n",
                        device_extension->device_number,
                        io_stack->Parameters.Read.ByteOffset.HighPart,
                        io_stack->Parameters.Read.ByteOffset.LowPart,
                        io_stack->Parameters.Read.Length));

                    Irp->IoStatus.Status = STATUS_SUCCESS;
                    Irp->IoStatus.Information = io_stack->Parameters.Read.Length;

                    RtlCopyMemory(system_buffer,
                        device_extension->last_io_data +
                        io_stack->Parameters.Read.ByteOffset.QuadPart -
                        device_extension->last_io_offset,
                        Irp->IoStatus.Information);

                    if (device_extension->byte_swap)
                        ImDiskByteSwapBuffer(system_buffer,
                            Irp->IoStatus.Information);

                    if (io_stack->FileObject != NULL)
                    {
                        io_stack->FileObject->CurrentByteOffset.QuadPart +=
                            Irp->IoStatus.Information;
                    }
                    status = STATUS_SUCCESS;
                }
            }
        }

        ImDiskReleaseLock(&lock_handle);
    }

    // In-thread I/O using a FILE_OBJECT
    if ((status == STATUS_PENDING) &&
        device_extension->parallel_io)
    {
        BOOLEAN set_zero_data = FALSE;

        if (device_extension->use_set_zero_data &&
            (io_stack->MajorFunction == IRP_MJ_WRITE) &&
            (io_stack->Parameters.Write.Length > sizeof(ULONGLONG)))
        {
            PUCHAR system_buffer =
                (PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
                    NormalPagePriority);

            if (system_buffer == NULL)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                Irp->IoStatus.Information = 0;
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else if (ImDiskIsBufferZero(system_buffer,
                io_stack->Parameters.Write.Length))
            {
                set_zero_data = TRUE;
            }
        }

        if (!set_zero_data)
        {
            return ImDiskReadWriteLowerDevice(Irp, device_extension);
        }
    }

    if (status == STATUS_PENDING)
    {
        IoMarkIrpPending(Irp);

        ImDiskInterlockedInsertTailList(&device_extension->list_head,
            &Irp->Tail.Overlay.ListEntry,
            &device_extension->list_lock);

        KeSetEvent(&device_extension->request_event, (KPRIORITY)0, FALSE);

        return STATUS_PENDING;
    }
    else
    {
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }
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

    Irp->IoStatus.Information = 0;

    device_extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    io_stack = IoGetCurrentIrpStackLocation(Irp);

    KdPrint(("ImDisk: Device %i received IOCTL %#x IRP %p.\n",
        device_extension->device_number,
        io_stack->Parameters.DeviceIoControl.IoControlCode,
        Irp));

    if (KeReadStateEvent(&device_extension->terminate_thread) != 0)
    {
        KdPrint(("ImDisk: IOCTL attempt on device %i that is being removed.\n",
            device_extension->device_number));

        status = STATUS_DEVICE_REMOVED;

        Irp->IoStatus.Status = status;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    // The control device can only receive version queries, enumeration queries
    // or device create requests.
    if (DeviceObject == ImDiskCtlDevice)
    {
        switch (io_stack->Parameters.DeviceIoControl.IoControlCode)
        {
        case IOCTL_IMDISK_QUERY_VERSION:
        case IOCTL_IMDISK_CREATE_DEVICE:
        case IOCTL_IMDISK_REMOVE_DEVICE:
        case IOCTL_IMDISK_QUERY_DRIVER:
        case IOCTL_IMDISK_REFERENCE_HANDLE:
        case IOCTL_IMDISK_GET_REFERENCED_HANDLE:
            break;

        default:
            KdPrint(("ImDisk: Invalid IOCTL %#x for control device.\n",
                io_stack->Parameters.DeviceIoControl.IoControlCode));

            status = STATUS_INVALID_DEVICE_REQUEST;

            Irp->IoStatus.Status = status;

            IoCompleteRequest(Irp, IO_NO_INCREMENT);

            return status;
        }
    }
    else
    {
        switch (io_stack->Parameters.DeviceIoControl.IoControlCode)
        {
            // Invalid IOCTL codes for this driver's disk devices.
        case IOCTL_IMDISK_CREATE_DEVICE:
        case IOCTL_IMDISK_REMOVE_DEVICE:
        case IOCTL_IMDISK_REFERENCE_HANDLE:
        case IOCTL_IMDISK_GET_REFERENCED_HANDLE:
            KdPrint(("ImDisk: Invalid IOCTL %#x for disk device.\n",
                io_stack->Parameters.DeviceIoControl.IoControlCode));

            status = STATUS_INVALID_DEVICE_REQUEST;

            Irp->IoStatus.Status = status;

            IoCompleteRequest(Irp, IO_NO_INCREMENT);

            return status;
        }
    }

    switch (io_stack->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_IMDISK_SET_DEVICE_FLAGS:
        KdPrint(("ImDisk: IOCTL_IMDISK_SET_DEVICE_FLAGS for device %i.\n",
            device_extension->device_number));

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(IMDISK_SET_DEVICE_FLAGS))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        {
            PIMDISK_SET_DEVICE_FLAGS device_flags = (PIMDISK_SET_DEVICE_FLAGS)
                Irp->AssociatedIrp.SystemBuffer;

            KIRQL irql = KeGetCurrentIrql();

            if (irql >= DISPATCH_LEVEL)
            {
                status = STATUS_ACCESS_DENIED;
                break;
            }

            status = STATUS_SUCCESS;

            if (IMDISK_READONLY(device_flags->FlagsToChange))
                if (DeviceObject->DeviceType == FILE_DEVICE_DISK)
                {
                    if ((IMDISK_READONLY(device_flags->FlagValues) != 0) &
                        (device_extension->special_file_count <= 0))
                    {
                        DeviceObject->Characteristics |= FILE_READ_ONLY_DEVICE;
                        device_extension->read_only = TRUE;

                        device_flags->FlagsToChange &= ~IMDISK_OPTION_RO;
                    }
                    else
                        // It is not possible to make a file- or proxy virtual disk
                        // writable on the fly. (A physical image file or the proxy
                        // comm channel might not be opened for writing.)
                        if (device_extension->vm_disk)
                        {
                            DeviceObject->Characteristics &= ~FILE_READ_ONLY_DEVICE;
                            device_extension->read_only = FALSE;

                            device_flags->FlagsToChange &= ~IMDISK_OPTION_RO;
                        }
                }

            if (IMDISK_REMOVABLE(device_flags->FlagsToChange))
                if (DeviceObject->DeviceType == FILE_DEVICE_DISK)
                {
                    if (IMDISK_REMOVABLE(device_flags->FlagValues))
                        DeviceObject->Characteristics |= FILE_REMOVABLE_MEDIA;
                    else
                        DeviceObject->Characteristics &= ~FILE_REMOVABLE_MEDIA;

                    device_flags->FlagsToChange &= ~IMDISK_OPTION_REMOVABLE;
                }

            if (device_flags->FlagsToChange & IMDISK_IMAGE_MODIFIED)
            {
                if (device_flags->FlagValues & IMDISK_IMAGE_MODIFIED)
                    device_extension->image_modified = TRUE;
                else
                    device_extension->image_modified = FALSE;

                device_flags->FlagsToChange &= ~IMDISK_IMAGE_MODIFIED;
            }

            if (irql == PASSIVE_LEVEL)
            {
                if (IMDISK_SPARSE_FILE(device_flags->FlagsToChange) &&
                    IMDISK_SPARSE_FILE(device_flags->FlagValues) &&
                    (!device_extension->use_proxy) &&
                    (!device_extension->vm_disk))
                {
                    IO_STATUS_BLOCK io_status;
                    status = ZwFsControlFile(device_extension->file_handle,
                        NULL,
                        NULL,
                        NULL,
                        &io_status,
                        FSCTL_SET_SPARSE,
                        NULL,
                        0,
                        NULL,
                        0);

                    if (NT_SUCCESS(status))
                    {
                        device_extension->use_set_zero_data = TRUE;
                        device_flags->FlagsToChange &= ~IMDISK_OPTION_SPARSE_FILE;
                    }
                }

                // Fire refresh event
                if (RefreshEvent != NULL)
                    KePulseEvent(RefreshEvent, 0, FALSE);
            }
            else
                KdPrint
                (("ImDisk: Some flags cannot be changed at this IRQL (%#x).\n",
                    irql));

            if (device_flags->FlagsToChange == 0)
                status = STATUS_SUCCESS;
            else if (NT_SUCCESS(status))
                status = STATUS_INVALID_DEVICE_REQUEST;
        }

        if (io_stack->Parameters.DeviceIoControl.OutputBufferLength >=
            sizeof(IMDISK_SET_DEVICE_FLAGS))
        {
            Irp->IoStatus.Information = sizeof(IMDISK_SET_DEVICE_FLAGS);
        }

        break;

    case IOCTL_IMDISK_REFERENCE_HANDLE:
    {
        KLOCK_QUEUE_HANDLE lock_handle;
        PREFERENCED_OBJECT record;

        KdPrint(("ImDisk: IOCTL_IMDISK_REFERENCE_HANDLE for device %i.\n",
            device_extension->device_number));

        // This IOCTL requires work that must be done at IRQL < DISPATCH_LEVEL
        // but must be done in the thread context of the calling application and
        // not by the worker thread so therefore this check is done. Also, the
        // control device does not have a worker thread so that is another
        // reason.
        if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        {
            KdPrint(("ImDisk: IOCTL_IMDISK_REFERENCE_HANDLE not accessible "
                "at current IRQL (%i).", KeGetCurrentIrql()));

            status = STATUS_ACCESS_DENIED;

            break;
        }

        if ((io_stack->MajorFunction != IRP_MJ_INTERNAL_DEVICE_CONTROL) &&
            !SeSinglePrivilegeCheck(RtlConvertLongToLuid(SE_TCB_PRIVILEGE),
                Irp->RequestorMode))
        {
            KdPrint(("ImDisk: IOCTL_IMDISK_REFERENCE_HANDLE not accessible, "
                "privilege not held by calling thread.\n"));

            status = STATUS_ACCESS_DENIED;

            break;
        }

        if ((io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(HANDLE)) |
            (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
                sizeof(PFILE_OBJECT)))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        record = (PREFERENCED_OBJECT)
            ExAllocatePoolWithTag(NonPagedPool,
                sizeof(REFERENCED_OBJECT), POOL_TAG);

        if (record == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        KdPrint(("ImDisk: Referencing handle %p.\n",
            *(PHANDLE)Irp->AssociatedIrp.SystemBuffer));

        status = ObReferenceObjectByHandle(
            *(PHANDLE)Irp->AssociatedIrp.SystemBuffer,
            FILE_READ_ATTRIBUTES |
            FILE_READ_DATA |
            FILE_WRITE_DATA,
            *IoFileObjectType,
            Irp->RequestorMode,
            (PVOID*)&record->file_object,
            NULL);

        KdPrint(("ImDisk: Status=%#x, PFILE_OBJECT %p.\n",
            status,
            record->file_object));

        if (!NT_SUCCESS(status))
        {
            ExFreePoolWithTag(record, POOL_TAG);
            break;
        }

        ImDiskAcquireLock(&ReferencedObjectsListLock, &lock_handle);

        InsertTailList(&ReferencedObjects, &record->list_entry);

        ImDiskReleaseLock(&lock_handle);

        *(PFILE_OBJECT*)Irp->AssociatedIrp.SystemBuffer = record->file_object;
        Irp->IoStatus.Information = sizeof(PFILE_OBJECT);

        break;
    }

    case IOCTL_IMDISK_GET_REFERENCED_HANDLE:
    {
        KLOCK_QUEUE_HANDLE lock_handle;
        PLIST_ENTRY list_entry;

        if (io_stack->MajorFunction != IRP_MJ_INTERNAL_DEVICE_CONTROL)
        {
            status = STATUS_ACCESS_DENIED;
            break;
        }

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(PFILE_OBJECT))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        status = STATUS_OBJECT_NAME_NOT_FOUND;

        ImDiskAcquireLock(&ReferencedObjectsListLock, &lock_handle);

        for (list_entry = ReferencedObjects.Flink;
        list_entry != &ReferencedObjects;
            list_entry = list_entry->Flink)
        {
            PREFERENCED_OBJECT record =
                CONTAINING_RECORD(list_entry, REFERENCED_OBJECT, list_entry);

            if (record->file_object ==
                *(PFILE_OBJECT*)Irp->AssociatedIrp.SystemBuffer)
            {
                list_entry->Blink->Flink = list_entry->Flink;
                list_entry->Flink->Blink = list_entry->Blink;

                ExFreePoolWithTag(record, POOL_TAG);

                status = STATUS_SUCCESS;
                break;
            }
        }

        ImDiskReleaseLock(&lock_handle);

        if (NT_SUCCESS(status))
        {
            KdPrint(("ImDisk: Successfully claimed referenced object %p.\n",
                *(PFILE_OBJECT*)Irp->AssociatedIrp.SystemBuffer));
        }
        else
        {
            DbgPrint("ImDisk Warning: Requested %p not in referenced objects list.\n",
                *(PFILE_OBJECT*)Irp->AssociatedIrp.SystemBuffer);
        }

        break;
    }

    case IOCTL_IMDISK_CREATE_DEVICE:
    {
        PIMDISK_CREATE_DATA create_data;

        KdPrint(("ImDisk: IOCTL_IMDISK_CREATE_DEVICE for device %i.\n",
            device_extension->device_number));

        // This IOCTL requires work that must be done at IRQL == PASSIVE_LEVEL
        // but the control device has no worker thread (does not handle any
        // other I/O) so therefore everything is done directly here. Therefore
        // this IRQL check is necessary.
        if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        {
            status = STATUS_ACCESS_DENIED;

            break;
        }

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(IMDISK_CREATE_DATA) - sizeof(*create_data->FileName))
        {
            KdPrint(("ImDisk: Invalid input buffer size (1). "
                "Got: %u Expected at least: %u.\n",
                io_stack->Parameters.DeviceIoControl.InputBufferLength,
                (int)(sizeof(IMDISK_CREATE_DATA) -
                    sizeof(*create_data->FileName))));

            status = STATUS_INVALID_PARAMETER;

            break;
        }

        create_data = (PIMDISK_CREATE_DATA)Irp->AssociatedIrp.SystemBuffer;

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(IMDISK_CREATE_DATA) +
            create_data->FileNameLength -
            sizeof(*create_data->FileName))
        {
            KdPrint(("ImDisk: Invalid input buffer size (2). "
                "Got: %u Expected at least: %u.\n",
                io_stack->Parameters.DeviceIoControl.InputBufferLength,
                (int)(sizeof(IMDISK_CREATE_DATA) +
                    create_data->FileNameLength -
                    sizeof(*create_data->FileName))));

            status = STATUS_INVALID_PARAMETER;

            break;
        }

        status = ImDiskAddVirtualDisk(DeviceObject->DriverObject,
            (PIMDISK_CREATE_DATA)
            Irp->AssociatedIrp.SystemBuffer,
            Irp->Tail.Overlay.Thread);

        if (NT_SUCCESS(status) &&
            (io_stack->Parameters.DeviceIoControl.OutputBufferLength >=
                io_stack->Parameters.DeviceIoControl.InputBufferLength))
        {
            Irp->IoStatus.Information =
                io_stack->Parameters.DeviceIoControl.OutputBufferLength;
        }

        break;
    }

    case IOCTL_IMDISK_REMOVE_DEVICE:
    {
        ULONG device_number;
        KLOCK_QUEUE_HANDLE lock_handle;
        PDEVICE_OBJECT device_object =
            ImDiskCtlDevice->DriverObject->DeviceObject;

        KdPrint(("ImDisk: IOCTL_IMDISK_REMOVE_DEVICE.\n"));

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(ULONG))
        {
            KdPrint(("ImDisk: Invalid input buffer size (1). "
                "Got: %u Expected at least: %u.\n",
                io_stack->Parameters.DeviceIoControl.InputBufferLength,
                (int)sizeof(ULONG)));

            status = STATUS_INVALID_PARAMETER;

            break;
        }

        device_number = *(PULONG)Irp->AssociatedIrp.SystemBuffer;

        KdPrint(("ImDisk: IOCTL_IMDISK_REMOVE_DEVICE for device %i.\n",
            device_number));

        status = STATUS_OBJECT_NAME_NOT_FOUND;

        ImDiskAcquireLock(&DeviceListLock, &lock_handle);

        while (device_object != NULL)
        {
            PDEVICE_EXTENSION device_extension =
                (PDEVICE_EXTENSION)device_object->DeviceExtension;

            if (device_extension->device_number == device_number)
            {
                if (device_extension->special_file_count > 0)
                {
                    status = STATUS_ACCESS_DENIED;
                }
                else
                {
                    status = STATUS_SUCCESS;
                }
                break;
            }

#pragma warning(suppress: 28175)
            device_object = device_object->NextDevice;
        }

        ImDiskReleaseLock(&lock_handle);

        if (status == STATUS_SUCCESS)
        {
            ImDiskRemoveVirtualDisk(device_object);
        }

        break;
    }

    case IOCTL_DISK_EJECT_MEDIA:
    case IOCTL_STORAGE_EJECT_MEDIA:
        KdPrint(("ImDisk: IOCTL_DISK/STORAGE_EJECT_MEDIA for device %i.\n",
            device_extension->device_number));

        if (device_extension->special_file_count > 0)
        {
            status = STATUS_ACCESS_DENIED;
        }
        else
        {
            ImDiskRemoveVirtualDisk(DeviceObject);

            status = STATUS_SUCCESS;
        }

        break;

    case IOCTL_IMDISK_QUERY_DRIVER:
    {
        KdPrint(("ImDisk: IOCTL_IMDISK_QUERY_DRIVER for device %i.\n",
            device_extension->device_number));

        if (io_stack->Parameters.DeviceIoControl.OutputBufferLength >
            sizeof(ULONGLONG))
        {
            KLOCK_QUEUE_HANDLE lock_handle;
            ULONG max_items =
                io_stack->Parameters.DeviceIoControl.OutputBufferLength >> 2;
            ULONG current_item = 0;
            PULONG device_list = (PULONG)Irp->AssociatedIrp.SystemBuffer;
            PDEVICE_OBJECT device_object;

            KdPrint2(("ImDisk: Max number of items %i.\n", max_items));

            ImDiskAcquireLock(&DeviceListLock, &lock_handle);

            for (device_object = DeviceObject->DriverObject->DeviceObject;
            device_object != NULL;
#pragma warning(suppress: 28175)
                device_object = device_object->NextDevice)
            {
                PDEVICE_EXTENSION this_device_extension = (PDEVICE_EXTENSION)
                    device_object->DeviceExtension;

                // Skip over control device
                if (this_device_extension->device_number == -1)
                    continue;

                current_item++;

                KdPrint2(("ImDisk: Found device %i.\n",
                    this_device_extension->device_number));

                if (current_item < max_items)
                    device_list[current_item] =
                    this_device_extension->device_number;
            }

            ImDiskReleaseLock(&lock_handle);

            device_list[0] = current_item;

            KdPrint(("ImDisk: Found %i devices.\n", device_list[0]));

            if (current_item >= max_items)
            {
                Irp->IoStatus.Information = 1 << 2;
                status = STATUS_SUCCESS;
            }
            else
            {
                Irp->IoStatus.Information = ((ULONG_PTR)current_item + 1) << 2;
                status = STATUS_SUCCESS;
            }
        }
        else
        {
            ULARGE_INTEGER DeviceList = { 0 };
            KLOCK_QUEUE_HANDLE lock_handle;
            PDEVICE_OBJECT device_object;
            ULONG HighestDeviceNumber = 0;

            ImDiskAcquireLock(&DeviceListLock, &lock_handle);

            for (device_object = DeviceObject->DriverObject->DeviceObject;
            device_object != NULL;
#pragma warning(suppress: 28175)
                device_object = device_object->NextDevice)
            {
                PDEVICE_EXTENSION this_device_extension = (PDEVICE_EXTENSION)
                    device_object->DeviceExtension;

                // Skip over control device
                if (this_device_extension->device_number == -1)
                    continue;

                KdPrint2(("ImDisk: Found device %i.\n",
                    this_device_extension->device_number));

                if (this_device_extension->device_number > HighestDeviceNumber)
                    HighestDeviceNumber = this_device_extension->device_number;

                if (this_device_extension->device_number < 64)
                    DeviceList.QuadPart |=
                    1ULL << this_device_extension->device_number;
            }

            ImDiskReleaseLock(&lock_handle);

            KdPrint(("ImDisk: 64 bit device list = 0x%.8x%.8x.\n",
                DeviceList.HighPart, DeviceList.LowPart));

            if (io_stack->Parameters.DeviceIoControl.OutputBufferLength >=
                sizeof(ULONGLONG))
            {
                *(PULONGLONG)Irp->AssociatedIrp.SystemBuffer =
                    DeviceList.QuadPart;
                Irp->IoStatus.Information = sizeof(ULONGLONG);

                if (HighestDeviceNumber > 63)
                    status = STATUS_INVALID_PARAMETER;
                else
                    status = STATUS_SUCCESS;
            }
            else if (io_stack->Parameters.DeviceIoControl.OutputBufferLength >=
                sizeof(ULONG))
            {
                *(PULONG)Irp->AssociatedIrp.SystemBuffer = DeviceList.LowPart;
                Irp->IoStatus.Information = sizeof(ULONG);

                if (HighestDeviceNumber > 31)
                    status = STATUS_INVALID_PARAMETER;
                else
                    status = STATUS_SUCCESS;
            }
            else
            {
                status = STATUS_INVALID_PARAMETER;
            }
        }

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
            break;
        }

        create_data = (PIMDISK_CREATE_DATA)Irp->AssociatedIrp.SystemBuffer;

        create_data->DeviceNumber = device_extension->device_number;
        create_data->DiskGeometry = device_extension->disk_geometry;

        create_data->Flags = 0;
        if (device_extension->read_only)
            create_data->Flags |= IMDISK_OPTION_RO;

        if (DeviceObject->Characteristics & FILE_REMOVABLE_MEDIA)
            create_data->Flags |= IMDISK_OPTION_REMOVABLE;

        if (DeviceObject->DeviceType == FILE_DEVICE_UNKNOWN)
            create_data->Flags |= IMDISK_DEVICE_TYPE_RAW;
        else if (DeviceObject->DeviceType == FILE_DEVICE_CD_ROM)
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

        if (device_extension->awealloc_disk)
            create_data->Flags |= IMDISK_FILE_TYPE_AWEALLOC;
        else if (device_extension->parallel_io)
            create_data->Flags |= IMDISK_FILE_TYPE_PARALLEL_IO;

        if (device_extension->image_modified)
            create_data->Flags |= IMDISK_IMAGE_MODIFIED;

        if (device_extension->use_set_zero_data)
            create_data->Flags |= IMDISK_OPTION_SPARSE_FILE;

        if (device_extension->shared_image)
            create_data->Flags |= IMDISK_OPTION_SHARED_IMAGE;

        create_data->ImageOffset = device_extension->image_offset;

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

            if (io_stack->Parameters.DeviceIoControl.OutputBufferLength >=
                sizeof(ULONG))
            {
                *(PULONG)Irp->AssociatedIrp.SystemBuffer =
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
            *(PULONG)Irp->AssociatedIrp.SystemBuffer = IMDISK_DRIVER_VERSION;
            Irp->IoStatus.Information = sizeof(ULONG);
            status = STATUS_SUCCESS;
        }

        break;
    }

    case IOCTL_IMDISK_IOCTL_PASS_THROUGH:
    case IOCTL_IMDISK_FSCTL_PASS_THROUGH:
    {
        KdPrint(("ImDisk: IOCTL_IMDISK_IOCTL/FSCTL_PASS_THROUGH for device %i.\n",
            device_extension->device_number));

        if (device_extension->file_handle == NULL)
        {
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (device_extension->parallel_io &&
            (METHOD_FROM_CTL_CODE(
                io_stack->Parameters.DeviceIoControl.IoControlCode) ==
                METHOD_BUFFERED))
        {
            return ImDiskDeviceControlLowerDevice(Irp, device_extension);
        }

        status = STATUS_PENDING;
        break;
    }

    case IOCTL_DISK_FORMAT_TRACKS:
    case IOCTL_DISK_FORMAT_TRACKS_EX:
        //	Only several checks are done here
        //	Actual operation is done by the device thread
    {
        PFORMAT_PARAMETERS param;
        PDISK_GEOMETRY geometry;

        KdPrint(("ImDisk: IOCTL_DISK_FORMAT_TRACKS for device %i.\n",
            device_extension->device_number));

        /*
        if (~DeviceObject->Characteristics & FILE_FLOPPY_DISKETTE)
        {
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
        }
        */

        //	Media is writable?

        if (device_extension->read_only)
        {
            KdPrint(("ImDisk: Attempt to format write-protected image.\n"));

            status = STATUS_MEDIA_WRITE_PROTECTED;
            break;
        }

        //	Check input parameter size

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(FORMAT_PARAMETERS))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        //	Input parameter sanity check

        param = (PFORMAT_PARAMETERS)Irp->AssociatedIrp.SystemBuffer;
        geometry = (PDISK_GEOMETRY)
            ExAllocatePoolWithTag(NonPagedPool,
                sizeof(DISK_GEOMETRY),
                POOL_TAG);
        if (geometry == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        RtlCopyMemory(geometry, &device_extension->disk_geometry,
            sizeof(DISK_GEOMETRY));

        geometry->Cylinders.QuadPart /= geometry->TracksPerCylinder;
        geometry->Cylinders.QuadPart /= geometry->SectorsPerTrack;
        geometry->Cylinders.QuadPart /= geometry->BytesPerSector;

        if ((param->StartHeadNumber > geometry->TracksPerCylinder - 1) ||
            (param->EndHeadNumber   > geometry->TracksPerCylinder - 1) ||
            ((LONGLONG)param->StartCylinderNumber >
                geometry->Cylinders.QuadPart) ||
            ((LONGLONG)param->EndCylinderNumber >
                geometry->Cylinders.QuadPart) ||
            (param->EndCylinderNumber	< param->StartCylinderNumber))
        {
            ExFreePoolWithTag(geometry, POOL_TAG);
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if ((param->StartCylinderNumber * geometry->TracksPerCylinder *
            geometry->BytesPerSector * geometry->SectorsPerTrack +
            param->StartHeadNumber * geometry->BytesPerSector *
            geometry->SectorsPerTrack >=
            device_extension->disk_geometry.Cylinders.QuadPart) |
            (param->EndCylinderNumber * geometry->TracksPerCylinder *
                geometry->BytesPerSector * geometry->SectorsPerTrack +
                param->EndHeadNumber * geometry->BytesPerSector *
                geometry->SectorsPerTrack >=
                device_extension->disk_geometry.Cylinders.QuadPart))
        {
            ExFreePoolWithTag(geometry, POOL_TAG);
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
                ExFreePoolWithTag(geometry, POOL_TAG);
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
                ExFreePoolWithTag(geometry, POOL_TAG);
                status = STATUS_INVALID_PARAMETER;
                break;
            }
        }

        ExFreePoolWithTag(geometry, POOL_TAG);
        status = STATUS_PENDING;
        break;
    }

    case IOCTL_DISK_GROW_PARTITION:
    {
        PDISK_GROW_PARTITION grow_partition;

        KdPrint(("ImDisk: IOCTL_DISK_GROW_PARTITION for device %i.\n",
            device_extension->device_number));

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength !=
            sizeof(DISK_GROW_PARTITION))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        if (device_extension->read_only)
        {
            status = STATUS_MEDIA_WRITE_PROTECTED;
            break;
        }

        grow_partition = (PDISK_GROW_PARTITION)
            Irp->AssociatedIrp.SystemBuffer;

        // Check so we don't get a smaller disk with these parameters
        if ((grow_partition->PartitionNumber != 1) |
            (device_extension->disk_geometry.Cylinders.QuadPart +
                grow_partition->BytesToGrow.QuadPart <
                device_extension->disk_geometry.Cylinders.QuadPart))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        status = STATUS_PENDING;
        break;
    }

    case IOCTL_DISK_UPDATE_PROPERTIES:
    {
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_DISK_GET_MEDIA_TYPES:
    case IOCTL_STORAGE_GET_MEDIA_TYPES:
    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    case IOCTL_CDROM_GET_DRIVE_GEOMETRY:
    case IOCTL_DISK_UPDATE_DRIVE_SIZE:
    {
        PDISK_GEOMETRY geometry;

        KdPrint(("ImDisk: IOCTL_DISK/STORAGE_GET_MEDIA_TYPES/DRIVE_GEOMETRY "
            "for device %i.\n", device_extension->device_number));

        if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
            sizeof(device_extension->disk_geometry))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        geometry = (PDISK_GEOMETRY)Irp->AssociatedIrp.SystemBuffer;
        *geometry = device_extension->disk_geometry;
        geometry->Cylinders.QuadPart /= geometry->TracksPerCylinder;
        geometry->Cylinders.QuadPart /= geometry->SectorsPerTrack;
        geometry->Cylinders.QuadPart /= geometry->BytesPerSector;

        status = STATUS_SUCCESS;
        Irp->IoStatus.Information = sizeof(DISK_GEOMETRY);
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
            break;
        }

        ((PGET_LENGTH_INFORMATION)Irp->AssociatedIrp.SystemBuffer)->
            Length.QuadPart =
            device_extension->disk_geometry.Cylinders.QuadPart;

        status = STATUS_SUCCESS;
        Irp->IoStatus.Information = sizeof(GET_LENGTH_INFORMATION);

        break;
    }

    case IOCTL_DISK_GET_PARTITION_INFO:
    {
        PPARTITION_INFORMATION partition_information;

        KdPrint(("ImDisk: IOCTL_DISK_GET_PARTITION_INFO for device %i.\n",
            device_extension->device_number));

        if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
            sizeof(PARTITION_INFORMATION))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        partition_information =
            (PPARTITION_INFORMATION)Irp->AssociatedIrp.SystemBuffer;

        partition_information->StartingOffset.QuadPart =
            (LONGLONG)device_extension->disk_geometry.BytesPerSector *
            device_extension->disk_geometry.SectorsPerTrack;
        partition_information->PartitionLength =
            device_extension->disk_geometry.Cylinders;
        partition_information->HiddenSectors = 1;
        partition_information->PartitionNumber = 1;
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

        KdPrint(("ImDisk: IOCTL_DISK_GET_PARTITION_INFO_EX for device %i.\n",
            device_extension->device_number));

        if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
            sizeof(PARTITION_INFORMATION_EX))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        partition_information_ex =
            (PPARTITION_INFORMATION_EX)Irp->AssociatedIrp.SystemBuffer;

        partition_information_ex->PartitionStyle = PARTITION_STYLE_MBR;
        partition_information_ex->StartingOffset.QuadPart =
            (LONGLONG)device_extension->disk_geometry.BytesPerSector *
            device_extension->disk_geometry.SectorsPerTrack;
        partition_information_ex->PartitionLength =
            device_extension->disk_geometry.Cylinders;
        partition_information_ex->PartitionNumber = 1;
        partition_information_ex->RewritePartition = FALSE;
        partition_information_ex->Mbr.PartitionType = PARTITION_HUGE;
        partition_information_ex->Mbr.BootIndicator = FALSE;
        partition_information_ex->Mbr.RecognizedPartition = FALSE;
        partition_information_ex->Mbr.HiddenSectors = 1;

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

        break;
    }

    case IOCTL_DISK_MEDIA_REMOVAL:
    case IOCTL_STORAGE_MEDIA_REMOVAL:
    {
        KdPrint(("ImDisk: IOCTL_DISK/STORAGE_MEDIA_REMOVAL for device %i.\n",
            device_extension->device_number));

        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES:
    {
        PDEVICE_MANAGE_DATA_SET_ATTRIBUTES attrs =
            (PDEVICE_MANAGE_DATA_SET_ATTRIBUTES)
            Irp->AssociatedIrp.SystemBuffer;

        if ((io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(DEVICE_MANAGE_DATA_SET_ATTRIBUTES)) ||
            (io_stack->Parameters.DeviceIoControl.InputBufferLength <
                (attrs->DataSetRangesOffset + attrs->DataSetRangesLength)))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (attrs->Action != DeviceDsmAction_Trim)
        {
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        status = STATUS_SUCCESS;

        int items = attrs->DataSetRangesLength /
            sizeof(DEVICE_DATA_SET_RANGE);

        if (items <= 0)
        {
            break;
        }

        if ((device_extension->use_proxy && !device_extension->proxy_unmap) ||
            device_extension->vm_disk ||
            device_extension->awealloc_disk)
        {
            break;
        }

        if (device_extension->use_proxy)
        {
            status = STATUS_PENDING;
            break;
        }

        FILE_ZERO_DATA_INFORMATION zerodata = { 0 };

        PDEVICE_DATA_SET_RANGE range = (PDEVICE_DATA_SET_RANGE)
            ((PUCHAR)attrs + attrs->DataSetRangesOffset);

        for (int i = 0; i < items; i++)
        {
            KdPrint(("ImDisk: Trim request 0x%I64X bytes at 0x%I64X\n",
                range[i].LengthInBytes, range[i].StartingOffset));

            zerodata.FileOffset.QuadPart = range[i].StartingOffset +
                device_extension->image_offset.QuadPart;

            zerodata.BeyondFinalZero.QuadPart = zerodata.FileOffset.QuadPart +
                range[i].LengthInBytes;

            status = ZwFsControlFile(
                device_extension->file_handle,
                NULL,
                NULL,
                NULL,
                &Irp->IoStatus,
                FSCTL_SET_ZERO_DATA,
                &zerodata,
                sizeof(zerodata),
                NULL,
                0);

            KdPrint(("ImDisk: FSCTL_SET_ZERO_DATA result: 0x%#X\n", status));

            if (!NT_SUCCESS(status))
            {
                break;
            }
        }

        if (!device_extension->no_file_level_trim)
        {
            IO_STATUS_BLOCK io_status;
            ULONG fltrim_size = FIELD_OFFSET(FILE_LEVEL_TRIM, Ranges) +
                (items * sizeof(FILE_LEVEL_TRIM_RANGE));

            PFILE_LEVEL_TRIM fltrim = (PFILE_LEVEL_TRIM)
                ExAllocatePoolWithTag(PagedPool, fltrim_size, POOL_TAG);

            if (fltrim == NULL)
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            RtlZeroMemory(fltrim, fltrim_size);

            fltrim->NumRanges = items;
            for (int i = 0; i < items; i++)
            {
                KdPrint(("ImDisk: Trim request 0x%I64X bytes at 0x%I64X\n",
                    range[i].LengthInBytes, range[i].StartingOffset));

                fltrim->Ranges[i].Length = range[i].LengthInBytes;
                fltrim->Ranges[i].Offset = range[i].StartingOffset +
                    device_extension->image_offset.QuadPart;
            }

            status = ZwFsControlFile(
                device_extension->file_handle,
                NULL,
                NULL,
                NULL,
                &io_status,
                FSCTL_FILE_LEVEL_TRIM,
                fltrim,
                fltrim_size,
                NULL,
                0);

            ExFreePoolWithTag(fltrim, POOL_TAG);

            KdPrint(("ImDisk: FSCTL_FILE_LEVEL_TRIM result: %#x\n", status));

            if (!NT_SUCCESS(status))
            {
                device_extension->no_file_level_trim = TRUE;
                status = STATUS_SUCCESS;
            }
        }

        break;
    }

    case IOCTL_CDROM_GET_LAST_SESSION:
    {
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_CDROM_READ_TOC:
    {
        PCDROM_TOC cdrom_toc;

        KdPrint(("ImDisk: IOCTL_CDROM_READ_TOC for device %i.\n",
            device_extension->device_number));

        if (DeviceObject->DeviceType != FILE_DEVICE_CD_ROM)
        {
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
            sizeof(CDROM_TOC))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        cdrom_toc = (PCDROM_TOC)Irp->AssociatedIrp.SystemBuffer;

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
            break;
        }

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(SET_PARTITION_INFORMATION))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        status = STATUS_SUCCESS;
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
            break;
        }

        if (io_stack->Parameters.DeviceIoControl.InputBufferLength <
            sizeof(SET_PARTITION_INFORMATION_EX))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        partition_information_ex = (PSET_PARTITION_INFORMATION_EX)
            Irp->AssociatedIrp.SystemBuffer;

        if (partition_information_ex->PartitionStyle != PARTITION_STYLE_MBR)
        {
            status = STATUS_UNSUCCESSFUL;
        }
        else
        {
            status = STATUS_SUCCESS;
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
            break;
        }

        verify_information = (PVERIFY_INFORMATION)
            Irp->AssociatedIrp.SystemBuffer;

        if (device_extension->read_only)
        {
            KdPrint(("ImDisk: Attempt to verify read-only media.\n"));

            status = STATUS_MEDIA_WRITE_PROTECTED;
            break;
        }

        if (verify_information->StartingOffset.QuadPart +
            verify_information->Length >
            device_extension->disk_geometry.Cylinders.QuadPart)
        {
            KdPrint(("ImDisk: Attempt to verify beyond image size.\n"));

            status = STATUS_NONEXISTENT_SECTOR;
            break;
        }

        status = STATUS_SUCCESS;
        break;
    }

    // Ver 1.0.2 does no longer handle IOCTL_STORAGE_GET_DEVICE_NUMBER.
    // It was a very ugly attempt to workaround some problems that do not
    // seem to exist any longer anyway. The data returned here made no sense
    // actually so in order to not risk breaking more things in the future I
    // have removed it completely.
    /*
    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
    {
    PSTORAGE_DEVICE_NUMBER device_number;

    KdPrint(("ImDisk: IOCTL_STORAGE_GET_DEVICE_NUMBER for device %i.\n",
    device_extension->device_number));

    if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
    sizeof(STORAGE_DEVICE_NUMBER))
    {
    status = STATUS_INVALID_PARAMETER;
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
    */

    case IOCTL_STORAGE_GET_HOTPLUG_INFO:
    {
        PSTORAGE_HOTPLUG_INFO hotplug_info;

        KdPrint(("ImDisk: IOCTL_STORAGE_GET_HOTPLUG_INFO for device %i.\n",
            device_extension->device_number));

        if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
            sizeof(STORAGE_HOTPLUG_INFO))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        hotplug_info = (PSTORAGE_HOTPLUG_INFO)
            Irp->AssociatedIrp.SystemBuffer;

        hotplug_info->Size = sizeof(STORAGE_HOTPLUG_INFO);
        if (DeviceObject->Characteristics & FILE_REMOVABLE_MEDIA)
        {
            hotplug_info->MediaRemovable = TRUE;
            hotplug_info->MediaHotplug = TRUE;
            hotplug_info->DeviceHotplug = TRUE;
            hotplug_info->WriteCacheEnableOverride = FALSE;
        }
        else
        {
            hotplug_info->MediaRemovable = FALSE;
            hotplug_info->MediaHotplug = FALSE;
            hotplug_info->DeviceHotplug = FALSE;
            hotplug_info->WriteCacheEnableOverride = FALSE;
        }

        status = STATUS_SUCCESS;
        Irp->IoStatus.Information = sizeof(STORAGE_HOTPLUG_INFO);

        break;
    }

    /*     case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME: */
    /*       { */
    /* 	PMOUNTDEV_NAME mountdev_name = Irp->AssociatedIrp.SystemBuffer; */
    /* 	int chars; */

    /* 	KdPrint(("ImDisk: IOCTL_MOUNTDEV_QUERY_DEVICE_NAME for device %i.\n", */
    /* 		 device_extension->device_number)); */

    /* 	if ((io_stack->Parameters.DeviceIoControl.OutputBufferLength == 4) & */
    /* 	    (device_extension->drive_letter != 0)) */
    /* 	  { */
    /* 	    mountdev_name->Name[0] = device_extension->drive_letter; */
    /* 	    mountdev_name->Name[1] = L':'; */
    /* 	    chars = 2; */
    /* 	  } */
    /* 	else */
    /* 	  chars = */
    /* 	    _snwprintf(mountdev_name->Name, */
    /* 		       (io_stack-> */
    /* 			Parameters.DeviceIoControl.OutputBufferLength - */
    /* 			FIELD_OFFSET(MOUNTDEV_NAME, Name)) >> 1, */
    /* 		       IMDISK_DEVICE_BASE_NAME L"%u", */
    /* 		       device_extension->device_number); */
    /* // 	  else */
    /* // 	  chars = */
    /* // 	  _snwprintf(mountdev_name->Name, */
    /* // 	  (io_stack-> */
    /* // 	  Parameters.DeviceIoControl.OutputBufferLength - */
    /* // 	  FIELD_OFFSET(MOUNTDEV_NAME, Name)) >> 1, */
    /* // 	  L"\\DosDevices\\%wc:", */
    /* // 	  device_extension->drive_letter); */

    /* 	if (chars < 0) */
    /* 	  { */
    /* 	    if (io_stack->Parameters.DeviceIoControl.OutputBufferLength >= */
    /* 		FIELD_OFFSET(MOUNTDEV_NAME, Name) + */
    /* 		sizeof(mountdev_name->NameLength)) */
    /* 	      mountdev_name->NameLength = sizeof(IMDISK_DEVICE_BASE_NAME) + */
    /* 		20; */

    /* 	    KdPrint(("ImDisk: IOCTL_MOUNTDEV_QUERY_DEVICE_NAME overflow, " */
    /* 		     "buffer length %u, returned %i.\n", */
    /* 		     io_stack->Parameters.DeviceIoControl.OutputBufferLength, */
    /* 		     chars)); */

    /* 	    status = STATUS_BUFFER_OVERFLOW; */

    /* 	    if (io_stack->Parameters.DeviceIoControl.OutputBufferLength >= */
    /* 		sizeof(MOUNTDEV_NAME)) */
    /* 	      Irp->IoStatus.Information = sizeof(MOUNTDEV_NAME); */

    /* 	    break; */
    /* 	  } */

    /* 	mountdev_name->NameLength = (USHORT) chars << 1; */

    /* 	status = STATUS_SUCCESS; */
    /* 	Irp->IoStatus.Information = */
    /* 	  FIELD_OFFSET(MOUNTDEV_NAME, Name) + mountdev_name->NameLength; */

    /* 	KdPrint(("ImDisk: IOCTL_MOUNTDEV_QUERY_DEVICE_NAME returning %ws, " */
    /* 		 "length %u total %u.\n", */
    /* 		 mountdev_name->Name, mountdev_name->NameLength, */
    /* 		 Irp->IoStatus.Information)); */

    /* 	break; */
    /*       } */

    default:
    {
        KdPrint(("ImDisk: Unknown IOCTL %#x.\n",
            io_stack->Parameters.DeviceIoControl.IoControlCode));

        status = STATUS_INVALID_DEVICE_REQUEST;
    }
    }

    if (status == STATUS_PENDING)
    {
        IoMarkIrpPending(Irp);

        ImDiskInterlockedInsertTailList(&device_extension->list_head,
            &Irp->Tail.Overlay.ListEntry,
            &device_extension->list_lock);

        KeSetEvent(&device_extension->request_event, (KPRIORITY)0, FALSE);
    }
    else
    {
        Irp->IoStatus.Status = status;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return status;
}

NTSTATUS
ImDiskFlushBuffers(IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PDEVICE_EXTENSION device_extension;
    PIO_STACK_LOCATION io_stack;
    NTSTATUS status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Irp != NULL);

    device_extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    io_stack = IoGetCurrentIrpStackLocation(Irp);

    // The control device cannot receive flush dispatch.
    if (DeviceObject == ImDiskCtlDevice)
    {
        KdPrint(("ImDisk: flush function %#x invalid for control device.\n",
            io_stack->MinorFunction));

        status = STATUS_INVALID_DEVICE_REQUEST;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        return status;
    }

    KdPrint(("ImDisk: Device %i received flush function %#x IRP %p.\n",
        device_extension->device_number,
        io_stack->MinorFunction,
        Irp));

    if (KeReadStateEvent(&device_extension->terminate_thread) != 0)
    {
        KdPrint(("ImDisk: flush dispatch on device %i that is being removed.\n",
            device_extension->device_number));

        status = STATUS_DEVICE_REMOVED;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    if (device_extension->read_only)
    {
        status = STATUS_MEDIA_WRITE_PROTECTED;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    if (device_extension->use_proxy | device_extension->vm_disk)
    {
        Irp->IoStatus.Status = STATUS_SUCCESS;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return STATUS_SUCCESS;
    }
    else if (device_extension->parallel_io)
    {
        return ImDiskReadWriteLowerDevice(Irp, device_extension);
    }
    else
    {
        IoMarkIrpPending(Irp);

        ImDiskInterlockedInsertTailList(&device_extension->list_head,
            &Irp->Tail.Overlay.ListEntry,
            &device_extension->list_lock);

        KeSetEvent(&device_extension->request_event, (KPRIORITY)0, FALSE);

        return STATUS_PENDING;
    }
}

NTSTATUS
ImDiskQueryInformation(IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PDEVICE_EXTENSION device_extension;
    PIO_STACK_LOCATION io_stack;
    NTSTATUS status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Irp != NULL);

    device_extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    io_stack = IoGetCurrentIrpStackLocation(Irp);

    KdPrint2(("ImDisk: QueryInformation: %u.\n",
        io_stack->Parameters.QueryFile.FileInformationClass));

    // The control device cannot receive PnP dispatch.
    if (DeviceObject == ImDiskCtlDevice)
    {
        KdPrint(("ImDisk: QueryInformation function %#x invalid for control device.\n",
            io_stack->Parameters.QueryFile.FileInformationClass));

        status = STATUS_INVALID_DEVICE_REQUEST;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        return status;
    }

    KdPrint(("ImDisk: Device %i received QueryInformation function %#x IRP %p.\n",
        device_extension->device_number,
        io_stack->Parameters.QueryFile.FileInformationClass,
        Irp));

    if (KeReadStateEvent(&device_extension->terminate_thread) != 0)
    {
        KdPrint(("ImDisk: QueryInformation dispatch on device %i that is being removed.\n",
            device_extension->device_number));

        status = STATUS_DEVICE_REMOVED;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    RtlZeroMemory(Irp->AssociatedIrp.SystemBuffer,
        io_stack->Parameters.QueryFile.Length);

    switch (io_stack->Parameters.QueryFile.FileInformationClass)
    {
    case FileStandardInformation:
    {
        PFILE_STANDARD_INFORMATION standard_info =
            (PFILE_STANDARD_INFORMATION)Irp->AssociatedIrp.SystemBuffer;

        if (io_stack->Parameters.QueryFile.Length < sizeof(FILE_STANDARD_INFORMATION))
        {
            Irp->IoStatus.Information = 0;
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        standard_info->AllocationSize =
            standard_info->EndOfFile = device_extension->disk_geometry.Cylinders;

        Irp->IoStatus.Information = sizeof(FILE_STANDARD_INFORMATION);
        status = STATUS_SUCCESS;
        break;
    }

    case FilePositionInformation:
    {
        PFILE_POSITION_INFORMATION position_info =
            (PFILE_POSITION_INFORMATION)Irp->AssociatedIrp.SystemBuffer;

        if (io_stack->Parameters.QueryFile.Length <
            sizeof(FILE_POSITION_INFORMATION))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        if (io_stack->FileObject != NULL)
        {
            position_info->CurrentByteOffset =
                io_stack->FileObject->CurrentByteOffset;
        }

        Irp->IoStatus.Information = sizeof(FILE_POSITION_INFORMATION);

        status = STATUS_SUCCESS;
        break;
    }

    default:
        KdPrint(("ImDisk: Unknown QueryInformation function %#x.\n",
            io_stack->Parameters.QueryFile.FileInformationClass));

        status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
    }

    if (status == STATUS_PENDING)
    {
        IoMarkIrpPending(Irp);

        ImDiskInterlockedInsertTailList(&device_extension->list_head,
            &Irp->Tail.Overlay.ListEntry,
            &device_extension->list_lock);

        KeSetEvent(&device_extension->request_event, (KPRIORITY)0, FALSE);
    }
    else
    {
        Irp->IoStatus.Status = status;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return status;
}

NTSTATUS
ImDiskSetInformation(IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PDEVICE_EXTENSION device_extension;
    PIO_STACK_LOCATION io_stack;
    NTSTATUS status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Irp != NULL);

    device_extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    io_stack = IoGetCurrentIrpStackLocation(Irp);

    KdPrint2(("ImDisk: SetInformation: %u.\n",
        io_stack->Parameters.SetFile.FileInformationClass));

    // The control device cannot receive PnP dispatch.
    if (DeviceObject == ImDiskCtlDevice)
    {
        KdPrint(("ImDisk: SetInformation function %#x invalid for control device.\n",
            io_stack->Parameters.SetFile.FileInformationClass));

        status = STATUS_INVALID_DEVICE_REQUEST;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        return status;
    }

    KdPrint(("ImDisk: Device %i received SetInformation function %#x IRP %p.\n",
        device_extension->device_number,
        io_stack->Parameters.SetFile.FileInformationClass,
        Irp));

    if (KeReadStateEvent(&device_extension->terminate_thread) != 0)
    {
        KdPrint(("ImDisk: SetInformation dispatch on device %i that is being removed.\n",
            device_extension->device_number));

        status = STATUS_DEVICE_REMOVED;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    switch (io_stack->Parameters.SetFile.FileInformationClass)
    {
    case FilePositionInformation:
    {
        PFILE_POSITION_INFORMATION position_info =
            (PFILE_POSITION_INFORMATION)Irp->AssociatedIrp.SystemBuffer;

        if (io_stack->Parameters.SetFile.Length <
            sizeof(FILE_POSITION_INFORMATION))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (io_stack->FileObject != NULL)
        {
            io_stack->FileObject->CurrentByteOffset =
                position_info->CurrentByteOffset;
        }

        status = STATUS_SUCCESS;
        break;
    }

    case FileBasicInformation:
    case FileDispositionInformation:
    case FileValidDataLengthInformation:
        status = STATUS_SUCCESS;
        break;

    default:
    {
        KdPrint(("ImDisk: Unknown SetInformation function %#x.\n",
            io_stack->Parameters.SetFile.FileInformationClass));

        status = STATUS_INVALID_DEVICE_REQUEST;
    }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;

    KdPrint2(("ImDisk: SetFile.FileInformationClass %u result status %#x\n",
        io_stack->Parameters.SetFile.FileInformationClass, status));

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

NTSTATUS
ImDiskDispatchPnP(IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PDEVICE_EXTENSION device_extension;
    PIO_STACK_LOCATION io_stack;
    NTSTATUS status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Irp != NULL);

    device_extension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    io_stack = IoGetCurrentIrpStackLocation(Irp);

    // The control device cannot receive PnP dispatch.
    if (DeviceObject == ImDiskCtlDevice)
    {
        KdPrint(("ImDisk: PnP function %#x invalid for control device.\n",
            io_stack->MinorFunction));

        status = STATUS_INVALID_DEVICE_REQUEST;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    KdPrint(("ImDisk: Device %i received PnP function %#x IRP %p.\n",
        device_extension->device_number,
        io_stack->MinorFunction,
        Irp));

    if (KeReadStateEvent(&device_extension->terminate_thread) != 0)
    {
        KdPrint(("ImDisk: PnP dispatch on device %i that is being removed.\n",
            device_extension->device_number));

        status = STATUS_DEVICE_REMOVED;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return status;
    }

    switch (io_stack->MinorFunction)
    {
    case IRP_MN_DEVICE_USAGE_NOTIFICATION:
        KdPrint(("ImDisk: Device %i got IRP_MN_DEVICE_USAGE_NOTIFICATION.\n",
            device_extension->device_number));

        switch (io_stack->Parameters.UsageNotification.Type)
        {
        case DeviceUsageTypePaging:
        case DeviceUsageTypeDumpFile:
            if (device_extension->read_only)
                status = STATUS_MEDIA_WRITE_PROTECTED;
            else
                if (io_stack->Parameters.UsageNotification.InPath == TRUE)
                {
                    (VOID)MmLockPagableCodeSection((PVOID)(ULONG_PTR)ImDiskDeviceThread);
                }

            IoAdjustPagingPathCount
                (&device_extension->special_file_count,
                    io_stack->Parameters.UsageNotification.InPath);
            status = STATUS_SUCCESS;

            break;

        default:
            status = STATUS_NOT_SUPPORTED;
        }

        break;

    default:
        KdPrint(("ImDisk: Unknown PnP function %#x.\n",
            io_stack->MinorFunction));

        status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
    }

    if (status == STATUS_PENDING)
    {
        IoMarkIrpPending(Irp);

        ImDiskInterlockedInsertTailList(&device_extension->list_head,
            &Irp->Tail.Overlay.ListEntry,
            &device_extension->list_lock);

        KeSetEvent(&device_extension->request_event, (KPRIORITY)0, FALSE);
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
    LARGE_INTEGER time_out;
    BOOLEAN system_drive_letter;

    ASSERT(Context != NULL);

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    device_thread_data = (PDEVICE_THREAD_DATA)Context;

    system_drive_letter = !device_thread_data->caller_waiting;

    // This is in case this thread is created by
    // ImDiskAddVirtualDiskAfterInitialization() when called from DriverEntry().
    // That indicates that no-one is waiting for us to return any status
    // in the device_thread_data members and that there is no-one freeing the
    // init structures after we are finished with them.
    // It also means that we need to wait for the control device to get ready for
    // I/O (in case a proxy or something need to call this driver during device
    // initialization).
    while (ImDiskCtlDevice->Flags & DO_DEVICE_INITIALIZING)
    {
        LARGE_INTEGER wait_time;

        KdPrint2(("ImDisk: Driver still initializing, waiting 100 ms...\n"));

        wait_time.QuadPart = -1000000;
        KeDelayExecutionThread(KernelMode, FALSE, &wait_time);
    }

    device_thread_data->status = ImDiskCreateDevice(
        device_thread_data->driver_object,
        device_thread_data->create_data,
        device_thread_data->client_thread,
        &device_object);

    if (!NT_SUCCESS(device_thread_data->status))
    {
        if (device_thread_data->caller_waiting)
            KeSetEvent(&device_thread_data->created_event, (KPRIORITY)0, FALSE);
        else
        {
            ExFreePoolWithTag(device_thread_data->create_data, POOL_TAG);
            ExFreePoolWithTag(device_thread_data, POOL_TAG);
        }

        ImDiskLogError((device_thread_data->driver_object,
            0,
            0,
            NULL,
            0,
            1000,
            device_thread_data->status,
            102,
            device_thread_data->status,
            0,
            0,
            NULL,
            L"Error creating virtual disk."));

        PsTerminateSystemThread(STATUS_SUCCESS);
    }

    // Now we are done with initialization. Let the one that asks us to create
    // this device now that, or if no-one left there, clean up init structures
    // here.
    if (device_thread_data->caller_waiting)
        KeSetEvent(&device_thread_data->created_event, (KPRIORITY)0, FALSE);
    else
    {
        ImDiskCreateDriveLetter(device_thread_data->create_data->DriveLetter,
            device_thread_data->create_data->DeviceNumber);

        ExFreePoolWithTag(device_thread_data->create_data, POOL_TAG);
        ExFreePoolWithTag(device_thread_data, POOL_TAG);
    }

    // Fire refresh event
    if (RefreshEvent != NULL)
        KePulseEvent(RefreshEvent, 0, FALSE);

    KdPrint(("ImDisk: Device thread initialized. (flags=%#x)\n",
        device_object->Flags));

    device_extension = (PDEVICE_EXTENSION)device_object->DeviceExtension;

    time_out.QuadPart = -1000000;

    // If this is a VM backed disk that should be pre-loaded with an image file
    // we have to load the contents of that file now before entering the service
    // loop.
    if (device_extension->vm_disk && (device_extension->file_handle != NULL))
    {
        LARGE_INTEGER byte_offset = device_extension->image_offset;
        IO_STATUS_BLOCK io_status;
        NTSTATUS status;
#ifdef _WIN64
        SIZE_T disk_size = device_extension->disk_geometry.Cylinders.QuadPart;
#else
        SIZE_T disk_size = device_extension->disk_geometry.Cylinders.LowPart;
#endif

        KdPrint(("ImDisk: Reading image file into vm disk buffer.\n"));

        status =
            ImDiskSafeReadFile(device_extension->file_handle,
                &io_status,
                device_extension->image_buffer,
                disk_size,
                &byte_offset);

        ZwClose(device_extension->file_handle);
        device_extension->file_handle = NULL;

        // Failure to read pre-load image is now considered a fatal error
        if (!NT_SUCCESS(status))
        {
            KdPrint(("ImDisk: Failed to read image file (%#x).\n", status));

            ImDiskRemoveVirtualDisk(device_object);
        }
        else
        {
            KdPrint(("ImDisk: Image loaded successfully.\n"));

            if (device_extension->byte_swap)
                ImDiskByteSwapBuffer(device_extension->image_buffer,
                    disk_size);
        }
    }

    for (;;)
    {
        PIRP irp;
        PIO_STACK_LOCATION io_stack;
        PLIST_ENTRY request;

        KeClearEvent(&device_extension->request_event);

        request =
            ImDiskInterlockedRemoveHeadList(&device_extension->list_head,
                &device_extension->list_lock);

        if (request == NULL)
        {
            PWCHAR symlink_name_buffer;
            NTSTATUS status;
            PKEVENT wait_objects[] = {
                &device_extension->request_event,
                &device_extension->terminate_thread
            };

            KdPrint2(("ImDisk: No pending requests. Waiting.\n"));

            status = KeWaitForMultipleObjects(sizeof(wait_objects) /
                sizeof(*wait_objects),
                (PVOID*)wait_objects,
                WaitAny,
                Executive,
                KernelMode,
                FALSE,
                NULL,
                NULL);

            // While pending requests in queue, service them before terminating
            // thread.
            if (KeReadStateEvent(&device_extension->request_event))
                continue;

            KdPrint(("ImDisk: Device %i thread is shutting down.\n",
                device_extension->device_number));

            // Fire refresh event
            if (RefreshEvent != NULL)
                KePulseEvent(RefreshEvent, 0, FALSE);

            if (device_extension->drive_letter != 0)
                if (system_drive_letter)
                    ImDiskRemoveDriveLetter(device_extension->drive_letter);

            ImDiskCloseProxy(&device_extension->proxy);

            if (device_extension->last_io_data != NULL)
            {
                ExFreePoolWithTag(device_extension->last_io_data, POOL_TAG);
                device_extension->last_io_data = NULL;
            }

            if (device_extension->vm_disk)
            {
                SIZE_T free_size = 0;
                if (device_extension->image_buffer != NULL)
                    ZwFreeVirtualMemory(NtCurrentProcess(),
                        (PVOID*)&device_extension->image_buffer,
                        &free_size, MEM_RELEASE);

                device_extension->image_buffer = NULL;
            }
            else
            {
                if (device_extension->file_handle != NULL)
                    ZwClose(device_extension->file_handle);

                device_extension->file_handle = NULL;

                if (device_extension->file_object != NULL)
                    ObDereferenceObject(device_extension->file_object);

                device_extension->file_object = NULL;
            }

            if (device_extension->file_name.Buffer != NULL)
            {
                ExFreePoolWithTag(device_extension->file_name.Buffer, POOL_TAG);
                device_extension->file_name.Buffer = NULL;
                device_extension->file_name.Length = 0;
                device_extension->file_name.MaximumLength = 0;
            }

            // If ReferenceCount is not zero, this device may have outstanding
            // IRP-s or otherwise unfinished things to do. Let IRP-s be done by
            // continuing this dispatch loop until ReferenceCount is zero.
#pragma warning(suppress: 28175)
            if (device_object->ReferenceCount != 0)
            {
#if DBG
#pragma warning(suppress: 28175)
                LONG ref_count = device_object->ReferenceCount;
                DbgPrint("ImDisk: Device %i has %i references. Waiting.\n",
                    device_extension->device_number,
                    ref_count);
#endif

                KeDelayExecutionThread(KernelMode, FALSE, &time_out);

                time_out.LowPart <<= 4;
                continue;
            }

            KdPrint(("ImDisk: Deleting symlink for device %i.\n",
                device_extension->device_number));

            symlink_name_buffer = (PWCHAR)
                ExAllocatePoolWithTag(PagedPool,
                    MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR), POOL_TAG);

            if (symlink_name_buffer == NULL)
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                UNICODE_STRING symlink_name;

                _snwprintf(symlink_name_buffer, MAXIMUM_FILENAME_LENGTH - 1,
                    IMDISK_SYMLNK_NATIVE_BASE_NAME L"%u", device_extension->device_number);
                symlink_name_buffer[MAXIMUM_FILENAME_LENGTH - 1] = 0;

                RtlInitUnicodeString(&symlink_name, symlink_name_buffer);

                KdPrint(("ImDisk: Deleting symlink '%ws'.\n", symlink_name_buffer));

                status = IoDeleteSymbolicLink(&symlink_name);

                ExFreePoolWithTag(symlink_name_buffer, POOL_TAG);
            }

            if (!NT_SUCCESS(status))
            {
                KdPrint(("ImDisk: Cannot delete symlink. (%#x)\n", status));
            }

            KdPrint(("ImDisk: Deleting device object %i.\n",
                device_extension->device_number));

            IoDeleteDevice(device_object);

            // Fire refresh event
            if (RefreshEvent != NULL)
                KePulseEvent(RefreshEvent, 0, FALSE);

            PsTerminateSystemThread(STATUS_SUCCESS);
        }

        irp = CONTAINING_RECORD(request, IRP, Tail.Overlay.ListEntry);

        io_stack = IoGetCurrentIrpStackLocation(irp);

        switch (io_stack->MajorFunction)
        {
        case IRP_MJ_FLUSH_BUFFERS:
        {
            NTSTATUS status;
            PIRP image_irp;
            KEVENT io_complete_event;
            PIO_STACK_LOCATION image_io_stack;

            if (device_extension->file_object == NULL)
            {
                status = ObReferenceObjectByHandle(
                    device_extension->file_handle,
                    FILE_WRITE_ATTRIBUTES |
                    FILE_WRITE_DATA |
                    SYNCHRONIZE,
                    *IoFileObjectType,
                    KernelMode,
                    (PVOID*)&device_extension->file_object,
                    NULL);

                if (!NT_SUCCESS(status))
                {
                    irp->IoStatus.Status = status;
                    irp->IoStatus.Information = 0;

                    KdPrint(("ImDisk: ObReferenceObjectByHandle failed on image handle: %#x\n",
                        status));

                    break;
                }

                device_extension->dev_object = IoGetRelatedDeviceObject(
                    device_extension->file_object);
            }

            KeInitializeEvent(&io_complete_event,
                NotificationEvent,
                FALSE);

            image_irp = IoBuildSynchronousFsdRequest(
                IRP_MJ_FLUSH_BUFFERS,
                device_extension->dev_object,
                NULL,
                0,
                NULL,
                &io_complete_event,
                &irp->IoStatus);

            if (image_irp == NULL)
            {
                KdPrint(("ImDisk: IoBuildSynchronousFsdRequest failed for image object.\n"));

                irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                irp->IoStatus.Information = 0;
                break;
            }

            image_io_stack = IoGetNextIrpStackLocation(image_irp);
            image_io_stack->FileObject = device_extension->file_object;

            status = IoCallDriver(device_extension->dev_object, image_irp);

            if (status == STATUS_PENDING)
            {
                KdPrint(("ImDisk: Waiting for IoCallDriver to complete.\n"));

                KeWaitForSingleObject(&io_complete_event,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL);
            }
            else
            {
                irp->IoStatus.Status = status;
            }

            KdPrint(("ImDisk: IoCallDriver result for flush request: %#x\n",
                status));

            break;
        }

        case IRP_MJ_READ:
        {
            PUCHAR system_buffer =
                (PUCHAR)MmGetSystemAddressForMdlSafe(irp->MdlAddress,
                    NormalPagePriority);
            LARGE_INTEGER offset;
            KLOCK_QUEUE_HANDLE lock_handle;

            if (system_buffer == NULL)
            {
                irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                irp->IoStatus.Information = 0;
                break;
            }

            if (device_extension->vm_disk)
            {
#ifdef _WIN64
                ULONG_PTR vm_offset =
                    io_stack->Parameters.Read.ByteOffset.QuadPart;
#else
                ULONG_PTR vm_offset =
                    io_stack->Parameters.Read.ByteOffset.LowPart;
#endif

                RtlCopyMemory(system_buffer,
                    device_extension->image_buffer +
                    vm_offset,
                    io_stack->Parameters.Read.Length);

                irp->IoStatus.Status = STATUS_SUCCESS;
                irp->IoStatus.Information = io_stack->Parameters.Read.Length;

                if (io_stack->FileObject != NULL)
                {
                    io_stack->FileObject->CurrentByteOffset.QuadPart +=
                        irp->IoStatus.Information;
                }

                break;
            }

            offset.QuadPart = io_stack->Parameters.Read.ByteOffset.QuadPart +
                device_extension->image_offset.QuadPart;

            ImDiskAcquireLock(&device_extension->last_io_lock, &lock_handle);

            if ((device_extension->last_io_data != NULL) &
                (device_extension->last_io_length <
                    io_stack->Parameters.Read.Length))
            {
                ExFreePoolWithTag(device_extension->last_io_data, POOL_TAG);
                device_extension->last_io_data = NULL;
            }

            device_extension->last_io_offset = 0;
            device_extension->last_io_length = 0;

            ImDiskReleaseLock(&lock_handle);

            if (device_extension->last_io_data == NULL)
                device_extension->last_io_data = (PUCHAR)
                ExAllocatePoolWithTag(NonPagedPool,
                    io_stack->Parameters.Read.Length,
                    POOL_TAG);

            if (device_extension->last_io_data == NULL)
            {
                irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                irp->IoStatus.Information = 0;
                break;
            }

            if (device_extension->use_proxy)
            {
                irp->IoStatus.Status =
                    ImDiskReadProxy(&device_extension->proxy,
                        &irp->IoStatus,
                        &device_extension->terminate_thread,
                        device_extension->last_io_data,
                        io_stack->Parameters.Read.Length,
                        &offset);

                if (!NT_SUCCESS(irp->IoStatus.Status))
                {
                    KdPrint(("ImDisk: Read failed on device %i: %#x.\n",
                        device_extension->device_number,
                        irp->IoStatus.Status));

                    // If indicating that proxy connection died we can do
                    // nothing else but remove this device.
                    // if (irp->IoStatus.Status == STATUS_CONNECTION_RESET)
                    ImDiskRemoveVirtualDisk(device_object);

                    irp->IoStatus.Status = STATUS_DEVICE_REMOVED;
                    irp->IoStatus.Information = 0;
                }
            }
            else
            {
                irp->IoStatus.Status =
                    NtReadFile(device_extension->file_handle,
                        NULL,
                        NULL,
                        NULL,
                        &irp->IoStatus,
                        device_extension->last_io_data,
                        io_stack->Parameters.Read.Length,
                        &offset,
                        NULL);
            }

            if (NT_SUCCESS(irp->IoStatus.Status))
            {
                RtlCopyMemory(system_buffer, device_extension->last_io_data,
                    irp->IoStatus.Information);

                if (device_extension->byte_swap)
                    ImDiskByteSwapBuffer(system_buffer,
                        irp->IoStatus.Information);

                device_extension->last_io_offset =
                    io_stack->Parameters.Read.ByteOffset.QuadPart;

                device_extension->last_io_length =
                    (ULONG)irp->IoStatus.Information;

                if (io_stack->FileObject != NULL)
                {
                    io_stack->FileObject->CurrentByteOffset.QuadPart +=
                        irp->IoStatus.Information;
                }
            }
            else
            {
                ExFreePoolWithTag(device_extension->last_io_data, POOL_TAG);
                device_extension->last_io_data = NULL;
            }

            break;
        }

        case IRP_MJ_WRITE:
        {
            PUCHAR system_buffer =
                (PUCHAR)MmGetSystemAddressForMdlSafe(irp->MdlAddress,
                    NormalPagePriority);
            LARGE_INTEGER offset;
            BOOLEAN set_zero_data = FALSE;
            KLOCK_QUEUE_HANDLE lock_handle;

            if (system_buffer == NULL)
            {
                irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                irp->IoStatus.Information = 0;
                break;
            }

            if (!device_extension->image_modified)
            {
                device_extension->image_modified = TRUE;

                // Fire refresh event
                if (RefreshEvent != NULL)
                    KePulseEvent(RefreshEvent, 0, FALSE);
            }

            if (device_extension->vm_disk)
            {
#ifdef _WIN64
                ULONG_PTR vm_offset =
                    io_stack->Parameters.Write.ByteOffset.QuadPart;
#else
                ULONG_PTR vm_offset =
                    io_stack->Parameters.Write.ByteOffset.LowPart;
#endif

                RtlCopyMemory(device_extension->image_buffer +
                    vm_offset,
                    system_buffer,
                    io_stack->Parameters.Write.Length);

                irp->IoStatus.Status = STATUS_SUCCESS;
                irp->IoStatus.Information = io_stack->Parameters.Write.Length;

                if (io_stack->FileObject != NULL)
                {
                    io_stack->FileObject->CurrentByteOffset.QuadPart +=
                        irp->IoStatus.Information;
                }

                break;
            }

            offset.QuadPart = io_stack->Parameters.Write.ByteOffset.QuadPart +
                device_extension->image_offset.QuadPart;

            ImDiskAcquireLock(&device_extension->last_io_lock, &lock_handle);

            if ((device_extension->last_io_data != NULL) &
                (device_extension->last_io_length <
                    io_stack->Parameters.Write.Length))
            {
                ExFreePoolWithTag(device_extension->last_io_data, POOL_TAG);
                device_extension->last_io_data = NULL;
            }

            device_extension->last_io_offset = 0;
            device_extension->last_io_length = 0;

            ImDiskReleaseLock(&lock_handle);

            if (device_extension->last_io_data == NULL)
            {
                device_extension->last_io_data = (PUCHAR)
                    ExAllocatePoolWithTag(NonPagedPool,
                        io_stack->Parameters.Write.Length,
                        POOL_TAG);
            }

            if (device_extension->last_io_data == NULL)
            {
                irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                irp->IoStatus.Information = 0;
                break;
            }

            RtlCopyMemory(device_extension->last_io_data, system_buffer,
                io_stack->Parameters.Write.Length);

            if ((device_extension->use_set_zero_data ||
                (device_extension->use_proxy &&
                    device_extension->proxy_zero)) &&
                ImDiskIsBufferZero(device_extension->last_io_data,
                    io_stack->Parameters.Write.Length))
            {
                set_zero_data = TRUE;
            }

            if ((!set_zero_data) && device_extension->byte_swap)
            {
                ImDiskByteSwapBuffer(device_extension->last_io_data,
                    irp->IoStatus.Information);
            }

            if (device_extension->use_proxy)
            {
                if (set_zero_data && device_extension->proxy_zero)
                {
                    DEVICE_DATA_SET_RANGE range;
                    range.StartingOffset = offset.QuadPart;
                    range.LengthInBytes = io_stack->Parameters.Write.Length;

                    irp->IoStatus.Status =
                        ImDiskUnmapOrZeroProxy(&device_extension->proxy,
                            IMDPROXY_REQ_ZERO,
                            &irp->IoStatus,
                            &device_extension->terminate_thread,
                            1,
                            &range);

                    if (NT_SUCCESS(irp->IoStatus.Status))
                    {
                        irp->IoStatus.Information =
                            io_stack->Parameters.Write.Length;
                    }
                }
                else
                {
                    irp->IoStatus.Status =
                        ImDiskWriteProxy(&device_extension->proxy,
                            &irp->IoStatus,
                            &device_extension->terminate_thread,
                            device_extension->last_io_data,
                            io_stack->Parameters.Write.Length,
                            &offset);
                }

                if (!NT_SUCCESS(irp->IoStatus.Status))
                {
                    KdPrint(("ImDisk: Write failed on device %i: %#x.\n",
                        device_extension->device_number,
                        irp->IoStatus.Status));

                    // If indicating that proxy connection died we can do
                    // nothing else but remove this device.
                    if (irp->IoStatus.Status == STATUS_CONNECTION_RESET)
                        ImDiskRemoveVirtualDisk(device_object);

                    irp->IoStatus.Status = STATUS_DEVICE_REMOVED;
                    irp->IoStatus.Information = 0;
                }
            }
            else
            {
                if (set_zero_data)
                {
                    FILE_ZERO_DATA_INFORMATION zero_data;
                    zero_data.FileOffset = offset;
                    zero_data.BeyondFinalZero.QuadPart = offset.QuadPart +
                        io_stack->Parameters.Write.Length;

                    irp->IoStatus.Status =
                        NtFsControlFile(device_extension->file_handle,
                            NULL,
                            NULL,
                            NULL,
                            &irp->IoStatus,
                            FSCTL_SET_ZERO_DATA,
                            &zero_data,
                            sizeof(zero_data),
                            NULL,
                            0);

                    if (NT_SUCCESS(irp->IoStatus.Status))
                    {
                        KdPrint2(("ImDisk: Zero block set.\n"));
                        irp->IoStatus.Information =
                            io_stack->Parameters.Write.Length;
                    }
                    else
                    {
                        KdPrint(("ImDisk: Volume does not support "
                            "FSCTL_SET_ZERO_DATA: 0x%#X\n",
                            irp->IoStatus.Status));

                        irp->IoStatus.Information = 0;
                        set_zero_data = FALSE;
                        device_extension->use_set_zero_data = FALSE;
                    }
                }

                if (!set_zero_data)
                {
                    irp->IoStatus.Status = NtWriteFile(
                        device_extension->file_handle,
                        NULL,
                        NULL,
                        NULL,
                        &irp->IoStatus,
                        device_extension->last_io_data,
                        io_stack->Parameters.Write.Length,
                        &offset,
                        NULL);
                }
            }

            if (NT_SUCCESS(irp->IoStatus.Status))
            {
                device_extension->last_io_offset =
                    io_stack->Parameters.Write.ByteOffset.QuadPart;
                device_extension->last_io_length =
                    io_stack->Parameters.Write.Length;

                if (io_stack->FileObject != NULL)
                {
                    io_stack->FileObject->CurrentByteOffset.QuadPart +=
                        irp->IoStatus.Information;
                }
            }
            else
            {
                ExFreePoolWithTag(device_extension->last_io_data, POOL_TAG);
                device_extension->last_io_data = NULL;
            }

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

                buffer = (PUCHAR)
                    ExAllocatePoolWithTag(NonPagedPool, 1, POOL_TAG);

                if (buffer == NULL)
                {
                    irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    irp->IoStatus.Information = 0;
                    break;
                }

                if (device_extension->use_proxy)
                {
                    irp->IoStatus.Status =
                        ImDiskReadProxy(&device_extension->proxy,
                            &irp->IoStatus,
                            &device_extension->terminate_thread,
                            buffer,
                            0,
                            &device_extension->image_offset);
                }
                else
                {
                    irp->IoStatus.Status =
                        NtReadFile(device_extension->file_handle,
                            NULL,
                            NULL,
                            NULL,
                            &irp->IoStatus,
                            buffer,
                            0,
                            &device_extension->image_offset,
                            NULL);
                }

                ExFreePoolWithTag(buffer, POOL_TAG);

                if (!NT_SUCCESS(irp->IoStatus.Status))
                {
                    KdPrint(("ImDisk: Verify failed on device %i.\n",
                        device_extension->device_number));

                    // If indicating that proxy connection died we can do
                    // nothing else but remove this device.
                    if (irp->IoStatus.Status == STATUS_CONNECTION_RESET)
                        ImDiskRemoveVirtualDisk(device_object);

                    irp->IoStatus.Status = STATUS_DEVICE_REMOVED;
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
                    *(PULONG)irp->AssociatedIrp.SystemBuffer =
                        device_extension->media_change_count;

                    irp->IoStatus.Information = sizeof(ULONG);
                }

                irp->IoStatus.Status = STATUS_SUCCESS;
                break;
            }

            case IOCTL_IMDISK_IOCTL_PASS_THROUGH:
            case IOCTL_IMDISK_FSCTL_PASS_THROUGH:
            {
                NTSTATUS status;
                ULONG ctl_code = *(PULONG)irp->AssociatedIrp.SystemBuffer;
                PVOID in_buffer = (PUCHAR)irp->AssociatedIrp.SystemBuffer +
                    sizeof(ULONG);
                PVOID out_buffer = irp->AssociatedIrp.SystemBuffer;
                ULONG in_size =
                    io_stack->Parameters.DeviceIoControl.InputBufferLength -
                    sizeof(ULONG);
                ULONG out_size =
                    io_stack->Parameters.DeviceIoControl.OutputBufferLength;

                UCHAR func;
                PIRP lower_irp;
                KEVENT event;
                PIO_STACK_LOCATION lower_io_stack;

                if (device_extension->file_object == NULL)
                {
                    status = ObReferenceObjectByHandle(
                        device_extension->file_handle,
                        FILE_WRITE_ATTRIBUTES |
                        FILE_WRITE_DATA |
                        SYNCHRONIZE,
                        *IoFileObjectType,
                        KernelMode,
                        (PVOID*)&device_extension->file_object,
                        NULL);

                    if (!NT_SUCCESS(status))
                    {
                        irp->IoStatus.Status = status;
                        irp->IoStatus.Information = 0;

                        KdPrint(("ImDisk: ObReferenceObjectByHandle failed on image handle: %#x\n",
                            status));

                        break;
                    }

                    device_extension->dev_object = IoGetRelatedDeviceObject(
                        device_extension->file_object);
                }

                if (io_stack->MajorFunction == IOCTL_IMDISK_FSCTL_PASS_THROUGH)
                {
                    KdPrint(("ImDisk: IOCTL_IMDISK_FSCTL_PASS_THROUGH for device %i control code %#x.\n",
                        device_extension->device_number, ctl_code));

                    func = IRP_MJ_FILE_SYSTEM_CONTROL;
                }
                else
                {
                    KdPrint(("ImDisk: IOCTL_IMDISK_IOCTL_PASS_THROUGH for device %i control code %#x.\n",
                        device_extension->device_number, ctl_code));

                    func = IRP_MJ_DEVICE_CONTROL;
                }

                KeInitializeEvent(&event, NotificationEvent, FALSE);

                lower_irp = IoBuildDeviceIoControlRequest(
                    ctl_code,
                    device_extension->dev_object,
                    in_buffer,
                    in_size,
                    out_buffer,
                    out_size,
                    FALSE,
                    &event,
                    &irp->IoStatus);

                if (lower_irp == NULL)
                {
                    irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    irp->IoStatus.Information = 0;
                    break;
                }

                lower_irp->RequestorMode = irp->RequestorMode;

                lower_io_stack = IoGetNextIrpStackLocation(lower_irp);
                lower_io_stack->FileObject = device_extension->file_object;
                lower_io_stack->MajorFunction = func;

                status = IoCallDriver(device_extension->dev_object,
                    lower_irp);

                if (status == STATUS_PENDING)
                {
                    KeWaitForSingleObject(&event, Executive, KernelMode,
                        FALSE, NULL);
                }
                else
                {
                    irp->IoStatus.Status = status;
                }

                KdPrint(("ImDisk: IOCTL_IMDISK_IOCTL/FSCTL_PASS_THROUGH for device %i control code %#x result status %#x.\n",
                    device_extension->device_number, ctl_code, status));

                break;
            }

#ifdef INCLUDE_VFD_ORIGIN

            case IOCTL_DISK_FORMAT_TRACKS:
            case IOCTL_DISK_FORMAT_TRACKS_EX:
            {
                NTSTATUS status =
                    ImDiskFloppyFormat(device_extension, irp);

                if (!NT_SUCCESS(status))
                {
                    // If indicating that proxy connection died we can do
                    // nothing else but remove this device.
                    if (status == STATUS_CONNECTION_RESET)
                        ImDiskRemoveVirtualDisk(device_object);

                    irp->IoStatus.Status = STATUS_DEVICE_REMOVED;
                    irp->IoStatus.Information = 0;
                    break;
                }

                irp->IoStatus.Information = 0;
                irp->IoStatus.Status = status;
                break;
            }

#endif // INCLUDE_VFD_ORIGIN

            case IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES:
            {
                irp->IoStatus.Information = 0;

                PDEVICE_MANAGE_DATA_SET_ATTRIBUTES attrs =
                    (PDEVICE_MANAGE_DATA_SET_ATTRIBUTES)
                    irp->AssociatedIrp.SystemBuffer;

                irp->IoStatus.Status = STATUS_SUCCESS;

                int items = attrs->DataSetRangesLength /
                    sizeof(DEVICE_DATA_SET_RANGE);

                PDEVICE_DATA_SET_RANGE range = (PDEVICE_DATA_SET_RANGE)
                    ExAllocatePoolWithTag(PagedPool,
                        items * sizeof(DEVICE_DATA_SET_RANGE), POOL_TAG);

                if (range == NULL)
                {
                    irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                RtlCopyMemory(range, (PUCHAR)attrs + attrs->DataSetRangesOffset,
                    items * sizeof(DEVICE_DATA_SET_RANGE));

                if (device_extension->image_offset.QuadPart > 0)
                {
                    for (int i = 0; i < items; i++)
                    {
                        range[i].StartingOffset +=
                            device_extension->image_offset.QuadPart;
                    }
                }

                irp->IoStatus.Status = ImDiskUnmapOrZeroProxy(
                    &device_extension->proxy,
                    IMDPROXY_REQ_UNMAP,
                    &irp->IoStatus,
                    &device_extension->terminate_thread,
                    items,
                    range);

                ExFreePoolWithTag(range, POOL_TAG);

                KdPrint(("ImDisk: Unmap result on device %i: %#x.\n",
                    device_extension->device_number,
                    irp->IoStatus.Status));

                break;
            }

            case IOCTL_DISK_GROW_PARTITION:
            {
                NTSTATUS status;
                FILE_END_OF_FILE_INFORMATION new_size;
                FILE_STANDARD_INFORMATION file_standard_information;

                new_size.EndOfFile.QuadPart =
                    device_extension->disk_geometry.Cylinders.QuadPart +
                    ((PDISK_GROW_PARTITION)irp->AssociatedIrp.SystemBuffer)->
                    BytesToGrow.QuadPart;

                if (device_extension->vm_disk)
                {
                    PVOID new_image_buffer = NULL;
                    SIZE_T free_size = 0;
#ifdef _WIN64
                    ULONG_PTR old_size =
                        device_extension->disk_geometry.Cylinders.QuadPart;
                    SIZE_T max_size = new_size.EndOfFile.QuadPart;
#else
                    ULONG_PTR old_size =
                        device_extension->disk_geometry.Cylinders.LowPart;
                    SIZE_T max_size = new_size.EndOfFile.LowPart;

                    // A vm type disk cannot be extended to a larger size than
                    // 2 GB.
                    if (new_size.EndOfFile.QuadPart & 0xFFFFFFFF80000000)
                    {
                        irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
                        irp->IoStatus.Information = 0;
                        break;
                    }
#endif // _WIN64

                    status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                        &new_image_buffer,
                        0,
                        &max_size,
                        MEM_COMMIT,
                        PAGE_READWRITE);

                    if (!NT_SUCCESS(status))
                    {
                        status = STATUS_NO_MEMORY;
                        irp->IoStatus.Status = status;
                        irp->IoStatus.Information = 0;
                        break;
                    }

                    RtlCopyMemory(new_image_buffer,
                        device_extension->image_buffer,
                        old_size);

                    ZwFreeVirtualMemory(NtCurrentProcess(),
                        (PVOID*)&device_extension->image_buffer,
                        &free_size,
                        MEM_RELEASE);

                    device_extension->image_buffer = (PUCHAR)new_image_buffer;
                    device_extension->disk_geometry.Cylinders =
                        new_size.EndOfFile;

                    // Fire refresh event
                    if (RefreshEvent != NULL)
                        KePulseEvent(RefreshEvent, 0, FALSE);

                    irp->IoStatus.Information = 0;
                    irp->IoStatus.Status = STATUS_SUCCESS;
                    break;
                }

                // For proxy-type disks the new size is just accepted and
                // that's it.
                if (device_extension->use_proxy)
                {
                    device_extension->disk_geometry.Cylinders =
                        new_size.EndOfFile;

                    // Fire refresh event
                    if (RefreshEvent != NULL)
                        KePulseEvent(RefreshEvent, 0, FALSE);

                    irp->IoStatus.Information = 0;
                    irp->IoStatus.Status = STATUS_SUCCESS;
                    break;
                }

                // Image file backed disks left to do.

                // For disks with offset, refuse to extend size. Otherwise we
                // could break compatibility with the header data we have
                // skipped and we don't know about.
                if (device_extension->image_offset.QuadPart != 0)
                {
                    irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
                    irp->IoStatus.Information = 0;
                    break;
                }

                status =
                    ZwQueryInformationFile(device_extension->file_handle,
                        &irp->IoStatus,
                        &file_standard_information,
                        sizeof file_standard_information,
                        FileStandardInformation);

                if (!NT_SUCCESS(status))
                {
                    irp->IoStatus.Status = status;
                    irp->IoStatus.Information = 0;
                    break;
                }

                if (file_standard_information.EndOfFile.QuadPart >=
                    new_size.EndOfFile.QuadPart)
                {
                    device_extension->disk_geometry.Cylinders =
                        new_size.EndOfFile;

                    // Fire refresh event
                    if (RefreshEvent != NULL)
                        KePulseEvent(RefreshEvent, 0, FALSE);

                    irp->IoStatus.Information = 0;
                    irp->IoStatus.Status = STATUS_SUCCESS;
                    break;
                }

                // For other, fixed file-backed disks we need to adjust the
                // physical file size.

                status = ZwSetInformationFile(device_extension->file_handle,
                    &irp->IoStatus,
                    &new_size,
                    sizeof new_size,
                    FileEndOfFileInformation);

                if (NT_SUCCESS(status))
                {
                    device_extension->disk_geometry.Cylinders =
                        new_size.EndOfFile;

                    // Fire refresh event
                    if (RefreshEvent != NULL)
                        KePulseEvent(RefreshEvent, 0, FALSE);
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

//
// Reads in a loop up to "Length" or until eof reached.
//
NTSTATUS
ImDiskSafeReadFile(IN HANDLE FileHandle,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    OUT PVOID Buffer,
    IN SIZE_T Length,
    IN PLARGE_INTEGER Offset)
{
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T length_done = 0;
    PUCHAR intermediate_buffer = NULL;
    ULONG request_length;

    ASSERT(FileHandle != NULL);
    ASSERT(IoStatusBlock != NULL);
    ASSERT(Buffer != NULL);

    if (Length > (8UL << 20))
    {
        request_length = (8UL << 20);
    }
    else
    {
        request_length = (ULONG)Length;
    }

    while (length_done < Length)
    {
        SIZE_T LongRequestLength = Length - length_done;
        if (LongRequestLength < request_length)
        {
            request_length = (ULONG)LongRequestLength;
        }

        for (;;)
        {
            LARGE_INTEGER current_file_offset;

            current_file_offset.QuadPart = Offset->QuadPart + length_done;

            if (intermediate_buffer == NULL)
            {
                intermediate_buffer = (PUCHAR)
                    ExAllocatePoolWithTag(NonPagedPool,
                        request_length,
                        POOL_TAG);

                if (intermediate_buffer == NULL)
                {
                    DbgPrint("ImDisk: ImDiskSafeReadFile: Insufficient paged pool to allocate "
                        "intermediate buffer (%u bytes).\n", request_length);

                    IoStatusBlock->Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoStatusBlock->Information = 0;
                    return IoStatusBlock->Status;
                }
            }

            status = ZwReadFile(FileHandle,
                NULL,
                NULL,
                NULL,
                IoStatusBlock,
                intermediate_buffer,
                request_length,
                &current_file_offset,
                NULL);

            if (((status == STATUS_INSUFFICIENT_RESOURCES) |
                (status == STATUS_INVALID_BUFFER_SIZE) |
                (status == STATUS_INVALID_PARAMETER)) &
                (request_length >= 2048))
            {
                ExFreePoolWithTag(intermediate_buffer, POOL_TAG);
                intermediate_buffer = NULL;

                DbgPrint("ImDisk: ImDiskSafeReadFile: ZwReadFile error reading "
                    "%u bytes. Retrying with smaller read size. (Status 0x%X)\n",
                    request_length,
                    status);

                request_length >>= 2;

                continue;
            }

            if (!NT_SUCCESS(status))
            {
                DbgPrint("ImDisk: ImDiskSafeReadFile: ZwReadFile error reading "
                    "%u bytes. (Status 0x%X)\n",
                    request_length,
                    status);

                break;
            }

            RtlCopyMemory((PUCHAR)Buffer + length_done, intermediate_buffer,
                IoStatusBlock->Information);

            break;
        }

        if (!NT_SUCCESS(status))
        {
            IoStatusBlock->Information = length_done;
            break;
        }

        if (IoStatusBlock->Information == 0)
        {
            DbgPrint("ImDisk: ImDiskSafeReadFile: IoStatusBlock->Information == 0, "
                "returning STATUS_CONNECTION_RESET.\n");

            status = STATUS_CONNECTION_RESET;
            break;
        }

        KdPrint(("ImDisk: ImDiskSafeReadFile: Done %u bytes.\n",
            (ULONG)IoStatusBlock->Information));

        length_done += IoStatusBlock->Information;
    }

    if (intermediate_buffer != NULL)
    {
        ExFreePoolWithTag(intermediate_buffer, POOL_TAG);
        intermediate_buffer = NULL;
    }

    if (!NT_SUCCESS(status))
    {
        DbgPrint("ImDisk: ImDiskSafeReadFile: Error return "
            "(Status 0x%X)\n", status);
    }
    else
    {
        KdPrint(("ImDisk: ImDiskSafeReadFile: Successful.\n"));
    }

    IoStatusBlock->Status = status;
    IoStatusBlock->Information = length_done;
    return status;
}

NTSTATUS
ImDiskSafeIOStream(IN PFILE_OBJECT FileObject,
    IN UCHAR MajorFunction,
    IN OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent,
    IN OUT PVOID Buffer,
    IN ULONG Length)
{
    NTSTATUS status;
    ULONG length_done = 0;
    KEVENT io_complete_event;
    PIO_STACK_LOCATION io_stack;
    LARGE_INTEGER offset = { 0 };
    PKEVENT wait_object[] = {
        &io_complete_event,
        CancelEvent
    };
    ULONG number_of_wait_objects = CancelEvent != NULL ? 2 : 1;

    KdPrint2(("ImDiskSafeIOStream: FileObject=%#x, MajorFunction=%#x, "
        "IoStatusBlock=%#x, Buffer=%#x, Length=%#x.\n",
        FileObject, MajorFunction, IoStatusBlock, Buffer, Length));

    ASSERT(FileObject != NULL);
    ASSERT(IoStatusBlock != NULL);
    ASSERT(Buffer != NULL);

    KeInitializeEvent(&io_complete_event,
        NotificationEvent,
        FALSE);

    while (length_done < Length)
    {
        ULONG RequestLength = Length - length_done;

        do
        {
            PIRP irp;
            PDEVICE_OBJECT device_object = IoGetRelatedDeviceObject(FileObject);

            KdPrint2(("ImDiskSafeIOStream: Building IRP...\n"));

#pragma warning(suppress: 6102)
            irp = IoBuildSynchronousFsdRequest(MajorFunction,
                device_object,
                (PUCHAR)Buffer + length_done,
                RequestLength,
                &offset,
                &io_complete_event,
                IoStatusBlock);

            if (irp == NULL)
            {
                KdPrint(("ImDiskSafeIOStream: Error building IRP.\n"));

                IoStatusBlock->Status = STATUS_INSUFFICIENT_RESOURCES;
                IoStatusBlock->Information = length_done;
                return IoStatusBlock->Status;
            }

            KdPrint2(("ImDiskSafeIOStream: Built IRP=%#x.\n", irp));

            io_stack = IoGetNextIrpStackLocation(irp);
            io_stack->FileObject = FileObject;

            KdPrint2(("ImDiskSafeIOStream: MajorFunction=%#x, Length=%#x\n",
                io_stack->MajorFunction,
                io_stack->Parameters.Read.Length));

            KeClearEvent(&io_complete_event);

            status = IoCallDriver(device_object, irp);

            if (status == STATUS_PENDING)
            {
                status = KeWaitForMultipleObjects(number_of_wait_objects,
                    (PVOID*)wait_object,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL,
                    NULL);

                if (KeReadStateEvent(&io_complete_event) == 0)
                {
                    IoCancelIrp(irp);
                    KeWaitForSingleObject(&io_complete_event,
                        Executive,
                        KernelMode,
                        FALSE,
                        NULL);
                }
            }
            else if (!NT_SUCCESS(status))
                break;

            status = IoStatusBlock->Status;

            KdPrint2(("ImDiskSafeIOStream: IRP %#x completed. Status=%#x.\n",
                irp, IoStatusBlock->Status));

            RequestLength >>= 1;
        } while ((status == STATUS_INVALID_BUFFER_SIZE) |
            (status == STATUS_INVALID_PARAMETER));

        if (!NT_SUCCESS(status))
        {
            KdPrint2(("ImDiskSafeIOStream: I/O failed. Status=%#x.\n", status));

            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        KdPrint2(("ImDiskSafeIOStream: I/O done. Status=%#x. Length=%#x\n",
            status, IoStatusBlock->Information));

        if (IoStatusBlock->Information == 0)
        {
            IoStatusBlock->Status = STATUS_CONNECTION_RESET;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        length_done += (ULONG)IoStatusBlock->Information;
    }

    KdPrint2(("ImDiskSafeIOStream: I/O complete.\n"));

    IoStatusBlock->Status = STATUS_SUCCESS;
    IoStatusBlock->Information = length_done;
    return IoStatusBlock->Status;
}

VOID
ImDiskCloseProxy(IN PPROXY_CONNECTION Proxy)
{
    ASSERT(Proxy != NULL);

    switch (Proxy->connection_type)
    {
    case PROXY_CONNECTION::PROXY_CONNECTION_DEVICE:
        if (Proxy->device != NULL)
            ObDereferenceObject(Proxy->device);

        Proxy->device = NULL;
        break;

    case PROXY_CONNECTION::PROXY_CONNECTION_SHM:
        if ((Proxy->request_event != NULL) &
            (Proxy->response_event != NULL) &
            (Proxy->shared_memory != NULL))
        {
            *(ULONGLONG*)Proxy->shared_memory = IMDPROXY_REQ_CLOSE;
            KeSetEvent(Proxy->request_event, (KPRIORITY)0, FALSE);
        }

        if (Proxy->request_event_handle != NULL)
        {
            ZwClose(Proxy->request_event_handle);
            Proxy->request_event_handle = NULL;
        }

        if (Proxy->response_event_handle != NULL)
        {
            ZwClose(Proxy->response_event_handle);
            Proxy->response_event_handle = NULL;
        }

        if (Proxy->request_event != NULL)
        {
            ObDereferenceObject(Proxy->request_event);
            Proxy->request_event = NULL;
        }

        if (Proxy->response_event != NULL)
        {
            ObDereferenceObject(Proxy->response_event);
            Proxy->response_event = NULL;
        }

        if (Proxy->shared_memory != NULL)
        {
            ZwUnmapViewOfSection(NtCurrentProcess(), Proxy->shared_memory);
            Proxy->shared_memory = NULL;
        }

        break;
    }
}

NTSTATUS
ImDiskCallProxy(IN PPROXY_CONNECTION Proxy,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent OPTIONAL,
    IN PVOID RequestHeader,
    IN ULONG RequestHeaderSize,
    IN PVOID RequestData,
    IN ULONG RequestDataSize,
    IN OUT PVOID ResponseHeader,
    IN ULONG ResponseHeaderSize,
    IN OUT PVOID ResponseData,
    IN ULONG ResponseDataBufferSize,
    IN ULONG *ResponseDataSize)
{
    NTSTATUS status;

    ASSERT(Proxy != NULL);

    switch (Proxy->connection_type)
    {
    case PROXY_CONNECTION::PROXY_CONNECTION_DEVICE:
    {
        PUCHAR io_buffer = NULL;
        PUCHAR temp_buffer = NULL;
        ULONG io_size = RequestHeaderSize + RequestDataSize;

        if ((RequestHeaderSize > 0) &&
            (RequestDataSize > 0))
        {
            temp_buffer = (PUCHAR)
                ExAllocatePoolWithTag(NonPagedPool, io_size, POOL_TAG);

            if (temp_buffer == NULL)
            {
                KdPrint(("ImDisk Proxy Client: Memory allocation failed.\n."));

                IoStatusBlock->Status = STATUS_INSUFFICIENT_RESOURCES;
                IoStatusBlock->Information = 0;
                return IoStatusBlock->Status;
            }

            if (RequestHeaderSize > 0)
            {
                RtlCopyMemory(temp_buffer, RequestHeader, RequestHeaderSize);
            }

            if (RequestDataSize > 0)
            {
                RtlCopyMemory(temp_buffer + RequestHeaderSize, RequestData, RequestDataSize);
            }

            io_buffer = temp_buffer;
        }
        else if (RequestHeaderSize > 0)
        {
            io_buffer = (PUCHAR)RequestHeader;
        }
        else if (RequestDataSize > 0)
        {
            io_buffer = (PUCHAR)RequestData;
        }

        if (io_size > 0)
        {
            if (CancelEvent != NULL ?
                KeReadStateEvent(CancelEvent) != 0 :
                FALSE)
            {
                KdPrint(("ImDisk Proxy Client: Request cancelled.\n."));

                if (temp_buffer != NULL)
                {
                    ExFreePoolWithTag(temp_buffer, POOL_TAG);
                }

                IoStatusBlock->Status = STATUS_CANCELLED;
                IoStatusBlock->Information = 0;
                return IoStatusBlock->Status;
            }

            status = ImDiskSafeIOStream(Proxy->device,
                IRP_MJ_WRITE,
                IoStatusBlock,
                CancelEvent,
                io_buffer,
                io_size);

            if (!NT_SUCCESS(status))
            {
                KdPrint(("ImDisk Proxy Client: Request error %#x\n.",
                    status));

                if (temp_buffer != NULL)
                {
                    ExFreePoolWithTag(temp_buffer, POOL_TAG);
                }

                IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
                IoStatusBlock->Information = 0;
                return IoStatusBlock->Status;
            }
        }

        if (temp_buffer != NULL)
        {
            ExFreePoolWithTag(temp_buffer, POOL_TAG);
        }

        if (ResponseHeaderSize > 0)
        {
            if (CancelEvent != NULL ?
                KeReadStateEvent(CancelEvent) != 0 :
                FALSE)
            {
                KdPrint(("ImDisk Proxy Client: Request cancelled.\n."));

                IoStatusBlock->Status = STATUS_CANCELLED;
                IoStatusBlock->Information = 0;
                return IoStatusBlock->Status;
            }

            status = ImDiskSafeIOStream(Proxy->device,
                IRP_MJ_READ,
                IoStatusBlock,
                CancelEvent,
                ResponseHeader,
                ResponseHeaderSize);

            if (!NT_SUCCESS(status))
            {
                KdPrint(("ImDisk Proxy Client: Response header error %#x\n.",
                    status));

                IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
                IoStatusBlock->Information = 0;
                return IoStatusBlock->Status;
            }
        }

        if (ResponseDataSize != NULL && *ResponseDataSize > 0)
        {
            if (*ResponseDataSize > ResponseDataBufferSize)
            {
                KdPrint(("ImDisk Proxy Client: Fatal: Request %u bytes, "
                    "receiving %u bytes.\n",
                    ResponseDataBufferSize, *ResponseDataSize));

                IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
                IoStatusBlock->Information = 0;
                return IoStatusBlock->Status;
            }

            if (CancelEvent != NULL ?
                KeReadStateEvent(CancelEvent) != 0 :
                FALSE)
            {
                KdPrint(("ImDisk Proxy Client: Request cancelled.\n."));

                IoStatusBlock->Status = STATUS_CANCELLED;
                IoStatusBlock->Information = 0;
                return IoStatusBlock->Status;
            }

            KdPrint2
                (("ImDisk Proxy Client: Got ok resp. Waiting for data.\n"));

            status = ImDiskSafeIOStream(Proxy->device,
                IRP_MJ_READ,
                IoStatusBlock,
                CancelEvent,
                ResponseData,
                *ResponseDataSize);

            if (!NT_SUCCESS(status))
            {
                KdPrint(("ImDisk Proxy Client: Response data error %#x\n.",
                    status));

                KdPrint(("ImDisk Proxy Client: Response data %u bytes, "
                    "got %u bytes.\n",
                    *ResponseDataSize,
                    (ULONG)IoStatusBlock->Information));

                IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
                IoStatusBlock->Information = 0;
                return IoStatusBlock->Status;
            }

            KdPrint2
                (("ImDisk Proxy Client: Received %u byte data stream.\n",
                    IoStatusBlock->Information));
        }

        IoStatusBlock->Status = STATUS_SUCCESS;

        IoStatusBlock->Information = RequestDataSize;

        if (ResponseDataSize != NULL)
        {
            IoStatusBlock->Information += *ResponseDataSize;
        }

        return IoStatusBlock->Status;
    }

    case PROXY_CONNECTION::PROXY_CONNECTION_SHM:
    {
        PKEVENT wait_objects[] = {
            Proxy->response_event,
            CancelEvent
        };

        ULONG number_of_wait_objects = CancelEvent != NULL ? 2 : 1;

        // Some parameter sanity checks
        if ((RequestHeaderSize > IMDPROXY_HEADER_SIZE) |
            (ResponseHeaderSize > IMDPROXY_HEADER_SIZE) |
            ((RequestDataSize + IMDPROXY_HEADER_SIZE) >
                Proxy->shared_memory_size))
        {
            KdPrint(("ImDisk Proxy Client: "
                "Parameter values not supported.\n."));

            IoStatusBlock->Status = STATUS_INVALID_BUFFER_SIZE;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        IoStatusBlock->Information = 0;

        if (RequestHeaderSize > 0)
            RtlCopyMemory(Proxy->shared_memory,
                RequestHeader,
                RequestHeaderSize);

        if (RequestDataSize > 0)
            RtlCopyMemory(Proxy->shared_memory + IMDPROXY_HEADER_SIZE,
                RequestData,
                RequestDataSize);

#pragma warning(suppress: 28160)
        KeSetEvent(Proxy->request_event, (KPRIORITY)0, TRUE);

        status = KeWaitForMultipleObjects(number_of_wait_objects,
            (PVOID*)wait_objects,
            WaitAny,
            Executive,
            KernelMode,
            FALSE,
            NULL,
            NULL);

        if (status == STATUS_WAIT_1)
        {
            KdPrint(("ImDisk Proxy Client: Incomplete wait %#x.\n.", status));

            IoStatusBlock->Status = STATUS_CANCELLED;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        if (ResponseHeaderSize > 0)
            RtlCopyMemory(ResponseHeader,
                Proxy->shared_memory,
                ResponseHeaderSize);

        // If server end requests to send more data than we requested, we
        // treat that as an unrecoverable device error and exit.
        if (ResponseDataSize != NULL ? *ResponseDataSize > 0 : FALSE)
            if ((*ResponseDataSize > ResponseDataBufferSize) |
                ((*ResponseDataSize + IMDPROXY_HEADER_SIZE) >
                    Proxy->shared_memory_size))
            {
                KdPrint(("ImDisk Proxy Client: Invalid response size %u.\n.",
                    *ResponseDataSize));

                IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
                IoStatusBlock->Information = 0;
                return IoStatusBlock->Status;
            }
            else
            {
                RtlCopyMemory(ResponseData,
                    Proxy->shared_memory + IMDPROXY_HEADER_SIZE,
                    *ResponseDataSize);

                IoStatusBlock->Information = *ResponseDataSize;
            }

        IoStatusBlock->Status = STATUS_SUCCESS;
        if ((RequestDataSize > 0) & (IoStatusBlock->Information == 0))
            IoStatusBlock->Information = RequestDataSize;
        return IoStatusBlock->Status;
    }

    default:
        return STATUS_DRIVER_INTERNAL_ERROR;
    }
}

#pragma code_seg("PAGE")

///
/// Note that this function when successful replaces the Proxy->device pointer
/// to point to the connected device object instead of the proxy service pipe.
/// This means that the only reference to the proxy service pipe after calling
/// this function is the original handle to the pipe.
///
NTSTATUS
ImDiskConnectProxy(IN OUT PPROXY_CONNECTION Proxy,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent OPTIONAL,
    IN ULONG Flags,
    IN PWSTR ConnectionString,
    IN USHORT ConnectionStringLength)
{
    IMDPROXY_CONNECT_REQ connect_req;
    IMDPROXY_CONNECT_RESP connect_resp;
    NTSTATUS status;

    PAGED_CODE();

    ASSERT(Proxy != NULL);
    ASSERT(IoStatusBlock != NULL);
    ASSERT(ConnectionString != NULL);

    if (IMDISK_PROXY_TYPE(Flags) == IMDISK_PROXY_TYPE_SHM)
    {
        OBJECT_ATTRIBUTES object_attributes;
        UNICODE_STRING base_name = { 0 };
        UNICODE_STRING event_name = { 0 };
        base_name.Buffer = ConnectionString;
        base_name.Length = ConnectionStringLength;
        base_name.MaximumLength = ConnectionStringLength;
        event_name.MaximumLength = ConnectionStringLength + 20;
        event_name.Buffer =
            (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                event_name.MaximumLength,
                POOL_TAG);
        if (event_name.Buffer == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;

            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        InitializeObjectAttributes(&object_attributes,
            &event_name,
            OBJ_CASE_INSENSITIVE,
            NULL,
            NULL);

        RtlCopyUnicodeString(&event_name, &base_name);
        RtlAppendUnicodeToString(&event_name, L"_Request");

        status = ZwOpenEvent(&Proxy->request_event_handle,
            EVENT_ALL_ACCESS,
            &object_attributes);

        if (!NT_SUCCESS(status))
        {
            Proxy->request_event_handle = NULL;
            ExFreePoolWithTag(event_name.Buffer, POOL_TAG);

            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        status = ObReferenceObjectByHandle(Proxy->request_event_handle,
            EVENT_ALL_ACCESS,
            *ExEventObjectType,
            KernelMode,
            (PVOID*)&Proxy->request_event,
            NULL);

        if (!NT_SUCCESS(status))
        {
            Proxy->request_event = NULL;
            ExFreePoolWithTag(event_name.Buffer, POOL_TAG);

            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        RtlCopyUnicodeString(&event_name, &base_name);
        RtlAppendUnicodeToString(&event_name, L"_Response");

        status = ZwOpenEvent(&Proxy->response_event_handle,
            EVENT_ALL_ACCESS,
            &object_attributes);

        if (!NT_SUCCESS(status))
        {
            Proxy->response_event_handle = NULL;
            ExFreePoolWithTag(event_name.Buffer, POOL_TAG);

            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        status = ObReferenceObjectByHandle(Proxy->response_event_handle,
            EVENT_ALL_ACCESS,
            *ExEventObjectType,
            KernelMode,
            (PVOID*)&Proxy->response_event,
            NULL);

        if (!NT_SUCCESS(status))
        {
            Proxy->response_event = NULL;
            ExFreePoolWithTag(event_name.Buffer, POOL_TAG);

            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        IoStatusBlock->Status = STATUS_SUCCESS;
        IoStatusBlock->Information = 0;
        return IoStatusBlock->Status;
    }

    connect_req.request_code = IMDPROXY_REQ_CONNECT;
    connect_req.flags = Flags;
    connect_req.length = ConnectionStringLength;

    KdPrint(("ImDisk Proxy Client: Sending IMDPROXY_CONNECT_REQ.\n"));

    status = ImDiskCallProxy(Proxy,
        IoStatusBlock,
        CancelEvent,
        &connect_req,
        sizeof(connect_req),
        ConnectionString,
        ConnectionStringLength,
        &connect_resp,
        sizeof(IMDPROXY_CONNECT_RESP),
        NULL,
        0,
        NULL);

    if (!NT_SUCCESS(status))
    {
        IoStatusBlock->Status = status;
        IoStatusBlock->Information = 0;
        return IoStatusBlock->Status;
    }

    if (connect_resp.error_code != 0)
    {
        IoStatusBlock->Status = STATUS_CONNECTION_REFUSED;
        IoStatusBlock->Information = 0;
        return IoStatusBlock->Status;
    }

    // If the proxy gave us a reference to an object to use for direct connection
    // to the server we have to change the active reference to use here.
    if (connect_resp.object_ptr != 0)
    {
        // First check that connect_resp.object_ptr is really something we have
        // referenced earlier.

        KEVENT event;
        IO_STATUS_BLOCK io_status;
        PIRP irp;

        KeInitializeEvent(&event, NotificationEvent, FALSE);

        irp = IoBuildDeviceIoControlRequest(
            IOCTL_IMDISK_GET_REFERENCED_HANDLE,
            ImDiskCtlDevice,
            &connect_resp.object_ptr,
            sizeof(PFILE_OBJECT),
            NULL,
            0,
            TRUE,
            &event,
            &io_status);

        if (irp == NULL)
        {
            IoStatusBlock->Status = STATUS_INSUFFICIENT_RESOURCES;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        status = IoCallDriver(ImDiskCtlDevice, irp);

        if (status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        }

        if (!NT_SUCCESS(status))
        {
            DbgPrint("ImDisk: Failed claiming referenced object %p: %#x\n",
                (PVOID)(ULONG_PTR)connect_resp.object_ptr, status);

            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
            return IoStatusBlock->Status;
        }

        ObDereferenceObject(Proxy->device);
        Proxy->device = (PFILE_OBJECT)(ULONG_PTR)connect_resp.object_ptr;
    }

    KdPrint(("ImDisk Proxy Client: Got ok response IMDPROXY_CONNECT_RESP.\n"));

    IoStatusBlock->Status = STATUS_SUCCESS;
    IoStatusBlock->Information = 0;
    return IoStatusBlock->Status;
}

NTSTATUS
ImDiskQueryInformationProxy(IN PPROXY_CONNECTION Proxy,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent,
    OUT PIMDPROXY_INFO_RESP ProxyInfoResponse,
    IN ULONG ProxyInfoResponseLength)
{
    ULONGLONG proxy_req = IMDPROXY_REQ_INFO;
    NTSTATUS status;

    PAGED_CODE();

    ASSERT(Proxy != NULL);
    ASSERT(IoStatusBlock != NULL);

    if ((ProxyInfoResponse == NULL) |
        (ProxyInfoResponseLength < sizeof(IMDPROXY_INFO_RESP)))
    {
        IoStatusBlock->Status = STATUS_BUFFER_OVERFLOW;
        IoStatusBlock->Information = 0;
        return IoStatusBlock->Status;
    }

    KdPrint(("ImDisk Proxy Client: Sending IMDPROXY_REQ_INFO.\n"));

    status = ImDiskCallProxy(Proxy,
        IoStatusBlock,
        CancelEvent,
        &proxy_req,
        sizeof(proxy_req),
        NULL,
        0,
        ProxyInfoResponse,
        sizeof(IMDPROXY_INFO_RESP),
        NULL,
        0,
        NULL);

    if (!NT_SUCCESS(status))
    {
        IoStatusBlock->Status = status;
        IoStatusBlock->Information = 0;
        return IoStatusBlock->Status;
    }

    KdPrint(("ImDisk Proxy Client: Got ok response IMDPROXY_INFO_RESP.\n"));

    if (ProxyInfoResponse->req_alignment - 1 > FILE_512_BYTE_ALIGNMENT)
    {
#pragma warning(suppress: 6064)
#pragma warning(suppress: 6328)
        KdPrint(("ImDisk IMDPROXY_INFO_RESP: Unsupported sizes. "
            "Got 0x%.8x%.8x size and 0x%.8x%.8x alignment.\n",
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

#pragma code_seg()

NTSTATUS
ImDiskReadProxy(IN PPROXY_CONNECTION Proxy,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent,
    OUT PVOID Buffer,
    IN ULONG Length,
    IN PLARGE_INTEGER ByteOffset)
{
    IMDPROXY_READ_REQ read_req;
    IMDPROXY_READ_RESP read_resp;
    NTSTATUS status;
    ULONG_PTR max_transfer_size;
    ULONG length_done;

    ASSERT(Proxy != NULL);
    ASSERT(IoStatusBlock != NULL);
    ASSERT(Buffer != NULL);
    ASSERT(ByteOffset != NULL);

    if (Proxy->connection_type == PROXY_CONNECTION::PROXY_CONNECTION_SHM)
        max_transfer_size = Proxy->shared_memory_size - IMDPROXY_HEADER_SIZE;
    else
        max_transfer_size = Length;

    length_done = 0;
    status = STATUS_SUCCESS;

    while (length_done < Length)
    {
        ULONG length_to_do = Length - length_done;

        KdPrint2(("ImDisk Proxy Client: "
            "IMDPROXY_REQ_READ 0x%.8x done 0x%.8x left to do.\n",
            length_done, length_to_do));

        read_req.request_code = IMDPROXY_REQ_READ;
        read_req.offset = ByteOffset->QuadPart + length_done;
        read_req.length =
            length_to_do <= max_transfer_size ?
            length_to_do : max_transfer_size;

        KdPrint2(("ImDisk Proxy Client: "
            "IMDPROXY_REQ_READ 0x%.8x%.8x bytes at 0x%.8x%.8x.\n",
            ((PLARGE_INTEGER)&read_req.length)->HighPart,
            ((PLARGE_INTEGER)&read_req.length)->LowPart,
            ((PLARGE_INTEGER)&read_req.offset)->HighPart,
            ((PLARGE_INTEGER)&read_req.offset)->LowPart));

        status = ImDiskCallProxy(Proxy,
            IoStatusBlock,
            CancelEvent,
            &read_req,
            sizeof(read_req),
            NULL,
            0,
            &read_resp,
            sizeof(read_resp),
            (PUCHAR)Buffer + length_done,
            (ULONG)read_req.length,
            (PULONG)&read_resp.length);

        if (!NT_SUCCESS(status))
        {
            IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
            IoStatusBlock->Information = length_done;
            return IoStatusBlock->Status;
        }

        length_done += (ULONG)read_resp.length;

        if (read_resp.errorno != 0)
        {
#pragma warning(suppress: 6064)
#pragma warning(suppress: 6328)
            KdPrint(("ImDisk Proxy Client: Server returned error 0x%.8x%.8x.\n",
                read_resp.errorno));
            IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
            IoStatusBlock->Information = length_done;
            return IoStatusBlock->Status;
        }

        KdPrint2(("ImDisk Proxy Client: Server sent 0x%.8x%.8x bytes.\n",
            ((PLARGE_INTEGER)&read_resp.length)->HighPart,
            ((PLARGE_INTEGER)&read_resp.length)->LowPart));

        if (read_resp.length == 0)
            break;
    }

    IoStatusBlock->Status = status;
    IoStatusBlock->Information = length_done;

    return status;
}

NTSTATUS
ImDiskWriteProxy(IN PPROXY_CONNECTION Proxy,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent,
    IN PVOID Buffer,
    IN ULONG Length,
    IN PLARGE_INTEGER ByteOffset)
{
    IMDPROXY_WRITE_REQ write_req;
    IMDPROXY_WRITE_RESP write_resp;
    NTSTATUS status;
    ULONG_PTR max_transfer_size;
    ULONG length_done;

    ASSERT(Proxy != NULL);
    ASSERT(IoStatusBlock != NULL);
    ASSERT(Buffer != NULL);
    ASSERT(ByteOffset != NULL);

    if (Proxy->connection_type == PROXY_CONNECTION::PROXY_CONNECTION_SHM)
        max_transfer_size = Proxy->shared_memory_size - IMDPROXY_HEADER_SIZE;
    else
        max_transfer_size = Length;

    length_done = 0;
    status = STATUS_SUCCESS;

    while (length_done < Length)
    {
        ULONG length_to_do = Length - length_done;

        KdPrint2(("ImDisk Proxy Client: "
            "IMDPROXY_REQ_WRITE 0x%.8x done 0x%.8x left to do.\n",
            length_done, length_to_do));

        write_req.request_code = IMDPROXY_REQ_WRITE;
        write_req.offset = ByteOffset->QuadPart + length_done;
        write_req.length =
            length_to_do <= max_transfer_size ?
            length_to_do : max_transfer_size;

        KdPrint2(("ImDisk Proxy Client: "
            "IMDPROXY_REQ_WRITE 0x%.8x%.8x bytes at 0x%.8x%.8x.\n",
            ((PLARGE_INTEGER)&write_req.length)->HighPart,
            ((PLARGE_INTEGER)&write_req.length)->LowPart,
            ((PLARGE_INTEGER)&write_req.offset)->HighPart,
            ((PLARGE_INTEGER)&write_req.offset)->LowPart));

        status = ImDiskCallProxy(Proxy,
            IoStatusBlock,
            CancelEvent,
            &write_req,
            sizeof(write_req),
            (PUCHAR)Buffer + length_done,
            (ULONG)write_req.length,
            &write_resp,
            sizeof(write_resp),
            NULL,
            0,
            NULL);

        if (!NT_SUCCESS(status))
        {
            IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
            IoStatusBlock->Information = length_done;
            return IoStatusBlock->Status;
        }

        if (write_resp.errorno != 0)
        {
#pragma warning(suppress: 6064)
#pragma warning(suppress: 6328)
            KdPrint(("ImDisk Proxy Client: Server returned error 0x%.8x%.8x.\n",
                write_resp.errorno));
            IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
            IoStatusBlock->Information = length_done;
            return IoStatusBlock->Status;
        }

        if (write_resp.length != write_req.length)
        {
            KdPrint(("ImDisk Proxy Client: IMDPROXY_REQ_WRITE %u bytes, "
                "IMDPROXY_RESP_WRITE %u bytes.\n",
                Length,
                (ULONG)write_resp.length));
            IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
            IoStatusBlock->Information = length_done;
            return IoStatusBlock->Status;
        }

        KdPrint2(("ImDisk Proxy Client: Server replied OK.\n"));

        length_done += (ULONG)write_req.length;
    }

    IoStatusBlock->Status = STATUS_SUCCESS;
    IoStatusBlock->Information = length_done;
    return IoStatusBlock->Status;
}

NTSTATUS
ImDiskUnmapOrZeroProxy(IN PPROXY_CONNECTION Proxy,
    IN ULONGLONG RequestCode,
    IN OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent OPTIONAL,
    IN ULONG Items,
    IN PDEVICE_DATA_SET_RANGE Ranges)
{
    IMDPROXY_UNMAP_REQ unmap_req;
    IMDPROXY_UNMAP_RESP unmap_resp;
    NTSTATUS status;
    ULONG byte_size = (ULONG)(Items * sizeof(DEVICE_DATA_SET_RANGE));

    ASSERT(Proxy != NULL);
    ASSERT(IoStatusBlock != NULL);
    ASSERT(Ranges != NULL);

    if ((Proxy->connection_type == PROXY_CONNECTION::PROXY_CONNECTION_SHM) &&
        (byte_size >= (Proxy->shared_memory_size - IMDPROXY_HEADER_SIZE)))
    {
        status = STATUS_BUFFER_OVERFLOW;
        IoStatusBlock->Information = 0;
        IoStatusBlock->Status = status;
        return status;
    }

    status = STATUS_SUCCESS;

    unmap_req.request_code = RequestCode;
    unmap_req.length = byte_size;

#pragma warning(suppress: 6064)
#pragma warning(suppress: 6328)
    KdPrint(("ImDisk Proxy Client: Unmap/Zero 0x%.8x%.8x\n", RequestCode));

    status = ImDiskCallProxy(Proxy,
        IoStatusBlock,
        CancelEvent,
        &unmap_req,
        sizeof(unmap_req),
        (PUCHAR)Ranges,
        (ULONG)unmap_req.length,
        &unmap_resp,
        sizeof(unmap_resp),
        NULL,
        0,
        NULL);

    if (!NT_SUCCESS(status))
    {
        IoStatusBlock->Status = status;
        IoStatusBlock->Information = 0;
        return status;
    }

    if (unmap_resp.errorno != 0)
    {
#pragma warning(suppress: 6064)
#pragma warning(suppress: 6328)
        KdPrint(("ImDisk Proxy Client: Server returned error 0x%.8x%.8x.\n",
            unmap_resp.errorno));
        IoStatusBlock->Status = STATUS_IO_DEVICE_ERROR;
        IoStatusBlock->Information = 0;
        return IoStatusBlock->Status;
    }

    KdPrint(("ImDisk Proxy Client: Server replied OK.\n"));

    IoStatusBlock->Status = STATUS_SUCCESS;
    IoStatusBlock->Information = 0;
    return IoStatusBlock->Status;
}

#pragma code_seg("PAGE")

#ifdef INCLUDE_VFD_ORIGIN

//
//	Format tracks
//	Actually, just fills specified range of tracks with fill characters
//
NTSTATUS
ImDiskFloppyFormat(IN PDEVICE_EXTENSION Extension,
    IN PIRP Irp)
{
    PFORMAT_PARAMETERS	param;
    ULONG			track_length;
    PUCHAR		format_buffer;
    LARGE_INTEGER		start_offset;
    LARGE_INTEGER		end_offset;
    NTSTATUS		status;

    PAGED_CODE();

    ASSERT(Extension != NULL);
    ASSERT(Irp != NULL);

    param = (PFORMAT_PARAMETERS)Irp->AssociatedIrp.SystemBuffer;

    track_length =
        Extension->disk_geometry.BytesPerSector *
        Extension->disk_geometry.SectorsPerTrack;

    start_offset.QuadPart =
        param->StartCylinderNumber * Extension->disk_geometry.TracksPerCylinder *
        track_length + param->StartHeadNumber * track_length;

    end_offset.QuadPart =
        param->EndCylinderNumber * Extension->disk_geometry.TracksPerCylinder *
        track_length + param->EndHeadNumber * track_length;

    if (Extension->vm_disk)
    {
        LARGE_INTEGER wait_time;

        RtlFillMemory(((PUCHAR)Extension->image_buffer) + start_offset.LowPart,
            end_offset.LowPart - start_offset.LowPart + track_length,
            MEDIA_FORMAT_FILL_DATA);

        wait_time.QuadPart = -1;
        KeDelayExecutionThread(KernelMode, FALSE, &wait_time);

        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    start_offset.QuadPart += Extension->image_offset.QuadPart;
    end_offset.QuadPart += Extension->image_offset.QuadPart;

    format_buffer = (PUCHAR)
        ExAllocatePoolWithTag(PagedPool, track_length, POOL_TAG);

    if (format_buffer == NULL)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlFillMemory(format_buffer, track_length, MEDIA_FORMAT_FILL_DATA);

    do
    {
        if (Extension->use_proxy)
        {
            status = ImDiskWriteProxy(&Extension->proxy,
                &Irp->IoStatus,
                &Extension->terminate_thread,
                format_buffer,
                track_length,
                &start_offset);
        }
        else
        {
            status = NtWriteFile(Extension->file_handle,
                NULL,
                NULL,
                NULL,
                &Irp->IoStatus,
                format_buffer,
                track_length,
                &start_offset,
                NULL);
        }

        if (!NT_SUCCESS(status))
        {
            KdPrint(("ImDisk Format failed: Write failed with status %#x.\n",
                status));

            break;
        }

        start_offset.QuadPart += track_length;
    } while (start_offset.QuadPart <= end_offset.QuadPart);

    ExFreePoolWithTag(format_buffer, POOL_TAG);

    Irp->IoStatus.Information = 0;

    return status;
}

#endif // INCLUDE_VFD_ORIGIN
