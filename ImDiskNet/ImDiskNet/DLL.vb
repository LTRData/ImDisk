Namespace IO.ImDisk

  <ComVisible(False)>
  Public Class DLL

    Public Declare Unicode Function ImDiskCheckDriverVersion _
      Lib "imdisk.cpl" _
      Alias "_ImDiskCheckDriverVersion@4" (
        ByVal Handle As IntPtr
      ) As UInt32

    Public Declare Unicode Function ImDiskGetOffsetByFileExt _
      Lib "imdisk.cpl" _
      Alias "_ImDiskGetOffsetByFileExt@8" (
        ByVal ImageFileName As String,
        ByRef Offset As Long
      ) As UInt32

    Public Declare Unicode Function ImDiskCreateMountPoint _
      Lib "imdisk.cpl" _
      Alias "_ImDiskCreateMountPoint@8" (
        ByVal Directory As String,
        ByRef Target As String
      ) As UInt32

    Public Declare Unicode Function ImDiskRemoveMountPoint _
      Lib "imdisk.cpl" _
      Alias "_ImDiskRemoveMountPoint@4" (
        ByVal MountPoint As String
      ) As UInt32

    Public Declare Unicode Function ImDiskOpenDeviceByNumber _
      Lib "imdisk.cpl" _
      Alias "_ImDiskOpenDeviceByNumber@8" (
        ByVal DeviceNumber As UInt32,
        ByVal AccessMode As UInt32
      ) As IntPtr

    Public Declare Unicode Function ImDiskOpenDeviceByMountPoint _
      Lib "imdisk.cpl" _
      Alias "_ImDiskOpenDeviceByMountPoint@8" (
        ByVal MountPoint As String,
        ByVal AccessMode As UInt32
      ) As IntPtr

    Public Declare Unicode Function ImDiskGetVolumeSize _
      Lib "imdisk.cpl" _
      Alias "_ImDiskGetVolumeSize@8" (
        ByVal Handle As IntPtr,
        ByRef Size As Int64
      ) As UInt32

    Public Declare Unicode Function ImDiskSaveImageFile _
      Lib "imdisk.cpl" _
      Alias "_ImDiskSaveImageFile@16" (
        ByVal DeviceHandle As IntPtr,
        ByVal FileHandle As IntPtr,
        ByVal BufferSize As UInt32,
        <MarshalAs(UnmanagedType.Bool)> ByRef CancelFlag As Boolean
      ) As UInt32

    Public Declare Unicode Function ImDiskSaveImageFile _
      Lib "imdisk.cpl" _
      Alias "_ImDiskSaveImageFile@16" (
        ByVal DeviceHandle As IntPtr,
        ByVal FileHandle As IntPtr,
        ByVal BufferSize As UInt32,
        ByVal CancelFlag As IntPtr
      ) As UInt32

    Public Declare Unicode Function ImDiskExtendDevice _
      Lib "imdisk.cpl" _
      Alias "_ImDiskExtendDevice@12" (
        ByVal hWndStatusText As IntPtr,
        ByVal DeviceNumber As UInt32,
        ByRef ExtendSize As Int64
      ) As UInt32

    Public Declare Unicode Function ImDiskCreateDevice _
      Lib "imdisk.cpl" _
      Alias "_ImDiskCreateDevice@28" (
        ByVal hWndStatusText As IntPtr,
        <MarshalAs(UnmanagedType.LPArray)> ByVal DiskGeometry As Byte(),
        ByRef ImageOffset As Int64,
        ByVal Flags As UInt32,
        ByVal Filename As String,
        <MarshalAs(UnmanagedType.Bool)> ByVal NativePath As Boolean,
        ByVal MountPoint As String
      ) As UInt32

    Public Declare Unicode Function ImDiskRemoveDevice _
      Lib "imdisk.cpl" _
      Alias "_ImDiskRemoveDevice@12" (
        ByVal hWndStatusText As IntPtr,
        ByVal DeviceNumber As UInt32,
        ByVal MountPoint As String
      ) As UInt32

    Public Declare Unicode Function ImDiskForceRemoveDevice _
      Lib "imdisk.cpl" _
      Alias "_ImDiskForceRemoveDevice@8" (
        ByVal DeviceHandle As IntPtr,
        ByVal DeviceNumber As UInt32
      ) As UInt32

    Public Declare Unicode Function ImDiskChangeFlags _
      Lib "imdisk.cpl" _
      Alias "_ImDiskChangeFlags@20" (
        ByVal hWndStatusText As IntPtr,
        ByVal DeviceNumber As UInt32,
        ByVal MountPoint As String,
        ByVal FlagsToChange As UInt32,
        ByVal Flags As UInt32
      ) As UInt32

    Public Declare Unicode Function ImDiskQueryDevice _
      Lib "imdisk.cpl" _
      Alias "_ImDiskQueryDevice@12" (
        ByVal DeviceNumber As UInt32,
        <MarshalAs(UnmanagedType.LPArray, SizeParamIndex:=2)> ByVal CreateData As Byte(),
        ByVal CreateDataSize As UInt32
      ) As UInt32

    Public Declare Unicode Function ImDiskFindFreeDriveLetter _
      Lib "imdisk.cpl" _
      Alias "_ImDiskFindFreeDriveLetter@0" (
      ) As Char

    Public Declare Unicode Function ImDiskGetDeviceList _
      Lib "imdisk.cpl" _
      Alias "_ImDiskGetDeviceList@0" (
      ) As UInt64

    Public Declare Unicode Function ImDiskSaveImageFileInteractive _
      Lib "imdisk.cpl" _
      Alias "_ImDiskSaveImageFileInteractive@16" (
        ByVal DeviceHandle As IntPtr,
        ByVal WindowHandle As IntPtr,
        ByVal BufferSize As UInt32,
        <MarshalAs(UnmanagedType.Bool)> ByVal IsCdRomType As Boolean
      ) As UInt64

  End Class

End Namespace
