/*
    ImDisk Virtual Disk Driver for Windows NT/2000/XP.
    This driver emulates harddisk partitions, floppy drives and CD/DVD-ROM
    drives from disk image files, in virtual memory or by redirecting I/O
    requests somewhere else, possibly to another machine, through a
    co-operating user-mode service, ImDskSvc.

    Copyright (C) 2005-2009 Olof Lagerkvist.

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
#include <ntintsafe.h>

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
#endif

#define POOL_TAG                'PIOC'

#define FILE_DEVICE_IMDISK      0x8372

#define AWEALLOC_DEVICE_NAME    L"\\Device\\AWEAlloc"
#define AWEALLOC_SYMLINK_NAME   L"\\DosDevices\\AWEAlloc"

typedef struct _OBJECT_CONTEXT
{
  LARGE_INTEGER Position;

  PMDL CompleteMdl;

  PMDL CurrentMdl;

  PUCHAR CompleteVA;

  ULONG_PTR CurrentPageBase;

  PUCHAR CurrentPtr;
} OBJECT_CONTEXT, *POBJECT_CONTEXT;

// Prototypes for functions defined in this driver

NTSTATUS
DriverEntry(IN PDRIVER_OBJECT DriverObject,
	    IN PUNICODE_STRING RegistryPath);

VOID
AWEAllocUnload(IN PDRIVER_OBJECT DriverObject);

NTSTATUS
AWEAllocCreate(IN PDEVICE_OBJECT DeviceObject,
	       IN PIRP Irp);

NTSTATUS
AWEAllocClose(IN PDEVICE_OBJECT DeviceObject,
	      IN PIRP Irp);

NTSTATUS
AWEAllocQueryInformation(IN PDEVICE_OBJECT DeviceObject,
			 IN PIRP Irp);

NTSTATUS
AWEAllocSetInformation(IN PDEVICE_OBJECT DeviceObject,
		       IN PIRP Irp);

NTSTATUS
AWEAllocReadWrite(IN PDEVICE_OBJECT DeviceObject,
		  IN PIRP Irp);

//
// Default page size 16 MB
//
const ULONG alloc_page_size = 16 << 20;
const PHYSICAL_ADDRESS physical_address_zero = { 0, 0 };
const PHYSICAL_ADDRESS physical_address_4GB = { 0, 1UL };
const PHYSICAL_ADDRESS physical_address_5GB = { 1UL << 30, 1UL };
const PHYSICAL_ADDRESS physical_address_6GB = { 1UL << 31, 1UL };
const PHYSICAL_ADDRESS physical_address_8GB = { 0, 2UL };
const PHYSICAL_ADDRESS physical_address_max32 = { ULONG_MAX, 0 };
const PHYSICAL_ADDRESS physical_address_max64 = { ULONG_MAX, ULONG_MAX };

//
// Macros for easier page/offset calculation
//
#define alloc_page_base_mask   (~((ULONG_PTR)alloc_page_size-1))
#define alloc_page_offset_mask ((ULONG_PTR)alloc_page_size-1)
#define AWEAllocGetPageBaseFromAbsOffset(OFFSET) \
  (OFFSET & alloc_page_base_mask)
#define AWEAllocGetPageOffsetFromAbsOffset(OFFSET) \
  (OFFSET & alloc_page_offset_mask)

#pragma code_seg("INIT")

//
// This is where it all starts...
//
NTSTATUS
DriverEntry(IN PDRIVER_OBJECT DriverObject,
	    IN PUNICODE_STRING RegistryPath)
{
  NTSTATUS status;
  PDEVICE_OBJECT device_object;
  UNICODE_STRING ctl_device_name;
  UNICODE_STRING sym_link;

  MmPageEntireDriver((PVOID)(ULONG_PTR)DriverEntry);

  // Create the control device.
  RtlInitUnicodeString(&ctl_device_name, AWEALLOC_DEVICE_NAME);

  status = IoCreateDevice(DriverObject,
			  0,
			  &ctl_device_name,
			  FILE_DEVICE_IMDISK,
			  0,
			  FALSE,
			  &device_object);

  if (!NT_SUCCESS(status))
    return status;

  device_object->Flags |= DO_DIRECT_IO;

  RtlInitUnicodeString(&sym_link, AWEALLOC_SYMLINK_NAME);
  IoCreateUnprotectedSymbolicLink(&sym_link, &ctl_device_name);

  DriverObject->MajorFunction[IRP_MJ_CREATE] = AWEAllocCreate;
  DriverObject->MajorFunction[IRP_MJ_CLOSE] = AWEAllocClose;
  DriverObject->MajorFunction[IRP_MJ_READ] = AWEAllocReadWrite;
  DriverObject->MajorFunction[IRP_MJ_WRITE] = AWEAllocReadWrite;
  DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] =
    AWEAllocQueryInformation;
  DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION] = AWEAllocSetInformation;

  DriverObject->DriverUnload = AWEAllocUnload;

  KdPrint(("AWEAlloc: Initialization done. Leaving DriverEntry().\n", status));

  return STATUS_SUCCESS;
}

#pragma code_seg()

NTSTATUS
AWEAllocMapPage(IN POBJECT_CONTEXT Context,
		IN ULONG_PTR Offset)
{
  ULONG_PTR page_base = AWEAllocGetPageBaseFromAbsOffset(Offset);
  ULONG size_to_map = alloc_page_size << 1;

  KdPrint2(("AWEAlloc: MapPage request Offset=%p BaseAddress=%p.\n",
	    Offset,
	    page_base));

  if ((Context->CurrentPageBase == page_base) &
      (Context->CurrentMdl != NULL))
    {
      KdPrint2(("AWEAlloc: MapPage: Page already mapped.\n"));
      return STATUS_SUCCESS;
    }

  try
    {
      Context->CurrentPtr = NULL;

      if (Context->CurrentMdl != NULL)
	IoFreeMdl(Context->CurrentMdl);

      Context->CurrentMdl = IoAllocateMdl(Context->CompleteVA,
					  size_to_map,
					  FALSE,
					  FALSE,
					  NULL);

      if (Context->CurrentMdl == NULL)
	{
	  KdPrint(("AWEAlloc: IoAllocateMdl() FAILED.\n"));
	  return STATUS_INSUFFICIENT_RESOURCES;
	}

      if (MmGetMdlByteCount(Context->CompleteMdl) - page_base < size_to_map)
	{
	  KdPrint2(("AWEAlloc: Remap close to end. Shrinking page size.\n"));
	  size_to_map = 0;
	}

      IoBuildPartialMdl(Context->CompleteMdl,
			Context->CurrentMdl,
			Context->CompleteVA + page_base,
			size_to_map);

      Context->CurrentPtr =
	MmGetSystemAddressForMdlSafe(Context->CurrentMdl, HighPagePriority);

      if (Context->CurrentPtr == NULL)
	{
	  KdPrint(("AWEAlloc: MmGetSystemAddressForMdlSafe() FAILED.\n"));

	  IoFreeMdl(Context->CurrentMdl);
	  Context->CurrentMdl = NULL;
	  return STATUS_INSUFFICIENT_RESOURCES;
	}

      Context->CurrentPageBase = page_base;

      KdPrint2(("AWEAlloc: MapPage success BaseAddress=%p.\n", page_base));

      return STATUS_SUCCESS;
    }
  except (EXCEPTION_EXECUTE_HANDLER)
    {
      return STATUS_DEVICE_BUSY;
    }
}

NTSTATUS
AWEAllocReadWrite(IN PDEVICE_OBJECT DeviceObject,
		  IN PIRP Irp)
{
  PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
  POBJECT_CONTEXT context = io_stack->FileObject->FsContext;
  ULONG_PTR page_offset = AWEAllocGetPageOffsetFromAbsOffset
    (io_stack->Parameters.Read.ByteOffset.LowPart);
  NTSTATUS status;
  PVOID system_buffer;

  KdPrint2(("AWEAlloc: Read/write request Offset=%p%p Len=%p Minor=%u.\n",
	    io_stack->Parameters.Read.ByteOffset.HighPart,
	    io_stack->Parameters.Read.ByteOffset.LowPart,
	    io_stack->Parameters.Read.Length,
	    io_stack->MinorFunction));

  if (io_stack->Parameters.Read.Length == 0)
    {
      Irp->IoStatus.Status = STATUS_SUCCESS;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_SUCCESS;
    }

  if (context == NULL)
    {
      Irp->IoStatus.Status = STATUS_END_OF_MEDIA;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_END_OF_MEDIA;
    }

  if (context->CompleteMdl == NULL)
    {
      Irp->IoStatus.Status = STATUS_END_OF_MEDIA;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_END_OF_MEDIA;
    }

  if ((io_stack->Parameters.Read.ByteOffset.QuadPart +
       io_stack->Parameters.Read.Length) >
      MmGetMdlByteCount(context->CompleteMdl))
    {
      Irp->IoStatus.Status = STATUS_END_OF_MEDIA;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_END_OF_MEDIA;
    }

  if (io_stack->Parameters.Read.Length > alloc_page_size)
    {
      KdPrint(("AWEAlloc: Unsupported request length: %u KB.\n",
	       io_stack->Parameters.Read.Length >> 10));

      Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_INVALID_PARAMETER;
    }

  status = AWEAllocMapPage(context,
			   io_stack->Parameters.Read.ByteOffset.LowPart);

  if (!NT_SUCCESS(status))
    {
      KdPrint(("AWEAlloc: Failed mapping current image page.\n"));

      Irp->IoStatus.Status = status;
      Irp->IoStatus.Information = 0;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return status;
    }

  KdPrint2(("AWEAlloc: Current image page mdl ptr=%p system ptr=%p.\n",
	    context->CurrentMdl,
	    context->CurrentPtr));

  system_buffer =
    MmGetSystemAddressForMdlSafe(Irp->MdlAddress, HighPagePriority);

  if (system_buffer == NULL)
    {
      KdPrint(("AWEAlloc: Failed mapping system buffer.\n"));

      Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      Irp->IoStatus.Information = 0;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_INSUFFICIENT_RESOURCES;
    }

  KdPrint2(("AWEAlloc: System buffer: %p\n", system_buffer));

  switch (io_stack->MajorFunction)
    {
    case IRP_MJ_READ:
      {
	KdPrint2(("AWEAlloc: Copying memory image -> I/O buffer.\n"));

	RtlCopyMemory(system_buffer,
		      context->CurrentPtr + page_offset,
		      io_stack->Parameters.Read.Length);

	break;
      }

    case IRP_MJ_WRITE:
      {
	KdPrint2(("AWEAlloc: Copying memory image <- I/O buffer.\n"));

	RtlCopyMemory(context->CurrentPtr + page_offset,
		      system_buffer,
		      io_stack->Parameters.Write.Length);

	break;
      }
    }

  KdPrint2(("AWEAlloc: Copy done.\n"));

  Irp->IoStatus.Status = STATUS_SUCCESS;
  Irp->IoStatus.Information = io_stack->Parameters.Read.Length;

  IoCompleteRequest(Irp, IO_NO_INCREMENT);

  return STATUS_SUCCESS;
}

#pragma code_seg("PAGE")

NTSTATUS
AWEAllocCreate(IN PDEVICE_OBJECT DeviceObject,
	       IN PIRP Irp)
{
  PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);

  KdPrint(("AWEAlloc: Create.\n"));

  PAGED_CODE();

  if (io_stack->FileObject->FileName.Length != 0)
    {
      Irp->IoStatus.Status = STATUS_OBJECT_NAME_NOT_FOUND;
      Irp->IoStatus.Information = 0;
      IoCompleteRequest(Irp, IO_NO_INCREMENT); 
      return STATUS_OBJECT_NAME_NOT_FOUND;
    }

  io_stack->FileObject->FsContext =
    ExAllocatePoolWithTag(NonPagedPool, sizeof(OBJECT_CONTEXT), POOL_TAG);

  if (io_stack->FileObject->FsContext == NULL)
    {
      Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      Irp->IoStatus.Information = 0;
      IoCompleteRequest(Irp, IO_NO_INCREMENT); 
      return STATUS_INSUFFICIENT_RESOURCES;
    }

  RtlZeroMemory(io_stack->FileObject->FsContext, sizeof(OBJECT_CONTEXT));

  MmResetDriverPaging((PVOID)(ULONG_PTR)DriverEntry);

  Irp->IoStatus.Status = STATUS_SUCCESS;
  Irp->IoStatus.Information = 0;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);

  return STATUS_SUCCESS;
}

NTSTATUS
AWEAllocClose(IN PDEVICE_OBJECT DeviceObject,
	      IN PIRP Irp)
{
  PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
  POBJECT_CONTEXT context = io_stack->FileObject->FsContext;

  KdPrint(("AWEAlloc: Close.\n"));

  PAGED_CODE();

  if (context != NULL)
    {
      if (context->CurrentMdl != NULL)
	IoFreeMdl(context->CurrentMdl);

      if (context->CompleteMdl != NULL)
	{
	  MmFreePagesFromMdl(context->CompleteMdl);
	  ExFreePool(context->CompleteMdl);
	}

      ExFreePoolWithTag(context, POOL_TAG);
    }

  Irp->IoStatus.Status = STATUS_SUCCESS;
  Irp->IoStatus.Information = 0;
  IoCompleteRequest(Irp, IO_NO_INCREMENT); 

  return STATUS_SUCCESS;
}

NTSTATUS
AWEAllocQueryInformation(IN PDEVICE_OBJECT DeviceObject,
			 IN PIRP Irp)
{
  PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
  POBJECT_CONTEXT context = io_stack->FileObject->FsContext;

  PAGED_CODE();

  KdPrint2(("AWEAlloc: QueryFileInformation: %u.\n",
	    io_stack->Parameters.QueryFile.FileInformationClass));

  RtlZeroMemory(Irp->AssociatedIrp.SystemBuffer,
		io_stack->Parameters.QueryFile.Length);

  switch (io_stack->Parameters.QueryFile.FileInformationClass)
    {
    case FileAlignmentInformation:
      {
	PFILE_ALIGNMENT_INFORMATION alignment_info =
	  (PFILE_ALIGNMENT_INFORMATION) Irp->AssociatedIrp.SystemBuffer;

	if (io_stack->Parameters.QueryFile.Length <
	    sizeof(FILE_ALIGNMENT_INFORMATION))
	  {
	    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_INVALID_PARAMETER;
	  }

	alignment_info->AlignmentRequirement = FILE_BYTE_ALIGNMENT;

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(FILE_ALIGNMENT_INFORMATION);
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
      }

    case FileAttributeTagInformation:
    case FileBasicInformation:
    case FileInternalInformation:
      Irp->IoStatus.Status = STATUS_SUCCESS;
      Irp->IoStatus.Information = io_stack->Parameters.QueryFile.Length;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_SUCCESS;

    case FileNetworkOpenInformation:
      {
	PFILE_NETWORK_OPEN_INFORMATION network_open_info =
	  (PFILE_NETWORK_OPEN_INFORMATION) Irp->AssociatedIrp.SystemBuffer;

	if (io_stack->Parameters.QueryFile.Length <
	    sizeof(FILE_NETWORK_OPEN_INFORMATION))
	  {
	    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_SUCCESS;
	  }

	if (context->CompleteMdl != NULL)
	  network_open_info->AllocationSize.QuadPart =
	    MmGetMdlByteCount(context->CompleteMdl);
	network_open_info->EndOfFile = network_open_info->AllocationSize;

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(FILE_NETWORK_OPEN_INFORMATION);
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
      }

    case FileStandardInformation:
      {
	PFILE_STANDARD_INFORMATION standard_info =
	  (PFILE_STANDARD_INFORMATION) Irp->AssociatedIrp.SystemBuffer;

	if (io_stack->Parameters.QueryFile.Length <
	    sizeof(FILE_STANDARD_INFORMATION))
	  {
	    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_INVALID_PARAMETER;
	  }

	if (context->CompleteMdl != NULL)
	  standard_info->AllocationSize.QuadPart =
	    MmGetMdlByteCount(context->CompleteMdl);
	standard_info->EndOfFile = standard_info->AllocationSize;
	standard_info->DeletePending = TRUE;

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(FILE_STANDARD_INFORMATION);
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
      }

    case FilePositionInformation:
      {
	PFILE_POSITION_INFORMATION position_info =
	  (PFILE_POSITION_INFORMATION) Irp->AssociatedIrp.SystemBuffer;

	if (io_stack->Parameters.QueryFile.Length <
	    sizeof(FILE_POSITION_INFORMATION))
	  {
	    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_INVALID_PARAMETER;
	  }

	position_info->CurrentByteOffset = context->Position;

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = sizeof(FILE_POSITION_INFORMATION);
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
      }

    default:
      KdPrint(("AWEAlloc: Unsupported QueryFile.FileInformationClass: %u\n",
	       io_stack->Parameters.QueryFile.FileInformationClass));

      Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
      Irp->IoStatus.Information = 0;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_INVALID_DEVICE_REQUEST;
    }
}

NTSTATUS
AWEAllocSetInformation(IN PDEVICE_OBJECT DeviceObject,
		       IN PIRP Irp)
{
  PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
  POBJECT_CONTEXT context = io_stack->FileObject->FsContext;

  PAGED_CODE();

  KdPrint2(("AWEAlloc: SetFileInformation: %u.\n",
	    io_stack->Parameters.SetFile.FileInformationClass));

  switch (io_stack->Parameters.SetFile.FileInformationClass)
    {
    case FileAllocationInformation:
    case FileEndOfFileInformation:
      {
	PFILE_END_OF_FILE_INFORMATION feof_info =
	  (PFILE_END_OF_FILE_INFORMATION) Irp->AssociatedIrp.SystemBuffer;

	if (io_stack->Parameters.SetFile.Length <
	    sizeof(FILE_END_OF_FILE_INFORMATION))
	  {
	    Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return Irp->IoStatus.Status;
	  }

	if (feof_info->EndOfFile.HighPart != 0)
	  {
	    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_INVALID_PARAMETER;
	  }

	KdPrint(("AWEAlloc: Setting size to %u KB.\n",
		 feof_info->EndOfFile.LowPart >> 10));

	if (feof_info->EndOfFile.LowPart == 0)
	  {
	    Irp->IoStatus.Status = STATUS_SUCCESS;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_SUCCESS;
	  }

	if (context->CompleteMdl != NULL)
	  {
	    ULONG current_size = MmGetMdlByteCount(context->CompleteMdl);

	    KdPrint(("AWEAlloc: Current size is: %u KB.\n",
		     current_size >> 10));

	    if (feof_info->EndOfFile.QuadPart <= current_size)
	      {
		KdPrint(("AWEAlloc: Current size is large enough.\n"));

		Irp->IoStatus.Status = STATUS_SUCCESS;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	      }

	    KdPrint(("AWEAlloc: Current size is too small.\n"));

	    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_INVALID_DEVICE_REQUEST;
	  }

	KdPrint(("AWEAlloc: Allocating %u KB.\n",
		 feof_info->EndOfFile.LowPart >> 10));

#ifdef _WIN64

	context->CompleteMdl =
	  MmAllocatePagesForMdl(physical_address_zero,
				physical_address_max64,
				physical_address_zero,
				feof_info->EndOfFile.LowPart);

#else // !_WIN64

	// In 32-bit, first try to allocate as high as possible
	KdPrint(("AWEAlloc: Allocating above 8 GB.\n"));

	context->CompleteMdl =
	  MmAllocatePagesForMdl(physical_address_8GB,
				physical_address_max64,
				physical_address_zero,
				feof_info->EndOfFile.LowPart);

	if (context->CompleteMdl != NULL)
	  if (MmGetMdlByteCount(context->CompleteMdl) <
	      feof_info->EndOfFile.LowPart)
	    {
	      KdPrint(("AWEAlloc: Not enough memory available above 8 GB.\n"));

	      MmFreePagesFromMdl(context->CompleteMdl);
	      ExFreePool(context->CompleteMdl);
	      context->CompleteMdl = NULL;
	    }

	if (context->CompleteMdl == NULL)
	  {
	    KdPrint(("AWEAlloc: Allocating above 6 GB.\n"));

	    context->CompleteMdl =
	      MmAllocatePagesForMdl(physical_address_6GB,
				    physical_address_max64,
				    physical_address_zero,
				    feof_info->EndOfFile.LowPart);
	  }

	if (context->CompleteMdl != NULL)
	  if (MmGetMdlByteCount(context->CompleteMdl) <
	      feof_info->EndOfFile.LowPart)
	    {
	      KdPrint(("AWEAlloc: Not enough memory available above 6 GB.\n"));

	      MmFreePagesFromMdl(context->CompleteMdl);
	      ExFreePool(context->CompleteMdl);
	      context->CompleteMdl = NULL;
	    }

	if (context->CompleteMdl == NULL)
	  {
	    KdPrint(("AWEAlloc: Allocating above 5 GB.\n"));

	    context->CompleteMdl =
	      MmAllocatePagesForMdl(physical_address_5GB,
				    physical_address_max64,
				    physical_address_zero,
				    feof_info->EndOfFile.LowPart);
	  }

	if (context->CompleteMdl != NULL)
	  if (MmGetMdlByteCount(context->CompleteMdl) <
	      feof_info->EndOfFile.LowPart)
	    {
	      KdPrint(("AWEAlloc: Not enough memory available above 5 GB.\n"));

	      MmFreePagesFromMdl(context->CompleteMdl);
	      ExFreePool(context->CompleteMdl);
	      context->CompleteMdl = NULL;
	    }

	if (context->CompleteMdl == NULL)
	  {
	    KdPrint(("AWEAlloc: Allocating above 4 GB.\n"));

	    context->CompleteMdl =
	      MmAllocatePagesForMdl(physical_address_4GB,
				    physical_address_max64,
				    physical_address_zero,
				    feof_info->EndOfFile.LowPart);
	  }

	if (context->CompleteMdl != NULL)
	  if (MmGetMdlByteCount(context->CompleteMdl) <
	      feof_info->EndOfFile.LowPart)
	    {
	      KdPrint(("AWEAlloc: Not enough memory available above 4 GB.\n"));

	      MmFreePagesFromMdl(context->CompleteMdl);
	      ExFreePool(context->CompleteMdl);
	      context->CompleteMdl = NULL;
	    }

	if (context->CompleteMdl == NULL)
	  {
	    KdPrint(("AWEAlloc: Allocating at any available location.\n"));

	    context->CompleteMdl =
	      MmAllocatePagesForMdl(physical_address_zero,
				    physical_address_max64,
				    physical_address_zero,
				    feof_info->EndOfFile.LowPart);
	  }

	if (context->CompleteMdl != NULL)
	  if (MmGetMdlByteCount(context->CompleteMdl) <
	      feof_info->EndOfFile.LowPart)
	    {
	      KdPrint(("AWEAlloc: Not enough memory available.\n"));

	      MmFreePagesFromMdl(context->CompleteMdl);
	      ExFreePool(context->CompleteMdl);
	      context->CompleteMdl = NULL;
	    }

#endif // _WIN64

	if (context->CompleteMdl == NULL)
	  {
	    KdPrint(("AWEAlloc: MmAllocatePagesForMdl failed.\n"));

	    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_INSUFFICIENT_RESOURCES;
	  }

	context->CompleteVA = MmGetMdlVirtualAddress(context->CompleteMdl);

	if (MmGetMdlByteCount(context->CompleteMdl) <
	    feof_info->EndOfFile.LowPart)
	  {
	    KdPrint(("AWEAlloc: Not enough memory available.\n"));

	    Irp->IoStatus.Status = STATUS_NO_MEMORY;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_NO_MEMORY;
	  }

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
      }

    case FilePositionInformation:
      {
	PFILE_POSITION_INFORMATION position_info =
	  (PFILE_POSITION_INFORMATION) Irp->AssociatedIrp.SystemBuffer;

	if (io_stack->Parameters.SetFile.Length <
	    sizeof(FILE_POSITION_INFORMATION))
	  {
	    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_INVALID_PARAMETER;
	  }

	context->Position = position_info->CurrentByteOffset;

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
      }

    case FileBasicInformation:
    case FileDispositionInformation:
    case FileValidDataLengthInformation:
      Irp->IoStatus.Status = STATUS_SUCCESS;
      Irp->IoStatus.Information = 0;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_SUCCESS;

    default:
      KdPrint(("AWEAlloc: Unsupported SetFile.FileInformationClass: %u\n",
	       io_stack->Parameters.SetFile.FileInformationClass));

      Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
      Irp->IoStatus.Information = 0;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_INVALID_DEVICE_REQUEST;
    }
}

VOID
AWEAllocUnload(IN PDRIVER_OBJECT DriverObject)
{
  PDEVICE_OBJECT device_object = DriverObject->DeviceObject;
  UNICODE_STRING sym_link;

  KdPrint(("AWEAlloc: Unload.\n"));

  PAGED_CODE();

  RtlInitUnicodeString(&sym_link, AWEALLOC_SYMLINK_NAME);
  IoDeleteSymbolicLink(&sym_link);

  while (device_object != NULL)
    {
      PDEVICE_OBJECT next_device = device_object->NextDevice;
      IoDeleteDevice(device_object);
      device_object = next_device;
    }
}
