Namespace Client

  ''' <summary>
  ''' Derives DevioStream and implements client side of Devio tcp/ip based communication
  ''' proxy.
  ''' </summary>
  Public Class DevioTcpStream
    Inherits DevioStream

    Protected ReadOnly Connection As NetworkStream

    Protected ReadOnly Reader As BinaryReader

    Protected ReadOnly Writer As New BinaryWriter(New MemoryStream, Encoding.Unicode)

    ''' <summary>
    ''' Creates a new instance by opening an tcp/ip connection to specified host and port
    ''' and starts communication with a Devio service using this connection.
    ''' </summary>
    ''' <param name="name">Host name and port where service is listening for incoming
    ''' connection. This can be on the form hostname:port or just hostname where default
    ''' port number 9000 will be used. The hostname part can be either an IP address or a
    ''' host name.</param>
    ''' <param name="read_only">Specifies if communication should be read-only.</param>
    ''' <returns>Returns new instance of DevioTcpStream.</returns>
    Public Shared Function Open(name As String, read_only As Boolean) As DevioTcpStream

      Return New DevioTcpStream(name, read_only)

    End Function

    ''' <summary>
    ''' Creates a new instance by opening an tcp/ip connection to specified host and port
    ''' and starts communication with a Devio service using this connection.
    ''' </summary>
    ''' <param name="name">Host name and port where service is listening for incoming
    ''' connection. This can be on the form hostname:port or just hostname where default
    ''' port number 9000 will be used. The hostname part can be either an IP address or a
    ''' host name.</param>
    ''' <param name="read_only">Specifies if communication should be read-only.</param>
    Public Sub New(name As String, read_only As Boolean)
      MyBase.New(name, read_only)

      Try
        Dim spl = ObjectName.Split({":"}, StringSplitOptions.RemoveEmptyEntries)
        Dim server = spl(0)
        Dim port = 9000
        If spl.Length >= 2 Then
          port = Integer.Parse(spl(1))
        End If

        Dim Socket As New Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp)
        Socket.Connect(server, port)
        Connection = New NetworkStream(Socket, ownsSocket:=True)

        Reader = New BinaryReader(Connection, Encoding.Unicode)

        Writer.Write(IMDPROXY_REQ.IMDPROXY_REQ_INFO)
        Writer.Flush()
        With DirectCast(Writer.BaseStream, MemoryStream)
          .WriteTo(Connection)
          .SetLength(0)
          .Position = 0
        End With
        Connection.Flush()

        Size = Reader.ReadInt64()
        Alignment = Reader.ReadInt64()
        Flags = Flags Or CType(Reader.ReadInt64(), IMDPROXY_FLAGS)

      Catch
        Dispose()
        Throw

      End Try

    End Sub

    Public Overrides Sub Close()
      MyBase.Close()

      For Each obj In New IDisposable() {Writer, Reader, Connection}
        Try
          If obj IsNot Nothing Then
            obj.Dispose()
          End If

        Catch

        End Try
      Next
    End Sub

    Public Overrides Function Read(buffer As Byte(), offset As Integer, count As Integer) As Integer

      Writer.Write(IMDPROXY_REQ.IMDPROXY_REQ_READ)
      Writer.Write(Position)
      Writer.Write(CLng(count))
      Writer.Flush()
      With DirectCast(Writer.BaseStream, MemoryStream)
        .WriteTo(Connection)
        .SetLength(0)
        .Position = 0
      End With
      Connection.Flush()

      Dim ErrorCode = Reader.ReadUInt64()
      If ErrorCode <> 0 Then
        Throw New EndOfStreamException("Read error: " & ErrorCode)
      End If
      Dim Length = CInt(Reader.ReadInt64())

      Length = Reader.Read(buffer, offset, Length)
      Position += Length

      Return Length

    End Function

    Public Overrides Sub Write(buffer As Byte(), offset As Integer, count As Integer)

      Writer.Write(IMDPROXY_REQ.IMDPROXY_REQ_WRITE)
      Writer.Write(Position)
      Writer.Write(CLng(count))
      Writer.Write(buffer, offset, count)
      Writer.Flush()
      With DirectCast(Writer.BaseStream, MemoryStream)
        .WriteTo(Connection)
        .SetLength(0)
        .Position = 0
      End With
      Connection.Flush()

      Dim ErrorCode = Reader.ReadUInt64()
      If ErrorCode <> 0 Then
        Throw New EndOfStreamException("Write error: " & ErrorCode)
      End If
      Dim Length = Reader.ReadInt64()
      Position += Length

      If Length <> count Then
        Throw New EndOfStreamException("Write length mismatch. Wrote " & Length & " of " & count & " bytes.")
      End If

    End Sub

  End Class

End Namespace
