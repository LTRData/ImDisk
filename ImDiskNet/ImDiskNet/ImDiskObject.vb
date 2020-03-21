Namespace IO.ImDisk

  ''' <summary>
  ''' Base class that represents ImDisk Virtual Disk Driver created device objects.
  ''' </summary>
  <ComClass(ImDiskObject.ClassId, ImDiskObject.InterfaceId, ImDiskObject.EventId)>
  Public Class ImDiskObject
    Inherits NativeFileStream

    Public Const ClassId = "dac633c4-10c9-4a35-94ad-6fc75fc46f44"
    Public Const InterfaceId = "4a23d6fa-2129-46cb-a1bb-aac50fab74ab"
    Public Const EventId = "403d914a-4234-489a-8036-3c11379f2b1c"

    Protected Sub New(ByVal Handle As SafeFileHandle, ByVal AccessMode As FileAccess)
      MyBase.New(Handle, AccessMode)

    End Sub

    Protected Sub New(ByVal Path As String, ByVal AccessMode As FileAccess)
      MyBase.New(Path, FileMode.Open, AccessMode, FileShare.Read)

    End Sub

    ''' <summary>
    ''' Checks if version of running ImDisk Virtual Disk Driver servicing this device object is compatible with this API
    ''' library. If this device object is not created by ImDisk Virtual Disk Driver this method returns False.
    ''' </summary>
    Public Function CheckDriverVersion() As Boolean

      If DLL.ImDiskCheckDriverVersion(SafeFileHandle.DangerousGetHandle()) <> 0 Then
        Return True
      Else
        Return False
      End If

    End Function

  End Class

End Namespace