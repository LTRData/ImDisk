Namespace ImDisk

  ''' <summary>
  ''' A FileStream derived class that represents disk devices by overriding properties and methods
  ''' where FileStream base implementation rely on file API not directly compatible with disk device
  ''' objects.
  ''' </summary>
  <Guid("f7a573b1-756d-42df-a0af-139fc007f66c")>
  <ClassInterface(ClassInterfaceType.AutoDual)>
  Public Class ImDiskDeviceStream
    Inherits FileStream

    ''' <summary>
    ''' Initializes an ImDiskDeviceStream object for an open disk device.
    ''' </summary>
    ''' <param name="SafeFileHandle">Open file handle for disk device.</param>
    ''' <param name="AccessMode">Access to request for stream.</param>
    Public Sub New(SafeFileHandle As SafeFileHandle, AccessMode As FileAccess)
      MyBase.New(SafeFileHandle, AccessMode)
    End Sub

    ''' <summary>
    ''' Retrieves raw disk size.
    ''' </summary>
    Public Overrides ReadOnly Property Length As Long
      Get
        Dim Size As Int64
        NativeFileIO.Win32Try(DLL.ImDiskGetVolumeSize(SafeFileHandle, Size))
        Return Size
      End Get
    End Property

    ''' <summary>
    ''' Not implemented.
    ''' </summary>
    Public Overrides Sub SetLength(value As Long)
      Throw New NotImplementedException
    End Sub

  End Class

End Namespace

