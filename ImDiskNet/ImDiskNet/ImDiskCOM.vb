Namespace IO.ImDisk

  <Guid("b0bf4b36-0ebe-4ef4-8974-07694a7a3a81")>
  <ClassInterface(ClassInterfaceType.AutoDual)>
  Public Class ImDiskCOM

    Public Sub New()
      MyBase.New()

    End Sub

    Public Function GetOffsetByFileExt(ImageFile As String) As Double

      Return ImDiskAPI.GetOffsetByFileExt(ImageFile)

    End Function

    Public Sub LoadDriver()

      ImDiskAPI.LoadDriver()

    End Sub

    Public Sub LoadHelperService()

      ImDiskAPI.LoadHelperService()

    End Sub

    Public Sub CreateMountPoint(Directory As String, Target As String)

      ImDiskAPI.CreateMountPoint(Directory, Target)

    End Sub

    Public Sub CreateMountPointForDeviceNumber(Directory As String, DeviceNumber As Int32)

      ImDiskAPI.CreateMountPoint(Directory, CType(DeviceNumber, UInt32))

    End Sub

    Public Sub RemoveMountPoint(MountPoint As String)

      ImDiskAPI.RemoveMountPoint(MountPoint)

    End Sub

    Public Function FindFreeDriveLetter() As String

      Return ImDiskAPI.FindFreeDriveLetter()

    End Function

    Public Function GetDeviceList() As Int32()

      Return ImDiskAPI.GetDeviceList().ToArray()

    End Function

    Public Sub ExtendDevice(DeviceNumber As Int32, ExtendSize As Double, Optional StatusControl As Int32 = 0)

      ImDiskAPI.ExtendDevice(CUInt(DeviceNumber), CLng(ExtendSize), CType(StatusControl, IntPtr))

    End Sub

    Public Sub CreateDevice(Optional DiskSize As Double = 0,
                            Optional TracksPerCylinder As Int32 = 0,
                            Optional SectorsPerTrack As Int32 = 0,
                            Optional BytesPerSector As Int32 = 0,
                            Optional ImageOffset As Double = 0,
                            Optional Flags As ImDiskFlags = 0,
                            Optional Filename As String = Nothing,
                            Optional NativePath As Boolean = False,
                            Optional MountPoint As String = Nothing,
                            Optional StatusControl As Int32 = 0)

      ImDiskAPI.CreateDevice(CLng(DiskSize),
                             CUInt(TracksPerCylinder),
                             CUInt(SectorsPerTrack),
                             CUInt(BytesPerSector),
                             CLng(ImageOffset),
                             Flags,
                             Filename,
                             NativePath,
                             MountPoint,
                             CType(StatusControl, IntPtr))

    End Sub

    Public Sub CreateDeviceEx(Optional DiskSize As Double = 0,
                              Optional TracksPerCylinder As Int32 = 0,
                              Optional SectorsPerTrack As Int32 = 0,
                              Optional BytesPerSector As Int32 = 0,
                              Optional ImageOffset As Double = 0,
                              Optional Flags As ImDiskFlags = 0,
                              Optional Filename As String = Nothing,
                              Optional NativePath As Boolean = False,
                              Optional MountPoint As String = Nothing,
                              Optional ByRef DeviceNumber As Int32 = -1,
                              Optional StatusControl As Int32 = 0)

      Dim _DeviceNumber = If(DeviceNumber >= 0, CUInt(DeviceNumber), UInt32.MaxValue)

      ImDiskAPI.CreateDevice(CLng(DiskSize),
                             CUInt(TracksPerCylinder),
                             CUInt(SectorsPerTrack),
                             CUInt(BytesPerSector),
                             CLng(ImageOffset),
                             Flags,
                             Filename,
                             NativePath,
                             MountPoint,
                             _DeviceNumber,
                             CType(StatusControl, IntPtr))

      DeviceNumber = CInt(_DeviceNumber)

    End Sub

    Public Sub RemoveDeviceByNumber(DeviceNumber As Int32, Optional StatusControl As Int32 = 0)

      ImDiskAPI.RemoveDevice(CUInt(DeviceNumber), CType(StatusControl, IntPtr))

    End Sub

    Public Sub RemoveDeviceByMountPoint(MountPoint As String, Optional StatusControl As Int32 = 0)

      ImDiskAPI.RemoveDevice(MountPoint, CType(StatusControl, IntPtr))

    End Sub

    Public Sub ForceRemoveDevice(DeviceNumber As Int32)

      ImDiskAPI.ForceRemoveDevice(CUInt(DeviceNumber))

    End Sub

    Public Sub QueryDevice(ByRef DeviceNumber As Int32,
                           Optional ByRef DiskSize As Double = 0,
                           Optional ByRef TracksPerCylinder As Int32 = 0,
                           Optional ByRef SectorsPerTrack As Int32 = 0,
                           Optional ByRef BytesPerSector As Int32 = 0,
                           Optional ByRef ImageOffset As Double = 0,
                           Optional ByRef Flags As ImDiskFlags = 0,
                           Optional ByRef DriveLetter As String = Nothing,
                           Optional ByRef Filename As String = Nothing)

      Dim _DeviceNumber = CUInt(DeviceNumber)
      Dim _DiskSize As Int64
      Dim _TracksPerCylinder As UInt32
      Dim _SectorsPerTrack As UInt32
      Dim _BytesPerSector As UInt32
      Dim _ImageOffset As Int64
      Dim _DriveLetter As Char

      ImDiskAPI.QueryDevice(_DeviceNumber,
                            _DiskSize,
                            _TracksPerCylinder,
                            _SectorsPerTrack,
                            _BytesPerSector,
                            _ImageOffset,
                            Flags,
                            _DriveLetter,
                            Filename)

      DeviceNumber = CInt(_DeviceNumber)
      DiskSize = _DiskSize
      TracksPerCylinder = CInt(_TracksPerCylinder)
      SectorsPerTrack = CInt(_SectorsPerTrack)
      BytesPerSector = CInt(_BytesPerSector)
      ImageOffset = _ImageOffset
      DriveLetter = _DriveLetter

    End Sub

    Public Sub ChangeFlagsByDeviceNumber(DeviceNumber As Int32,
                                         FlagsToChange As ImDiskFlags,
                                         Flags As ImDiskFlags,
                                         Optional StatusControl As Int32 = 0)

      ImDiskAPI.ChangeFlags(CUInt(DeviceNumber), FlagsToChange, Flags, CType(StatusControl, IntPtr))

    End Sub

    Public Sub ChangeFlagsByMountPoint(MountPoint As String,
                                       FlagsToChange As ImDiskFlags,
                                       Flags As ImDiskFlags,
                                       Optional StatusControl As Int32 = 0)

      ImDiskAPI.ChangeFlags(MountPoint, FlagsToChange, Flags, CType(StatusControl, IntPtr))

    End Sub

    Public Function IsReadOnly(Flags As ImDiskFlags) As Boolean

      Return (Flags And ImDiskFlags.ReadOnly) = ImDiskFlags.ReadOnly

    End Function

    Public Function IsRemovable(Flags As ImDiskFlags) As Boolean

      Return (Flags And ImDiskFlags.Removable) = ImDiskFlags.Removable

    End Function

    Public Function IsModified(Flags As ImDiskFlags) As Boolean

      Return (Flags And ImDiskFlags.Modified) = ImDiskFlags.Modified

    End Function

    Public Function GetDeviceType(Flags As ImDiskFlags) As ImDiskFlags

      Return CType(Flags And &HF0UI, ImDiskFlags)

    End Function

    Public Function GetDiskType(Flags As ImDiskFlags) As ImDiskFlags

      Return CType(Flags And &HF00UI, ImDiskFlags)

    End Function

    Public Function GetProxyType(Flags As ImDiskFlags) As ImDiskFlags

      Return CType(Flags And &HF000UI, ImDiskFlags)

    End Function

    Public Function OpenDeviceByNumber(DeviceNumber As Int32, AccessMode As FileAccess) As ImDiskDevice

      Return New ImDiskDevice(CUInt(DeviceNumber), AccessMode)

    End Function

    Public Function OpenDeviceByMountPoint(MountPoint As String, AccessMode As FileAccess) As ImDiskDevice

      Return New ImDiskDevice(MountPoint, AccessMode)

    End Function

    Public Function OpenControlDevice() As ImDiskObject

      Return New ImDiskControl

    End Function

  End Class

End Namespace
