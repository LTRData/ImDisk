Namespace IO.ImDisk

  ''' <summary>
  ''' Base class that represents ImDisk Virtual Disk Driver created device objects.
  ''' </summary>
  <Guid("797bbe31-0d5a-4225-8139-385539c5ce7e")>
  <ClassInterface(ClassInterfaceType.AutoDual)>
  Public Class ImDiskObject
    Implements IDisposable

    Public ReadOnly SafeFileHandle As SafeFileHandle
    Public ReadOnly AccessMode As FileAccess

    Protected Sub New(Path As String, AccessMode As FileAccess)
      Me.New(NativeFileIO.OpenFileHandle(Path, AccessMode, FileShare.Read, FileMode.Open, Overlapped:=False), AccessMode)
    End Sub

    Protected Sub New(Handle As SafeFileHandle, Access As FileAccess)
      SafeFileHandle = Handle
      AccessMode = Access
    End Sub

    ''' <summary>
    ''' Checks if version of running ImDisk Virtual Disk Driver servicing this device object is compatible with this API
    ''' library. If this device object is not created by ImDisk Virtual Disk Driver this method returns False.
    ''' </summary>
    Public Function CheckDriverVersion() As Boolean

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        If DLL.ImDiskCheckDriverVersion(SafeFileHandle.DangerousGetHandle()) <> 0 Then
          Return True
        Else
          Return False
        End If

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Function

#Region "IDisposable Support"
    Private disposedValue As Boolean ' To detect redundant calls

    ' IDisposable
    Protected Overridable Sub Dispose(disposing As Boolean)
      If Not Me.disposedValue Then
        If disposing Then
          ' TODO: dispose managed state (managed objects).
        End If

        ' TODO: free unmanaged resources (unmanaged objects) and override Finalize() below.
        SafeFileHandle.Dispose()

        ' TODO: set large fields to null.
      End If
      Me.disposedValue = True
    End Sub

    ' TODO: override Finalize() only if Dispose(disposing As Boolean) above has code to free unmanaged resources.
    Protected Overrides Sub Finalize()
      ' Do not change this code.  Put cleanup code in Dispose(disposing As Boolean) above.
      Dispose(False)
      MyBase.Finalize()
    End Sub

    ' This code added by Visual Basic to correctly implement the disposable pattern.
    Public Sub Dispose() Implements IDisposable.Dispose
      ' Do not change this code.  Put cleanup code in Dispose(disposing As Boolean) above.
      Dispose(True)
      GC.SuppressFinalize(Me)
    End Sub
#End Region

  End Class

End Namespace