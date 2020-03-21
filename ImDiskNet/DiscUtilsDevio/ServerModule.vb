Module ServerModule

  <MTAThread()>
  Sub Main(args As String())

    Try
      SafeMain(args)

    Catch ex As AbandonedMutexException
      Console.WriteLine("Unexpected client exit.")

    Catch ex As Exception
      Console.WriteLine(ex.ToString())

    End Try

    If Debugger.IsAttached Then
      Console.ReadKey()
    End If

  End Sub

  Sub SafeMain(args As String())

    Dim DeviceName As String = Nothing
    Dim ObjectName As String = Nothing
    Dim ListenAddress As IPAddress = IPAddress.Any
    Dim ListenPort As Integer
    Dim BufferSize As Long = DevioShmService.DefaultBufferSize
    Dim DiskAccess As FileAccess = FileAccess.ReadWrite
    Dim PartitionNumber As Integer? = Nothing
    Dim Mount As Boolean = False
    Dim MountPoint As String = Nothing
    Dim ShowHelp As Boolean = False

    For Each arg In args
      If arg.StartsWith("/name=", StringComparison.InvariantCultureIgnoreCase) Then
        ObjectName = arg.Substring("/name=".Length)
      ElseIf arg.StartsWith("/ipaddress=", StringComparison.InvariantCultureIgnoreCase) Then
        ListenAddress = IPAddress.Parse(arg.Substring("/ipaddress=".Length))
      ElseIf arg.StartsWith("/port=", StringComparison.InvariantCultureIgnoreCase) Then
        ListenPort = Integer.Parse(arg.Substring("/port=".Length))
      ElseIf arg.StartsWith("/buffersize=", StringComparison.InvariantCultureIgnoreCase) Then
        BufferSize = Long.Parse(arg.Substring("/buffersize=".Length))
      ElseIf arg.StartsWith("/partition=", StringComparison.InvariantCultureIgnoreCase) Then
        PartitionNumber = Integer.Parse(arg.Substring("/partition=".Length))
      ElseIf arg.StartsWith("/filename=", StringComparison.InvariantCultureIgnoreCase) Then
        DeviceName = arg.Substring("/filename=".Length)
      ElseIf arg.Equals("/readonly", StringComparison.InvariantCultureIgnoreCase) Then
        DiskAccess = FileAccess.Read
      ElseIf arg.Equals("/mount", StringComparison.InvariantCultureIgnoreCase) Then
        Mount = True
      ElseIf arg.StartsWith("/mount=", StringComparison.InvariantCultureIgnoreCase) Then
        Mount = True
        MountPoint = arg.Substring("/mount=".Length)
      ElseIf arg = "/?" OrElse arg.Equals("/help", StringComparison.InvariantCultureIgnoreCase) Then
        ShowHelp = True
        Exit For
      Else
        Console.WriteLine("Unsupported switch: " & arg)
        ShowHelp = True
        Exit For
      End If
    Next

    If _
      ShowHelp OrElse
      String.IsNullOrEmpty(DeviceName) Then

      Console.WriteLine("Syntax:" & Environment.NewLine &
                        "DiscUtilsDevio /name=objectname [/buffersize=bytes] [/partition=number]" & Environment.NewLine &
                        "    /filename=imagefilename [/readonly] [/mount[=d:]]" & Environment.NewLine &
                        Environment.NewLine &
                        "DiscUtilsDevio [/ipaddress=address] /port=tcpport [/partition=number]" & Environment.NewLine &
                        "    /filename=imagefilename [/readonly] [/mount[=d:]]")

      Return

    End If

    Console.WriteLine("Opening image " & DeviceName)

    Dim Device = VirtualDisk.OpenDisk(DeviceName, DiskAccess)

    If Device Is Nothing Then
      Console.WriteLine("Error opening " & DeviceName)
      Return
    End If

    Console.WriteLine("Image virtual size is " & Device.Capacity & " bytes")
    If Device.Geometry Is Nothing Then
      Console.WriteLine("Image sector size is unknown")
    Else
      Console.WriteLine("Image sector size is " & Device.Geometry.BytesPerSector & " bytes")
    End If

    Dim DiskStream As Stream

    If PartitionNumber.HasValue = False Then
      If Device.IsPartitioned = True Then
        PartitionNumber = 1
        Console.WriteLine("Partition table found.")
      Else
        PartitionNumber = 0
        Console.WriteLine("Partition table not found.")
      End If
    End If

    If PartitionNumber = 0 Then
      DiskStream = Device.Content
      Console.WriteLine("Using entire image")
    Else
      If Device.IsPartitioned = False Then
        Console.WriteLine("Partition table not found in image.")
        Return
      End If

      If PartitionNumber.Value > Device.Partitions.Count Then
        Console.WriteLine("Partition " & PartitionNumber & " not found.")
        Return
      End If

      DiskStream = Device.Partitions(PartitionNumber.Value - 1).Open()
      Console.WriteLine("Using partition " & PartitionNumber)
    End If
    Console.WriteLine("Used size is " & DiskStream.Length & " bytes")

    Dim Service As DevioServiceBase
    Dim Provider As New DevioProviderFromStream(DiskStream, ownsStream:=True)

    If Not String.IsNullOrEmpty(ObjectName) Then

      Service = New DevioShmService(ObjectName, Provider, OwnsProvider:=True, BufferSize:=BufferSize)

    ElseIf Not ListenPort <> 0 Then

      Service = New DevioTcpService(ListenAddress, ListenPort, Provider, OwnsProvider:=True)

    ElseIf Mount Then

      Service = New DevioShmService(Provider, OwnsProvider:=True, BufferSize:=BufferSize)

    Else

      Provider.Dispose()
      Console.WriteLine("Shared memory object name or TCP/IP port must be specified.")
      Return

    End If

    If Mount Then
      Service.StartServiceThreadAndMountImDisk(ImDiskFlags.Auto, MountPoint)
    Else
      Service.StartServiceThread()
    End If

    Service.WaitForServiceThreadExit()

  End Sub

End Module
