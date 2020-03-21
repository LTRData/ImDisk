Namespace IO.ImDisk

  <ComVisible(False)>
  Public NotInheritable Class DLL

    Private Sub New()

    End Sub

    Public Declare Unicode Function ImDiskCheckDriverVersion _
      Lib "imdisk.cpl" (
        Handle As IntPtr
      ) As UInt32

    Public Declare Unicode Function ImDiskStartService _
      Lib "imdisk.cpl" (
        ServiceName As String
      ) As Boolean

    Public Declare Unicode Function ImDiskGetOffsetByFileExt _
      Lib "imdisk.cpl" (
        ImageFileName As String,
        ByRef Offset As Int64
      ) As Boolean

    Public Declare Unicode Function ImDiskCreateMountPoint _
      Lib "imdisk.cpl" (
        Directory As String,
        Target As String
      ) As Boolean

    Public Declare Unicode Function ImDiskRemoveMountPoint _
      Lib "imdisk.cpl" (
        MountPoint As String
      ) As Boolean

    Public Declare Unicode Function ImDiskOpenDeviceByNumber _
      Lib "imdisk.cpl" (
        DeviceNumber As UInt32,
        AccessMode As UInt32
      ) As IntPtr

    Public Declare Unicode Function ImDiskOpenDeviceByMountPoint _
      Lib "imdisk.cpl" (
        MountPoint As String,
        AccessMode As UInt32
      ) As IntPtr

    Public Declare Unicode Function ImDiskGetVolumeSize _
      Lib "imdisk.cpl" (
        Handle As IntPtr,
        ByRef Size As Int64
      ) As Boolean

    Public Declare Unicode Function ImDiskSaveImageFile _
      Lib "imdisk.cpl" (
        DeviceHandle As IntPtr,
        FileHandle As IntPtr,
        BufferSize As UInt32,
        <MarshalAs(UnmanagedType.Bool)> ByRef CancelFlag As Boolean
      ) As Boolean

    Public Declare Unicode Function ImDiskSaveImageFile _
      Lib "imdisk.cpl" (
        DeviceHandle As IntPtr,
        FileHandle As IntPtr,
        BufferSize As UInt32,
        CancelFlag As IntPtr
      ) As Boolean

    Public Declare Unicode Function ImDiskExtendDevice _
      Lib "imdisk.cpl" (
        hWndStatusText As IntPtr,
        DeviceNumber As UInt32,
        ByRef ExtendSize As Int64
      ) As Boolean

    Public Declare Unicode Function ImDiskCreateDevice _
      Lib "imdisk.cpl" (
        hWndStatusText As IntPtr,
        <MarshalAs(UnmanagedType.LPArray)> DiskGeometry As Byte(),
        ByRef ImageOffset As Int64,
        Flags As UInt32,
        Filename As String,
        <MarshalAs(UnmanagedType.Bool)> NativePath As Boolean,
        MountPoint As String
      ) As Boolean

    Public Declare Unicode Function ImDiskCreateDeviceEx _
      Lib "imdisk.cpl" (
        hWndStatusText As IntPtr,
        ByRef DeviceNumber As UInt32,
        <MarshalAs(UnmanagedType.LPArray)> DiskGeometry As Byte(),
        ByRef ImageOffset As Int64,
        Flags As UInt32,
        Filename As String,
        <MarshalAs(UnmanagedType.Bool)> NativePath As Boolean,
        MountPoint As String
      ) As Boolean

    Public Declare Unicode Function ImDiskRemoveDevice _
      Lib "imdisk.cpl" (
        hWndStatusText As IntPtr,
        DeviceNumber As UInt32,
        MountPoint As String
      ) As Boolean

    Public Declare Unicode Function ImDiskForceRemoveDevice _
      Lib "imdisk.cpl" (
        DeviceHandle As IntPtr,
        DeviceNumber As UInt32
      ) As Boolean

    Public Declare Unicode Function ImDiskChangeFlags _
      Lib "imdisk.cpl" (
        hWndStatusText As IntPtr,
        DeviceNumber As UInt32,
        MountPoint As String,
        FlagsToChange As UInt32,
        Flags As UInt32
      ) As Boolean

    Public Declare Unicode Function ImDiskQueryDevice _
      Lib "imdisk.cpl" (
        DeviceNumber As UInt32,
        <MarshalAs(UnmanagedType.LPArray, SizeParamIndex:=2), Out()> CreateData As Byte(),
        CreateDataSize As UInt32
      ) As Boolean

    Public Declare Unicode Function ImDiskFindFreeDriveLetter _
      Lib "imdisk.cpl" (
      ) As Char

    Public Declare Unicode Function ImDiskGetDeviceList _
      Lib "imdisk.cpl" (
      ) As UInt64

    Public Declare Unicode Sub ImDiskSaveImageFileInteractive _
      Lib "imdisk.cpl" (
        DeviceHandle As IntPtr,
        WindowHandle As IntPtr,
        BufferSize As UInt32,
        <MarshalAs(UnmanagedType.Bool)> IsCdRomType As Boolean
      )

  End Class

End Namespace
