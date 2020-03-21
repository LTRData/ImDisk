Namespace IO.ImDisk

  ''' <summary>
  ''' Represents ImDisk Virtual Disk Driver disk device objects.
  ''' </summary>
  <ComClass(ImDiskDevice.ClassId, ImDiskDevice.InterfaceId, ImDiskDevice.EventId)>
  Public Class ImDiskDevice
    Inherits ImDiskObject

    Public Shadows Const ClassId = "1c671a5a-fb3b-4294-9aca-02e888ec4b7e"
    Public Shadows Const InterfaceId = "2a867de5-7f63-40a1-b9c9-ab95de188fef"
    Public Shadows Const EventId = "73fa0e55-0892-4f09-ab1b-9ffed429431e"

    Private Shared Function OpenDevice(ByVal DeviceNumber As UInt32, ByVal AccessMode As FileAccess) As SafeFileHandle

      Dim NativeAccessMode As UInt32 = NativeFileStream.FILE_READ_ATTRIBUTES
      If (AccessMode And FileAccess.Read) = FileAccess.Read Then
        NativeAccessMode += NativeFileStream.GENERIC_READ
      End If
      If (AccessMode And FileAccess.Write) = FileAccess.Write Then
        NativeAccessMode += NativeFileStream.GENERIC_WRITE
      End If

      Dim Handle As New SafeFileHandle(DLL.ImDiskOpenDeviceByNumber(DeviceNumber, NativeAccessMode), ownsHandle:=True)
      If Handle.IsInvalid Then
        Throw Marshal.GetExceptionForHR(Marshal.GetHRForLastWin32Error())
      Else
        Return Handle
      End If

    End Function

    Private Shared Function OpenDevice(ByVal MountPoint As String, ByVal AccessMode As FileAccess) As SafeFileHandle

      Dim NativeAccessMode As UInt32 = NativeFileStream.FILE_READ_ATTRIBUTES
      If (AccessMode And FileAccess.Read) = FileAccess.Read Then
        NativeAccessMode += NativeFileStream.GENERIC_READ
      End If
      If (AccessMode And FileAccess.Write) = FileAccess.Write Then
        NativeAccessMode += NativeFileStream.GENERIC_WRITE
      End If

      Dim Handle As New SafeFileHandle(DLL.ImDiskOpenDeviceByMountPoint(MountPoint, NativeAccessMode), ownsHandle:=True)
      If Handle.IsInvalid Then
        Throw Marshal.GetExceptionForHR(Marshal.GetHRForLastWin32Error())
      Else
        Return Handle
      End If

    End Function

    ''' <summary>
    ''' Creates a new instance and opens an existing ImDisk virtual disk device.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number of ImDisk virtual disk to open.</param>
    ''' <param name="AccessMode">Access mode to request for accessing disk object.</param>
    Public Sub New(ByVal DeviceNumber As UInt32, ByVal AccessMode As FileAccess)
      MyBase.New(OpenDevice(DeviceNumber, AccessMode), AccessMode)

    End Sub

    ''' <summary>
    ''' Creates a new instance and opens an existing disk device.
    ''' </summary>
    ''' <param name="MountPoint">Mount point of disk device to open.</param>
    ''' <param name="AccessMode">Access mode to request for accessing disk object.</param>
    Public Sub New(ByVal MountPoint As String, ByVal AccessMode As FileAccess)
      MyBase.New(OpenDevice(MountPoint, AccessMode), AccessMode)

    End Sub

    ''' <summary>
    ''' Retrieves volume size of disk device.
    ''' </summary>
    Public Overrides ReadOnly Property Length As Long
      Get
        Dim Size As Int64
        If DLL.ImDiskGetVolumeSize(SafeFileHandle.DangerousGetHandle(), Size) <> 0 Then
          Return Size
        Else
          Throw Marshal.GetExceptionForHR(Marshal.GetHRForLastWin32Error())
        End If
      End Get
    End Property

    ''' <summary>
    ''' Close device object.
    ''' </summary>
    Public Overloads Sub Close()
      MyBase.Close()
    End Sub

    ''' <summary>
    ''' Flush internal buffers to disk.
    ''' </summary>
    Public Overloads Sub Flush()
      MyBase.Flush()
    End Sub

    ''' <summary>
    ''' Reads raw disk data.
    ''' </summary>
    ''' <param name="count">Number of bytes to read from disk.</param>
    Public Function ReadBytes(ByVal count As Integer) As Byte()
      Dim ByteArray As Byte() = Nothing
      Array.Resize(ByteArray, count)
      count = MyBase.Read(ByteArray, 0, count)
      Array.Resize(ByteArray, count)
      Return ByteArray
    End Function

    ''' <summary>
    ''' Reads raw disk data.
    ''' </summary>
    Public Overloads Function ReadByte() As Integer
      Return MyBase.ReadByte()
    End Function

    ''' <summary>
    ''' Writes raw disk data.
    ''' </summary>
    ''' <param name="array">Source Byte array to write disk data from.</param>
    Public Sub WriteBytes(ByRef array As Byte())
      MyBase.Write(array, 0, array.Length)
    End Sub

    ''' <summary>
    ''' Writes raw disk data.
    ''' </summary>
    ''' <param name="value">Byte value to write to disk.</param>
    Public Overloads Sub WriteByte(ByVal value As Byte)
      MyBase.WriteByte(value)
    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file.
    ''' </summary>
    ''' <param name="ImageFile">FileStream object opened for writing where disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ByVal ImageFile As FileStream, ByVal BufferSize As UInt32)

      If DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                 ImageFile.SafeFileHandle.DangerousGetHandle(),
                                 BufferSize,
                                 IntPtr.Zero) <> 0 Then
        Return
      Else
        Marshal.ThrowExceptionForHR(Marshal.GetHRForLastWin32Error())
      End If

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file while pumping window messages between reads and writes.
    ''' </summary>
    ''' <param name="ImageFile">FileStream object opened for writing where disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    ''' <param name="CancelFlag">A boolean flag that will be checked between buffer reads/writes. If flag is set to True
    ''' operation will be cancelled and an exception thrown.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ByVal ImageFile As FileStream, ByVal BufferSize As UInt32, ByRef CancelFlag As Boolean)

      If DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                 ImageFile.SafeFileHandle.DangerousGetHandle(),
                                 BufferSize,
                                 CancelFlag) <> 0 Then
        Return
      Else
        Marshal.ThrowExceptionForHR(Marshal.GetHRForLastWin32Error())
      End If

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file.
    ''' </summary>
    ''' <param name="ImageFile">Native file handle opened for writing where disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ByVal ImageFile As IntPtr, ByVal BufferSize As UInt32)

      If DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                 ImageFile,
                                 BufferSize,
                                 IntPtr.Zero) <> 0 Then
        Return
      Else
        Marshal.ThrowExceptionForHR(Marshal.GetHRForLastWin32Error())
      End If

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file while pumping window messages between reads and writes.
    ''' </summary>
    ''' <param name="ImageFile">Native file handle opened for writing where disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    ''' <param name="CancelFlag">A boolean flag that will be checked between buffer reads/writes. If flag is set to True
    ''' operation will be cancelled and an exception thrown.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ByVal ImageFile As IntPtr, ByVal BufferSize As UInt32, ByRef CancelFlag As Boolean)

      If DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                 ImageFile,
                                 BufferSize,
                                 CancelFlag) <> 0 Then
        Return
      Else
        Marshal.ThrowExceptionForHR(Marshal.GetHRForLastWin32Error())
      End If

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file.
    ''' </summary>
    ''' <param name="ImageFile">Name of file to which disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ByVal ImageFile As String, ByVal BufferSize As UInt32)

      Using ImageFileHandle = NativeOpenFile(ImageFile, FileAccess.Write, FileShare.None, FileMode.Create, Overlapped:=False)

        If DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                   ImageFileHandle.DangerousGetHandle(),
                                   BufferSize,
                                   IntPtr.Zero) <> 0 Then
          Return
        Else
          Marshal.ThrowExceptionForHR(Marshal.GetHRForLastWin32Error())
        End If

      End Using

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file while pumping window messages between reads and writes.
    ''' </summary>
    ''' <param name="ImageFile">Name of file to which disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    ''' <param name="CancelFlag">A boolean flag that will be checked between buffer reads/writes. If flag is set to True
    ''' operation will be cancelled and an exception thrown.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ByVal ImageFile As String, ByVal BufferSize As UInt32, ByRef CancelFlag As Boolean)

      Using ImageFileHandle = NativeOpenFile(ImageFile, FileAccess.Write, FileShare.None, FileMode.Create, Overlapped:=False)

        If DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                   ImageFileHandle.DangerousGetHandle(),
                                   BufferSize,
                                   CancelFlag) <> 0 Then
          Return
        Else
          Marshal.ThrowExceptionForHR(Marshal.GetHRForLastWin32Error())
        End If

      End Using

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file.
    ''' </summary>
    ''' <param name="ImageFile">Name of file to which disk contents will be written.</param>
    Public Sub SaveImageFile(ByVal ImageFile As String)

      Using ImageFileHandle = NativeOpenFile(ImageFile, FileAccess.Write, FileShare.None, FileMode.Create, Overlapped:=False)

        If DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                   ImageFileHandle.DangerousGetHandle(),
                                   0,
                                   New Boolean) <> 0 Then
          Return
        Else
          Marshal.ThrowExceptionForHR(Marshal.GetHRForLastWin32Error())
        End If

      End Using

    End Sub

    ''' <summary>
    ''' This function saves the contents of a device to an image file. This is a
    ''' user-interactive function that displays dialog boxes where user can select
    ''' image file and other options.
    ''' </summary>
    ''' <param name="hWnd">Handle to existing window that will be parent to dialog
    ''' boxes etc.</param>
    ''' <param name="BufferSize">I/O buffer size to use when reading source disk. This
    ''' parameter is optional, if it is zero the buffer size to use
    ''' will automatically choosen.</param>
    ''' <param name="IsCdRomType">If this parameter is TRUE and the source device type cannot
    ''' be automatically determined this function will ask user for
    ''' a .iso suffixed image file name.</param>
    Public Sub SaveImageFileInteractive(ByVal hWnd As IntPtr, ByVal BufferSize As UInt32, ByVal IsCdRomType As Boolean)

      DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), hWnd, BufferSize, IsCdRomType)

    End Sub

    ''' <summary>
    ''' This function saves the contents of a device to an image file. This is a
    ''' user-interactive function that displays dialog boxes where user can select
    ''' image file and other options.
    ''' </summary>
    ''' <param name="hWnd">Handle to existing window that will be parent to dialog
    ''' boxes etc.</param>
    ''' <param name="IsCdRomType">If this parameter is TRUE and the source device type cannot
    ''' be automatically determined this function will ask user for
    ''' a .iso suffixed image file name.</param>
    <ComVisible(False)>
    Public Sub SaveImageFileInteractive(ByVal hWnd As IntPtr, ByVal IsCdRomType As Boolean)

      DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), hWnd, 0, IsCdRomType)

    End Sub

    ''' <summary>
    ''' This function saves the contents of a device to an image file. This is a
    ''' user-interactive function that displays dialog boxes where user can select
    ''' image file and other options.
    ''' </summary>
    ''' <param name="hWnd">Handle to existing window that will be parent to dialog
    ''' boxes etc.</param>
    ''' <param name="BufferSize">I/O buffer size to use when reading source disk. This
    ''' parameter is optional, if it is zero the buffer size to use
    ''' will automatically choosen.</param>
    <ComVisible(False)>
    Public Sub SaveImageFileInteractive(ByVal hWnd As IntPtr, ByVal BufferSize As UInt32)

      DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), hWnd, BufferSize, False)

    End Sub

    ''' <summary>
    ''' This function saves the contents of a device to an image file. This is a
    ''' user-interactive function that displays dialog boxes where user can select
    ''' image file and other options.
    ''' </summary>
    ''' <param name="hWnd">Handle to existing window that will be parent to dialog
    ''' boxes etc.</param>
    <ComVisible(False)>
    Public Sub SaveImageFileInteractive(ByVal hWnd As IntPtr)

      DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), hWnd, 0, False)

    End Sub

    ''' <summary>
    ''' This function saves the contents of a device to an image file. This is a
    ''' user-interactive function that displays dialog boxes where user can select
    ''' image file and other options.
    ''' </summary>
    ''' <param name="IsCdRomType">If this parameter is TRUE and the source device type cannot
    ''' be automatically determined this function will ask user for
    ''' a .iso suffixed image file name.</param>
    <ComVisible(False)>
    Public Sub SaveImageFileInteractive(ByVal IsCdRomType As Boolean)

      DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), IntPtr.Zero, 0, IsCdRomType)

    End Sub

    ''' <summary>
    ''' This function saves the contents of a device to an image file. This is a
    ''' user-interactive function that displays dialog boxes where user can select
    ''' image file and other options.
    ''' </summary>
    <ComVisible(False)>
    Public Sub SaveImageFileInteractive()

      DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), IntPtr.Zero, 0, False)

    End Sub

    ''' <summary>
    ''' Forcefully removes ImDisk virtual disk from system even if it is use by other applications.
    ''' </summary>
    Public Overloads Sub ForceRemoveDevice()

      If DLL.ImDiskForceRemoveDevice(SafeFileHandle.DangerousGetHandle(), 0) <> 0 Then
        Return
      Else
        Marshal.ThrowExceptionForHR(Marshal.GetHRForLastWin32Error())
      End If

    End Sub

  End Class

End Namespace
