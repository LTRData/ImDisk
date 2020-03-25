Imports LTR.IO.ImDisk.Devio.Server.Providers

Namespace Client

    ''' <summary>
    ''' Class that provides access to a provider through a System.IO.Stream.
    ''' </summary>
    Public Class DevioProviderStream
        Inherits Stream

        Private _Provider As IDevioProvider
        Private _OwnsProvider As Boolean

        Public Sub New(Provider As IDevioProvider, ownsProvider As Boolean)
            _Provider = Provider
            _OwnsProvider = ownsProvider
        End Sub

        Public Overrides ReadOnly Property CanRead As Boolean
            Get
                Return True
            End Get
        End Property

        Public Overrides ReadOnly Property CanSeek As Boolean
            Get
                Return True
            End Get
        End Property

        Public Overrides ReadOnly Property CanWrite As Boolean
            Get
                Return _Provider.CanWrite
            End Get
        End Property

        Public Overrides Sub Flush()

        End Sub

        Public Overrides ReadOnly Property Length As Long
            Get
                Return _Provider.Length
            End Get
        End Property

        Public Overrides Property Position As Long

        Public Overrides Function Read(buffer() As Byte, offset As Integer, count As Integer) As Integer
            Dim rc = _Provider.Read(buffer, offset, count, _Position)
            _Position += rc
            Return rc
        End Function

        Public Overrides Function Seek(offset As Long, origin As SeekOrigin) As Long
            Select Case origin
                Case SeekOrigin.Begin
                    _Position = offset
                Case SeekOrigin.Current
                    _Position += offset
                Case SeekOrigin.End
                    _Position = _Provider.Length + offset
                Case Else
                    Throw New InvalidEnumArgumentException("origin", origin, GetType(SeekOrigin))
            End Select
            Return _Position
        End Function

        Public Overrides Sub SetLength(value As Long)
            Throw New NotImplementedException
        End Sub

        Public Overrides Sub Write(buffer() As Byte, offset As Integer, count As Integer)
            Dim rc = _Provider.Write(buffer, offset, count, _Position)
            If rc <> count Then
                Throw New IOException("Partial write, " & rc & " bytes written of requested " & count & " bytes.")
            End If
        End Sub

        Public Overrides Sub Close()
            If _OwnsProvider AndAlso _Provider IsNot Nothing Then
                _Provider.Dispose()
            End If

            _Provider = Nothing

            MyBase.Close()
        End Sub
    End Class

End Namespace