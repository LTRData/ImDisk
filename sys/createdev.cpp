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

#include "imdsksys.h"

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

const DISK_GEOMETRY media_table[] = {
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

#pragma code_seg("PAGE")

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
ImDiskCreateDevice(__in PDRIVER_OBJECT DriverObject,
    __inout __deref PIMDISK_CREATE_DATA CreateData,
    __in PETHREAD ClientThread,
    __out PDEVICE_OBJECT *DeviceObject)
{
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

    WPoolMem<WCHAR, NonPagedPool> file_name_buffer;

    UNICODE_STRING file_name;
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
        ACCESS_MASK desired_access = 0;
        ULONG share_access = 0;
        ULONG create_options = 0;

        if (file_name.MaximumLength > 0)
        {
            file_name.Buffer = file_name_buffer.Alloc(file_name.MaximumLength);

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

        WPoolMem<WCHAR, PagedPool> real_file_name_buffer;
        UNICODE_STRING real_file_name = { 0 };

        if ((IMDISK_TYPE(CreateData->Flags) == IMDISK_TYPE_FILE) &&
            (IMDISK_FILE_TYPE(CreateData->Flags) == IMDISK_FILE_TYPE_AWEALLOC))
        {
            real_file_name.MaximumLength = file_name.Length +
                sizeof(AWEALLOC_DEVICE_NAME);

            real_file_name.Buffer = real_file_name_buffer.Alloc(real_file_name.MaximumLength);

            if (real_file_name.Buffer == NULL)
            {
                KdPrint(("ImDisk: Out of memory while allocating %#x bytes\n",
                    real_file_name.MaximumLength));

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
            KdPrint(("ImDisk: Creating new image file DesiredAccess=%#x ShareAccess=%#x CreateOptions=%#x\n",
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

            return status;
        }

        KdPrint(("ImDisk: File '%wZ' opened successfully.\n",
            &real_file_name));

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
                WPoolMem<FAT_VBR, PagedPool> fat_vbr(sizeof(FAT_VBR));

                if (!fat_vbr)
                {
                    if (file_handle != NULL)
                        ZwClose(file_handle);

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
                WPoolMem<FAT_VBR, PagedPool> fat_vbr(sizeof(FAT_VBR));

                if (!fat_vbr)
                {
                    ImDiskCloseProxy(&proxy);
                    ZwClose(file_handle);

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
            }

            if ((proxy_info.req_alignment - 1 > FILE_512_BYTE_ALIGNMENT) |
                (CreateData->DiskGeometry.Cylinders.QuadPart == 0))
            {
                ImDiskCloseProxy(&proxy);
                ZwClose(file_handle);

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

    WPoolMem<WCHAR, PagedPool> device_name_buffer;

    if (NT_SUCCESS(status))
    {
        // Buffer for device name
        device_name_buffer.Alloc(MAXIMUM_FILENAME_LENGTH *
            sizeof(*device_name_buffer));

        if (!device_name_buffer)
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
            (PWSTR)device_name_buffer, device_type, device_characteristics));

    status = IoCreateDevice(DriverObject,
        sizeof(DEVICE_EXTENSION),
        &device_name,
        device_type,
        device_characteristics,
        FALSE,
        DeviceObject);

    if (NT_SUCCESS(status))
    {
        WPoolMem<WCHAR, PagedPool> symlink_name_buffer(
            MAXIMUM_FILENAME_LENGTH * sizeof(WCHAR));

        if (!symlink_name_buffer)
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

            KdPrint(("ImDisk: Creating symlink '%ws'.\n", (PWSTR)symlink_name_buffer));

            status = IoCreateUnprotectedSymbolicLink(&symlink_name, &device_name);
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

        ImDiskCloseProxy(&proxy);
        
        if (file_handle != NULL)
            ZwClose(file_handle);
        
        if (file_object != NULL)
            ObDereferenceObject(file_object);
        
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

    file_name_buffer.Abandon();

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

    KdPrint(("ImDisk: Device '%ws' created.\n", (PWSTR)device_name_buffer));

    return STATUS_SUCCESS;
}

