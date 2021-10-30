Imports System.IO
Imports System.Net
Imports System.Reflection
Imports System.Threading
Imports DiscUtils
Imports DiscUtils.Setup
Imports LTR.IO.ImDisk.Devio.Server.Providers
Imports LTR.IO.ImDisk.Devio.Server.Services

Public Module ServerModule

    Private ReadOnly asms As New List(Of Assembly) From {
        GetType(Dmg.Disk).Assembly,
        GetType(Vdi.Disk).Assembly,
        GetType(Vmdk.Disk).Assembly,
        GetType(Vhd.Disk).Assembly,
        GetType(Vhdx.Disk).Assembly,
        GetType(Xva.Disk).Assembly
    }

    <MTAThread>
    Friend Sub Main(args As String())

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

    Public Sub SafeMain(ParamArray args As String())

        Dim device_name As String = Nothing
        Dim object_name As String = Nothing
        Dim listen_address As IPAddress = IPAddress.Any
        Dim listen_port As Integer
        Dim buffer_size As Long = DevioShmService.DefaultBufferSize
        Dim disk_access As FileAccess = FileAccess.ReadWrite
        Dim partition_number As Integer? = Nothing
        Dim mount As Boolean = False
        Dim mount_point As String = Nothing
        Dim show_help As Boolean = False

        For Each arg In args
            If arg.StartsWith("/name=", StringComparison.OrdinalIgnoreCase) Then
                object_name = arg.Substring("/name=".Length)
            ElseIf arg.StartsWith("/ipaddress=", StringComparison.OrdinalIgnoreCase) Then
                listen_address = IPAddress.Parse(arg.Substring("/ipaddress=".Length))
            ElseIf arg.StartsWith("/port=", StringComparison.OrdinalIgnoreCase) Then
                listen_port = Integer.Parse(arg.Substring("/port=".Length), Globalization.NumberFormatInfo.InvariantInfo)
            ElseIf arg.StartsWith("/buffersize=", StringComparison.OrdinalIgnoreCase) Then
                buffer_size = Long.Parse(arg.Substring("/buffersize=".Length), Globalization.NumberFormatInfo.InvariantInfo)
            ElseIf arg.StartsWith("/partition=", StringComparison.OrdinalIgnoreCase) Then
                partition_number = Integer.Parse(arg.Substring("/partition=".Length), Globalization.NumberFormatInfo.InvariantInfo)
            ElseIf arg.StartsWith("/filename=", StringComparison.OrdinalIgnoreCase) Then
                device_name = arg.Substring("/filename=".Length)
            ElseIf arg.Equals("/readonly", StringComparison.OrdinalIgnoreCase) Then
                disk_access = FileAccess.Read
            ElseIf arg.Equals("/mount", StringComparison.OrdinalIgnoreCase) Then
                mount = True
            ElseIf arg.Equals("/trace", StringComparison.OrdinalIgnoreCase) Then
                Trace.Listeners.Add(New ConsoleTraceListener)
            ElseIf arg.StartsWith("/mount=", StringComparison.OrdinalIgnoreCase) Then
                mount = True
                mount_point = arg.Substring("/mount=".Length)
            ElseIf arg.StartsWith("/asm=", StringComparison.OrdinalIgnoreCase) Then
                Dim asmname = AssemblyName.GetAssemblyName(arg.Substring("/asm=".Length))
                asms.Add(Assembly.Load(asmname))
            ElseIf arg.Equals("/?", StringComparison.Ordinal) OrElse arg.Equals("/help", StringComparison.OrdinalIgnoreCase) Then
                show_help = True
                Exit For
            Else
                Console.WriteLine("Unsupported switch: " & arg)
                show_help = True
                Exit For
            End If
        Next

        If _
          show_help OrElse
          String.IsNullOrEmpty(device_name) Then

            Dim msg = "Syntax:
DiscUtilsDevio /name=objectname [/buffersize=bytes] [/partition=number] /filename=imagefilename [/readonly] [/mount[=d:]] [/trace]

DiscUtilsDevio [/name=objectname] [/buffersize=bytes] [/partition=number] /filename=imagefilename [/readonly] /mount[=d:] [/trace]

DiscUtilsDevio [/ipaddress=address] /port=tcpport [/partition=number] /filename=imagefilename [/readonly] [/mount[=d:]] [/trace]

You can additionally use the /asm=path switch to load an additional DiscUtils compatible assembly file that provides support for more virtual disk formats."

            msg = LineFormat(msg, 4)

            Console.WriteLine(msg)

            Return

        End If

        For Each asm In asms.Distinct()

            Trace.WriteLine($"Registering assembly {asm.GetName().Name}...")

            SetupHelper.RegisterAssembly(asm)

        Next

        Console.WriteLine($"Opening image {device_name}")

        Using device = VirtualDisk.OpenDisk(device_name, disk_access)

            If device Is Nothing Then

                Console.WriteLine($"Image not recognized by DiscUtils.

Formats currently supported: {String.Join(", ", VirtualDiskManager.SupportedDiskTypes.ToArray())}")

                Return

            End If

            Dim partition_table As Partitions.PartitionTable = Nothing

            Console.WriteLine($"Image type class: {device.GetType()}")

            If device.IsPartitioned Then
                partition_table = device.Partitions
            End If

            If partition_table Is Nothing Then
                Console.WriteLine("Unknown partition table format or partition table not found.")
            Else
                Console.WriteLine($"Partition table class: {partition_table.GetType()}")
            End If

            Console.WriteLine($"Image virtual size is {device.Capacity} bytes")

            If device.Geometry Is Nothing Then
                Console.WriteLine("Image sector size is unknown")
            Else
                Console.WriteLine($"Image sector size is {device.Geometry.BytesPerSector} bytes")
            End If

            Dim disk_stream As Stream

            If partition_number.HasValue = False Then
                If partition_table IsNot Nothing Then
                    partition_number = 1
                    Console.WriteLine("Partition table found.")
                Else
                    partition_number = 0
                    Console.WriteLine("Partition table not found.")
                End If
            End If

            If partition_number = 0 Then
                If device.Content Is Nothing Then
                    Console.WriteLine($"Raw image access for this format is not supported by DiscUtils.

Formats currently supported: {String.Join(", ", VirtualDiskManager.SupportedDiskTypes.ToArray())}")
                    Return
                End If

                disk_stream = device.Content

                Console.WriteLine($"Using entire image file: {device_name}")
            Else
                If partition_table Is Nothing Then
                    Console.WriteLine("Partition table not found in image.")
                    Return
                End If

                If partition_number > partition_table.Count Then
                    Console.WriteLine($"Partition {partition_number} not defined in partition table.")
                    Return
                End If

                disk_stream = partition_table(partition_number.Value - 1).Open()

                Console.WriteLine($"Using partition {partition_number}")
            End If

            Console.WriteLine($"Used size is {disk_stream.Length} bytes")

            If disk_stream.CanWrite Then
                Console.WriteLine("Read/write mode.")
            Else
                Console.WriteLine("Read-only mode.")
            End If

            Dim service As DevioServiceBase

            Using provider As New DevioProviderFromStream(disk_stream, ownsStream:=True)

                If Not String.IsNullOrEmpty(object_name) Then

                    service = New DevioShmService(object_name, provider, OwnsProvider:=True, BufferSize:=buffer_size)

                ElseIf listen_port <> 0 Then

                    service = New DevioTcpService(listen_address, listen_port, provider, OwnsProvider:=True)

                ElseIf mount Then

                    service = New DevioShmService(provider, OwnsProvider:=True, BufferSize:=buffer_size)

                Else

                    provider.Dispose()

                    Console.WriteLine("Shared memory object name or TCP/IP port must be specified.")
                    Return

                End If

                Using service

                    If mount Then
                        If "#:".Equals(mount_point, StringComparison.Ordinal) Then
                            Dim drive_letter = ImDiskAPI.FindFreeDriveLetter()
                            If drive_letter = Nothing Then
                                Console.Error.WriteLine("No drive letter available")
                                Return
                            End If
                            mount_point = {drive_letter, ":"c}
                            Console.WriteLine($"Selected {mount_point} as drive letter mount point")
                        End If
                        Console.WriteLine("Opening image file and mounting as virtual disk...")
                        service.StartServiceThreadAndMountImDisk(ImDiskFlags.Auto, mount_point)
                        Console.WriteLine("Virtual disk created. Press Ctrl+C to remove virtual disk and exit.")
                    Else
                        Console.WriteLine("Opening image file...")
                        service.StartServiceThread()
                        Console.WriteLine("Image file opened, waiting for incoming connections. Press Ctrl+C to exit.")
                    End If

                    AddHandler Console.CancelKeyPress,
                Sub(sender, e)
                    ThreadPool.QueueUserWorkItem(
                    Sub()

                        If Not Monitor.TryEnter(break_lock) Then
                            Return
                        End If

                        Try
                            Console.WriteLine("Stopping service...")
                            service.Dispose()

                        Finally
                            Monitor.Exit(break_lock)

                        End Try
                    End Sub)

                    Try
                        e.Cancel = True
                    Catch
                    End Try

                End Sub

                    service.WaitForServiceThreadExit()

                    Console.WriteLine("Service stopped.")

                End Using

            End Using

        End Using

    End Sub

    Private ReadOnly break_lock As New Object

End Module
