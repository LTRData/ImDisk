Namespace IO.ImDisk

  <ComClass(ImDiskCOM.ClassId, ImDiskCOM.InterfaceId, ImDiskCOM.EventId)>
  Public Class ImDiskCOM

    Public Const ClassId = "681ce961-47a4-4e1b-a7d8-b4273cfc1e9c"
    Public Const InterfaceId = "bd74520a-370a-4f1d-8be7-bc64f3475d39"
    Public Const EventId = "34aa85a1-b86f-40ae-9be7-817a6fb8b54f"

    Public Sub New()
      MyBase.New()

    End Sub

    Public Function GetOffsetByFileExt(ByVal ImageFile As String) As Double

      Return ImDiskAPI.GetOffsetByFileExt(ImageFile)

    End Function

    Public Sub LoadDriver()

      ImDiskAPI.LoadDriver()

    End Sub

    Public Sub LoadHelperService()

      ImDiskAPI.LoadHelperService()

    End Sub

    Public Sub CreateMountPoint(ByVal Directory As String, ByVal Target As String)

      ImDiskAPI.CreateMountPoint(Directory, Target)

    End Sub

    Public Sub RemoveMountPoint(ByVal MountPoint As String)

      ImDiskAPI.RemoveMountPoint(MountPoint)

    End Sub

    Public Function FindFreeDriveLetter() As String

      Return ImDiskAPI.FindFreeDriveLetter()

    End Function

    Public Function GetDeviceList() As Int32()

      Return ImDiskAPI.GetDeviceList().ToArray()

    End Function

    Public Sub ExtendDevice(ByVal DeviceNumber As Int32, ByVal ExtendSize As Double, Optional ByVal StatusControl As Int32 = 0)

      ImDiskAPI.ExtendDevice(CUInt(DeviceNumber), CLng(ExtendSize), CType(StatusControl, IntPtr))

    End Sub

    Public Sub CreateDevice(Optional ByVal DiskSize As Double = 0,
                            Optional ByVal TracksPerCylinder As Int32 = 0,
                            Optional ByVal SectorsPerTrack As Int32 = 0,
                            Optional ByVal BytesPerSector As Int32 = 0,
                            Optional ByVal ImageOffset As Double = 0,
                            Optional ByVal Flags As ImDiskFlags = 0,
                            Optional ByVal Filename As String = Nothing,
                            Optional ByVal NativePath As Boolean = False,
                            Optional ByVal MountPoint As String = Nothing,
                            Optional ByVal StatusControl As Int32 = 0)

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

    Public Sub RemoveDeviceByNumber(ByVal DeviceNumber As Int32, Optional ByVal StatusControl As Int32 = 0)

      ImDiskAPI.RemoveDevice(CUInt(DeviceNumber), CType(StatusControl, IntPtr))

    End Sub

    Public Sub RemoveDeviceByMountPoint(ByVal MountPoint As String, Optional ByVal StatusControl As Int32 = 0)

      ImDiskAPI.RemoveDevice(MountPoint, CType(StatusControl, IntPtr))

    End Sub

    Public Sub ForceRemoveDevice(ByVal DeviceNumber As Int32)

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

    Public Sub ChangeFlagsByDeviceNumber(ByVal DeviceNumber As Int32,
                                         ByVal FlagsToChange As ImDiskFlags,
                                         ByVal Flags As ImDiskFlags,
                                         Optional ByVal StatusControl As Int32 = 0)

      ImDiskAPI.ChangeFlags(CUInt(DeviceNumber), FlagsToChange, Flags, CType(StatusControl, IntPtr))

    End Sub

    Public Sub ChangeFlagsByMountPoint(ByVal MountPoint As String,
                                       ByVal FlagsToChange As ImDiskFlags,
                                       ByVal Flags As ImDiskFlags,
                                       Optional ByVal StatusControl As Int32 = 0)

      ImDiskAPI.ChangeFlags(MountPoint, FlagsToChange, Flags, CType(StatusControl, IntPtr))

    End Sub

    Public Function IsReadOnly(ByVal Flags As ImDiskFlags) As Boolean

      Return (Flags And ImDiskFlags.ReadOnly) = ImDiskFlags.ReadOnly

    End Function

    Public Function IsRemovable(ByVal Flags As ImDiskFlags) As Boolean

      Return (Flags And ImDiskFlags.Removable) = ImDiskFlags.Removable

    End Function

    Public Function IsModified(ByVal Flags As ImDiskFlags) As Boolean

      Return (Flags And ImDiskFlags.Modified) = ImDiskFlags.Modified

    End Function

    Public Function GetDeviceType(ByVal Flags As ImDiskFlags) As ImDiskFlags

      Return CType(Flags And &HF0UI, ImDiskFlags)

    End Function

    Public Function GetDiskType(ByVal Flags As ImDiskFlags) As ImDiskFlags

      Return CType(Flags And &HF00UI, ImDiskFlags)

    End Function

    Public Function GetProxyType(ByVal Flags As ImDiskFlags) As ImDiskFlags

      Return CType(Flags And &HF000UI, ImDiskFlags)

    End Function

    Public Function OpenDeviceByNumber(ByVal DeviceNumber As Int32, ByVal AccessMode As FileAccess) As ImDiskDevice

      Return New ImDiskDevice(CUInt(DeviceNumber), AccessMode)

    End Function

    Public Function OpenDeviceByMountPoint(ByVal MountPoint As String, ByVal AccessMode As FileAccess) As ImDiskDevice

      Return New ImDiskDevice(MountPoint, AccessMode)

    End Function

    Public Function OpenControlDevice() As ImDiskObject

      Return New ImDiskControl

    End Function

  End Class

End Namespace
