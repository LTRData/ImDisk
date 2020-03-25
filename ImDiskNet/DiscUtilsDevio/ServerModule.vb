Imports DiscUtils

Module ServerModule

    <MTAThread>
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
            ElseIf arg.Equals("/trace", StringComparison.InvariantCultureIgnoreCase) Then
                Trace.Listeners.Add(New ConsoleTraceListener)
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
                              "    /filename=imagefilename [/readonly] [/mount[=d:]] [/trace]" & Environment.NewLine &
                              Environment.NewLine &
                              "DiscUtilsDevio [/name=objectname] [/buffersize=bytes] [/partition=number]" & Environment.NewLine &
                              "    /filename=imagefilename [/readonly] /mount[=d:] [/trace]" & Environment.NewLine &
                              Environment.NewLine &
                              "DiscUtilsDevio [/ipaddress=address] /port=tcpport [/partition=number]" & Environment.NewLine &
                              "    /filename=imagefilename [/readonly] [/mount[=d:]] [/trace]")

            Return

        End If

        Console.WriteLine("Opening image " & DeviceName)

        Dim Device = VirtualDisk.OpenDisk(DeviceName, DiskAccess)

        If Device Is Nothing Then
            Dim fs As New FileStream(DeviceName, FileMode.Open, DiskAccess, FileShare.Read Or FileShare.Delete)
            Try
                Device = New Dmg.Disk(fs, Ownership.Dispose)
            Catch
                fs.Dispose()
            End Try
        End If

        If Device Is Nothing Then
            Console.WriteLine("Image not recognized by DiscUtils." & Environment.NewLine &
                              Environment.NewLine &
                              "Formats currently supported: " & String.Join(", ", VirtualDisk.SupportedDiskTypes.ToArray()),
                              "Error")
            Return
        End If
        Dim Table As Partitions.PartitionTable = Nothing
        Console.WriteLine("Image type class: " & Device.GetType().ToString())
        If Device.IsPartitioned Then
            Table = Device.Partitions
        End If
        If Table Is Nothing Then
            Console.WriteLine("Unknown partition table format or partition table not found.")
        Else
            Console.WriteLine("Partition table class: " & Table.GetType().ToString())
        End If

        Console.WriteLine("Image virtual size is " & Device.Capacity & " bytes")
        If Device.Geometry Is Nothing Then
            Console.WriteLine("Image sector size is unknown")
        Else
            Console.WriteLine("Image sector size is " & Device.Geometry.BytesPerSector & " bytes")
        End If

        Dim DiskStream As Stream

        If PartitionNumber.HasValue = False Then
            If Table IsNot Nothing Then
                PartitionNumber = 1
                Console.WriteLine("Partition table found.")
            Else
                PartitionNumber = 0
                Console.WriteLine("Partition table not found.")
            End If
        End If

        If PartitionNumber = 0 Then
            If Device IsNot Nothing Then
                DiskStream = Device.Content
            Else
                Console.WriteLine("Raw image access for this format is not supported by DiscUtils." & Environment.NewLine &
                                  Environment.NewLine &
                                  "Formats currently supported: " & String.Join(", ", VirtualDisk.SupportedDiskTypes.ToArray()))
                Return
            End If
            Console.WriteLine("Using entire image file: " & DeviceName)
        Else
            If Table Is Nothing Then
                Console.WriteLine("Partition table not found in image.")
                Return
            End If
            If PartitionNumber > Table.Count Then
                Console.WriteLine("Partition " & PartitionNumber & " not defined in partition table.")
                Return
            End If

            DiskStream = Table(PartitionNumber.Value - 1).Open()
            Console.WriteLine("Using partition " & PartitionNumber)
        End If
        Console.WriteLine("Used size is " & DiskStream.Length & " bytes")
        If DiskStream.CanWrite Then
            Console.WriteLine("Read/write mode.")
        Else
            Console.WriteLine("Read-only mode.")
        End If

        Dim Service As DevioServiceBase
        Dim Provider As New DevioProviderFromStream(DiskStream, ownsStream:=True)

        If Not String.IsNullOrEmpty(ObjectName) Then

            Service = New DevioShmService(ObjectName, Provider, OwnsProvider:=True, BufferSize:=BufferSize)

        ElseIf ListenPort <> 0 Then

            Service = New DevioTcpService(ListenAddress, ListenPort, Provider, OwnsProvider:=True)

        ElseIf Mount Then

            Service = New DevioShmService(Provider, OwnsProvider:=True, BufferSize:=BufferSize)

        Else

            Provider.Dispose()
            Console.WriteLine("Shared memory object name or TCP/IP port must be specified.")
            Return

        End If

        If Mount Then
            Console.WriteLine("Opening image file and mounting as virtual disk...")
            Service.StartServiceThreadAndMountImDisk(ImDiskFlags.Auto, MountPoint)
            Console.WriteLine("Virtual disk created. Press Ctrl+C to remove virtual disk and exit.")
        Else
            Console.WriteLine("Opening image file...")
            Service.StartServiceThread()
            Console.WriteLine("Image file opened, waiting for incoming connections. Press Ctrl+C to exit.")
        End If

        AddHandler Console.CancelKeyPress,
            Sub(sender, e)
                Console.WriteLine("Stopping service...")
                Service.Dispose()

                Try
                    e.Cancel = True
                Catch
                End Try
            End Sub

        Service.WaitForServiceThreadExit()

        Console.WriteLine("Service stopped.")

    End Sub

End Module
