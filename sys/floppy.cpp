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
