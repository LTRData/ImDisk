﻿#Disable Warning CA1401 ' P/Invokes should not be visible
#Disable Warning CA1711

Imports System.Runtime.InteropServices
Imports Microsoft.Win32.SafeHandles

Namespace ImDisk

    <ComVisible(False)>
    Public NotInheritable Class UnsafeNativeMethods

        Private Sub New()
        End Sub

        ''' <summary>
        ''' ImDisk API behaviour flags.
        ''' </summary>
        <Flags>
        Public Enum ImDiskAPIFlags As UInt64

            ''' <summary>
            ''' If set, no broadcast window messages are sent on creation and removal of drive letters.
            ''' </summary>
            NoBroadcastNotify = &H1

            ''' <summary>
            ''' If set, RemoveDevice() will automatically force a dismount of filesystem invalidating
            ''' any open handles.
            ''' </summary>
            ForceDismount = &H2

        End Enum

        Public Declare Unicode Function ImDiskGetAPIFlags _
          Lib "imdisk.cpl" (
          ) As ImDiskAPIFlags

        Public Declare Unicode Function ImDiskSetAPIFlags _
          Lib "imdisk.cpl" (
            Flags As ImDiskAPIFlags
          ) As ImDiskAPIFlags

        Public Declare Unicode Function ImDiskCheckDriverVersion _
          Lib "imdisk.cpl" (
            Handle As SafeFileHandle
          ) As Boolean

        Public Declare Unicode Function ImDiskStartService _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> ServiceName As String
          ) As Boolean

        Public Declare Unicode Function ImDiskGetOffsetByFileExt _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> ImageFileName As String,
            ByRef Offset As Int64
          ) As Boolean

        Public Declare Unicode Function ImDiskGetPartitionInformation _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> ImageFileName As String,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            PartitionInformation As IntPtr
          ) As Boolean

        Public Declare Unicode Function ImDiskGetPartitionInformation _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> ImageFileName As String,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            <MarshalAs(UnmanagedType.LPArray), Out> PartitionInformation As NativeFileIO.UnsafeNativeMethods.PARTITION_INFORMATION()
          ) As Boolean

        Public Declare Unicode Function ImDiskGetPartitionInformationEx _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> ImageFileName As String,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            <MarshalAs(UnmanagedType.FunctionPtr)> PartitionInformationProc As ImDiskGetPartitionInfoProc,
            UserData As IntPtr
          ) As Boolean

        Public Declare Unicode Function ImDiskGetSinglePartitionInformation _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> ImageFileName As String,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            <Out> ByRef PartitionInformation As NativeFileIO.UnsafeNativeMethods.PARTITION_INFORMATION
          ) As Boolean

        Public Delegate Function ImDiskReadFileManagedProc _
          (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.LPArray, SizeParamIndex:=3), Out> Buffer As Byte(),
            Offset As Int64,
            NumberOfBytes As UInt32,
            <Out> ByRef NumberOfBytesRead As UInt32
          ) As Boolean

        Public Delegate Function ImDiskReadFileUnmanagedProc _
          (
            Handle As IntPtr,
            Buffer As IntPtr,
            Offset As Int64,
            NumberOfBytes As UInt32,
            <Out> ByRef NumberOfBytesRead As UInt32
          ) As Boolean

        Public Delegate Function ImDiskGetPartitionInfoProc _
          (
            UserData As IntPtr,
            <[In]> ByRef PartitionInformation As NativeFileIO.UnsafeNativeMethods.PARTITION_INFORMATION
          ) As Boolean

        Public Declare Unicode Function ImDiskReadFileHandle _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.LPArray, SizeParamIndex:=3), Out> Buffer As Byte(),
            Offset As Int64,
            NumberOfBytes As UInt32,
            <Out> ByRef NumberOfBytesRead As UInt32
          ) As Boolean

        Public Declare Unicode Function ImDiskGetPartitionInfoIndirect _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileManagedProc,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            PartitionInformation As IntPtr
          ) As Boolean

        Public Declare Unicode Function ImDiskGetPartitionInfoIndirect _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileManagedProc,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            <MarshalAs(UnmanagedType.LPArray), Out> PartitionInformation As NativeFileIO.UnsafeNativeMethods.PARTITION_INFORMATION()
          ) As Boolean

        Public Declare Unicode Function ImDiskGetPartitionInfoIndirect _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileUnmanagedProc,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            PartitionInformation As IntPtr
          ) As Boolean

        Public Declare Unicode Function ImDiskGetPartitionInfoIndirect _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileUnmanagedProc,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            <MarshalAs(UnmanagedType.LPArray), Out> PartitionInformation As NativeFileIO.UnsafeNativeMethods.PARTITION_INFORMATION()
          ) As Boolean

        Public Declare Unicode Function ImDiskGetPartitionInfoIndirectEx _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileManagedProc,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            <MarshalAs(UnmanagedType.FunctionPtr)> PartitionInformationProc As ImDiskGetPartitionInfoProc,
            UserData As IntPtr
          ) As Boolean

        Public Declare Unicode Function ImDiskGetPartitionInfoIndirectEx _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileUnmanagedProc,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            <MarshalAs(UnmanagedType.FunctionPtr)> PartitionInformationProc As ImDiskGetPartitionInfoProc,
            UserData As IntPtr
          ) As Boolean

        Public Declare Unicode Function ImDiskGetSinglePartitionInfoIndirect _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileManagedProc,
            SectorSize As UInt32,
            <[In]> ByRef Offset As Int64,
            <Out> ByRef PartitionInformation As NativeFileIO.UnsafeNativeMethods.PARTITION_INFORMATION
          ) As Boolean

        Public Declare Unicode Function ImDiskImageContainsISOFS _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> ImageFileName As String,
            <[In]> ByRef Offset As Int64
          ) As Boolean

        Public Declare Unicode Function ImDiskImageContainsISOFSIndirect _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileManagedProc,
            <[In]> ByRef Offset As Int64
          ) As Boolean

        Public Declare Unicode Function ImDiskImageContainsISOFSIndirect _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileUnmanagedProc,
            <[In]> ByRef Offset As Int64
          ) As Boolean

        Public Declare Unicode Function ImDiskGetFormattedGeometry _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> ImageFileName As String,
            <[In]> ByRef Offset As Int64,
            <Out> ByRef DiskGeometry As NativeFileIO.UnsafeNativeMethods.DISK_GEOMETRY
          ) As Boolean

        Public Declare Unicode Function ImDiskGetFormattedGeometryIndirect _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileManagedProc,
            <[In]> ByRef Offset As Int64,
            <Out> ByRef DiskGeometry As NativeFileIO.UnsafeNativeMethods.DISK_GEOMETRY
          ) As Boolean

        Public Declare Unicode Function ImDiskGetFormattedGeometryIndirect _
          Lib "imdisk.cpl" (
            Handle As IntPtr,
            <MarshalAs(UnmanagedType.FunctionPtr)> ReadFileProc As ImDiskReadFileUnmanagedProc,
            <[In]> ByRef Offset As Int64,
            <Out> ByRef DiskGeometry As NativeFileIO.UnsafeNativeMethods.DISK_GEOMETRY
          ) As Boolean

        Public Declare Unicode Function ImDiskCreateMountPoint _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> Directory As String,
            <MarshalAs(UnmanagedType.LPWStr), [In]> Target As String
          ) As Boolean

        Public Declare Unicode Function ImDiskRemoveMountPoint _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> MountPoint As String
          ) As Boolean

        Public Declare Unicode Function ImDiskOpenDeviceByNumber _
          Lib "imdisk.cpl" (
            DeviceNumber As UInt32,
            AccessMode As UInt32
          ) As SafeFileHandle

        Public Declare Unicode Function ImDiskOpenDeviceByMountPoint _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.LPWStr), [In]> MountPoint As String,
            AccessMode As UInt32
          ) As SafeFileHandle

        Public Declare Unicode Function ImDiskGetVolumeSize _
          Lib "imdisk.cpl" (
            Handle As SafeFileHandle,
            ByRef Size As Int64
          ) As Boolean

        Public Declare Unicode Function ImDiskSaveImageFile _
          Lib "imdisk.cpl" (
            DeviceHandle As SafeFileHandle,
            FileHandle As SafeFileHandle,
            BufferSize As UInt32,
            CancelFlagPtr As IntPtr
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
            ByRef DiskGeometry As NativeFileIO.UnsafeNativeMethods.DISK_GEOMETRY,
            ByRef ImageOffset As Int64,
            Flags As UInt32,
            <MarshalAs(UnmanagedType.LPWStr), [In]> Filename As String,
            <MarshalAs(UnmanagedType.Bool)> NativePath As Boolean,
            <MarshalAs(UnmanagedType.LPWStr), [In]> MountPoint As String
          ) As Boolean

        Public Declare Unicode Function ImDiskCreateDeviceEx _
          Lib "imdisk.cpl" (
            hWndStatusText As IntPtr,
            ByRef DeviceNumber As UInt32,
            ByRef DiskGeometry As NativeFileIO.UnsafeNativeMethods.DISK_GEOMETRY,
            ByRef ImageOffset As Int64,
            Flags As UInt32,
            <MarshalAs(UnmanagedType.LPWStr), [In]> Filename As String,
            <MarshalAs(UnmanagedType.Bool)> NativePath As Boolean,
            <MarshalAs(UnmanagedType.LPWStr), [In]> MountPoint As String
          ) As Boolean

        Public Declare Unicode Function ImDiskRemoveDevice _
          Lib "imdisk.cpl" (
            hWndStatusText As IntPtr,
            DeviceNumber As UInt32,
            <MarshalAs(UnmanagedType.LPWStr), [In]> MountPoint As String
          ) As Boolean

        Public Declare Unicode Function ImDiskForceRemoveDevice _
          Lib "imdisk.cpl" (
            DeviceHandle As IntPtr,
            DeviceNumber As UInt32
          ) As Boolean

        Public Declare Unicode Function ImDiskForceRemoveDevice _
          Lib "imdisk.cpl" (
            DeviceHandle As SafeFileHandle,
            DeviceNumber As UInt32
          ) As Boolean

        Public Declare Unicode Function ImDiskChangeFlags _
          Lib "imdisk.cpl" (
            hWndStatusText As IntPtr,
            DeviceNumber As UInt32,
            <MarshalAs(UnmanagedType.LPWStr), [In]> MountPoint As String,
            FlagsToChange As UInt32,
            Flags As UInt32
          ) As Boolean

        Public Declare Unicode Function ImDiskQueryDevice _
          Lib "imdisk.cpl" (
            DeviceNumber As UInt32,
            <MarshalAs(UnmanagedType.LPArray, SizeParamIndex:=2), Out> CreateData As Byte(),
            CreateDataSize As Int32
          ) As Boolean

        <StructLayout(LayoutKind.Sequential, CharSet:=CharSet.Unicode)>
        <ComVisible(False)>
        Public Structure ImDiskCreateData
            Public Property DeviceNumber As Int32
            Private ReadOnly _Dummy As Int32
            Public Property DiskSize As Int64
            Public Property MediaType As Int32
            Public Property TracksPerCylinder As UInt32
            Public Property SectorsPerTrack As UInt32
            Public Property BytesPerSector As UInt32
            Public Property ImageOffset As Int64
            Public Property Flags As ImDiskFlags
            Public Property DriveLetter As Char
            Private _FilenameLength As UInt16

            <MarshalAs(UnmanagedType.ByValTStr, SizeConst:=16384)>
            Private _Filename As String

            Public Property Filename As String
                Get
                    If _Filename IsNot Nothing AndAlso _Filename.Length > _FilenameLength \ 2 Then
                        _Filename = _Filename.Remove(_FilenameLength \ 2)
                    End If
                    Return _Filename
                End Get
                Set
                    If Value Is Nothing Then
                        _Filename = Nothing
                        _FilenameLength = 0
                        Return
                    End If
                    _Filename = Value
                    _FilenameLength = CUShort(_Filename.Length * 2)
                End Set
            End Property
        End Structure

        Public Declare Unicode Function ImDiskQueryDevice _
          Lib "imdisk.cpl" (
            DeviceNumber As UInt32,
            <Out> ByRef CreateData As ImDiskCreateData,
            CreateDataSize As Int32
          ) As Boolean

        Public Declare Unicode Function ImDiskFindFreeDriveLetter _
          Lib "imdisk.cpl" (
          ) As Char

        <Obsolete("This method only supports a maximum of 64 simultaneously mounted devices. Use ImDiskGetDeviceListEx instead.")>
        Public Declare Unicode Function ImDiskGetDeviceList _
          Lib "imdisk.cpl" (
          ) As UInt64

        Public Declare Unicode Function ImDiskGetDeviceListEx _
          Lib "imdisk.cpl" (
            ListLength As Int32,
            <MarshalAs(UnmanagedType.LPArray)> DeviceList As Int32()
          ) As Boolean

        Public Declare Unicode Function ImDiskBuildMBR _
          Lib "imdisk.cpl" (
            <[In]> ByRef DiskGeometry As NativeFileIO.UnsafeNativeMethods.DISK_GEOMETRY,
            <MarshalAs(UnmanagedType.LPArray), [In]> PartitionInfo As NativeFileIO.UnsafeNativeMethods.PARTITION_INFORMATION(),
            NumberOfParts As Byte,
            <MarshalAs(UnmanagedType.LPArray)> MBR As Byte(),
            MBRSize As IntPtr
          ) As Boolean

        Public Declare Unicode Function ImDiskConvertCHSToLBA _
          Lib "imdisk.cpl" (
            <[In]> ByRef DiskGeometry As NativeFileIO.UnsafeNativeMethods.DISK_GEOMETRY,
            <MarshalAs(UnmanagedType.LPArray)> CHS As Byte()
          ) As UInt32

        Public Declare Unicode Function ImDiskConvertLBAToCHS _
          Lib "imdisk.cpl" (
            <[In]> ByRef DiskGeometry As NativeFileIO.UnsafeNativeMethods.DISK_GEOMETRY,
            LBA As UInt32
          ) As UInt32

        Public Declare Unicode Sub ImDiskSaveImageFileInteractive _
          Lib "imdisk.cpl" (
            DeviceHandle As SafeFileHandle,
            WindowHandle As IntPtr,
            BufferSize As UInt32,
            <MarshalAs(UnmanagedType.Bool)> IsCdRomType As Boolean
          )

        Public Declare Unicode Function ImDiskOpenRefreshEvent _
          Lib "imdisk.cpl" (
            <MarshalAs(UnmanagedType.Bool)> InheritHandle As Boolean
          ) As SafeWaitHandle

        Public Declare Unicode Function ImDiskSaveRegistrySettings _
            Lib "imdisk.cpl" _
            (<[In]> ByRef CreateData As ImDiskCreateData
             ) As Boolean

        Public Declare Unicode Function ImDiskRemoveRegistrySettings _
            Lib "imdisk.cpl" _
            (DeviceNumber As UInt32
             ) As Boolean

        Public Declare Unicode Function ImDiskGetRegistryAutoLoadDevices _
            Lib "imdisk.cpl" _
            (<Out> ByRef LoadDevicesValue As UInt32
             ) As Boolean

        Public Declare Unicode Function ImDiskNotifyShellDriveLetter _
            Lib "imdisk.cpl" _
            (WindowHandle As IntPtr,
             <MarshalAs(UnmanagedType.LPWStr), [In]> DriveLetterPath As String
             ) As Boolean

        Public Declare Unicode Function ImDiskNotifyRemovePending _
            Lib "imdisk.cpl" _
            (WindowHandle As IntPtr,
             DriveLetter As Char
             ) As Boolean

    End Class

End Namespace
