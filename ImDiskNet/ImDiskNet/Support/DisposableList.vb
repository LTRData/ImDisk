Imports System.Diagnostics.CodeAnalysis
Imports System.Runtime.InteropServices

''' <summary>
''' A System.Collections.Generic.List(Of T) extended with IDisposable implementation that disposes each
''' object in the list when the list is disposed.
''' </summary>
''' <typeparam name="T"></typeparam>
<ComVisible(False)>
Public Class DisposableList(Of T As IDisposable)
    Inherits List(Of T)

    Implements IDisposable

    ' IDisposable
    Protected Overridable Sub Dispose(disposing As Boolean)
        If disposing Then
            ' TODO: free managed resources when explicitly called
            For Each obj In Me
                obj.Dispose()
            Next
        End If

        ' TODO: free shared unmanaged resources
        Clear()

    End Sub

    ' This code added by Visual Basic to correctly implement the disposable pattern.
    Public Sub Dispose() Implements IDisposable.Dispose
        ' Do not change this code.  Put cleanup code in Dispose(disposing As Boolean) above.
        Dispose(True)
        GC.SuppressFinalize(Me)
    End Sub

    Protected Overrides Sub Finalize()
        Dispose(False)
        MyBase.Finalize()
    End Sub
End Class

