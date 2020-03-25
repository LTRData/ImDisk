///
/// Some additional native kernel-mode API functions we use
///

//
// Ensures that we build a pre Win 2000 compatible x86 sys file
// (without ExFreePoolWithTag()). // Olof Lagerkvist
//
#ifndef _WIN64
#ifdef ExFreePool
#undef ExFreePool
#endif
#ifdef ExFreePoolWithTag
#undef ExFreePoolWithTag
#endif
#define ExFreePoolWithTag(b, t) ExFreePool(b)
#endif

#pragma warning(disable: 4996)

//
// We include some stuff from newer DDK:s here so that one
// version of the driver for all versions of Windows can
// be compiled with the Windows 2000 DDK.
//
#if (VER_PRODUCTBUILD < 2600)

#define IOCTL_DISK_GET_PARTITION_INFO_EX    CTL_CODE(IOCTL_DISK_BASE, 0x0012, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_GET_LENGTH_INFO          CTL_CODE(IOCTL_DISK_BASE, 0x0017, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DISK_GET_PARTITION_INFO_EX    CTL_CODE(IOCTL_DISK_BASE, 0x0012, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_SET_PARTITION_INFO_EX    CTL_CODE(IOCTL_DISK_BASE, 0x0013, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

typedef enum _PARTITION_STYLE
  {
    PARTITION_STYLE_MBR,
    PARTITION_STYLE_GPT
  } PARTITION_STYLE;

typedef unsigned __int64 ULONG64, *PULONG64;

typedef struct _PARTITION_INFORMATION_MBR
{
  UCHAR PartitionType;
  BOOLEAN BootIndicator;
  BOOLEAN RecognizedPartition;
  ULONG HiddenSectors;
} PARTITION_INFORMATION_MBR, *PPARTITION_INFORMATION_MBR;

typedef struct _PARTITION_INFORMATION_GPT
{
  GUID PartitionType;
  GUID PartitionId;
  ULONG64 Attributes;
  WCHAR Name[36];
} PARTITION_INFORMATION_GPT, *PPARTITION_INFORMATION_GPT;

typedef struct _PARTITION_INFORMATION_EX
{
  PARTITION_STYLE PartitionStyle;
  LARGE_INTEGER StartingOffset;
  LARGE_INTEGER PartitionLength;
  ULONG PartitionNumber;
  BOOLEAN RewritePartition;
  union
  {
    PARTITION_INFORMATION_MBR Mbr;
    PARTITION_INFORMATION_GPT Gpt;
  };
} PARTITION_INFORMATION_EX, *PPARTITION_INFORMATION_EX;

typedef struct _GET_LENGTH_INFORMATION
{
  LARGE_INTEGER Length;
} GET_LENGTH_INFORMATION, *PGET_LENGTH_INFORMATION;

typedef SET_PARTITION_INFORMATION SET_PARTITION_INFORMATION_MBR;
typedef PARTITION_INFORMATION_GPT SET_PARTITION_INFORMATION_GPT;

typedef struct _SET_PARTITION_INFORMATION_EX {
    PARTITION_STYLE PartitionStyle;
    union {
        SET_PARTITION_INFORMATION_MBR Mbr;
        SET_PARTITION_INFORMATION_GPT Gpt;
    };
} SET_PARTITION_INFORMATION_EX, *PSET_PARTITION_INFORMATION_EX;

#endif // (VER_PRODUCTBUILD < 2600)

#if (VER_PRODUCTBUILD < 3790)

#define IOCTL_STORAGE_GET_HOTPLUG_INFO        CTL_CODE(IOCTL_STORAGE_BASE, 0x0305, METHOD_BUFFERED, FILE_ANY_ACCESS)

//
// IOCTL_STORAGE_GET_HOTPLUG_INFO
//

typedef struct _STORAGE_HOTPLUG_INFO {
    ULONG Size;
    BOOLEAN MediaRemovable;
    BOOLEAN MediaHotplug;
    BOOLEAN DeviceHotplug;
    BOOLEAN WriteCacheEnableOverride;
} STORAGE_HOTPLUG_INFO, *PSTORAGE_HOTPLUG_INFO;

#endif // (VER_PRODUCTBUILD < 3790)

NTSYSAPI
NTSTATUS
NTAPI
ZwPulseEvent(IN HANDLE EventHandle,
	     OUT PLONG PreviousState OPTIONAL);

//
// We include some stuff from ntifs.h here so that
// the driver can be compiled with only the Win2K DDK.
//
#ifndef _NTIFS_INCLUDED_

NTSYSAPI
NTSTATUS
NTAPI
ZwSetEvent(IN HANDLE EventHandle,
	   OUT PLONG PreviousState OPTIONAL);

NTSYSAPI
NTSTATUS
NTAPI
ZwWaitForSingleObject(IN HANDLE Handle,
		      IN BOOLEAN Alertable,
		      IN PLARGE_INTEGER Timeout OPTIONAL);

NTSYSAPI
NTSTATUS
NTAPI
ZwAllocateVirtualMemory(IN HANDLE               ProcessHandle,
			IN OUT PVOID            *BaseAddress,
			IN ULONG_PTR            ZeroBits,
			IN OUT PSIZE_T          RegionSize,
			IN ULONG                AllocationType,
			IN ULONG                Protect);

NTSYSAPI
NTSTATUS
NTAPI
ZwFreeVirtualMemory(IN HANDLE               ProcessHandle,
		    IN PVOID                *BaseAddress,
		    IN OUT PSIZE_T          RegionSize,
		    IN ULONG                FreeType);

NTSYSAPI
NTSTATUS
NTAPI
ZwFsControlFile(
    __in HANDLE FileHandle,
    __in_opt HANDLE Event,
    __in_opt PIO_APC_ROUTINE ApcRoutine,
    __in_opt PVOID ApcContext,
    __out PIO_STATUS_BLOCK IoStatusBlock,
    __in ULONG FsControlCode,
    __in_bcount_opt(InputBufferLength) PVOID InputBuffer,
    __in ULONG InputBufferLength,
    __out_bcount_opt(OutputBufferLength) PVOID OutputBuffer,
    __in ULONG OutputBufferLength
    );

#define FSCTL_SET_SPARSE                CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 49, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

typedef enum _TOKEN_TYPE {
  TokenPrimary = 1,
  TokenImpersonation
} TOKEN_TYPE;
typedef TOKEN_TYPE *PTOKEN_TYPE;

#define TOKEN_SOURCE_LENGTH 8

typedef struct _TOKEN_SOURCE {
  CHAR SourceName[TOKEN_SOURCE_LENGTH];
  LUID SourceIdentifier;
} TOKEN_SOURCE, *PTOKEN_SOURCE;

typedef struct _TOKEN_CONTROL
{
  LUID TokenId;
  LUID AuthenticationId;
  LUID ModifiedId;
  TOKEN_SOURCE TokenSource;
} TOKEN_CONTROL, *PTOKEN_CONTROL;

typedef struct _SECURITY_CLIENT_CONTEXT
{
  SECURITY_QUALITY_OF_SERVICE SecurityQos;
  PACCESS_TOKEN ClientToken;
  BOOLEAN DirectlyAccessClientToken;
  BOOLEAN DirectAccessEffectiveOnly;
  BOOLEAN ServerIsRemote;
  TOKEN_CONTROL ClientTokenControl;
} SECURITY_CLIENT_CONTEXT, *PSECURITY_CLIENT_CONTEXT;

NTKERNELAPI
NTSTATUS
SeCreateClientSecurity(IN PETHREAD Thread,
		       IN PSECURITY_QUALITY_OF_SERVICE QualityOfService,
		       IN BOOLEAN RemoteClient,
		       OUT PSECURITY_CLIENT_CONTEXT ClientContext);

NTKERNELAPI
VOID
SeImpersonateClient(IN PSECURITY_CLIENT_CONTEXT ClientContext,
		    IN PETHREAD ServerThread OPTIONAL);

NTKERNELAPI
TOKEN_TYPE
SeTokenType(IN PACCESS_TOKEN Token);

// PsRevertToSelf() removed for Windows NT 3.51 compatibility, Olof Lagerkvist.

#define SeDeleteClientSecurity(C)  				\
  ((SeTokenType((C)->ClientToken) == TokenPrimary) ?		\
   (PsDereferencePrimaryToken( (C)->ClientToken )) :		\
   (PsDereferenceImpersonationToken( (C)->ClientToken )))

#endif // _NTIFS_INCLUDED_

#define PsDereferenceImpersonationToken(T)	\
  ((ARGUMENT_PRESENT((T))) ?			\
   (ObDereferenceObject((T))) : 0)

#define PsDereferencePrimaryToken(T) (ObDereferenceObject((T)))

//
// For backward compatibility with <= Windows NT 4.0 by Bruce Engle.
//
#ifndef _WIN64
#ifdef MmGetSystemAddressForMdlSafe
#undef MmGetSystemAddressForMdlSafe
#endif
#define MmGetSystemAddressForMdlSafe(MDL, PRIORITY) \
  MmGetSystemAddressForMdlPrettySafe(MDL)

__forceinline PVOID
MmGetSystemAddressForMdlPrettySafe(PMDL Mdl)
{
  CSHORT MdlMappingCanFail;
  PVOID MappedSystemVa;

  if (Mdl == NULL)
    return NULL;

  if (Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL))
    return Mdl->MappedSystemVa;

  MdlMappingCanFail = Mdl->MdlFlags & MDL_MAPPING_CAN_FAIL;

  Mdl->MdlFlags |= MDL_MAPPING_CAN_FAIL;

  MappedSystemVa = MmMapLockedPages(Mdl, KernelMode);

  if (MdlMappingCanFail == 0)
    Mdl->MdlFlags &= ~MDL_MAPPING_CAN_FAIL;

  return MappedSystemVa;
}
#endif
