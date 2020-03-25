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

