#Disable Warning CA1707 ' Identifiers should not contain underscores
#Disable Warning CA1401 ' P/Invokes should not be visible
#Disable Warning CA2101

Imports System.ComponentModel
Imports System.IO
Imports System.Runtime.InteropServices
Imports Microsoft.Win32.SafeHandles
''' <summary>
''' Provides wrappers for Win32 file API. This makes it possible to open everything that
''' CreateFile() can open and get a FileStream based .NET wrapper around the file handle.
''' </summary>
Public NotInheritable Class NativeFileIO

#Region "Win32 API"
    Public NotInheritable Class UnsafeNativeMethods

        Private Sub New()
        End Sub

        Public Const GENERIC_READ As UInt32 = &H80000000UI
        Public Const GENERIC_WRITE As UInt32 = &H40000000UI
        Public Const GENERIC_ALL As UInt32 = &H10000000

        Public Const FILE_SHARE_READ As UInt32 = &H1UI
        Public Const FILE_SHARE_WRITE As UInt32 = &H2UI
        Public Const FILE_SHARE_DELETE As UInt32 = &H4UI
        Public Const FILE_READ_ATTRIBUTES As UInt32 = &H80UI
        Public Const FILE_ATTRIBUTE_NORMAL As UInt32 = &H80UI
        Public Const FILE_FLAG_OVERLAPPED As UInt32 = &H40000000UI
        Public Const FILE_FLAG_BACKUP_SEMANTICS As UInt32 = &H2000000UI
        Public Const OPEN_ALWAYS As UInt32 = 4UI
        Public Const OPEN_EXISTING As UInt32 = 3UI
        Public Const CREATE_ALWAYS As UInt32 = 2UI
        Public Const CREATE_NEW As UInt32 = 1UI
        Public Const TRUNCATE_EXISTING As UInt32 = 5UI

        Public Const ERROR_FILE_NOT_FOUND As UInt32 = 2UI
        Public Const ERROR_PATH_NOT_FOUND As UInt32 = 3UI
        Public Const ERROR_ACCESS_DENIED As UInt32 = 5UI
        Public Const ERROR_DEVICE_REMOVED As UInt32 = 1617UI
        Public Const ERROR_DEV_NOT_EXIST As UInt32 = 55UI

        Public Const FSCTL_GET_COMPRESSION As UInt32 = &H9003C
        Public Const FSCTL_SET_COMPRESSION As UInt32 = &H9C040
        Public Const COMPRESSION_FORMAT_NONE As UInt16 = 0US
        Public Const COMPRESSION_FORMAT_DEFAULT As UInt16 = 1US

        Public Const FSCTL_ALLOW_EXTENDED_DASD_IO As UInt32 = &H90083

        Public Const FSCTL_LOCK_VOLUME As UInt32 = &H90018
        Public Const FSCTL_DISMOUNT_VOLUME As UInt32 = &H90020

        Public Const IOCTL_DISK_GET_DRIVE_GEOMETRY As UInt32 = &H70000
        Public Const IOCTL_DISK_GET_LENGTH_INFO As UInt32 = &H7405C
        Public Const IOCTL_DISK_GROW_PARTITION As UInt32 = &H7C0D0

        Public Const SC_MANAGER_CREATE_SERVICE As UInt32 = &H2
        Public Const SC_MANAGER_ALL_ACCESS As UInt32 = &HF003F
        Public Const SERVICE_KERNEL_DRIVER As UInt32 = &H1
        Public Const SERVICE_FILE_SYSTEM_DRIVER As UInt32 = &H2
        Public Const SERVICE_WIN32_OWN_PROCESS As UInt32 = &H10 'Service that runs in its own process. 
        Public Const SERVICE_WIN32_SHARE_PROCESS As UInt32 = &H20

        Public Const SERVICE_DEMAND_START As UInt32 = &H3
        Public Const SERVICE_ERROR_IGNORE As UInt32 = &H0
        Public Const SERVICE_CONTROL_STOP As UInt32 = &H1
        Public Const ERROR_SERVICE_DOES_NOT_EXIST As UInt32 = 1060
        Public Const ERROR_SERVICE_ALREADY_RUNNING As UInt32 = 1056

        Public Declare Function DeviceIoControl Lib "kernel32" (
          hDevice As SafeFileHandle,
          dwIoControlCode As UInt32,
          lpInBuffer As IntPtr,
          nInBufferSize As UInt32,
          <Out> ByRef lpOutBuffer As DISK_GEOMETRY,
          nOutBufferSize As UInt32,
          ByRef lpBytesReturned As UInt32,
          lpOverlapped As IntPtr) As Boolean

        Public Declare Function DeviceIoControl Lib "kernel32" (
          hDevice As SafeFileHandle,
          dwIoControlCode As UInt32,
          lpInBuffer As IntPtr,
          nInBufferSize As UInt32,
          <Out> ByRef lpOutBuffer As Int64,
          nOutBufferSize As UInt32,
          ByRef lpBytesReturned As UInt32,
          lpOverlapped As IntPtr) As Boolean

        Public Declare Function DeviceIoControl Lib "kernel32" (
          hDevice As SafeFileHandle,
          dwIoControlCode As UInt32,
          <[In]> ByRef lpInBuffer As DISK_GROW_PARTITION,
          nInBufferSize As UInt32,
          lpOutBuffer As IntPtr,
          nOutBufferSize As UInt32,
          ByRef lpBytesReturned As UInt32,
          lpOverlapped As IntPtr) As Boolean

        <StructLayout(LayoutKind.Sequential)>
        Structure DISK_GEOMETRY
            Public Enum MEDIA_TYPE As Int32
                Unknown = &H0
                F5_1Pt2_512 = &H1
                F3_1Pt44_512 = &H2
                F3_2Pt88_512 = &H3
                F3_20Pt8_512 = &H4
                F3_720_512 = &H5
                F5_360_512 = &H6
                F5_320_512 = &H7
                F5_320_1024 = &H8
                F5_180_512 = &H9
                F5_160_512 = &HA
                RemovableMedia = &HB
                FixedMedia = &HC
                F3_120M_512 = &HD
                F3_640_512 = &HE
                F5_640_512 = &HF
                F5_720_512 = &H10
                F3_1Pt2_512 = &H11
                F3_1Pt23_1024 = &H12
                F5_1Pt23_1024 = &H13
                F3_128Mb_512 = &H14
                F3_230Mb_512 = &H15
                F8_256_128 = &H16
                F3_200Mb_512 = &H17
                F3_240M_512 = &H18
                F3_32M_512 = &H19
            End Enum

            Public Property Cylinders As Int64
            Public Property MediaType As MEDIA_TYPE
            Public Property TracksPerCylinder As UInt32
            Public Property SectorsPerTrack As UInt32
            Public Property BytesPerSector As UInt32
        End Structure

        <StructLayout(LayoutKind.Sequential, Pack:=8)>
        Public Structure PARTITION_INFORMATION
            Public Enum PARTITION_TYPE As Byte
                PARTITION_ENTRY_UNUSED = &H0      ' Entry unused
                PARTITION_FAT_12 = &H1      ' 12-bit FAT entries
                PARTITION_XENIX_1 = &H2      ' Xenix
                PARTITION_XENIX_2 = &H3      ' Xenix
                PARTITION_FAT_16 = &H4      ' 16-bit FAT entries
                PARTITION_EXTENDED = &H5      ' Extended partition entry
                PARTITION_HUGE = &H6      ' Huge partition MS-DOS V4
                PARTITION_IFS = &H7      ' IFS Partition
                PARTITION_OS2BOOTMGR = &HA      ' OS/2 Boot Manager/OPUS/Coherent swap
                PARTITION_FAT32 = &HB      ' FAT32
                PARTITION_FAT32_XINT13 = &HC      ' FAT32 using extended int13 services
                PARTITION_XINT13 = &HE      ' Win95 partition using extended int13 services
                PARTITION_XINT13_EXTENDED = &HF      ' Same as type 5 but uses extended int13 services
                PARTITION_PREP = &H41      ' PowerPC Reference Platform (PReP) Boot Partition
                PARTITION_LDM = &H42      ' Logical Disk Manager partition
                PARTITION_UNIX = &H63      ' Unix
                PARTITION_NTFT = &H80      ' NTFT partition      
            End Enum

            Public Property StartingOffset As Int64
            Public Property PartitionLength As Int64
            Public Property HiddenSectors As UInt32
            Public Property PartitionNumber As UInt32
            Public Property PartitionType As PARTITION_TYPE
            Public Property BootIndicator As Byte
            Public Property RecognizedPartition As Byte
            Public Property RewritePartition As Byte

            ''' <summary>
            ''' Indicates whether this partition entry represents a Windows NT fault tolerant partition,
            ''' such as mirror or stripe set.
            ''' </summary>
            ''' <value>
            ''' Indicates whether this partition entry represents a Windows NT fault tolerant partition,
            ''' such as mirror or stripe set.
            ''' </value>
            ''' <returns>True if this partition entry represents a Windows NT fault tolerant partition,
            ''' such as mirror or stripe set. False otherwise.</returns>
            Public ReadOnly Property IsFTPartition As Boolean
                Get
                    Return (PartitionType And PARTITION_TYPE.PARTITION_NTFT) = PARTITION_TYPE.PARTITION_NTFT
                End Get
            End Property

            ''' <summary>
            ''' If this partition entry represents a Windows NT fault tolerant partition, such as mirror or stripe,
            ''' set, then this property returns partition subtype, such as PARTITION_IFS for NTFS or HPFS
            ''' partitions.
            ''' </summary>
            ''' <value>
            ''' If this partition entry represents a Windows NT fault tolerant partition, such as mirror or stripe,
            ''' set, then this property returns partition subtype, such as PARTITION_IFS for NTFS or HPFS
            ''' partitions.
            ''' </value>
            ''' <returns>If this partition entry represents a Windows NT fault tolerant partition, such as mirror or
            ''' stripe, set, then this property returns partition subtype, such as PARTITION_IFS for NTFS or HPFS
            ''' partitions.</returns>
            Public ReadOnly Property FTPartitionSubType As PARTITION_TYPE
                Get
                    Return PartitionType And Not PARTITION_TYPE.PARTITION_NTFT
                End Get
            End Property

            ''' <summary>
            ''' Indicates whether this partition entry represents a container partition, also known as extended
            ''' partition, where an extended partition table can be found in first sector.
            ''' </summary>
            ''' <value>
            ''' Indicates whether this partition entry represents a container partition.
            ''' </value>
            ''' <returns>True if this partition entry represents a container partition. False otherwise.</returns>
            Public ReadOnly Property IsContainerPartition As Boolean
                Get
                    Return (PartitionType = PARTITION_TYPE.PARTITION_EXTENDED) OrElse (PartitionType = PARTITION_TYPE.PARTITION_XINT13_EXTENDED)
                End Get
            End Property
        End Structure

        <StructLayout(LayoutKind.Sequential)>
        Public Structure DISK_GROW_PARTITION
            Public Property PartitionNumber As Int32
            Public Property BytesToGrow As Int64
        End Structure

        <StructLayout(LayoutKind.Sequential)>
        Public Structure COMMTIMEOUTS
            Public Property ReadIntervalTimeout As UInt32
            Public Property ReadTotalTimeoutMultiplier As UInt32
            Public Property ReadTotalTimeoutConstant As UInt32
            Public Property WriteTotalTimeoutMultiplier As UInt32
            Public Property WriteTotalTimeoutConstant As UInt32
        End Structure

        <StructLayout(LayoutKind.Sequential)>
        Public Structure SERVICE_STATUS
            Public Property dwServiceType As Integer
            Public Property dwCurrentState As Integer
            Public Property dwControlsAccepted As Integer
            Public Property dwWin32ExitCode As Integer
            Public Property dwServiceSpecificExitCode As Integer
            Public Property dwCheckPoint As Integer
            Public Property dwWaitHint As Integer
        End Structure

        <Flags>
        Public Enum DEFINE_DOS_DEVICE_FLAGS As UInt32
            DDD_EXACT_MATCH_ON_REMOVE = &H4
            DDD_NO_BROADCAST_SYSTEM = &H8
            DDD_RAW_TARGET_PATH = &H1
            DDD_REMOVE_DEFINITION = &H2
        End Enum

        ''' <summary>
        ''' Encapsulates a Service Control Management object handle that is closed by calling CloseServiceHandle() Win32 API.
        ''' </summary>
        Public NotInheritable Class SafeServiceHandle
            Inherits SafeHandleZeroOrMinusOneIsInvalid

            ''' <summary>
            ''' Initiates a new instance with an existing open handle.
            ''' </summary>
            ''' <param name="open_handle">Existing open handle.</param>
            ''' <param name="owns_handle">Indicates whether handle should be closed when this
            ''' instance is released.</param>
            Public Sub New(open_handle As IntPtr, owns_handle As Boolean)
                MyBase.New(owns_handle)

                SetHandle(open_handle)
            End Sub

            ''' <summary>
            ''' Creates a new empty instance. This constructor is used by native to managed
            ''' handle marshaller.
            ''' </summary>
            Private Sub New()
                MyBase.New(ownsHandle:=True)

            End Sub

            ''' <summary>
            ''' Closes contained handle by calling CloseServiceHandle() Win32 API.
            ''' </summary>
            ''' <returns>Return value from CloseServiceHandle() Win32 API.</returns>
            Protected Overrides Function ReleaseHandle() As Boolean
                Return CloseServiceHandle(handle)
            End Function
        End Class

        Public Const ERROR_MORE_DATA As Int32 = 234L

        Public Declare Auto Function OpenSCManager Lib "advapi32.dll" (
          <MarshalAs(UnmanagedType.LPTStr), [In]> lpMachineName As String,
          <MarshalAs(UnmanagedType.LPTStr), [In]> lpDatabaseName As String,
          dwDesiredAccess As Integer) As SafeServiceHandle

        Public Declare Auto Function CreateService Lib "advapi32.dll" (
          hSCManager As SafeServiceHandle,
          lpServiceName As String,
          lpDisplayName As String,
          dwDesiredAccess As Integer,
          dwServiceType As Integer,
          dwStartType As Integer,
          dwErrorControl As Integer,
          lpBinaryPathName As String,
          lpLoadOrderGroup As String,
          lpdwTagId As IntPtr,
          lpDependencies As String,
          lp As String,
          lpPassword As String) As SafeServiceHandle

        Public Declare Auto Function OpenService Lib "advapi32.dll" (
          hSCManager As SafeServiceHandle,
          <MarshalAs(UnmanagedType.LPTStr), [In]> lpServiceName As String,
          dwDesiredAccess As Integer) As SafeServiceHandle

        Public Declare Auto Function ControlService Lib "advapi32.dll" (
          hSCManager As SafeServiceHandle,
          dwControl As Integer,
          ByRef lpServiceStatus As SERVICE_STATUS) As Boolean

        Public Declare Auto Function DeleteService Lib "advapi32.dll" (
          hSCObject As SafeServiceHandle) As Boolean

        Public Declare Auto Function CloseServiceHandle Lib "advapi32.dll" (
          hSCObject As IntPtr) As Boolean

        Public Declare Auto Function StartService Lib "advapi32.dll" (
          hService As SafeServiceHandle,
          dwNumServiceArgs As Integer,
          lpServiceArgVectors As IntPtr) As Boolean

        Public Declare Auto Function GetModuleHandle Lib "kernel32.dll" (
          <MarshalAs(UnmanagedType.LPTStr), [In]> ModuleName As String) As IntPtr

        Public Declare Auto Function LoadLibrary Lib "kernel32.dll" (
          <MarshalAs(UnmanagedType.LPTStr), [In]> lpFileName As String) As IntPtr

        Public Declare Auto Function FreeLibrary Lib "kernel32.dll" (
          hModule As IntPtr) As Boolean

        Public Declare Auto Function AllocConsole Lib "kernel32.dll" (
          ) As Boolean

        Public Declare Auto Function FreeConsole Lib "kernel32.dll" (
          ) As Boolean

        Public Declare Auto Function DefineDosDevice Lib "kernel32.dll" (
          dwFlags As DEFINE_DOS_DEVICE_FLAGS,
          <MarshalAs(UnmanagedType.LPTStr), [In]> lpDeviceName As String,
          <MarshalAs(UnmanagedType.LPTStr), [In]> lpTargetPath As String) As Boolean

        Public Declare Unicode Function QueryDosDeviceW Lib "kernel32.dll" (
          <MarshalAs(UnmanagedType.LPTStr), [In]> lpDeviceName As String,
          ByRef lpTargetPath As Char(),
          ucchMax As UInt32) As Boolean

        Public Declare Auto Function GetCommTimeouts Lib "kernel32" (
          hFile As SafeFileHandle,
          <Out> ByRef lpCommTimeouts As COMMTIMEOUTS) As Boolean

        Public Declare Auto Function SetCommTimeouts Lib "kernel32" (
          hFile As SafeFileHandle,
          <[In]> ByRef lpCommTimeouts As COMMTIMEOUTS) As Boolean

        Public Declare Auto Function CreateFile Lib "kernel32" (
          <MarshalAs(UnmanagedType.LPTStr), [In]> lpFileName As String,
          dwDesiredAccess As UInt32,
          dwShareMode As UInt32,
          lpSecurityAttributes As IntPtr,
          dwCreationDisposition As UInt32,
          dwFlagsAndAttributes As UInt32,
          hTemplateFile As IntPtr) As SafeFileHandle

        Public Declare Function GetFileSize Lib "kernel32" Alias "GetFileSizeEx" (
          hFile As SafeFileHandle,
          ByRef liFileSize As Int64) As Boolean

        Public Declare Function DeviceIoControl Lib "kernel32" Alias "DeviceIoControl" (
          hDevice As SafeFileHandle,
          dwIoControlCode As UInt32,
          <MarshalAs(UnmanagedType.LPArray), [In]> lpInBuffer As Byte(),
          nInBufferSize As UInt32,
          <MarshalAs(UnmanagedType.LPArray, SizeParamIndex:=6), Out> lpOutBuffer As Byte(),
          nOutBufferSize As UInt32,
          ByRef lpBytesReturned As UInt32,
          lpOverlapped As IntPtr) As Boolean

        Public Declare Function DeviceIoControl Lib "kernel32" Alias "DeviceIoControl" (
          hDevice As SafeFileHandle,
          dwIoControlCode As UInt32,
          lpInBuffer As IntPtr,
          nInBufferSize As UInt32,
          lpOutBuffer As IntPtr,
          nOutBufferSize As UInt32,
          ByRef lpBytesReturned As UInt32,
          lpOverlapped As IntPtr) As Boolean

        Public Declare Auto Function GetModuleFileName Lib "kernel32" (
          hModule As IntPtr,
          <MarshalAs(UnmanagedType.LPTStr)> lpFilename As String,
          nSize As Int32) As Int32

        Public Declare Ansi Function GetProcAddress Lib "kernel32" (
          hModule As IntPtr,
          <[In], MarshalAs(UnmanagedType.LPStr)> lpEntryName As String) As IntPtr

        Public Declare Ansi Function GetProcAddress Lib "kernel32" (
          hModule As IntPtr,
          ordinal As IntPtr) As IntPtr

        Public Declare Ansi Function PulseEvent Lib "kernel32" (
          safeWaitHandle As SafeWaitHandle) As Boolean

        Public Declare Auto Function RtlGenRandom Lib "advapi32" Alias "SystemFunction036" (
              <Out> ByRef buffer As Int32,
              length As Int32) As Byte

        Public Declare Function GetFileType Lib "kernel32.dll" (handle As IntPtr) As Win32FileType

        Public Declare Function GetFileType Lib "kernel32.dll" (handle As SafeFileHandle) As Win32FileType

        Public Declare Function GetStdHandle Lib "kernel32.dll" (nStdHandle As StdHandle) As IntPtr

        Public Declare Function GetConsoleScreenBufferInfo Lib "kernel32.dll" (hConsoleOutput As IntPtr, <Out> ByRef lpConsoleScreenBufferInfo As CONSOLE_SCREEN_BUFFER_INFO) As Boolean

        Public Declare Function GetConsoleScreenBufferInfo Lib "kernel32.dll" (hConsoleOutput As SafeFileHandle, <Out> ByRef lpConsoleScreenBufferInfo As CONSOLE_SCREEN_BUFFER_INFO) As Boolean

    End Class
#End Region

    Private Sub New()
    End Sub

    ''' <summary>
    ''' Encapsulates call to a Win32 API function that returns a BOOL value indicating success
    ''' or failure and where an error value is available through a call to GetLastError() in case
    ''' of failure. If value True is passed to this method it does nothing. If False is passed,
    ''' it calls GetLastError(), converts error code to a HRESULT value and throws a managed
    ''' exception for that HRESULT.
    ''' </summary>
    ''' <param name="result">Return code from a Win32 API function call.</param>
    Public Shared Sub Win32Try(result As Boolean)

        If result = False Then
            Throw New Win32Exception
        End If

    End Sub

    ''' <summary>
    ''' Encapsulates call to a Win32 API function that returns a value where failure
    ''' is indicated as a NULL return and GetLastError() returns an error code. If
    ''' non-zero value is passed to this method it just returns that value. If zero
    ''' value is passed, it calls GetLastError() and throws a managed exception for
    ''' that error code.
    ''' </summary>
    ''' <param name="result">Return code from a Win32 API function call.</param>
    <DebuggerHidden>
    Public Shared Function Win32Try(Of T)(result As T) As T

        If result Is Nothing Then
            Throw New Win32Exception
        End If
        Return result

    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle in a SafeFileHandle object.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="Overlapped">Specifies whether to request overlapped I/O.</param>
    Public Shared Function OpenFileHandle(
      FileName As String,
      DesiredAccess As FileAccess,
      ShareMode As FileShare,
      CreationDisposition As FileMode,
      Overlapped As Boolean) As SafeFileHandle

        If String.IsNullOrEmpty(FileName) Then
            Throw New ArgumentNullException(NameOf(FileName))
        End If

        Dim NativeDesiredAccess As UInt32 = UnsafeNativeMethods.FILE_READ_ATTRIBUTES
        If (DesiredAccess And FileAccess.Read) = FileAccess.Read Then
            NativeDesiredAccess = NativeDesiredAccess Or UnsafeNativeMethods.GENERIC_READ
        End If
        If (DesiredAccess And FileAccess.Write) = FileAccess.Write Then
            NativeDesiredAccess = NativeDesiredAccess Or UnsafeNativeMethods.GENERIC_WRITE
        End If

        Dim NativeShareMode As UInt32 = 0
        If (ShareMode And FileShare.Read) = FileShare.Read Then
            NativeShareMode = NativeShareMode Or UnsafeNativeMethods.FILE_SHARE_READ
        End If
        If (ShareMode And FileShare.Write) = FileShare.Write Then
            NativeShareMode = NativeShareMode Or UnsafeNativeMethods.FILE_SHARE_WRITE
        End If
        If (ShareMode And FileShare.Delete) = FileShare.Delete Then
            NativeShareMode = NativeShareMode Or UnsafeNativeMethods.FILE_SHARE_DELETE
        End If

        Dim NativeCreationDisposition As UInt32
        Select Case CreationDisposition
            Case FileMode.Create
                NativeCreationDisposition = UnsafeNativeMethods.CREATE_ALWAYS
            Case FileMode.CreateNew
                NativeCreationDisposition = UnsafeNativeMethods.CREATE_NEW
            Case FileMode.Open
                NativeCreationDisposition = UnsafeNativeMethods.OPEN_EXISTING
            Case FileMode.OpenOrCreate
                NativeCreationDisposition = UnsafeNativeMethods.OPEN_ALWAYS
            Case FileMode.Truncate
                NativeCreationDisposition = UnsafeNativeMethods.TRUNCATE_EXISTING
            Case Else
                Throw New NotImplementedException
        End Select

        Dim NativeFlagsAndAttributes As UInt32 = UnsafeNativeMethods.FILE_ATTRIBUTE_NORMAL
        If Overlapped Then
            NativeFlagsAndAttributes += UnsafeNativeMethods.FILE_FLAG_OVERLAPPED
        End If

        Dim Handle = UnsafeNativeMethods.CreateFile(FileName,
                                         NativeDesiredAccess,
                                         NativeShareMode,
                                         IntPtr.Zero,
                                         NativeCreationDisposition,
                                         NativeFlagsAndAttributes,
                                         IntPtr.Zero)
        If Handle.IsInvalid Then
            Throw New Win32Exception
        End If

        Return Handle
    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function to create a backup handle for a file or
    ''' directory and encapsulates returned handle in a SafeFileHandle object. This
    ''' handle can later be used in calls to Win32 Backup API functions or similar.
    ''' </summary>
    ''' <param name="FilePath">Name of file or directory to open.</param>
    ''' <param name="DesiredAccess">Access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    Public Shared Function OpenBackupHandle(
      FilePath As String,
      DesiredAccess As FileAccess,
      ShareMode As FileShare,
      CreationDisposition As FileMode) As SafeFileHandle

        If String.IsNullOrEmpty(FilePath) Then
            Throw New ArgumentNullException(NameOf(FilePath))
        End If

        Dim NativeDesiredAccess As UInt32 = UnsafeNativeMethods.FILE_READ_ATTRIBUTES
        If (DesiredAccess And FileAccess.Read) = FileAccess.Read Then
            NativeDesiredAccess = NativeDesiredAccess Or UnsafeNativeMethods.GENERIC_READ
        End If
        If (DesiredAccess And FileAccess.Write) = FileAccess.Write Then
            NativeDesiredAccess = NativeDesiredAccess Or UnsafeNativeMethods.GENERIC_WRITE
        End If

        Dim NativeShareMode As UInt32 = 0
        If (ShareMode And FileShare.Read) = FileShare.Read Then
            NativeShareMode = NativeShareMode Or UnsafeNativeMethods.FILE_SHARE_READ
        End If
        If (ShareMode And FileShare.Write) = FileShare.Write Then
            NativeShareMode = NativeShareMode Or UnsafeNativeMethods.FILE_SHARE_WRITE
        End If
        If (ShareMode And FileShare.Delete) = FileShare.Delete Then
            NativeShareMode = NativeShareMode Or UnsafeNativeMethods.FILE_SHARE_DELETE
        End If

        Dim NativeCreationDisposition As UInt32
        Select Case CreationDisposition
            Case FileMode.Create
                NativeCreationDisposition = UnsafeNativeMethods.CREATE_ALWAYS
            Case FileMode.CreateNew
                NativeCreationDisposition = UnsafeNativeMethods.CREATE_NEW
            Case FileMode.Open
                NativeCreationDisposition = UnsafeNativeMethods.OPEN_EXISTING
            Case FileMode.OpenOrCreate
                NativeCreationDisposition = UnsafeNativeMethods.OPEN_ALWAYS
            Case FileMode.Truncate
                NativeCreationDisposition = UnsafeNativeMethods.TRUNCATE_EXISTING
            Case Else
                Throw New NotImplementedException
        End Select

        Dim NativeFlagsAndAttributes As UInt32 = UnsafeNativeMethods.FILE_FLAG_BACKUP_SEMANTICS

        Dim Handle = UnsafeNativeMethods.CreateFile(FilePath,
                                         NativeDesiredAccess,
                                         NativeShareMode,
                                         IntPtr.Zero,
                                         NativeCreationDisposition,
                                         NativeFlagsAndAttributes,
                                         IntPtr.Zero)
        If Handle.IsInvalid Then
            Throw New Win32Exception
        End If

        Return Handle
    End Function

    ''' <summary>
    ''' Converts FileAccess flags to values legal in constructor call to FileStream class.
    ''' </summary>
    ''' <param name="Value">FileAccess values.</param>
    Private Shared Function GetFileStreamLegalAccessValue(Value As FileAccess) As FileAccess
        If Value = 0 Then
            Return FileAccess.Read
        Else
            Return Value
        End If
    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    Public Shared Function OpenFileStream(
      FileName As String,
      CreationDisposition As FileMode,
      DesiredAccess As FileAccess,
      ShareMode As FileShare) As FileStream

        Return New FileStream(OpenFileHandle(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped:=False), GetFileStreamLegalAccessValue(DesiredAccess))

    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="BufferSize">Buffer size to specify in constructor call to FileStream class.</param>
    Public Shared Function OpenFileStream(
      FileName As String,
      CreationDisposition As FileMode,
      DesiredAccess As FileAccess,
      ShareMode As FileShare,
      BufferSize As Integer) As FileStream

        Return New FileStream(OpenFileHandle(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped:=False), GetFileStreamLegalAccessValue(DesiredAccess), BufferSize)

    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="BufferSize">Buffer size to specify in constructor call to FileStream class.</param>
    ''' <param name="Overlapped">Specifies whether to request overlapped I/O.</param>
    Public Shared Function OpenFileStream(
      FileName As String,
      CreationDisposition As FileMode,
      DesiredAccess As FileAccess,
      ShareMode As FileShare,
      BufferSize As Integer,
      Overlapped As Boolean) As FileStream

        Return New FileStream(OpenFileHandle(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped), GetFileStreamLegalAccessValue(DesiredAccess), BufferSize, Overlapped)

    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="Overlapped">Specifies whether to request overlapped I/O.</param>
    Public Shared Function OpenFileStream(
      FileName As String,
      CreationDisposition As FileMode,
      DesiredAccess As FileAccess,
      ShareMode As FileShare,
      Overlapped As Boolean) As FileStream

        Return New FileStream(OpenFileHandle(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped), GetFileStreamLegalAccessValue(DesiredAccess), 1, Overlapped)

    End Function

    Private Shared Sub SetFileCompressionState(SafeFileHandle As SafeFileHandle, State As UShort)

        Dim pinptr = GCHandle.Alloc(State, GCHandleType.Pinned)
        Try
            Win32Try(UnsafeNativeMethods.DeviceIoControl(SafeFileHandle,
                                              UnsafeNativeMethods.FSCTL_SET_COMPRESSION,
                                              pinptr.AddrOfPinnedObject(),
                                              2UI,
                                              IntPtr.Zero,
                                              0UI,
                                              Nothing,
                                              IntPtr.Zero))

        Finally
            pinptr.Free()

        End Try

    End Sub

    Public Shared Function GetFileSize(SafeFileHandle As SafeFileHandle) As Int64

        Dim FileSize As Int64

        Win32Try(UnsafeNativeMethods.GetFileSize(SafeFileHandle, FileSize))

        Return FileSize

    End Function

    Public Shared Function GetDiskSize(SafeFileHandle As SafeFileHandle) As Int64

        Dim FileSize As Int64

        Win32Try(UnsafeNativeMethods.DeviceIoControl(SafeFileHandle, UnsafeNativeMethods.IOCTL_DISK_GET_LENGTH_INFO, IntPtr.Zero, 0UI, FileSize, CUInt(Marshal.SizeOf(FileSize.GetType())), 0UI, IntPtr.Zero))

        Return FileSize

    End Function

    Public Shared Sub GrowPartition(DiskHandle As SafeFileHandle, PartitionNumber As Integer, BytesToGrow As Int64)

        Dim DiskGrowPartition As UnsafeNativeMethods.DISK_GROW_PARTITION
        DiskGrowPartition.PartitionNumber = PartitionNumber
        DiskGrowPartition.BytesToGrow = BytesToGrow
        Win32Try(UnsafeNativeMethods.DeviceIoControl(DiskHandle, UnsafeNativeMethods.IOCTL_DISK_GROW_PARTITION, DiskGrowPartition, CUInt(Marshal.SizeOf(DiskGrowPartition.GetType())), IntPtr.Zero, 0UI, 0UI, IntPtr.Zero))

    End Sub

    Public Shared Sub CompressFile(SafeFileHandle As SafeFileHandle)

        SetFileCompressionState(SafeFileHandle, UnsafeNativeMethods.COMPRESSION_FORMAT_DEFAULT)

    End Sub

    Public Shared Sub UncompressFile(SafeFileHandle As SafeFileHandle)

        SetFileCompressionState(SafeFileHandle, UnsafeNativeMethods.COMPRESSION_FORMAT_NONE)

    End Sub

    Public Shared Sub AllowExtendedDASDIO(SafeFileHandle As SafeFileHandle)

        Win32Try(UnsafeNativeMethods.DeviceIoControl(SafeFileHandle, UnsafeNativeMethods.FSCTL_ALLOW_EXTENDED_DASD_IO, IntPtr.Zero, 0UI, IntPtr.Zero, 0UI, 0UI, IntPtr.Zero))

    End Sub

    ''' <summary>
    ''' Adds a semicolon separated list of paths to the PATH environment variable of
    ''' current process. Any paths already in present PATH variable are not added again.
    ''' </summary>
    ''' <param name="AddPaths">Semicolon separated list of directory paths</param>
    ''' <param name="BeforeExisting">Indicates whether to insert new paths before existing path list or move
    ''' existing of specified paths first if True, or add new paths after existing path list if False.</param>
    Public Shared Sub AddProcessPaths(BeforeExisting As Boolean, AddPaths As String)

        If String.IsNullOrEmpty(AddPaths) Then
            Return
        End If

        Dim AddPathsArray = AddPaths.Split({";"c}, StringSplitOptions.RemoveEmptyEntries)

        AddProcessPaths(BeforeExisting, AddPathsArray)

    End Sub

    ''' <summary>
    ''' Adds a list of paths to the PATH environment variable of current process. Any
    ''' paths already in present PATH variable are not added again.
    ''' </summary>
    ''' <param name="AddPathsArray">Array of directory paths</param>
    ''' <param name="BeforeExisting">Indicates whether to insert new paths before existing path list or move
    ''' existing of specified paths first if True, or add new paths after existing path list if False.</param>
    Public Shared Sub AddProcessPaths(BeforeExisting As Boolean, ParamArray AddPathsArray As String())

        If AddPathsArray Is Nothing OrElse AddPathsArray.Length = 0 Then
            Return
        End If

        Dim Paths As New List(Of String)(Environment.GetEnvironmentVariable("PATH").Split({";"c}, StringSplitOptions.RemoveEmptyEntries))

        If BeforeExisting Then
            For Each AddPath In AddPathsArray
                If Paths.BinarySearch(AddPath, StringComparer.CurrentCultureIgnoreCase) >= 0 Then
                    Paths.Remove(AddPath)
                End If
            Next
            Paths.InsertRange(0, AddPathsArray)
        Else
            For Each AddPath In AddPathsArray
                If Paths.BinarySearch(AddPath, StringComparer.CurrentCultureIgnoreCase) < 0 Then
                    Paths.Add(AddPath)
                End If
            Next
        End If

        Environment.SetEnvironmentVariable("PATH", String.Join(";", Paths.ToArray()))
    End Sub

    ''' <summary>
    ''' Locks and dismounts filesystem on a volume. Upon successful return, further access to the device
    ''' can only be done through the handle passed to this function until handle is closed or lock is
    ''' released.
    ''' </summary>
    ''' <param name="hDevice">Handle to device to lock and dismount.</param>
    ''' <param name="bForce">Indicates if True that volume should be immediately dismounted even if it
    ''' cannot be locked. This causes all open handles to files on the volume to become invalid. If False,
    ''' successful lock (no other open handles) is required before attempting to dismount filesystem.</param>
    Public Shared Function DismountVolumeFilesystem(hDevice As SafeFileHandle, bForce As Boolean) As Boolean

        If Not UnsafeNativeMethods.DeviceIoControl(hDevice, UnsafeNativeMethods.FSCTL_LOCK_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, Nothing, Nothing) Then
            If Not bForce Then
                Return False
            End If
        End If

        If Not UnsafeNativeMethods.DeviceIoControl(hDevice, UnsafeNativeMethods.FSCTL_DISMOUNT_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, Nothing, Nothing) Then
            Return False
        End If

        Return UnsafeNativeMethods.DeviceIoControl(hDevice, UnsafeNativeMethods.FSCTL_LOCK_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, Nothing, Nothing)

    End Function

    ''' <summary>
    ''' Retrieves disk geometry.
    ''' </summary>
    ''' <param name="hDevice">Handle to device.</param>
    Public Shared Function GetDiskGeometry(hDevice As SafeFileHandle) As UnsafeNativeMethods.DISK_GEOMETRY

        Dim DiskGeometry As UnsafeNativeMethods.DISK_GEOMETRY

        Win32Try(UnsafeNativeMethods.DeviceIoControl(hDevice, UnsafeNativeMethods.IOCTL_DISK_GET_DRIVE_GEOMETRY, IntPtr.Zero, 0, DiskGeometry, CUInt(Marshal.SizeOf(GetType(UnsafeNativeMethods.DISK_GEOMETRY))), Nothing, Nothing))

        Return DiskGeometry

    End Function

    Public Shared Function GetProcAddress(hModule As IntPtr, procedureName As String, delegateType As Type) As [Delegate]

        Return Marshal.GetDelegateForFunctionPointer(Win32Try(UnsafeNativeMethods.GetProcAddress(hModule, procedureName)), delegateType)

    End Function

    Public Shared Function GetProcAddress(moduleName As String, procedureName As String, delegateType As Type) As [Delegate]

        Dim hModule = Win32Try(UnsafeNativeMethods.LoadLibrary(moduleName))
        Return Marshal.GetDelegateForFunctionPointer(Win32Try(UnsafeNativeMethods.GetProcAddress(hModule, procedureName)), delegateType)

    End Function

    Public Enum Win32FileType As Int32
        Unknown = &H0
        Disk = &H1
        Character = &H2
        Pipe = &H3
        Remote = &H8000
    End Enum

    Public Enum StdHandle As Int32
        Input = -10
        Output = -11
        [Error] = -12
    End Enum

    <StructLayout(LayoutKind.Sequential), ComVisible(False)>
    Public Structure COORD
        Public ReadOnly Property X As Short
        Public ReadOnly Property Y As Short
    End Structure

    <StructLayout(LayoutKind.Sequential), ComVisible(False)>
    Public Structure SMALL_RECT
        Public ReadOnly Property Left As Short
        Public ReadOnly Property Top As Short
        Public ReadOnly Property Right As Short
        Public ReadOnly Property Bottom As Short
        Public ReadOnly Property Width As Short
            Get
                Return _Right - _Left + 1S
            End Get
        End Property
        Public ReadOnly Property Height As Short
            Get
                Return _Bottom - _Top + 1S
            End Get
        End Property
    End Structure

    <StructLayout(LayoutKind.Sequential), ComVisible(False)>
    Public Structure CONSOLE_SCREEN_BUFFER_INFO
        Public ReadOnly Property dwSize As COORD
        Public ReadOnly Property dwCursorPosition As COORD
        Public ReadOnly Property wAttributes As Short
        Public ReadOnly Property srWindow As SMALL_RECT
        Public ReadOnly Property dwMaximumWindowSize As COORD
    End Structure

End Class
