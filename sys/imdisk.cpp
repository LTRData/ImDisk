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

    DriverObject->MajorFunction[IRP_MJ_CREATE] = ImDiskDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = ImDiskDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = ImDiskDispatchReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = ImDiskDispatchReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ImDiskDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = ImDiskDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = ImDiskDispatchPnP;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = ImDiskDispatchQueryInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION] = ImDiskDispatchSetInformation;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = ImDiskDispatchFlushBuffers;

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

