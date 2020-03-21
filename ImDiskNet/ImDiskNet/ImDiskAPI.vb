Namespace IO.ImDisk

  ''' <summary>
  ''' ImDisk API for sending commands to ImDisk Virtual Disk Driver from .NET applications.
  ''' </summary>
  <ComVisible(False)>
  Public NotInheritable Class ImDiskAPI

    Private Sub New()

    End Sub

    ''' <summary>
    ''' Checks if filename contains a known extension for which ImDisk knows of a constant offset value. That value can be
    ''' later passed as Offset parameter to CreateDevice method.
    ''' </summary>
    ''' <param name="ImageFile">Name of disk image file.</param>
    Public Shared Function GetOffsetByFileExt(ImageFile As String) As Long

      Dim Offset As Long
      If DLL.ImDiskGetOffsetByFileExt(ImageFile, Offset) Then
        Return Offset
      Else
        Return 0
      End If

    End Function

    ''' <summary>
    ''' Loads ImDisk Virtual Disk Driver into Windows kernel. This driver is needed to create ImDisk virtual disks. For
    ''' this method to be called successfully, driver needs to be installed and caller needs permission to load kernel mode
    ''' drivers.
    ''' </summary>
    Public Shared Sub LoadDriver()

      NativeFileIO.Win32Try(DLL.ImDiskStartService("ImDisk"))

    End Sub

    ''' <summary>
    ''' Starts ImDisk Virtual Disk Driver Helper Service. This service is needed to create proxy type ImDisk virtual disks
    ''' where the I/O proxy application is called through TCP/IP or a serial communications port. For
    ''' this method to be called successfully, service needs to be installed and caller needs permission to start services.
    ''' </summary>
    ''' <remarks></remarks>
    Public Shared Sub LoadHelperService()

      NativeFileIO.Win32Try(DLL.ImDiskStartService("ImDskSvc"))

    End Sub

    ''' <summary>
    ''' Creates a mount point on an empty subdirectory on an NTFS volume.
    ''' </summary>
    ''' <param name="Directory">Path to an empty subdirectory on an NTFS volume</param>
    ''' <param name="Target">Target path in native format, for example \Device\ImDisk0</param>
    ''' <remarks></remarks>
    Public Shared Sub CreateMountPoint(Directory As String, Target As String)

      NativeFileIO.Win32Try(DLL.ImDiskCreateMountPoint(Directory, Target))

    End Sub

    ''' <summary>
    ''' Creates a mount point for an ImDisk virtual disk on an empty subdirectory on an NTFS volume.
    ''' </summary>
    ''' <param name="Directory">Path to an empty subdirectory on an NTFS volume</param>
    ''' <param name="DeviceNumber">Device number of an existing ImDisk virtual disk</param>
    ''' <remarks></remarks>
    Public Shared Sub CreateMountPoint(Directory As String, DeviceNumber As UInt32)

      NativeFileIO.Win32Try(DLL.ImDiskCreateMountPoint(Directory, "\Device\ImDisk" & DeviceNumber))
      
    End Sub

    ''' <summary>
    ''' Removes a mount point. Subdirectory will be restored to an empty subdirectory.
    ''' </summary>
    ''' <param name="MountPoint">Path to mount point subdirectory</param>
    Public Shared Sub RemoveMountPoint(MountPoint As String)

      NativeFileIO.Win32Try(DLL.ImDiskRemoveMountPoint(MountPoint))
      
    End Sub

    ''' <summary>
    ''' Returns first free drive letter available.
    ''' </summary>
    Public Shared Function FindFreeDriveLetter() As Char

      Return DLL.ImDiskFindFreeDriveLetter()

    End Function

    ''' <summary>
    ''' Retrieves a list of virtual disks on this system. Each element in returned list holds a device number of a loaded
    ''' ImDisk virtual disk.
    ''' </summary>
    Public Shared Function GetDeviceList() As List(Of Int32)

      Dim List As New List(Of Int32)
      Dim NativeDeviceList = DLL.ImDiskGetDeviceList()
      Dim NumberValue As UInt64 = 1
      Dim Number As Int32 = 0
      Do
        If (NativeDeviceList And NumberValue) <> 0 Then
          List.Add(Number)
        End If
        Number += 1
        If Number > 63 Then
          Exit Do
        End If
        NumberValue += NumberValue
      Loop

      Return List

    End Function

    ''' <summary>
    ''' Extends size of an existing ImDisk virtual disk.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number of ImDisk virtual disk to extend.</param>
    ''' <param name="ExtendSize">Size to add.</param>
    ''' <param name="StatusControl">Optional handle to control that can display status messages during operation.</param>
    Public Shared Sub ExtendDevice(DeviceNumber As UInt32, ExtendSize As Int64, StatusControl As IntPtr)

      NativeFileIO.Win32Try(DLL.ImDiskExtendDevice(StatusControl, DeviceNumber, ExtendSize))
      
    End Sub

    ''' <summary>
    ''' Extends size of an existing ImDisk virtual disk.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number of ImDisk virtual disk to extend.</param>
    ''' <param name="ExtendSize">Size to add.</param>
    Public Shared Sub ExtendDevice(DeviceNumber As UInt32, ExtendSize As Int64)

      ExtendDevice(DeviceNumber, ExtendSize, Nothing)

    End Sub

    ''' <summary>
    ''' Creates a new ImDisk virtual disk.
    ''' </summary>
    ''' <param name="DiskSize">Size of virtual disk. If this parameter is zero, current size of disk image file will
    ''' automatically be used as virtual disk size.</param>
    ''' <param name="TracksPerCylinder">Number of tracks per cylinder for virtual disk geometry. This parameter can be zero
    '''  in which case most reasonable value will be automatically used by the driver.</param>
    ''' <param name="SectorsPerTrack">Number of sectors per track for virtual disk geometry. This parameter can be zero
    '''  in which case most reasonable value will be automatically used by the driver.</param>
    ''' <param name="BytesPerSector">Number of bytes per sector for virtual disk geometry. This parameter can be zero
    '''  in which case most reasonable value will be automatically used by the driver.</param>
    ''' <param name="ImageOffset">A skip offset if virtual disk data does not begin immediately at start of disk image file.
    ''' Frequently used with image formats like Nero NRG which start with a file header not used by ImDisk or Windows
    ''' filesystem drivers.</param>
    ''' <param name="Flags">Flags specifying properties for virtual disk. See comments for each flag value.</param>
    ''' <param name="Filename">Name of disk image file to use or create. If disk image file already exists the DiskSize
    ''' parameter can be zero in which case current disk image file size will be used as virtual disk size. If Filename
    ''' paramter is Nothing/null disk will be created in virtual memory and not backed by a physical disk image file.</param>
    ''' <param name="NativePath">Specifies whether Filename parameter specifies a path in Windows native path format, the
    ''' path format used by drivers in Windows NT kernels, for example \Device\Harddisk0\Partition1\imagefile.img. If this
    ''' parameter is False path in FIlename parameter will be interpreted as an ordinary user application path.</param>
    ''' <param name="MountPoint">Mount point in the form of a drive letter and colon to create for newly created virtual
    ''' disk. If this parameter is Nothing/null the virtual disk will be created without a drive letter.</param>
    ''' <param name="StatusControl">Optional handle to control that can display status messages during operation.</param>
    Public Shared Sub CreateDevice(DiskSize As Int64,
                                   TracksPerCylinder As UInt32,
                                   SectorsPerTrack As UInt32,
                                   BytesPerSector As UInt32,
                                   ImageOffset As Int64,
                                   Flags As ImDiskFlags,
                                   Filename As String,
                                   NativePath As Boolean,
                                   MountPoint As String,
                                   StatusControl As IntPtr)

      Dim MediaType As Int32

      Dim DiskGeometry As New BufferedBinaryWriter
      DiskGeometry.Write(DiskSize)
      DiskGeometry.Write(MediaType)
      DiskGeometry.Write(TracksPerCylinder)
      DiskGeometry.Write(SectorsPerTrack)
      DiskGeometry.Write(BytesPerSector)

      NativeFileIO.Win32Try(DLL.ImDiskCreateDevice(StatusControl,
                                                   DiskGeometry.ToArray(),
                                                   ImageOffset,
                                                   Flags,
                                                   Filename,
                                                   NativePath,
                                                   MountPoint))

    End Sub

    ''' <summary>
    ''' Creates a new ImDisk virtual disk.
    ''' </summary>
    ''' <param name="DiskSize">Size of virtual disk. If this parameter is zero, current size of disk image file will
    ''' automatically be used as virtual disk size.</param>
    ''' <param name="TracksPerCylinder">Number of tracks per cylinder for virtual disk geometry. This parameter can be zero
    '''  in which case most reasonable value will be automatically used by the driver.</param>
    ''' <param name="SectorsPerTrack">Number of sectors per track for virtual disk geometry. This parameter can be zero
    '''  in which case most reasonable value will be automatically used by the driver.</param>
    ''' <param name="BytesPerSector">Number of bytes per sector for virtual disk geometry. This parameter can be zero
    '''  in which case most reasonable value will be automatically used by the driver.</param>
    ''' <param name="ImageOffset">A skip offset if virtual disk data does not begin immediately at start of disk image file.
    ''' Frequently used with image formats like Nero NRG which start with a file header not used by ImDisk or Windows
    ''' filesystem drivers.</param>
    ''' <param name="Flags">Flags specifying properties for virtual disk. See comments for each flag value.</param>
    ''' <param name="Filename">Name of disk image file to use or create. If disk image file already exists the DiskSize
    ''' parameter can be zero in which case current disk image file size will be used as virtual disk size. If Filename
    ''' paramter is Nothing/null disk will be created in virtual memory and not backed by a physical disk image file.</param>
    ''' <param name="NativePath">Specifies whether Filename parameter specifies a path in Windows native path format, the
    ''' path format used by drivers in Windows NT kernels, for example \Device\Harddisk0\Partition1\imagefile.img. If this
    ''' parameter is False path in FIlename parameter will be interpreted as an ordinary user application path.</param>
    ''' <param name="MountPoint">Mount point in the form of a drive letter and colon to create for newly created virtual
    ''' disk. If this parameter is Nothing/null the virtual disk will be created without a drive letter.</param>
    ''' <param name="DeviceNumber">In: Device number for device to create. Device number must not be in use by an existing
    ''' virtual disk. For automatic allocation of device number, pass UInt32.MaxValue.
    '''
    ''' Out: Device number for created device.</param>
    ''' <param name="StatusControl">Optional handle to control that can display status messages during operation.</param>
    Public Shared Sub CreateDevice(DiskSize As Int64,
                                   TracksPerCylinder As UInt32,
                                   SectorsPerTrack As UInt32,
                                   BytesPerSector As UInt32,
                                   ImageOffset As Int64,
                                   Flags As ImDiskFlags,
                                   Filename As String,
                                   NativePath As Boolean,
                                   MountPoint As String,
                                   ByRef DeviceNumber As UInt32,
                                   StatusControl As IntPtr)

      Dim MediaType As Int32

      Dim DiskGeometry As New BufferedBinaryWriter
      DiskGeometry.Write(DiskSize)
      DiskGeometry.Write(MediaType)
      DiskGeometry.Write(TracksPerCylinder)
      DiskGeometry.Write(SectorsPerTrack)
      DiskGeometry.Write(BytesPerSector)

      NativeFileIO.Win32Try(DLL.ImDiskCreateDeviceEx(StatusControl,
                                                     DeviceNumber,
                                                     DiskGeometry.ToArray(),
                                                     ImageOffset,
                                                     Flags,
                                                     Filename,
                                                     NativePath,
                                                     MountPoint))

    End Sub

    ''' <summary>
    ''' Removes an existing ImDisk virtual disk from system.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number to remove.</param>
    Public Shared Sub RemoveDevice(DeviceNumber As UInt32)

      RemoveDevice(DeviceNumber, Nothing)

    End Sub

    ''' <summary>
    ''' Removes an existing ImDisk virtual disk from system.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number to remove.</param>
    ''' <param name="StatusControl">Optional handle to control that can display status messages during operation.</param>
    Public Shared Sub RemoveDevice(DeviceNumber As UInt32, StatusControl As IntPtr)

      NativeFileIO.Win32Try(DLL.ImDiskRemoveDevice(StatusControl, DeviceNumber, Nothing))
      
    End Sub

    ''' <summary>
    ''' Removes an existing ImDisk virtual disk from system.
    ''' </summary>
    ''' <param name="MountPoint">Mount point of virtual disk to remove.</param>
    Public Shared Sub RemoveDevice(MountPoint As String)

      RemoveDevice(MountPoint, Nothing)

    End Sub

    ''' <summary>
    ''' Removes an existing ImDisk virtual disk from system.
    ''' </summary>
    ''' <param name="MountPoint">Mount point of virtual disk to remove.</param>
    ''' <param name="StatusControl">Optional handle to control that can display status messages during operation.</param>
    Public Shared Sub RemoveDevice(MountPoint As String, StatusControl As IntPtr)

      NativeFileIO.Win32Try(DLL.ImDiskRemoveDevice(StatusControl, 0, MountPoint))

    End Sub

    ''' <summary>
    ''' Forcefully removes an existing ImDisk virtual disk from system even if it is use by other applications.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number to remove.</param>
    Public Shared Sub ForceRemoveDevice(DeviceNumber As UInt32)

      NativeFileIO.Win32Try(DLL.ImDiskForceRemoveDevice(IntPtr.Zero, DeviceNumber))
      
    End Sub

    ''' <summary>
    ''' Retrieves properties for an existing ImDisk virtual disk.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number of ImDisk virtual disk to retrieve properties for.</param>
    ''' <param name="DiskSize">Size of virtual disk.</param>
    ''' <param name="TracksPerCylinder">Number of tracks per cylinder for virtual disk geometry.</param>
    ''' <param name="SectorsPerTrack">Number of sectors per track for virtual disk geometry.</param>
    ''' <param name="BytesPerSector">Number of bytes per sector for virtual disk geometry.</param>
    ''' <param name="ImageOffset">A skip offset if virtual disk data does not begin immediately at start of disk image file.
    ''' Frequently used with image formats like Nero NRG which start with a file header not used by ImDisk or Windows
    ''' filesystem drivers.</param>
    ''' <param name="Flags">Flags specifying properties for virtual disk. See comments for each flag value.</param>
    ''' <param name="DriveLetter">Drive letter if specified when virtual disk was created. If virtual disk was created
    ''' without a drive letter this parameter will be set to an empty Char value.</param>
    ''' <param name="Filename">Name of disk image file holding storage for file type virtual disk or used to create a
    ''' virtual memory type virtual disk.</param>
    Public Shared Sub QueryDevice(ByRef DeviceNumber As UInt32,
                                  ByRef DiskSize As Int64,
                                  ByRef TracksPerCylinder As UInt32,
                                  ByRef SectorsPerTrack As UInt32,
                                  ByRef BytesPerSector As UInt32,
                                  ByRef ImageOffset As Int64,
                                  ByRef Flags As ImDiskFlags,
                                  ByRef DriveLetter As Char,
                                  ByRef Filename As String)

      Dim CreateDataBuffer As Byte() = Nothing
      Array.Resize(CreateDataBuffer, 1096)

      NativeFileIO.Win32Try(DLL.ImDiskQueryDevice(DeviceNumber, CreateDataBuffer, 1096))

      Dim CreateDataReader As New BinaryReader(New MemoryStream(CreateDataBuffer), Encoding.Unicode)
      DeviceNumber = CreateDataReader.ReadUInt32()
      Dim Dummy = CreateDataReader.ReadUInt32()
      DiskSize = CreateDataReader.ReadInt64()
      Dim MediaType = CreateDataReader.ReadInt32()
      TracksPerCylinder = CreateDataReader.ReadUInt32()
      SectorsPerTrack = CreateDataReader.ReadUInt32()
      BytesPerSector = CreateDataReader.ReadUInt32()
      ImageOffset = CreateDataReader.ReadInt64()
      Flags = CType(CreateDataReader.ReadUInt32(), ImDiskFlags)
      DriveLetter = CreateDataReader.ReadChar()
      Dim FilenameLength = CreateDataReader.ReadUInt16()
      If FilenameLength = 0 Then
        Filename = Nothing
      Else
        Filename = Encoding.Unicode.GetString(CreateDataReader.ReadBytes(FilenameLength))
      End If

    End Sub

    ''' <summary>
    ''' Modifies properties for an existing ImDisk virtual disk.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number of ImDisk virtual disk to modify properties for.</param>
    ''' <param name="FlagsToChange">Flags for which to change values for.</param>
    ''' <param name="Flags">New flag values.</param>
    Public Shared Sub ChangeFlags(DeviceNumber As UInt32,
                                  FlagsToChange As ImDiskFlags,
                                  Flags As ImDiskFlags)

      ChangeFlags(DeviceNumber, FlagsToChange, Flags, Nothing)

    End Sub

    ''' <summary>
    ''' Modifies properties for an existing ImDisk virtual disk.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number of ImDisk virtual disk to modify properties for.</param>
    ''' <param name="FlagsToChange">Flags for which to change values for.</param>
    ''' <param name="Flags">New flag values.</param>
    ''' <param name="StatusControl">Optional handle to control that can display status messages during operation.</param>
    Public Shared Sub ChangeFlags(DeviceNumber As UInt32,
                                  FlagsToChange As ImDiskFlags,
                                  Flags As ImDiskFlags,
                                  StatusControl As IntPtr)

      NativeFileIO.Win32Try(DLL.ImDiskChangeFlags(StatusControl,
                                                  DeviceNumber,
                                                  Nothing,
                                                  FlagsToChange,
                                                  Flags))

    End Sub

    ''' <summary>
    ''' Modifies properties for an existing ImDisk virtual disk.
    ''' </summary>
    ''' <param name="MountPoint">Mount point of ImDisk virtual disk to modify properties for.</param>
    ''' <param name="FlagsToChange">Flags for which to change values for.</param>
    ''' <param name="Flags">New flag values.</param>
    Public Shared Sub ChangeFlags(MountPoint As String,
                                  FlagsToChange As ImDiskFlags,
                                  Flags As ImDiskFlags)

      ChangeFlags(MountPoint, FlagsToChange, Flags, Nothing)

    End Sub

    ''' <summary>
    ''' Modifies properties for an existing ImDisk virtual disk.
    ''' </summary>
    ''' <param name="MountPoint">Mount point of ImDisk virtual disk to modify properties for.</param>
    ''' <param name="FlagsToChange">Flags for which to change values for.</param>
    ''' <param name="Flags">New flag values.</param>
    ''' <param name="StatusControl">Optional handle to control that can display status messages during operation.</param>
    Public Shared Sub ChangeFlags(MountPoint As String,
                                  FlagsToChange As ImDiskFlags,
                                  Flags As ImDiskFlags,
                                  StatusControl As IntPtr)

      NativeFileIO.Win32Try(DLL.ImDiskChangeFlags(StatusControl,
                                                  0,
                                                  MountPoint,
                                                  FlagsToChange,
                                                  Flags))

    End Sub

    ''' <summary>
    ''' Checks if Flags specifies a read only virtual disk.
    ''' </summary>
    ''' <param name="Flags">Flag field to check.</param>
    Public Shared Function IsReadOnly(Flags As ImDiskFlags) As Boolean

      Return (Flags And ImDiskFlags.ReadOnly) = ImDiskFlags.ReadOnly

    End Function

    ''' <summary>
    ''' Checks if Flags specifies a removable virtual disk.
    ''' </summary>
    ''' <param name="Flags">Flag field to check.</param>
    Public Shared Function IsRemovable(Flags As ImDiskFlags) As Boolean

      Return (Flags And ImDiskFlags.Removable) = ImDiskFlags.Removable

    End Function

    ''' <summary>
    ''' Checks if Flags specifies a modified virtual disk.
    ''' </summary>
    ''' <param name="Flags">Flag field to check.</param>
    Public Shared Function IsModified(Flags As ImDiskFlags) As Boolean

      Return (Flags And ImDiskFlags.Modified) = ImDiskFlags.Modified

    End Function

    ''' <summary>
    ''' Gets device type bits from a Flag field.
    ''' </summary>
    ''' <param name="Flags">Flag field to check.</param>
    Public Shared Function GetDeviceType(Flags As ImDiskFlags) As ImDiskFlags

      Return CType(Flags And &HF0UI, ImDiskFlags)

    End Function

    ''' <summary>
    ''' Gets disk type bits from a Flag field.
    ''' </summary>
    ''' <param name="Flags">Flag field to check.</param>
    Public Shared Function GetDiskType(Flags As ImDiskFlags) As ImDiskFlags

      Return CType(Flags And &HF00UI, ImDiskFlags)

    End Function

    ''' <summary>
    ''' Gets proxy type bits from a Flag field.
    ''' </summary>
    ''' <param name="Flags">Flag field to check.</param>
    Public Shared Function GetProxyType(Flags As ImDiskFlags) As ImDiskFlags

      Return CType(Flags And &HF000UI, ImDiskFlags)

    End Function

  End Class

End Namespace
