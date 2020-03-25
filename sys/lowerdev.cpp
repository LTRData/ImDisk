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

    IoCompleteRequest(item->OriginalIrp, IO_DISK_INCREMENT);

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

