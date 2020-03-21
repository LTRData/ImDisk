Namespace ImDisk

  ''' <summary>
  ''' Represents ImDisk Virtual Disk Driver control device object.
  ''' </summary>
  <ComVisible(False)>
  Public Class ImDiskControl
    Inherits ImDiskObject

    ''' <summary>
    ''' Creates a new instance and opens ImDisk Virtual Disk Driver control device object.
    ''' </summary>
    Public Sub New()
      MyBase.New("\\?\ImDiskCtl", AccessMode:=0)

    End Sub

  End Class

End Namespace