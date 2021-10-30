Imports System.Threading
Imports System.Security.AccessControl

Namespace ImDisk

    Public Class ImDiskRefreshEvent
        Inherits WaitHandle

        Public Sub New(InheritHandle As Boolean)
            SafeWaitHandle = UnsafeNativeMethods.ImDiskOpenRefreshEvent(InheritHandle)
        End Sub

        ''' <summary>
        ''' Notifies other applications that ImDisk drive list has changed. This
        ''' simulates the same action done by the driver after such changes.
        ''' </summary>
        Public Sub Notify()
            NativeFileIO.Win32Try(NativeFileIO.UnsafeNativeMethods.PulseEvent(SafeWaitHandle))
        End Sub

    End Class

End Namespace
