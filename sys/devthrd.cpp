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

VOID
ImDiskDeviceThreadRead(IN PIRP Irp,
    IN PDEVICE_EXTENSION DeviceExtension,
    IN PDEVICE_OBJECT DeviceObject)
{
    PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
    PUCHAR system_buffer =
        (PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
            NormalPagePriority);
    LARGE_INTEGER offset;
    KLOCK_QUEUE_HANDLE lock_handle;

    if (system_buffer == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        return;
    }

    if (DeviceExtension->vm_disk)
    {
#ifdef _WIN64
        ULONG_PTR vm_offset =
            io_stack->Parameters.Read.ByteOffset.QuadPart;
#else
        ULONG_PTR vm_offset =
            io_stack->Parameters.Read.ByteOffset.LowPart;
#endif

        RtlCopyMemory(system_buffer,
            DeviceExtension->image_buffer +
            vm_offset,
            io_stack->Parameters.Read.Length);

        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = io_stack->Parameters.Read.Length;

        if (io_stack->FileObject != NULL)
        {
            io_stack->FileObject->CurrentByteOffset.QuadPart +=
                Irp->IoStatus.Information;
        }

        return;
    }

    offset.QuadPart = io_stack->Parameters.Read.ByteOffset.QuadPart +
        DeviceExtension->image_offset.QuadPart;

    ImDiskAcquireLock(&DeviceExtension->last_io_lock, &lock_handle);

    if ((DeviceExtension->last_io_data != NULL) &
        (DeviceExtension->last_io_length <
            io_stack->Parameters.Read.Length))
    {
        ExFreePoolWithTag(DeviceExtension->last_io_data, POOL_TAG);
        DeviceExtension->last_io_data = NULL;
    }

    DeviceExtension->last_io_offset = 0;
    DeviceExtension->last_io_length = 0;

    ImDiskReleaseLock(&lock_handle);

    if (DeviceExtension->last_io_data == NULL)
        DeviceExtension->last_io_data = (PUCHAR)
        ExAllocatePoolWithTag(NonPagedPool,
            io_stack->Parameters.Read.Length,
            POOL_TAG);

    if (DeviceExtension->last_io_data == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        return;
    }

    if (DeviceExtension->use_proxy)
    {
        Irp->IoStatus.Status =
            ImDiskReadProxy(&DeviceExtension->proxy,
                &Irp->IoStatus,
                &DeviceExtension->terminate_thread,
                DeviceExtension->last_io_data,
                io_stack->Parameters.Read.Length,
                &offset);

        if (!NT_SUCCESS(Irp->IoStatus.Status))
        {
            KdPrint(("ImDisk: Read failed on device %i: %#x.\n",
                DeviceExtension->device_number,
                Irp->IoStatus.Status));

            // If indicating that proxy connection died we can do
            // nothing else but remove this device.
            // if (Irp->IoStatus.Status == STATUS_CONNECTION_RESET)
            ImDiskRemoveVirtualDisk(DeviceObject);

            Irp->IoStatus.Status = STATUS_DEVICE_REMOVED;
            Irp->IoStatus.Information = 0;
        }
    }
    else
    {
        Irp->IoStatus.Status =
            NtReadFile(DeviceExtension->file_handle,
                NULL,
                NULL,
                NULL,
                &Irp->IoStatus,
                DeviceExtension->last_io_data,
                io_stack->Parameters.Read.Length,
                &offset,
                NULL);
    }

    if (NT_SUCCESS(Irp->IoStatus.Status))
    {
        RtlCopyMemory(system_buffer, DeviceExtension->last_io_data,
            Irp->IoStatus.Information);

        if (DeviceExtension->byte_swap)
            ImDiskByteSwapBuffer(system_buffer,
                Irp->IoStatus.Information);

        DeviceExtension->last_io_offset =
            io_stack->Parameters.Read.ByteOffset.QuadPart;

        DeviceExtension->last_io_length =
            (ULONG)Irp->IoStatus.Information;

        if (io_stack->FileObject != NULL)
        {
            io_stack->FileObject->CurrentByteOffset.QuadPart +=
                Irp->IoStatus.Information;
        }
    }
    else
    {
        ExFreePoolWithTag(DeviceExtension->last_io_data, POOL_TAG);
        DeviceExtension->last_io_data = NULL;
    }

    return;
}

VOID
ImDiskDeviceThreadWrite(IN PIRP Irp,
    IN PDEVICE_EXTENSION DeviceExtension,
    IN PDEVICE_OBJECT DeviceObject)
{
    PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
    PUCHAR system_buffer =
        (PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
            NormalPagePriority);
    LARGE_INTEGER offset;
    BOOLEAN set_zero_data = FALSE;
    KLOCK_QUEUE_HANDLE lock_handle;

    if (system_buffer == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        return;
    }

    if (!DeviceExtension->image_modified)
    {
        DeviceExtension->image_modified = TRUE;

        // Fire refresh event
        if (RefreshEvent != NULL)
            KePulseEvent(RefreshEvent, 0, FALSE);
    }

    if (DeviceExtension->vm_disk)
    {
#ifdef _WIN64
        ULONG_PTR vm_offset =
            io_stack->Parameters.Write.ByteOffset.QuadPart;
#else
        ULONG_PTR vm_offset =
            io_stack->Parameters.Write.ByteOffset.LowPart;
#endif

        RtlCopyMemory(DeviceExtension->image_buffer +
            vm_offset,
            system_buffer,
            io_stack->Parameters.Write.Length);

        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = io_stack->Parameters.Write.Length;

        if (io_stack->FileObject != NULL)
        {
            io_stack->FileObject->CurrentByteOffset.QuadPart +=
                Irp->IoStatus.Information;
        }

        return;
    }

    offset.QuadPart = io_stack->Parameters.Write.ByteOffset.QuadPart +
        DeviceExtension->image_offset.QuadPart;

    ImDiskAcquireLock(&DeviceExtension->last_io_lock, &lock_handle);

    if ((DeviceExtension->last_io_data != NULL) &
        (DeviceExtension->last_io_length <
            io_stack->Parameters.Write.Length))
    {
        ExFreePoolWithTag(DeviceExtension->last_io_data, POOL_TAG);
        DeviceExtension->last_io_data = NULL;
    }

    DeviceExtension->last_io_offset = 0;
    DeviceExtension->last_io_length = 0;

    ImDiskReleaseLock(&lock_handle);

    if (DeviceExtension->last_io_data == NULL)
    {
        DeviceExtension->last_io_data = (PUCHAR)
            ExAllocatePoolWithTag(NonPagedPool,
                io_stack->Parameters.Write.Length,
                POOL_TAG);
    }

    if (DeviceExtension->last_io_data == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        return;
    }

    RtlCopyMemory(DeviceExtension->last_io_data, system_buffer,
        io_stack->Parameters.Write.Length);

    if ((DeviceExtension->use_set_zero_data ||
        (DeviceExtension->use_proxy &&
            DeviceExtension->proxy_zero)) &&
        ImDiskIsBufferZero(DeviceExtension->last_io_data,
            io_stack->Parameters.Write.Length))
    {
        set_zero_data = TRUE;
    }

    if ((!set_zero_data) && DeviceExtension->byte_swap)
    {
        ImDiskByteSwapBuffer(DeviceExtension->last_io_data,
            Irp->IoStatus.Information);
    }

    if (DeviceExtension->use_proxy)
    {
        if (set_zero_data && DeviceExtension->proxy_zero)
        {
            DEVICE_DATA_SET_RANGE range;
            range.StartingOffset = offset.QuadPart;
            range.LengthInBytes = io_stack->Parameters.Write.Length;

            Irp->IoStatus.Status =
                ImDiskUnmapOrZeroProxy(&DeviceExtension->proxy,
                    IMDPROXY_REQ_ZERO,
                    &Irp->IoStatus,
                    &DeviceExtension->terminate_thread,
                    1,
                    &range);

            if (NT_SUCCESS(Irp->IoStatus.Status))
            {
                Irp->IoStatus.Information =
                    io_stack->Parameters.Write.Length;
            }
        }
        else
        {
            Irp->IoStatus.Status =
                ImDiskWriteProxy(&DeviceExtension->proxy,
                    &Irp->IoStatus,
                    &DeviceExtension->terminate_thread,
                    DeviceExtension->last_io_data,
                    io_stack->Parameters.Write.Length,
                    &offset);
        }

        if (!NT_SUCCESS(Irp->IoStatus.Status))
        {
            KdPrint(("ImDisk: Write failed on device %i: %#x.\n",
                DeviceExtension->device_number,
                Irp->IoStatus.Status));

            // If indicating that proxy connection died we can do
            // nothing else but remove this device.
            if (Irp->IoStatus.Status == STATUS_CONNECTION_RESET)
                ImDiskRemoveVirtualDisk(DeviceObject);

            Irp->IoStatus.Status = STATUS_DEVICE_REMOVED;
            Irp->IoStatus.Information = 0;
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

            Irp->IoStatus.Status =
                NtFsControlFile(DeviceExtension->file_handle,
                    NULL,
                    NULL,
                    NULL,
                    &Irp->IoStatus,
                    FSCTL_SET_ZERO_DATA,
                    &zero_data,
                    sizeof(zero_data),
                    NULL,
                    0);

            if (NT_SUCCESS(Irp->IoStatus.Status))
            {
                KdPrint2(("ImDisk: Zero block set.\n"));
                Irp->IoStatus.Information =
                    io_stack->Parameters.Write.Length;
            }
            else
            {
                KdPrint(("ImDisk: Volume does not support "
                    "FSCTL_SET_ZERO_DATA: 0x%#X\n",
                    Irp->IoStatus.Status));

                Irp->IoStatus.Information = 0;
                set_zero_data = FALSE;
                DeviceExtension->use_set_zero_data = FALSE;
            }
        }

        if (!set_zero_data)
        {
            Irp->IoStatus.Status = NtWriteFile(
                DeviceExtension->file_handle,
                NULL,
                NULL,
                NULL,
                &Irp->IoStatus,
                DeviceExtension->last_io_data,
                io_stack->Parameters.Write.Length,
                &offset,
                NULL);
        }
    }

    if (NT_SUCCESS(Irp->IoStatus.Status))
    {
        DeviceExtension->last_io_offset =
            io_stack->Parameters.Write.ByteOffset.QuadPart;
        DeviceExtension->last_io_length =
            io_stack->Parameters.Write.Length;

        if (io_stack->FileObject != NULL)
        {
            io_stack->FileObject->CurrentByteOffset.QuadPart +=
                Irp->IoStatus.Information;
        }
    }
    else
    {
        ExFreePoolWithTag(DeviceExtension->last_io_data, POOL_TAG);
        DeviceExtension->last_io_data = NULL;
    }

    return;
}

VOID
ImDiskDeviceThreadFlushBuffers(IN PIRP Irp,
    IN PDEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS status;
    PIRP image_irp;
    KEVENT io_complete_event;
    PIO_STACK_LOCATION image_io_stack;

    if (DeviceExtension->file_object == NULL)
    {
        status = ObReferenceObjectByHandle(
            DeviceExtension->file_handle,
            FILE_WRITE_ATTRIBUTES |
            FILE_WRITE_DATA |
            SYNCHRONIZE,
            *IoFileObjectType,
            KernelMode,
            (PVOID*)&DeviceExtension->file_object,
            NULL);

        if (!NT_SUCCESS(status))
        {
            Irp->IoStatus.Status = status;
            Irp->IoStatus.Information = 0;

            KdPrint(("ImDisk: ObReferenceObjectByHandle failed on image handle: %#x\n",
                status));

            return;
        }

        DeviceExtension->dev_object = IoGetRelatedDeviceObject(
            DeviceExtension->file_object);
    }

    KeInitializeEvent(&io_complete_event,
        NotificationEvent,
        FALSE);

    image_irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_FLUSH_BUFFERS,
        DeviceExtension->dev_object,
        NULL,
        0,
        NULL,
        &io_complete_event,
        &Irp->IoStatus);

    if (image_irp == NULL)
    {
        KdPrint(("ImDisk: IoBuildSynchronousFsdRequest failed for image object.\n"));

        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        return;
    }

    image_io_stack = IoGetNextIrpStackLocation(image_irp);
    image_io_stack->FileObject = DeviceExtension->file_object;

    status = IoCallDriver(DeviceExtension->dev_object, image_irp);

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
        Irp->IoStatus.Status = status;
    }

    KdPrint(("ImDisk: IoCallDriver result for flush request: %#x\n",
        status));

    return;
}

VOID
ImDiskDeviceThreadDeviceControl(IN PIRP Irp,
    IN PDEVICE_EXTENSION DeviceExtension,
    IN PDEVICE_OBJECT DeviceObject)
{
    PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);

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
            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            Irp->IoStatus.Information = 0;
            break;
        }

        if (DeviceExtension->use_proxy)
        {
            Irp->IoStatus.Status =
                ImDiskReadProxy(&DeviceExtension->proxy,
                    &Irp->IoStatus,
                    &DeviceExtension->terminate_thread,
                    buffer,
                    0,
                    &DeviceExtension->image_offset);
        }
        else
        {
            Irp->IoStatus.Status =
                NtReadFile(DeviceExtension->file_handle,
                    NULL,
                    NULL,
                    NULL,
                    &Irp->IoStatus,
                    buffer,
                    0,
                    &DeviceExtension->image_offset,
                    NULL);
        }

        ExFreePoolWithTag(buffer, POOL_TAG);

        if (!NT_SUCCESS(Irp->IoStatus.Status))
        {
            KdPrint(("ImDisk: Verify failed on device %i.\n",
                DeviceExtension->device_number));

            // If indicating that proxy connection died we can do
            // nothing else but remove this device.
            if (Irp->IoStatus.Status == STATUS_CONNECTION_RESET)
                ImDiskRemoveVirtualDisk(DeviceObject);

            Irp->IoStatus.Status = STATUS_DEVICE_REMOVED;
            Irp->IoStatus.Information = 0;
            break;
        }

        KdPrint(("ImDisk: Verify ok on device %i.\n",
            DeviceExtension->device_number));

        if (io_stack->Parameters.DeviceIoControl.OutputBufferLength <
            sizeof(ULONG))
            Irp->IoStatus.Information = 0;
        else
        {
            *(PULONG)Irp->AssociatedIrp.SystemBuffer =
                DeviceExtension->media_change_count;

            Irp->IoStatus.Information = sizeof(ULONG);
        }

        Irp->IoStatus.Status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_IMDISK_IOCTL_PASS_THROUGH:
    case IOCTL_IMDISK_FSCTL_PASS_THROUGH:
    {
        NTSTATUS status;
        ULONG ctl_code = *(PULONG)Irp->AssociatedIrp.SystemBuffer;
        PVOID in_buffer = (PUCHAR)Irp->AssociatedIrp.SystemBuffer +
            sizeof(ULONG);
        PVOID out_buffer = Irp->AssociatedIrp.SystemBuffer;
        ULONG in_size =
            io_stack->Parameters.DeviceIoControl.InputBufferLength -
            sizeof(ULONG);
        ULONG out_size =
            io_stack->Parameters.DeviceIoControl.OutputBufferLength;

        UCHAR func;
        PIRP lower_irp;
        KEVENT event;
        PIO_STACK_LOCATION lower_io_stack;

        if (DeviceExtension->file_object == NULL)
        {
            status = ObReferenceObjectByHandle(
                DeviceExtension->file_handle,
                FILE_WRITE_ATTRIBUTES |
                FILE_WRITE_DATA |
                SYNCHRONIZE,
                *IoFileObjectType,
                KernelMode,
                (PVOID*)&DeviceExtension->file_object,
                NULL);

            if (!NT_SUCCESS(status))
            {
                Irp->IoStatus.Status = status;
                Irp->IoStatus.Information = 0;

                KdPrint(("ImDisk: ObReferenceObjectByHandle failed on image handle: %#x\n",
                    status));

                break;
            }

            DeviceExtension->dev_object = IoGetRelatedDeviceObject(
                DeviceExtension->file_object);
        }

        if (io_stack->MajorFunction == IOCTL_IMDISK_FSCTL_PASS_THROUGH)
        {
            KdPrint(("ImDisk: IOCTL_IMDISK_FSCTL_PASS_THROUGH for device %i control code %#x.\n",
                DeviceExtension->device_number, ctl_code));

            func = IRP_MJ_FILE_SYSTEM_CONTROL;
        }
        else
        {
            KdPrint(("ImDisk: IOCTL_IMDISK_IOCTL_PASS_THROUGH for device %i control code %#x.\n",
                DeviceExtension->device_number, ctl_code));

            func = IRP_MJ_DEVICE_CONTROL;
        }

        KeInitializeEvent(&event, NotificationEvent, FALSE);

        lower_irp = IoBuildDeviceIoControlRequest(
            ctl_code,
            DeviceExtension->dev_object,
            in_buffer,
            in_size,
            out_buffer,
            out_size,
            FALSE,
            &event,
            &Irp->IoStatus);

        if (lower_irp == NULL)
        {
            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            Irp->IoStatus.Information = 0;
            break;
        }

        lower_irp->RequestorMode = Irp->RequestorMode;

        lower_io_stack = IoGetNextIrpStackLocation(lower_irp);
        lower_io_stack->FileObject = DeviceExtension->file_object;
        lower_io_stack->MajorFunction = func;

        status = IoCallDriver(DeviceExtension->dev_object,
            lower_irp);

        if (status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&event, Executive, KernelMode,
                FALSE, NULL);
        }
        else
        {
            Irp->IoStatus.Status = status;
        }

        KdPrint(("ImDisk: IOCTL_IMDISK_IOCTL/FSCTL_PASS_THROUGH for device %i control code %#x result status %#x.\n",
            DeviceExtension->device_number, ctl_code, status));

        break;
    }

#ifdef INCLUDE_VFD_ORIGIN

    case IOCTL_DISK_FORMAT_TRACKS:
    case IOCTL_DISK_FORMAT_TRACKS_EX:
    {
        NTSTATUS status =
            ImDiskFloppyFormat(DeviceExtension, Irp);

        if (!NT_SUCCESS(status))
        {
            // If indicating that proxy connection died we can do
            // nothing else but remove this device.
            if (status == STATUS_CONNECTION_RESET)
                ImDiskRemoveVirtualDisk(DeviceObject);

            Irp->IoStatus.Status = STATUS_DEVICE_REMOVED;
            Irp->IoStatus.Information = 0;
            break;
        }

        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = status;
        break;
    }

#endif // INCLUDE_VFD_ORIGIN

    case IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES:
    {
        Irp->IoStatus.Information = 0;

        PDEVICE_MANAGE_DATA_SET_ATTRIBUTES attrs =
            (PDEVICE_MANAGE_DATA_SET_ATTRIBUTES)
            Irp->AssociatedIrp.SystemBuffer;

        Irp->IoStatus.Status = STATUS_SUCCESS;

        int items = attrs->DataSetRangesLength /
            sizeof(DEVICE_DATA_SET_RANGE);

        PDEVICE_DATA_SET_RANGE range = (PDEVICE_DATA_SET_RANGE)
            ExAllocatePoolWithTag(PagedPool,
                items * sizeof(DEVICE_DATA_SET_RANGE), POOL_TAG);

        if (range == NULL)
        {
            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        RtlCopyMemory(range, (PUCHAR)attrs + attrs->DataSetRangesOffset,
            items * sizeof(DEVICE_DATA_SET_RANGE));

        if (DeviceExtension->image_offset.QuadPart > 0)
        {
            for (int i = 0; i < items; i++)
            {
                range[i].StartingOffset +=
                    DeviceExtension->image_offset.QuadPart;
            }
        }

        Irp->IoStatus.Status = ImDiskUnmapOrZeroProxy(
            &DeviceExtension->proxy,
            IMDPROXY_REQ_UNMAP,
            &Irp->IoStatus,
            &DeviceExtension->terminate_thread,
            items,
            range);

        ExFreePoolWithTag(range, POOL_TAG);

        KdPrint(("ImDisk: Unmap result on device %i: %#x.\n",
            DeviceExtension->device_number,
            Irp->IoStatus.Status));

        break;
    }

    case IOCTL_DISK_GROW_PARTITION:
    {
        NTSTATUS status;
        FILE_END_OF_FILE_INFORMATION new_size;
        FILE_STANDARD_INFORMATION file_standard_information;
        PDISK_GROW_PARTITION grow_data = (PDISK_GROW_PARTITION)Irp->AssociatedIrp.SystemBuffer;

        KdPrint(("ImDisk: Request to grow device %i by %I64i bytes.\n",
            DeviceExtension->device_number,
            grow_data->BytesToGrow.QuadPart));

        new_size.EndOfFile.QuadPart =
            DeviceExtension->disk_geometry.Cylinders.QuadPart +
            grow_data->BytesToGrow.QuadPart;

        KdPrint(("ImDisk: New size of device %i will be %I64i bytes.\n",
            DeviceExtension->device_number,
            new_size.EndOfFile.QuadPart));

        if (new_size.EndOfFile.QuadPart <= 0)
        {
            Irp->IoStatus.Status = STATUS_END_OF_MEDIA;
            Irp->IoStatus.Information = 0;
            break;
        }

        if (DeviceExtension->vm_disk)
        {
            PVOID new_image_buffer = NULL;
            SIZE_T free_size = 0;
#ifdef _WIN64
            ULONG_PTR old_size =
                DeviceExtension->disk_geometry.Cylinders.QuadPart;
            SIZE_T max_size = new_size.EndOfFile.QuadPart;
#else
            ULONG_PTR old_size =
                DeviceExtension->disk_geometry.Cylinders.LowPart;
            SIZE_T max_size = new_size.EndOfFile.LowPart;

            // A vm type disk cannot be extended to a larger size than
            // 2 GB.
            if (new_size.EndOfFile.QuadPart & 0xFFFFFFFF80000000)
            {
                Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
                Irp->IoStatus.Information = 0;
                break;
            }
#endif // _WIN64

            KdPrint(("ImDisk: Allocating %I64u bytes.\n",
                (ULONGLONG)max_size));

            status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                &new_image_buffer,
                0,
                &max_size,
                MEM_COMMIT,
                PAGE_READWRITE);

            if (!NT_SUCCESS(status))
            {
                status = STATUS_NO_MEMORY;
                Irp->IoStatus.Status = status;
                Irp->IoStatus.Information = 0;
                break;
            }

            RtlCopyMemory(new_image_buffer,
                DeviceExtension->image_buffer,
                min(old_size, max_size));

            ZwFreeVirtualMemory(NtCurrentProcess(),
                (PVOID*)&DeviceExtension->image_buffer,
                &free_size,
                MEM_RELEASE);

            DeviceExtension->image_buffer = (PUCHAR)new_image_buffer;
            DeviceExtension->disk_geometry.Cylinders =
                new_size.EndOfFile;

            // Fire refresh event
            if (RefreshEvent != NULL)
                KePulseEvent(RefreshEvent, 0, FALSE);

            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;
        }

        // For proxy-type disks the new size is just accepted and
        // that's it.
        if (DeviceExtension->use_proxy)
        {
            DeviceExtension->disk_geometry.Cylinders =
                new_size.EndOfFile;

            // Fire refresh event
            if (RefreshEvent != NULL)
                KePulseEvent(RefreshEvent, 0, FALSE);

            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;
        }

        // Image file backed disks left to do.

        // For disks with offset, refuse to extend size. Otherwise we
        // could break compatibility with the header data we have
        // skipped and we don't know about.
        if (DeviceExtension->image_offset.QuadPart != 0)
        {
            Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
            Irp->IoStatus.Information = 0;
            break;
        }

        status =
            ZwQueryInformationFile(DeviceExtension->file_handle,
                &Irp->IoStatus,
                &file_standard_information,
                sizeof file_standard_information,
                FileStandardInformation);

        if (!NT_SUCCESS(status))
        {
            Irp->IoStatus.Status = status;
            Irp->IoStatus.Information = 0;
            break;
        }

        KdPrint(("ImDisk: Current image size is %I64u bytes.\n",
            file_standard_information.EndOfFile.QuadPart));

        if (file_standard_information.EndOfFile.QuadPart >=
            new_size.EndOfFile.QuadPart)
        {
            DeviceExtension->disk_geometry.Cylinders =
                new_size.EndOfFile;

            // Fire refresh event
            if (RefreshEvent != NULL)
                KePulseEvent(RefreshEvent, 0, FALSE);

            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;
        }

        // For other, fixed file-backed disks we need to adjust the
        // physical file size.

        KdPrint(("ImDisk: Setting new image size to %I64u bytes.\n",
            new_size.EndOfFile.QuadPart));

        status = ZwSetInformationFile(DeviceExtension->file_handle,
            &Irp->IoStatus,
            &new_size,
            sizeof new_size,
            FileEndOfFileInformation);

        if (NT_SUCCESS(status))
        {
            DeviceExtension->disk_geometry.Cylinders =
                new_size.EndOfFile;

            // Fire refresh event
            if (RefreshEvent != NULL)
                KePulseEvent(RefreshEvent, 0, FALSE);
        }

        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = status;
        break;
    }

    default:
        Irp->IoStatus.Status = STATUS_DRIVER_INTERNAL_ERROR;
    }
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

        return;
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

            // Continue into IRP loop anyway. Above call has inserted a request
            // to terminate so everything will be safely freed anyway.
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

            return;
        }

        PIRP Irp = CONTAINING_RECORD(request, IRP, Tail.Overlay.ListEntry);

        switch (IoGetCurrentIrpStackLocation(Irp)->MajorFunction)
        {
        case IRP_MJ_FLUSH_BUFFERS:
            ImDiskDeviceThreadFlushBuffers(Irp, device_extension);
            break;

        case IRP_MJ_READ:
            ImDiskDeviceThreadRead(Irp, device_extension, device_object);
            break;

        case IRP_MJ_WRITE:
            ImDiskDeviceThreadWrite(Irp, device_extension, device_object);
            break;

        case IRP_MJ_DEVICE_CONTROL:
            ImDiskDeviceThreadDeviceControl(Irp, device_extension, device_object);
            break;

        default:
            Irp->IoStatus.Status = STATUS_DRIVER_INTERNAL_ERROR;
        }

        IoCompleteRequest(Irp,
            NT_SUCCESS(Irp->IoStatus.Status) ?
            IO_DISK_INCREMENT : IO_NO_INCREMENT);
    }
}

