Imports System.Threading

Namespace ImDisk

    ''' <summary>
    ''' ImDisk API for sending commands to ImDisk Virtual Disk Driver from .NET applications.
    ''' </summary>
    <ComVisible(False)>
    Public Class ImDiskAPI

        Private Sub New()

        End Sub

        Private Shared ReadOnly _RefreshListeners As New List(Of EventHandler)

        Private Shared ReadOnly _EventListenerThread As New Thread(AddressOf RefreshEventThread)

        Private Shared ReadOnly _ThreadStopEvent As New EventWaitHandle(initialState:=False, mode:=EventResetMode.ManualReset)

        Private Shared Sub RefreshEventThread()
            Using RefreshEvent = OpenRefreshEvent()
                Do
                    Dim wait_handles As WaitHandle() = {_ThreadStopEvent, RefreshEvent}
                    Dim wait_result = WaitHandle.WaitAny(wait_handles)
                    If wait_handles(wait_result) Is _ThreadStopEvent Then
                        Exit Do
                    End If
                    RaiseEvent DriveListChanged(Nothing, EventArgs.Empty)
                Loop
            End Using
        End Sub

        ''' <summary>
        ''' This event is fired when drives are added or removed, or when parameters and options
        ''' are changed for a drive.
        ''' </summary>
        Public Shared Custom Event DriveListChanged As EventHandler
            AddHandler(value As EventHandler)
                SyncLock _RefreshListeners
                    If Not _EventListenerThread.IsAlive Then
                        _ThreadStopEvent.Reset()
                        _EventListenerThread.Start()
                    End If
                    _RefreshListeners.Add(value)
                End SyncLock
            End AddHandler

            RemoveHandler(value As EventHandler)
                SyncLock _RefreshListeners
                    _RefreshListeners.Remove(value)

                    If _RefreshListeners.Count = 0 AndAlso
                        _EventListenerThread.IsAlive Then

                        _ThreadStopEvent.Set()
                        _EventListenerThread.Join()
                    End If
                End SyncLock
            End RemoveHandler

            RaiseEvent(sender As Object, e As EventArgs)
                SyncLock _RefreshListeners
                    _RefreshListeners.ForEach(Sub(eh) eh(sender, e))
                End SyncLock
            End RaiseEvent
        End Event

        ''' <summary>
        ''' ImDisk API behaviour flags.
        ''' </summary>
        Public Shared Property APIFlags As DLL.ImDiskAPIFlags
            Get
                Return DLL.ImDiskGetAPIFlags()
            End Get
            Set(value As DLL.ImDiskAPIFlags)
                DLL.ImDiskSetAPIFlags(value)
            End Set
        End Property

        ''' <summary>
        ''' Opens a synchronization event that can be used with wait functions to
        ''' synchronize with change events in ImDisk driver. Event is fired when
        ''' for example a drive is added or removed, or when some options or
        ''' settings are changed for an existing drive.
        ''' </summary>
        Public Shared Function OpenRefreshEvent() As ImDiskRefreshEvent

            Return New ImDiskRefreshEvent(InheritHandle:=False)

        End Function

        ''' <summary>
        ''' Opens a synchronization event that can be used with wait functions to
        ''' synchronize with change events in ImDisk driver. Event is fired when
        ''' for example a drive is added or removed, or when some options or
        ''' settings are changed for an existing drive.
        ''' </summary>
        Public Shared Function OpenRefreshEvent(InheritHandle As Boolean) As ImDiskRefreshEvent

            Return New ImDiskRefreshEvent(InheritHandle)

        End Function

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

        Private Shared Function GetStreamReaderFunction(stream As Stream) As DLL.ImDiskReadFileManagedProc

            Return _
                Function(_Handle As IntPtr,
                         _Buffer As Byte(),
                         _Offset As Int64,
                         _NumberOfBytesToRead As UInt32,
                         ByRef _NumberOfBytesRead As UInt32) As Boolean

                    Try
                        stream.Position = _Offset
                        _NumberOfBytesRead = CUInt(stream.Read(_Buffer, 0, CInt(_NumberOfBytesToRead)))
                        Return True

                    Catch
                        Return False

                    End Try

                End Function

        End Function

        ''' <summary>
        ''' Parses partition table entries from a master boot record and extended partition table record, if any.
        ''' </summary>
        ''' <param name="ImageFile">Name of image file to examine.</param>
        ''' <param name="SectorSize">Sector size for translating sector values to absolute byte positions. This
        ''' parameter is in most cases 512.</param>
        ''' <param name="Offset">Offset in image file where master boot record is located.</param>
        ''' <returns>An array of eight PARTITION_INFORMATION structures</returns>
        Public Shared Function GetPartitionInformation(ImageFile As String, SectorSize As UInt32, Offset As Long) As ReadOnlyCollection(Of NativeFileIO.Win32API.PARTITION_INFORMATION)

            Dim PartitionInformation(0 To 7) As NativeFileIO.Win32API.PARTITION_INFORMATION

            NativeFileIO.Win32Try(DLL.ImDiskGetPartitionInformation(ImageFile, SectorSize, Offset, PartitionInformation))

            Return Array.AsReadOnly(PartitionInformation)

        End Function

        ''' <summary>
        ''' Parses partition table entries from a master boot record and extended partition table record, if any.
        ''' </summary>
        ''' <param name="ImageFile">Disk image to examine.</param>
        ''' <param name="SectorSize">Sector size for translating sector values to absolute byte positions. This
        ''' parameter is in most cases 512.</param>
        ''' <param name="Offset">Offset in image file where master boot record is located.</param>
        ''' <returns>An array of eight PARTITION_INFORMATION structures</returns>
        Public Shared Function GetPartitionInformation(ImageFile As Stream, SectorSize As UInt32, Offset As Long) As ReadOnlyCollection(Of NativeFileIO.Win32API.PARTITION_INFORMATION)

            Dim StreamReader = GetStreamReaderFunction(ImageFile)

            Dim PartitionInformation(0 To 7) As NativeFileIO.Win32API.PARTITION_INFORMATION

            NativeFileIO.Win32Try(DLL.ImDiskGetPartitionInfoIndirect(Nothing, StreamReader, SectorSize, Offset, PartitionInformation))

            Return Array.AsReadOnly(PartitionInformation)

        End Function

        ''' <summary>
        ''' Parses partition table entries from a master boot record and extended partition table record, if any.
        ''' </summary>
        ''' <param name="Handle">Value to pass as first parameter to ReadFileProc.</param>
        ''' <param name="ReadFileProc">Reference to method that reads disk image.</param>
        ''' <param name="SectorSize">Sector size for translating sector values to absolute byte positions. This
        ''' parameter is in most cases 512.</param>
        ''' <param name="Offset">Offset in image file where master boot record is located.</param>
        ''' <returns>An array of eight PARTITION_INFORMATION structures</returns>
        Public Shared Function GetPartitionInformation(Handle As IntPtr, ReadFileProc As DLL.ImDiskReadFileManagedProc, SectorSize As UInt32, Offset As Long) As ReadOnlyCollection(Of NativeFileIO.Win32API.PARTITION_INFORMATION)

            Dim PartitionInformation(0 To 7) As NativeFileIO.Win32API.PARTITION_INFORMATION

            NativeFileIO.Win32Try(DLL.ImDiskGetPartitionInfoIndirect(Handle, ReadFileProc, SectorSize, Offset, PartitionInformation))

            Return Array.AsReadOnly(PartitionInformation)

        End Function

        ''' <summary>
        ''' Parses partition table entries from a master boot record and extended partition table record, if any.
        ''' </summary>
        ''' <param name="Handle">Value to pass as first parameter to ReadFileProc.</param>
        ''' <param name="ReadFileProc">Reference to method that reads disk image.</param>
        ''' <param name="SectorSize">Sector size for translating sector values to absolute byte positions. This
        ''' parameter is in most cases 512.</param>
        ''' <returns>An array of eight PARTITION_INFORMATION structures</returns>
        Public Shared Function GetPartitionInformation(Handle As IntPtr, ReadFileProc As DLL.ImDiskReadFileManagedProc, SectorSize As UInt32) As ReadOnlyCollection(Of NativeFileIO.Win32API.PARTITION_INFORMATION)

            Return GetPartitionInformation(Handle, ReadFileProc, SectorSize, 0)

        End Function

        ''' <summary>
        ''' Parses partition table entries from a master boot record and extended partition table record, if any.
        ''' </summary>
        ''' <param name="Handle">Value to pass as first parameter to ReadFileProc.</param>
        ''' <param name="ReadFileProc">Reference to method that reads disk image.</param>
        ''' <param name="SectorSize">Sector size for translating sector values to absolute byte positions. This
        ''' parameter is in most cases 512.</param>
        ''' <param name="Offset">Offset in image file where master boot record is located.</param>
        ''' <returns>An array of eight PARTITION_INFORMATION structures</returns>
        Public Shared Function GetPartitionInformation(Handle As IntPtr, ReadFileProc As DLL.ImDiskReadFileUnmanagedProc, SectorSize As UInt32, Offset As Long) As ReadOnlyCollection(Of NativeFileIO.Win32API.PARTITION_INFORMATION)

            Dim PartitionInformation(0 To 7) As NativeFileIO.Win32API.PARTITION_INFORMATION

            NativeFileIO.Win32Try(DLL.ImDiskGetPartitionInfoIndirect(Handle, ReadFileProc, SectorSize, Offset, PartitionInformation))

            Return Array.AsReadOnly(PartitionInformation)

        End Function

        ''' <summary>
        ''' Parses partition table entries from a master boot record and extended partition table record, if any.
        ''' </summary>
        ''' <param name="Handle">Value to pass as first parameter to ReadFileProc.</param>
        ''' <param name="ReadFileProc">Reference to method that reads disk image.</param>
        ''' <param name="SectorSize">Sector size for translating sector values to absolute byte positions. This
        ''' parameter is in most cases 512.</param>
        ''' <returns>An array of eight PARTITION_INFORMATION structures</returns>
        Public Shared Function GetPartitionInformation(Handle As IntPtr, ReadFileProc As DLL.ImDiskReadFileUnmanagedProc, SectorSize As UInt32) As ReadOnlyCollection(Of NativeFileIO.Win32API.PARTITION_INFORMATION)

            Return GetPartitionInformation(Handle, ReadFileProc, SectorSize, 0)

        End Function

        ''' <summary>
        ''' Parses partition table entries from a master boot record and extended partition table record, if any.
        ''' </summary>
        ''' <param name="ImageFile">Name of image file to examine</param>
        ''' <param name="SectorSize">Sector size for translating sector values to absolute byte positions. This
        ''' parameter is in most cases 512.</param>
        ''' <returns>An array of eight PARTITION_INFORMATION structures</returns>
        Public Shared Function GetPartitionInformation(ImageFile As String, SectorSize As UInt32) As ReadOnlyCollection(Of NativeFileIO.Win32API.PARTITION_INFORMATION)

            Return GetPartitionInformation(ImageFile, SectorSize, 0)

        End Function

        ''' <summary>
        ''' Creates a new collection of partition table entries that only contains those entries
        ''' from source sequence with valid partition definitions.
        ''' </summary>
        ''' <param name="PartitionList">Sequence of partition table entries</param>
        Public Shared Function FilterDefinedPartitions(PartitionList As IEnumerable(Of NativeFileIO.Win32API.PARTITION_INFORMATION)) As ReadOnlyCollection(Of NativeFileIO.Win32API.PARTITION_INFORMATION)
            Dim DefinedPartitions As New List(Of NativeFileIO.Win32API.PARTITION_INFORMATION)(7)
            For Each PartitionInfo In PartitionList
                If PartitionInfo.PartitionLength <> 0 AndAlso
                  Not PartitionInfo.IsContainerPartition Then

                    DefinedPartitions.Add(PartitionInfo)
                End If
            Next
            Return DefinedPartitions.AsReadOnly()
        End Function

        ''' <summary>
        ''' Checks whether an image file contains an ISO9660 filesystem.
        ''' </summary>
        ''' <param name="Imagefile">Path to a volume image file or a device path to a disk volume,
        ''' such as \\.\A: or \\.\C:.</param>
        ''' <param name="Offset">Optional offset in bytes to where raw disk data begins, for use
        ''' with "non-raw" image files with headers before the actual disk image data.</param>
        Public Shared Function ImageContainsISOFS(Imagefile As String, Offset As Int64) As Boolean
            Dim rc = DLL.ImDiskImageContainsISOFS(Imagefile, Offset)
            If rc Then
                Return True
            ElseIf Marshal.GetLastWin32Error() = 0 Then
                Return False
            Else
                Throw New Win32Exception
            End If
        End Function

        ''' <summary>
        ''' Checks whether an image file contains an ISO9660 filesystem.
        ''' </summary>
        ''' <param name="Imagefile">Open stream that can be used to read the image file.</param>
        ''' <param name="Offset">Optional offset in bytes to where raw disk data begins, for use
        ''' with "non-raw" image files with headers before the actual disk image data.</param>
        Public Shared Function ImageContainsISOFS(Imagefile As Stream, Offset As Int64) As Boolean
            Dim rc = DLL.ImDiskImageContainsISOFSIndirect(Nothing, GetStreamReaderFunction(Imagefile), Offset)
            If rc Then
                Return True
            ElseIf Marshal.GetLastWin32Error() = 0 Then
                Return False
            Else
                Throw New Win32Exception
            End If
        End Function

        ''' <summary>
        '''    Reads formatted geometry for a volume by parsing BPB, BIOS Parameter Block,
        '''    from volume boot record into a DISK_GEOMETRY structure.
        '''
        '''    If no boot record signature is found, an exception is thrown.
        ''' </summary>
        ''' <param name="Imagefile">Path to a volume image file or a device path to a disk volume,
        ''' such as \\.\A: or \\.\C:.</param>
        ''' <param name="Offset">Optional offset in bytes to volume boot record within file for
        ''' use with "non-raw" volume image files. This parameter can be used to for example
        ''' skip over headers for specific disk image formats, or to skip over master boot
        ''' record in a disk image file that contains a complete raw disk image and not only a
        ''' single volume.</param>
        ''' <returns>A DISK_GEOMETRY structure that receives information about formatted geometry.
        ''' This function zeroes the Cylinders member.</returns>
        Public Shared Function GetFormattedGeometry(Imagefile As String, Offset As Int64) As NativeFileIO.Win32API.DISK_GEOMETRY
            Dim DiskGeometry As NativeFileIO.Win32API.DISK_GEOMETRY
            NativeFileIO.Win32Try(DLL.ImDiskGetFormattedGeometry(Imagefile, Offset, DiskGeometry))
            Return DiskGeometry
        End Function

        ''' <summary>
        '''    Reads formatted geometry for a volume by parsing BPB, BIOS Parameter Block,
        '''    from volume boot record into a DISK_GEOMETRY structure.
        '''
        '''    If no boot record signature is found, an exception is thrown.
        ''' </summary>
        ''' <param name="Imagefile">Open stream that can be used to read from volume image.</param>
        ''' <param name="Offset">Optional offset in bytes to volume boot record within file for
        ''' use with "non-raw" volume image files. This parameter can be used to for example
        ''' skip over headers for specific disk image formats, or to skip over master boot
        ''' record in a disk image file that contains a complete raw disk image and not only a
        ''' single volume.</param>
        ''' <returns>A DISK_GEOMETRY structure that receives information about formatted geometry.
        ''' This function zeroes the Cylinders member.</returns>
        Public Shared Function GetFormattedGeometry(Imagefile As Stream, Offset As Int64) As NativeFileIO.Win32API.DISK_GEOMETRY
            Dim DiskGeometry As NativeFileIO.Win32API.DISK_GEOMETRY
            NativeFileIO.Win32Try(DLL.ImDiskGetFormattedGeometryIndirect(Nothing, GetStreamReaderFunction(Imagefile), Offset, DiskGeometry))
            Return DiskGeometry
        End Function

        ''' <summary>
        ''' Combines GetOffsetByFileExt() and GetPartitionInformation() so that both format-specific offset and 
        ''' offset to first partition is combined into resulting Offset. If a partition was found, size of it is
        ''' also returned in the Size parameter.
        ''' </summary>
        ''' <param name="Imagefile">Name of image file to examine</param>
        ''' <param name="SectorSize">Sector size for translating sector values to absolute byte positions. This
        ''' parameter is in most cases 512.</param>
        ''' <param name="Offset">Absolute offset in image file where volume data begins</param>
        ''' <param name="Size">Size of partition if a partition table was found, otherwise zero</param>
        ''' <remarks></remarks>
        Public Shared Sub AutoFindOffsetAndSize(Imagefile As String,
                                                SectorSize As UInt32,
                                                <Out()> ByRef Offset As Long,
                                                <Out()> ByRef Size As Long)

            Offset = 0
            Size = 0

            Try
                Offset = ImDiskAPI.GetOffsetByFileExt(Imagefile)

                Dim PartitionList = ImDiskAPI.FilterDefinedPartitions(ImDiskAPI.GetPartitionInformation(Imagefile, SectorSize, Offset))
                If PartitionList Is Nothing OrElse PartitionList.Count = 0 Then
                    Exit Try
                End If
                If PartitionList(0).StartingOffset > 0 AndAlso
                      PartitionList(0).PartitionLength > 0 AndAlso
                      Not PartitionList(0).IsContainerPartition Then

                    Offset += PartitionList(0).StartingOffset
                    Size = PartitionList(0).PartitionLength
                End If

            Catch

            End Try

        End Sub

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
        ''' An easy way to turn an empty NTFS directory to a reparsepoint that redirects
        ''' requests to a mounted device. Acts quite like mount points or symbolic links
        ''' in *nix. If MountPoint specifies a character followed by a colon, a drive
        ''' letter is instead created to point to Target.
        ''' </summary>
        ''' <param name="Directory">Path to empty directory on an NTFS volume, or a drive letter
        ''' followed by a colon.</param>
        ''' <param name="Target">Target path in native format, for example \Device\ImDisk0</param>
        Public Shared Sub CreateMountPoint(Directory As String, Target As String)

            NativeFileIO.Win32Try(DLL.ImDiskCreateMountPoint(Directory, Target))

        End Sub

        ''' <summary>
        ''' An easy way to turn an empty NTFS directory to a reparsepoint that redirects
        ''' requests to an ImDisk device. Acts quite like mount points or symbolic links
        ''' in *nix. If MountPoint specifies a character followed by a colon, a drive
        ''' letter is instead created to point to Target.
        ''' </summary>
        ''' <param name="Directory">Path to empty directory on an NTFS volume, or a drive letter
        ''' followed by a colon.</param>
        ''' <param name="DeviceNumber">Device number of an existing ImDisk virtual disk</param>
        Public Shared Sub CreateMountPoint(Directory As String, DeviceNumber As UInt32)

            NativeFileIO.Win32Try(DLL.ImDiskCreateMountPoint(Directory, "\Device\ImDisk" & DeviceNumber))

        End Sub

        ''' <summary>
        ''' Restores a reparsepoint to be an ordinary empty directory, or removes a drive
        ''' letter mount point.
        ''' </summary>
        ''' <param name="MountPoint">Path to a reparse point on an NTFS volume, or a drive
        ''' letter followed by a colon to remove a drive letter mount point.</param>
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
        Public Shared Function GetDeviceList() As List(Of Integer)

            Dim NativeList(0 To 2) As Integer

            For i = 0 To 1

                If DLL.ImDiskGetDeviceListEx(NativeList.Length, NativeList) Then
                    Exit For
                End If

                Dim errorcode = Marshal.GetLastWin32Error()

                Select Case errorcode

                    Case NativeFileIO.Win32API.ERROR_MORE_DATA
                        Array.Resize(NativeList, NativeList(0) + 1)
                        Continue For

                    Case Else
                        Throw New Win32Exception(errorcode)

                End Select

            Next

            Array.Resize(NativeList, NativeList(0) + 1)

            Dim List As New List(Of Integer)(NativeList)

            List.RemoveAt(0)

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

            Dim DiskGeometry As New NativeFileIO.Win32API.DISK_GEOMETRY With {
              .Cylinders = DiskSize,
              .TracksPerCylinder = TracksPerCylinder,
              .SectorsPerTrack = SectorsPerTrack,
              .BytesPerSector = BytesPerSector
            }

            NativeFileIO.Win32Try(DLL.ImDiskCreateDevice(StatusControl,
                                                         DiskGeometry,
                                                         ImageOffset,
                                                         Flags,
                                                         Filename,
                                                         NativePath,
                                                         MountPoint))

        End Sub

        ''' <summary>
        ''' Creates a new ImDisk virtual disk.
        ''' </summary>
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
        Public Shared Sub CreateDevice(ImageOffset As Int64,
                                       Flags As ImDiskFlags,
                                       Filename As String,
                                       NativePath As Boolean,
                                       MountPoint As String,
                                       ByRef DeviceNumber As UInt32)

            Dim DiskGeometry As New NativeFileIO.Win32API.DISK_GEOMETRY

            NativeFileIO.Win32Try(DLL.ImDiskCreateDeviceEx(IntPtr.Zero,
                                                           DeviceNumber,
                                                           DiskGeometry,
                                                           ImageOffset,
                                                           Flags,
                                                           Filename,
                                                           NativePath,
                                                           MountPoint))

        End Sub

        ''' <summary>
        ''' Creates a new memory backed ImDisk virtual disk with the specified size in bytes.
        ''' </summary>
        ''' <param name="DiskSize">Size of virtual disk.</param>
        ''' <param name="MountPoint">Mount point in the form of a drive letter and colon to create for newly created virtual
        ''' disk. If this parameter is Nothing/null the virtual disk will be created without a drive letter.</param>
        ''' <param name="DeviceNumber">In: Device number for device to create. Device number must not be in use by an existing
        ''' virtual disk. For automatic allocation of device number, pass UInt32.MaxValue.
        '''
        ''' Out: Device number for created device.</param>
        Public Shared Sub CreateDevice(DiskSize As Int64,
                                       MountPoint As String,
                                       ByRef DeviceNumber As UInt32)

            Dim DiskGeometry As New NativeFileIO.Win32API.DISK_GEOMETRY With {
              .Cylinders = DiskSize
            }

            NativeFileIO.Win32Try(DLL.ImDiskCreateDeviceEx(IntPtr.Zero,
                                                           DeviceNumber,
                                                           DiskGeometry,
                                                           0,
                                                           0,
                                                           Nothing,
                                                           Nothing,
                                                           MountPoint))

        End Sub

        Public Enum MemoryType

            ''' <summary>
            ''' Virtual memory, allocated directly by ImDisk driver.
            ''' </summary>
            VirtualMemory

            ''' <summary>
            ''' Physical memory, allocated through AWEAlloc driver.
            ''' </summary>
            PhysicalMemory

        End Enum

        ''' <summary>
        ''' Creates a new memory backed ImDisk virtual disk with the specified size in bytes, or with disk volume data from an
        ''' image file. Memory could be either virtual memory allocated directly by ImDisk driver, or physical memory allocated
        ''' by AWEAlloc driver.
        ''' </summary>
        ''' <param name="DiskSize">Size of virtual disk. This parameter can be zero if ImageFile parameter specifies an image
        ''' file, in which case the size of the existing image file will be used as size of the newly created virtual disk
        ''' volume.</param>
        ''' <param name="MountPoint">Mount point in the form of a drive letter and colon to create for newly created virtual
        ''' disk. If this parameter is Nothing/null the virtual disk will be created without a drive letter.</param>
        ''' <param name="DeviceNumber">In: Device number for device to create. Device number must not be in use by an existing
        ''' virtual disk. For automatic allocation of device number, pass UInt32.MaxValue.
        '''
        ''' Out: Device number for created device.</param>
        ''' <param name="ImageFile">Optional name of image file that will be loaded onto the newly created memory disk.</param>
        ''' <param name="MemoryType">Specifies whether to use virtual or physical memory for the virtual disk.</param>
        Public Shared Sub CreateDevice(DiskSize As Int64,
                                       ImageFile As String,
                                       MemoryType As MemoryType,
                                       MountPoint As String,
                                       ByRef DeviceNumber As UInt32)

            Dim DiskGeometry As New NativeFileIO.Win32API.DISK_GEOMETRY With {
              .Cylinders = DiskSize
            }

            NativeFileIO.Win32Try(DLL.ImDiskCreateDeviceEx(IntPtr.Zero,
                                                           DeviceNumber,
                                                           DiskGeometry,
                                                           0,
                                                           If(MemoryType = ImDiskAPI.MemoryType.PhysicalMemory,
                                                              ImDiskFlags.TypeFile Or ImDiskFlags.FileTypeAwe,
                                                              ImDiskFlags.TypeVM),
                                                           ImageFile,
                                                           Nothing,
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

            Dim DiskGeometry As New NativeFileIO.Win32API.DISK_GEOMETRY With {
              .Cylinders = DiskSize,
              .TracksPerCylinder = TracksPerCylinder,
              .SectorsPerTrack = SectorsPerTrack,
              .BytesPerSector = BytesPerSector
            }

            NativeFileIO.Win32Try(DLL.ImDiskCreateDeviceEx(StatusControl,
                                                           DeviceNumber,
                                                           DiskGeometry,
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

            If String.IsNullOrEmpty(MountPoint) Then
                Throw New ArgumentNullException("MountPoint")
            End If
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
        Public Shared Sub QueryDevice(DeviceNumber As UInt32,
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

            NativeFileIO.Win32Try(DLL.ImDiskQueryDevice(DeviceNumber, CreateDataBuffer, CreateDataBuffer.Length))

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
        ''' Retrieves properties for an existing ImDisk virtual disk.
        ''' </summary>
        ''' <param name="DeviceNumber">Device number of ImDisk virtual disk to retrieve properties for.</param>
        Public Shared Function QueryDevice(DeviceNumber As UInt32) As DLL.ImDiskCreateData

            Dim CreateDataBuffer As New DLL.ImDiskCreateData
            NativeFileIO.Win32Try(DLL.ImDiskQueryDevice(DeviceNumber, CreateDataBuffer, Marshal.SizeOf(CreateDataBuffer.GetType())))
            Return CreateDataBuffer

        End Function

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

        ''' <summary>
        ''' Gets file type bits from a Flag field.
        ''' </summary>
        ''' <param name="Flags">Flag field to check.</param>
        Public Shared Function GetFileType(Flags As ImDiskFlags) As ImDiskFlags

            Return CType(Flags And &HF000UI, ImDiskFlags)

        End Function

        ''' <summary>
        ''' Determines whether flags specify either a virtual memory drive, or an
        ''' AWEAlloc (physical memory) drive.
        ''' </summary>
        ''' <param name="Flags">Flag field to check</param>
        Public Shared Function IsMemoryDrive(Flags As ImDiskFlags) As Boolean

            Return _
                    GetDiskType(Flags) = ImDiskFlags.TypeVM OrElse
                    (GetDiskType(Flags) = ImDiskFlags.TypeFile AndAlso
                     GetFileType(Flags) = ImDiskFlags.FileTypeAwe)

        End Function

        ''' <summary>
        '''    This function builds a Master Boot Record, MBR, in memory. The MBR will
        '''    contain a default Initial Program Loader, IPL, which could be used to boot
        '''    an operating system partition when the MBR is written to a disk.
        ''' </summary>
        ''' <param name="DiskGeometry">Pointer to a DISK_GEOMETRY structure that contains
        ''' information about logical geometry of the disk.
        ''' 
        ''' This function only uses the BytesPerSector, SectorsPerTrack and
        ''' TracksPerCylinder members.
        ''' 
        ''' This parameter can be Nothing/null if PartitionInfo parameter is Nothing/null
        ''' or references an empty array.</param>
        ''' <param name="PartitionInfo">Array of up to four PARTITION_INFORMATION structures
        ''' containing information about partitions to store in MBR partition table.
        ''' 
        ''' This function only uses the StartingOffset, PartitionLength, BootIndicator and
        ''' PartitionType members.
        ''' 
        ''' This parameter can be Nothing/null to create an empty MBR with just boot code
        ''' without any partition definitions.</param>
        ''' <param name="MBR">Pointer to memory buffer of at least 512 bytes where MBR will
        ''' be built.</param>
        Public Shared Sub BuildInMemoryMBR(DiskGeometry As NativeFileIO.Win32API.DISK_GEOMETRY,
                                           PartitionInfo As NativeFileIO.Win32API.PARTITION_INFORMATION(),
                                           MBR As Byte())

            NativeFileIO.Win32Try(DLL.ImDiskBuildMBR(DiskGeometry,
                                                     PartitionInfo,
                                                     CByte(If(PartitionInfo Is Nothing, 0, PartitionInfo.Length)),
                                                     MBR,
                                                     New IntPtr(MBR.Length)))

        End Sub

        ''' <summary>
        '''    This function builds a Master Boot Record, MBR, in memory. The MBR will
        '''    contain a default Initial Program Loader, IPL, which could be used to boot
        '''    an operating system partition when the MBR is written to a disk.
        ''' </summary>
        ''' <param name="DiskGeometry">Pointer to a DISK_GEOMETRY structure that contains
        ''' information about logical geometry of the disk.
        ''' 
        ''' This function only uses the BytesPerSector, SectorsPerTrack and
        ''' TracksPerCylinder members.
        ''' 
        ''' This parameter can be Nothing/null if PartitionInfo parameter is Nothing/null
        ''' or references an empty array.</param>
        ''' <param name="PartitionInfo">Array of up to four PARTITION_INFORMATION structures
        ''' containing information about partitions to store in MBR partition table.
        ''' 
        ''' This function only uses the StartingOffset, PartitionLength, BootIndicator and
        ''' PartitionType members.
        ''' 
        ''' This parameter can be Nothing/null to create an empty MBR with just boot code
        ''' without any partition definitions.</param>
        ''' <returns>Memory buffer containing built MBR.</returns>
        Public Shared Function BuildInMemoryMBR(DiskGeometry As NativeFileIO.Win32API.DISK_GEOMETRY,
                                           PartitionInfo As NativeFileIO.Win32API.PARTITION_INFORMATION()) As Byte()

            Dim MBR(0 To 511) As Byte

            NativeFileIO.Win32Try(DLL.ImDiskBuildMBR(DiskGeometry,
                                                     PartitionInfo,
                                                     CByte(If(PartitionInfo Is Nothing, 0, PartitionInfo.Length)),
                                                     MBR,
                                                     New IntPtr(MBR.Length)))

            Return MBR

        End Function

        ''' <summary>
        ''' This function converts a CHS disk address to LBA format.
        ''' </summary>
        ''' <param name="DiskGeometry">Pointer to a DISK_GEOMETRY structure that contains
        ''' information about logical geometry of the disk. This function only uses the
        ''' SectorsPerTrack and TracksPerCylinder members.</param>
        ''' <param name="CHS">Pointer to CHS disk address in three-byte partition table
        ''' style format.</param>
        ''' <returns>Calculated LBA disk address.</returns>
        Public Shared Function ConvertCHSToLBA(DiskGeometry As NativeFileIO.Win32API.DISK_GEOMETRY,
                                               CHS As Byte()) As UInteger

            Return DLL.ImDiskConvertCHSToLBA(DiskGeometry, CHS)

        End Function

        ''' <summary>
        ''' This function converts an LBA disk address to three-byte partition style CHS
        ''' format.
        ''' </summary>
        ''' <param name="DiskGeometry">Pointer to a DISK_GEOMETRY structure that contains
        ''' information about logical geometry of the disk. This function only uses the
        ''' SectorsPerTrack and TracksPerCylinder members.</param>
        ''' <param name="LBA">LBA disk address.</param>
        ''' <returns>Calculated CHS values expressed in an array of three bytes.</returns>
        Public Shared Function ConvertCHSToLBA(DiskGeometry As NativeFileIO.Win32API.DISK_GEOMETRY,
                                               LBA As UInteger) As Byte()

            Dim bytes = BitConverter.GetBytes(DLL.ImDiskConvertLBAToCHS(DiskGeometry, LBA))
            Array.Resize(bytes, 3)
            Return bytes

        End Function

    End Class

End Namespace
