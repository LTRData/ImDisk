/*
    AWE Allocation Driver for Windows 2000/XP and later.

    Copyright (C) 2005-2011 Olof Lagerkvist.

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

// FILE_OBJECT context for "files" handled by this driver

typedef struct _BLOCK_DESCRIPTOR
{
  LONGLONG Offset;

  PMDL Mdl;

  struct _BLOCK_DESCRIPTOR *NextBlock;

} BLOCK_DESCRIPTOR, *PBLOCK_DESCRIPTOR;

typedef struct _OBJECT_CONTEXT
{
  LONGLONG FilePosition;

  LONGLONG TotalSize;

  PBLOCK_DESCRIPTOR FirstBlock;

  PMDL CurrentMdl;

  LONGLONG CurrentPageBase;

  PUCHAR CurrentPtr;

} OBJECT_CONTEXT, *POBJECT_CONTEXT;

//
// Default page size 2 MB
//
#define ALLOC_PAGE_SIZE (2ULL << 20)

//
// Macros for easier page/offset calculation
//
#define ALLOC_PAGE_BASE_MASK   (~(ALLOC_PAGE_SIZE-1))
#define AWEAllocGetPageBaseFromAbsOffset(absolute_offset) \
  ((absolute_offset) & ALLOC_PAGE_BASE_MASK)
#define ALLOC_PAGE_OFFSET_MASK (ALLOC_PAGE_SIZE-1)
#define AWEAllocGetPageOffsetFromAbsOffset(absolute_offset) \
  ((ULONG)((absolute_offset) & ALLOC_PAGE_OFFSET_MASK))
#define AWEAllocGetRequiredPagesForSize(size) \
  (((size) + ALLOC_PAGE_OFFSET_MASK) & ALLOC_PAGE_BASE_MASK)
#define MAX_BLOCK_SIZE (ULONG_MAX - ALLOC_PAGE_OFFSET_MASK)

//
// Prototypes for functions defined in this driver
//

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

VOID
AWEAllocLogError(IN PVOID Object,
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

const PHYSICAL_ADDRESS physical_address_zero = { 0, 0 };
const PHYSICAL_ADDRESS physical_address_4GB = { 0, 1UL };
const PHYSICAL_ADDRESS physical_address_5GB = { 1UL << 30, 1UL };
const PHYSICAL_ADDRESS physical_address_6GB = { 1UL << 31, 1UL };
const PHYSICAL_ADDRESS physical_address_8GB = { 0, 2UL };
const PHYSICAL_ADDRESS physical_address_max32 = { ULONG_MAX, 0 };
const PHYSICAL_ADDRESS physical_address_max64 = { ULONG_MAX, ULONG_MAX };

PDRIVER_OBJECT AWEAllocDriverObject;

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

  AWEAllocDriverObject = DriverObject;

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
		IN LONGLONG Offset)
{
  LONGLONG page_base = AWEAllocGetPageBaseFromAbsOffset(Offset);
  LONGLONG page_base_within_block;
  ULONG size_to_map = ALLOC_PAGE_SIZE;
  PBLOCK_DESCRIPTOR block;

  KdPrint2(("AWEAlloc: MapPage request Offset=%p%p BaseAddress=%p%p.\n",
	    Offset,
	    page_base));

  if ((Context->CurrentPageBase == page_base) &
      (Context->CurrentMdl != NULL) &
      (Context->CurrentPtr != NULL))
    {
      KdPrint2(("AWEAlloc: MapPage: Page already mapped.\n"));
      return STATUS_SUCCESS;
    }

  // Find block that contains this page
  for (block = Context->FirstBlock;
       block != NULL;
       block = block->NextBlock)
    if (block->Offset <= page_base)
      break;

  if (block == NULL)
    {
      KdPrint2(("AWEAlloc: MapPage: Cannot find block for page.\n"));
      return STATUS_DRIVER_INTERNAL_ERROR;
    }

  page_base_within_block = page_base - block->Offset;

  KdPrint2(("AWEAlloc: MapPage found block Offset=%p%p BaseAddress=%p%p.\n",
	    block->Offset,
	    page_base_within_block));

  Context->CurrentPtr = NULL;
  Context->CurrentPageBase = (ULONG_PTR)-1;

  try
    {
      if (Context->CurrentMdl != NULL)
	IoFreeMdl(Context->CurrentMdl);

      Context->CurrentMdl = NULL;

      Context->CurrentMdl =
	IoAllocateMdl(MmGetMdlVirtualAddress(block->Mdl),
		      size_to_map,
		      FALSE,
		      FALSE,
		      NULL);

      if (Context->CurrentMdl == NULL)
	{
	  KdPrint(("AWEAlloc: IoAllocateMdl() FAILED.\n"));
	  return STATUS_INSUFFICIENT_RESOURCES;
	}

      if ((MmGetMdlByteCount(block->Mdl) - page_base_within_block) <
	  size_to_map)
	{
	  KdPrint(("AWEAlloc: Incomplete page size! Shrinking page size.\n"));
	  size_to_map = 0;  // This will map remaining bytes
	}

      IoBuildPartialMdl(block->Mdl,
			Context->CurrentMdl,
			(PUCHAR) MmGetMdlVirtualAddress(block->Mdl) +
			page_base_within_block,
			size_to_map);

      Context->CurrentPtr =
	MmGetSystemAddressForMdlSafe(Context->CurrentMdl, HighPagePriority);

      if (Context->CurrentPtr == NULL)
	{
	  KdPrint(("AWEAlloc: MmGetSystemAddressForMdlSafe() FAILED.\n"));

	  AWEAllocLogError(AWEAllocDriverObject,
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
			   L"MmGetSystemAddressForMdlSafe() failed during "
			   L"page mapping.");

	  IoFreeMdl(Context->CurrentMdl);
	  Context->CurrentMdl = NULL;
	  return STATUS_INSUFFICIENT_RESOURCES;
	}

      Context->CurrentPageBase = page_base;

      KdPrint2(("AWEAlloc: MapPage success BaseAddress=%p%p.\n", page_base));

      return STATUS_SUCCESS;
    }
  except (EXCEPTION_EXECUTE_HANDLER)
    {
      AWEAllocLogError(AWEAllocDriverObject,
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
		       L"Exception occured during page mapping.");

      return STATUS_DEVICE_BUSY;
    }
}

NTSTATUS
AWEAllocReadWrite(IN PDEVICE_OBJECT DeviceObject,
		  IN PIRP Irp)
{
  PIO_STACK_LOCATION io_stack = IoGetCurrentIrpStackLocation(Irp);
  POBJECT_CONTEXT context = io_stack->FileObject->FsContext;
  ULONG length_done = 0;
  NTSTATUS status;
  PUCHAR system_buffer;

  KdPrint2(("AWEAlloc: Read/write request Offset=%p%p Len=%p Minor=%u.\n",
	    io_stack->Parameters.Read.ByteOffset.HighPart,
	    io_stack->Parameters.Read.ByteOffset.LowPart,
	    io_stack->Parameters.Read.Length,
	    io_stack->MinorFunction));

  if (context == NULL)
    {
      Irp->IoStatus.Status = STATUS_END_OF_MEDIA;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_END_OF_MEDIA;
    }

  if (context->FirstBlock == NULL)
    {
      Irp->IoStatus.Status = STATUS_END_OF_MEDIA;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_END_OF_MEDIA;
    }

  if ((io_stack->Parameters.Read.ByteOffset.QuadPart +
       io_stack->Parameters.Read.Length) >
      context->TotalSize)
    {
      Irp->IoStatus.Status = STATUS_END_OF_MEDIA;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_END_OF_MEDIA;
    }

  if (io_stack->Parameters.Read.Length == 0)
    {
      KdPrint2(("AWEAlloc: Zero bytes read/write request.\n"));

      Irp->IoStatus.Status = STATUS_SUCCESS;
      Irp->IoStatus.Information = 0;

      IoCompleteRequest(Irp, IO_NO_INCREMENT);

      return STATUS_SUCCESS;
    }

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

  for (;;)
    {
      LONGLONG abs_offset_this_iter =
	io_stack->Parameters.Read.ByteOffset.QuadPart + length_done;
      ULONG page_offset_this_iter =
	AWEAllocGetPageOffsetFromAbsOffset(abs_offset_this_iter);
      ULONG bytes_this_iter = io_stack->Parameters.Read.Length - length_done;

      if (length_done >= io_stack->Parameters.Read.Length)
	{
	  KdPrint2(("AWEAlloc: Nothing left to do.\n"));

	  Irp->IoStatus.Status = STATUS_SUCCESS;
	  Irp->IoStatus.Information = length_done;

	  IoCompleteRequest(Irp, IO_NO_INCREMENT);

	  return STATUS_SUCCESS;
	}

      if ((page_offset_this_iter + bytes_this_iter) > ALLOC_PAGE_SIZE)
	bytes_this_iter = ALLOC_PAGE_SIZE - page_offset_this_iter;

      /*
      if (io_stack->Parameters.Read.Length > ALLOC_PAGE_SIZE)
	{
	  KdPrint(("AWEAlloc: Unsupported request length: %u KB.\n",
		   io_stack->Parameters.Read.Length >> 10));

	  Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	  Irp->IoStatus.Information = 0;

	  IoCompleteRequest(Irp, IO_NO_INCREMENT);

	  return STATUS_INVALID_PARAMETER;
	}
      */

      status = AWEAllocMapPage(context, abs_offset_this_iter);

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

      switch (io_stack->MajorFunction)
	{
	case IRP_MJ_READ:
	  {
	    KdPrint2(("AWEAlloc: Copying memory image -> I/O buffer.\n"));

	    RtlCopyMemory(system_buffer + length_done,
			  context->CurrentPtr + page_offset_this_iter,
			  bytes_this_iter);

	    break;
	  }

	case IRP_MJ_WRITE:
	  {
	    KdPrint2(("AWEAlloc: Copying memory image <- I/O buffer.\n"));

	    RtlCopyMemory(context->CurrentPtr + page_offset_this_iter,
			  system_buffer + length_done,
			  bytes_this_iter);

	    break;
	  }
	}

      KdPrint2(("AWEAlloc: Copy done.\n"));

      length_done += bytes_this_iter;

    }

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
      PBLOCK_DESCRIPTOR block = context->FirstBlock;

      KdPrint2(("AWEAlloc: Freeing context data. First block=%p\n", block));

      if (context->CurrentMdl != NULL)
	IoFreeMdl(context->CurrentMdl);

      while (block != NULL)
	{
	  PBLOCK_DESCRIPTOR next_block = block->NextBlock;

	  KdPrint2(("AWEAlloc: Freeing block=%p mdl=%p.\n",
		    block,
		    block->Mdl));

	  MmFreePagesFromMdl(block->Mdl);
	  ExFreePool(block->Mdl);
	  ExFreePoolWithTag(block, POOL_TAG);

	  block = next_block;
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

	network_open_info->AllocationSize.QuadPart = context->TotalSize;
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

	standard_info->AllocationSize.QuadPart = context->TotalSize;
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

	position_info->CurrentByteOffset.QuadPart = context->FilePosition;

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

BOOLEAN
AWEAllocAddBlock(IN POBJECT_CONTEXT Context,
		 IN PBLOCK_DESCRIPTOR Block)
{
  ULONG block_size;

  if (Block->Mdl == NULL)
    return FALSE;

  block_size = AWEAllocGetPageBaseFromAbsOffset(MmGetMdlByteCount(Block->Mdl));
  if (block_size == 0)
    {
      KdPrint(("AWEAlloc: Got %u bytes which is too small for page size.\n",
	       MmGetMdlByteCount(Block->Mdl)));

      MmFreePagesFromMdl(Block->Mdl);
      ExFreePool(Block->Mdl);
      Block->Mdl = NULL;
      return FALSE;
    }

  Block->Offset = Context->TotalSize;
  Block->NextBlock = Context->FirstBlock;
  Context->FirstBlock = Block;
  Context->TotalSize += block_size;
  return TRUE;
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

#ifndef _WIN64

	/*
	if (feof_info->EndOfFile.HighPart != 0)
	  {
	    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_INVALID_PARAMETER;
	  }
	*/

#endif // ! _WIN64

	KdPrint(("AWEAlloc: Setting size to %u MB.\n",
		 (ULONG) (feof_info->EndOfFile.QuadPart >> 20)));

	if (feof_info->EndOfFile.QuadPart == 0)
	  {
	    Irp->IoStatus.Status = STATUS_SUCCESS;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_SUCCESS;
	  }

	/*
	if (context->FirstBlock != NULL)
	  {
	    KdPrint(("AWEAlloc: Current size is: %u MB.\n",
		     (ULONG) (context->TotalSize >> 20)));

	    if (feof_info->EndOfFile.QuadPart <= context->TotalSize)
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
	*/

	for (;;)
	  {
	    PBLOCK_DESCRIPTOR block;
	    SIZE_T bytes_to_allocate;

	    if (context->TotalSize >= feof_info->EndOfFile.QuadPart)
	      {
		Irp->IoStatus.Status = STATUS_SUCCESS;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	      }

	    block = ExAllocatePoolWithTag(NonPagedPool,
					  sizeof(BLOCK_DESCRIPTOR), POOL_TAG);
	    RtlZeroMemory(block, sizeof(BLOCK_DESCRIPTOR));

	    if ((feof_info->EndOfFile.QuadPart - context->TotalSize) >
		MAX_BLOCK_SIZE)
	      bytes_to_allocate = MAX_BLOCK_SIZE;
	    else
	      bytes_to_allocate = (SIZE_T)
		AWEAllocGetRequiredPagesForSize(feof_info->EndOfFile.QuadPart -
						context->TotalSize);

	    KdPrint(("AWEAlloc: Allocating %u MB.\n",
		     (ULONG) (bytes_to_allocate >> 20)));

#ifndef _WIN64

	    // On 32-bit, first try to allocate as high as possible
	    KdPrint(("AWEAlloc: Allocating above 8 GB.\n"));

	    block->Mdl = MmAllocatePagesForMdl(physical_address_8GB,
					       physical_address_max64,
					       physical_address_zero,
					       bytes_to_allocate);

	    if (AWEAllocAddBlock(context, block))
	      continue;

	    KdPrint(("AWEAlloc: Not enough memory available above 8 GB.\n"
		     "AWEAlloc: Allocating above 6 GB.\n"));

	    AWEAllocLogError(AWEAllocDriverObject,
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
			     L"Error allocating above 8 GB.");

	    block->Mdl = MmAllocatePagesForMdl(physical_address_6GB,
					       physical_address_max64,
					       physical_address_zero,
					       bytes_to_allocate);

	    if (AWEAllocAddBlock(context, block))
	      continue;

	    KdPrint(("AWEAlloc: Not enough memory available above 6 GB.\n"
		     "AWEAlloc: Allocating above 5 GB.\n"));

	    AWEAllocLogError(AWEAllocDriverObject,
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
			     L"Error allocating above 6 GB.");

	    block->Mdl = MmAllocatePagesForMdl(physical_address_5GB,
					       physical_address_max64,
					       physical_address_zero,
					       bytes_to_allocate);

	    if (AWEAllocAddBlock(context, block))
	      continue;

	    KdPrint(("AWEAlloc: Not enough memory available above 5 GB.\n"
		     "AWEAlloc: Allocating above 4 GB.\n"));

	    AWEAllocLogError(AWEAllocDriverObject,
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
			     L"Error allocating above 5 GB.");

	    block->Mdl = MmAllocatePagesForMdl(physical_address_4GB,
					       physical_address_max64,
					       physical_address_zero,
					       bytes_to_allocate);

	    if (AWEAllocAddBlock(context, block))
	      continue;

	    KdPrint(("AWEAlloc: Not enough memory available above 4 GB.\n"
		     "AWEAlloc: Allocating at any available location.\n"));

	    AWEAllocLogError(AWEAllocDriverObject,
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
			     L"Error allocating above 4 GB.");

#endif // !_WIN64

	    block->Mdl = MmAllocatePagesForMdl(physical_address_zero,
					       physical_address_max64,
					       physical_address_zero,
					       bytes_to_allocate);

	    if (AWEAllocAddBlock(context, block))
	      continue;

	    KdPrint(("AWEAlloc: MmAllocatePagesForMdl failed.\n"));

	    AWEAllocLogError(AWEAllocDriverObject,
			     0,
			     0,
			     NULL,
			     0,
			     1000,
			     STATUS_NO_MEMORY,
			     101,
			     STATUS_NO_MEMORY,
			     0,
			     0,
			     NULL,
			     L"Error allocating physical memory.");

	    ExFreePoolWithTag(block, POOL_TAG);

	    Irp->IoStatus.Status = STATUS_NO_MEMORY;
	    Irp->IoStatus.Information = 0;
	    IoCompleteRequest(Irp, IO_NO_INCREMENT);
	    return STATUS_NO_MEMORY;
	  }
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

	context->FilePosition = position_info->CurrentByteOffset.QuadPart;

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

VOID
AWEAllocLogError(IN PVOID Object,
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
    (PIO_ERROR_LOG_PACKET) IoAllocateErrorLogEntry(Object,
						   (UCHAR) packet_size);

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
