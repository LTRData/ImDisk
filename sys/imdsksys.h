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

#pragma once

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

#define POOL_TAG                         'iDmI'

#include "..\inc\ntkmapi.h"
#include "..\inc\imdisk.h"
#include "..\inc\imdproxy.h"
#include "..\inc\wkmem.hpp"

#pragma warning(disable: 28719)

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
DRIVER_DISPATCH ImDiskDispatchCreateClose;

__drv_dispatchType(IRP_MJ_READ)
__drv_dispatchType(IRP_MJ_WRITE)
DRIVER_DISPATCH ImDiskDispatchReadWrite;

__drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
__drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL)
DRIVER_DISPATCH ImDiskDispatchDeviceControl;

__drv_dispatchType(IRP_MJ_PNP)
DRIVER_DISPATCH ImDiskDispatchPnP;

__drv_dispatchType(IRP_MJ_QUERY_INFORMATION)
DRIVER_DISPATCH ImDiskDispatchQueryInformation;

__drv_dispatchType(IRP_MJ_SET_INFORMATION)
DRIVER_DISPATCH ImDiskDispatchSetInformation;

__drv_dispatchType(IRP_MJ_FLUSH_BUFFERS)
DRIVER_DISPATCH ImDiskDispatchFlushBuffers;

KSTART_ROUTINE ImDiskDeviceThread;

IO_COMPLETION_ROUTINE ImDiskReadWriteLowerDeviceCompletion;

NTSTATUS
ImDiskCreateDevice(__in PDRIVER_OBJECT DriverObject,
    __inout __deref PIMDISK_CREATE_DATA CreateData,
    __in PETHREAD ClientThread,
    __out PDEVICE_OBJECT *DeviceObject);

NTSTATUS
ImDiskSafeIOStream(IN PFILE_OBJECT FileObject,
    IN UCHAR MajorFunction,
    IN OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PKEVENT CancelEvent,
    IN OUT PVOID Buffer,
    IN ULONG Length);

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

NTSTATUS
ImDiskDeviceControlLowerDevice(PIRP Irp, PDEVICE_EXTENSION DeviceExtension);

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
extern PDEVICE_OBJECT ImDiskCtlDevice;

//
// Allocation bitmap with currently configured device numbers.
// (No device list bit field is maintained anymore. Device list is always
// enumerated "live" directly from current device objects.)
//
//extern volatile ULONGLONG DeviceList = 0;

//
// Max number of devices that can be dynamically created by IOCTL calls
// to the control device.
//
extern ULONG MaxDevices;

//
// Device list lock
//
extern KSPIN_LOCK DeviceListLock;

//
// Device list lock
//
extern KSPIN_LOCK ReferencedObjectsListLock;

//
// List of objects referenced using
// IOCTL_IMDISK_REFERENCE_HANDLE
//
extern LIST_ENTRY ReferencedObjects;

//
// Handle to global refresh event
//
extern PKEVENT RefreshEvent;

