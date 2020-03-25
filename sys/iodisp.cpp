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

NTSTATUS
ImDiskDispatchCreateClose(IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION io_stack;
    PDEVICE_EXTENSION device_extension;
    NTSTATUS status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Irp != NULL);

    KdPrint(("ImDisk: Entering ImDiskDispatchCreateClose.\n"));

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

    IoCompleteRequest(Irp, IO_DISK_INCREMENT);

    return status;
}

NTSTATUS
ImDiskDispatchReadWrite(IN PDEVICE_OBJECT DeviceObject,
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

        IoCompleteRequest(Irp, IO_DISK_INCREMENT);

        return status;
    }

    if (io_stack->Parameters.Read.Length == 0)
    {
        KdPrint(("ImDisk: Read/write zero bytes on device %i.\n",
            device_extension->device_number));

        status = STATUS_SUCCESS;

        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;

        IoCompleteRequest(Irp, IO_DISK_INCREMENT);

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
        IoCompleteRequest(Irp, IO_DISK_INCREMENT);

        return status;
    }
}

NTSTATUS
ImDiskDispatchDeviceControl(IN PDEVICE_OBJECT DeviceObject,
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

        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    }

    return status;
}

NTSTATUS
ImDiskDispatchFlushBuffers(IN PDEVICE_OBJECT DeviceObject,
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

        IoCompleteRequest(Irp, IO_DISK_INCREMENT);

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
ImDiskDispatchQueryInformation(IN PDEVICE_OBJECT DeviceObject,
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

        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    }

    return status;
}

NTSTATUS
ImDiskDispatchSetInformation(IN PDEVICE_OBJECT DeviceObject,
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

    IoCompleteRequest(Irp, IO_DISK_INCREMENT);

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
            {
                status = STATUS_MEDIA_WRITE_PROTECTED;
            }
            else
            {
                if (io_stack->Parameters.UsageNotification.InPath == TRUE)
                {
                    // Not needed anymore. Device thread routines are non-pageable.

                    //(VOID)MmLockPagableCodeSection((PVOID)(ULONG_PTR)ImDiskDeviceThread);
                }
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

        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    }

    return status;
}

