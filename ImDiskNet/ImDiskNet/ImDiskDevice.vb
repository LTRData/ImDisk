Imports System.ComponentModel

Namespace IO.ImDisk

  ''' <summary>
  ''' Represents ImDisk Virtual Disk Driver disk device objects.
  ''' </summary>
  <Guid("e1edf6ff-7b7a-49d7-9943-c30812dcf9b1")>
  <ClassInterface(ClassInterfaceType.AutoDual)>
  Public Class ImDiskDevice
    Inherits ImDiskObject

    Private RawDiskStream As FileStream

    Private Shared Function OpenDeviceHandle(DeviceNumber As UInt32, AccessMode As FileAccess) As SafeFileHandle

      Dim NativeAccessMode As UInt32 = NativeFileIO.Win32API.FILE_READ_ATTRIBUTES
      If (AccessMode And FileAccess.Read) = FileAccess.Read Then
        NativeAccessMode += NativeFileIO.Win32API.GENERIC_READ
      End If
      If (AccessMode And FileAccess.Write) = FileAccess.Write Then
        NativeAccessMode += NativeFileIO.Win32API.GENERIC_WRITE
      End If

      Dim Handle As New SafeFileHandle(DLL.ImDiskOpenDeviceByNumber(DeviceNumber, NativeAccessMode), ownsHandle:=True)
      If Handle.IsInvalid Then
        Throw New Win32Exception
      Else
        Return Handle
      End If

    End Function

    Private Shared Function OpenDeviceHandle(MountPoint As String, AccessMode As FileAccess) As SafeFileHandle

      Dim NativeAccessMode As UInt32 = NativeFileIO.Win32API.FILE_READ_ATTRIBUTES
      If (AccessMode And FileAccess.Read) = FileAccess.Read Then
        NativeAccessMode += NativeFileIO.Win32API.GENERIC_READ
      End If
      If (AccessMode And FileAccess.Write) = FileAccess.Write Then
        NativeAccessMode += NativeFileIO.Win32API.GENERIC_WRITE
      End If

      Dim Handle As New SafeFileHandle(DLL.ImDiskOpenDeviceByMountPoint(MountPoint, NativeAccessMode), ownsHandle:=True)
      If Handle.IsInvalid Then
        Throw New Win32Exception
      Else
        Return Handle
      End If

    End Function

    ''' <summary>
    ''' Creates a new instance and opens an existing ImDisk virtual disk device.
    ''' </summary>
    ''' <param name="DeviceNumber">Device number of ImDisk virtual disk to open.</param>
    ''' <param name="AccessMode">Access mode to request for accessing disk object.</param>
    Public Sub New(DeviceNumber As UInt32, AccessMode As FileAccess)
      MyBase.New(OpenDeviceHandle(DeviceNumber, AccessMode), AccessMode)

    End Sub

    ''' <summary>
    ''' Creates a new instance and opens an existing disk device.
    ''' </summary>
    ''' <param name="MountPoint">Mount point of disk device to open.</param>
    ''' <param name="AccessMode">Access mode to request for accessing disk object.</param>
    Public Sub New(MountPoint As String, AccessMode As FileAccess)
      MyBase.New(OpenDeviceHandle(MountPoint, AccessMode), AccessMode)

    End Sub

    ''' <summary>
    ''' Retrieves volume size of disk device.
    ''' </summary>
    Public ReadOnly Property DiskSize As Long
      Get
        Dim Size As Int64
        Dim HandleReferenced As Boolean
        Try
          SafeFileHandle.DangerousAddRef(HandleReferenced)
          If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
            Throw New ArgumentException("Handle is invalid")
          End If
          NativeFileIO.Win32Try(DLL.ImDiskGetVolumeSize(SafeFileHandle.DangerousGetHandle(), Size))

        Finally
          If HandleReferenced Then
            SafeFileHandle.DangerousRelease()
          End If

        End Try
        Return Size
      End Get
    End Property

    ''' <summary>
    ''' Close device object.
    ''' </summary>
    Public Sub Close()
      Dispose()
    End Sub

    ''' <summary>
    ''' Opens a FileStream object around this ImDisk device that can be used to directly access disk data.
    ''' </summary>
    ''' <returns></returns>
    ''' <remarks></remarks>
    Public Function GetRawDiskStream() As FileStream
      If RawDiskStream Is Nothing Then
        RawDiskStream = New FileStream(SafeFileHandle, AccessMode)
      End If
      Return RawDiskStream
    End Function

    ''' <summary>
    ''' Saves contents of disk device to an image file.
    ''' </summary>
    ''' <param name="ImageFile">FileStream object opened for writing where disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ImageFile As FileStream, BufferSize As UInt32)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        NativeFileIO.Win32Try(DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                                      ImageFile.SafeFileHandle.DangerousGetHandle(),
                                                      BufferSize,
                                                      IntPtr.Zero))

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file while pumping window messages between reads and writes.
    ''' </summary>
    ''' <param name="ImageFile">FileStream object opened for writing where disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    ''' <param name="CancelFlag">A boolean flag that will be checked between buffer reads/writes. If flag is set to True
    ''' operation will be cancelled and an exception thrown.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ImageFile As FileStream, BufferSize As UInt32, ByRef CancelFlag As Boolean)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        NativeFileIO.Win32Try(DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                                      ImageFile.SafeFileHandle.DangerousGetHandle(),
                                                      BufferSize,
                                                      CancelFlag))

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file.
    ''' </summary>
    ''' <param name="ImageFile">Native file handle opened for writing where disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ImageFile As IntPtr, BufferSize As UInt32)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        NativeFileIO.Win32Try(DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                                      ImageFile,
                                                      BufferSize,
                                                      IntPtr.Zero))

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file while pumping window messages between reads and writes.
    ''' </summary>
    ''' <param name="ImageFile">Native file handle opened for writing where disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    ''' <param name="CancelFlag">A boolean flag that will be checked between buffer reads/writes. If flag is set to True
    ''' operation will be cancelled and an exception thrown.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ImageFile As IntPtr, BufferSize As UInt32, ByRef CancelFlag As Boolean)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        NativeFileIO.Win32Try(DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                                      ImageFile,
                                                      BufferSize,
                                                      CancelFlag))

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file.
    ''' </summary>
    ''' <param name="ImageFile">Name of file to which disk contents will be written.</param>
    ''' <param name="BufferSize">Buffer size to use when transferring data from disk device to file.</param>
    <ComVisible(False)>
    Public Sub SaveImageFile(ImageFile As String, BufferSize As UInt32)

      Using ImageFileHandle = NativeFileIO.OpenFileHandle(ImageFile, FileAccess.Write, FileShare.None, FileMode.Create, Overlapped:=False)

        Dim HandleReferenced As Boolean
        Try
          SafeFileHandle.DangerousAddRef(HandleReferenced)
          If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
            Throw New ArgumentException("Handle is invalid")
          End If
          NativeFileIO.Win32Try(DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                                        ImageFileHandle.DangerousGetHandle(),
                                                        BufferSize,
                                                        IntPtr.Zero))

        Finally
          If HandleReferenced Then
            SafeFileHandle.DangerousRelease()
          End If

        End Try

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
    Public Sub SaveImageFile(ImageFile As String, BufferSize As UInt32, ByRef CancelFlag As Boolean)

      Using ImageFileHandle = NativeFileIO.OpenFileHandle(ImageFile, FileAccess.Write, FileShare.None, FileMode.Create, Overlapped:=False)

        Dim HandleReferenced As Boolean
        Try
          SafeFileHandle.DangerousAddRef(HandleReferenced)
          If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
            Throw New ArgumentException("Handle is invalid")
          End If
          NativeFileIO.Win32Try(DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                                        ImageFileHandle.DangerousGetHandle(),
                                                        BufferSize,
                                                        CancelFlag))

        Finally
          If HandleReferenced Then
            SafeFileHandle.DangerousRelease()
          End If

        End Try

      End Using

    End Sub

    ''' <summary>
    ''' Saves contents of disk device to an image file.
    ''' </summary>
    ''' <param name="ImageFile">Name of file to which disk contents will be written.</param>
    Public Sub SaveImageFile(ImageFile As String)

      Using ImageFileHandle = NativeFileIO.OpenFileHandle(ImageFile, FileAccess.Write, FileShare.None, FileMode.Create, Overlapped:=False)

        Dim HandleReferenced As Boolean
        Try
          SafeFileHandle.DangerousAddRef(HandleReferenced)
          If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
            Throw New ArgumentException("Handle is invalid")
          End If
          NativeFileIO.Win32Try(DLL.ImDiskSaveImageFile(SafeFileHandle.DangerousGetHandle(),
                                                        ImageFileHandle.DangerousGetHandle(),
                                                        0,
                                                        New Boolean))

        Finally
          If HandleReferenced Then
            SafeFileHandle.DangerousRelease()
          End If

        End Try

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
    Public Sub SaveImageFileInteractive(hWnd As IntPtr, BufferSize As UInt32, IsCdRomType As Boolean)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), hWnd, BufferSize, IsCdRomType)

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

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
    Public Sub SaveImageFileInteractive(hWnd As IntPtr, IsCdRomType As Boolean)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), hWnd, 0, IsCdRomType)

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

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
    Public Sub SaveImageFileInteractive(hWnd As IntPtr, BufferSize As UInt32)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), hWnd, BufferSize, False)

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    ''' <summary>
    ''' This function saves the contents of a device to an image file. This is a
    ''' user-interactive function that displays dialog boxes where user can select
    ''' image file and other options.
    ''' </summary>
    ''' <param name="hWnd">Handle to existing window that will be parent to dialog
    ''' boxes etc.</param>
    <ComVisible(False)>
    Public Sub SaveImageFileInteractive(hWnd As IntPtr)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), hWnd, 0, False)

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

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
    Public Sub SaveImageFileInteractive(IsCdRomType As Boolean)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), IntPtr.Zero, 0, IsCdRomType)

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    ''' <summary>
    ''' This function saves the contents of a device to an image file. This is a
    ''' user-interactive function that displays dialog boxes where user can select
    ''' image file and other options.
    ''' </summary>
    <ComVisible(False)>
    Public Sub SaveImageFileInteractive()

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        DLL.ImDiskSaveImageFileInteractive(SafeFileHandle.DangerousGetHandle(), IntPtr.Zero, 0, False)

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    ''' <summary>
    ''' Forcefully removes ImDisk virtual disk from system even if it is use by other applications.
    ''' </summary>
    Public Overloads Sub ForceRemoveDevice()

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        NativeFileIO.Win32Try(DLL.ImDiskForceRemoveDevice(SafeFileHandle.DangerousGetHandle(), 0))

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    Protected Overrides Sub Dispose(disposing As Boolean)
      If RawDiskStream IsNot Nothing Then
        RawDiskStream.Dispose()
        RawDiskStream = Nothing
      End If

      MyBase.Dispose(disposing)
    End Sub

  End Class

End Namespace
