Imports LTR.IO.ImDisk.Devio.Server.Providers

Namespace Server.Services

    ''' <summary>
    ''' Base class for classes that implement functionality for acting as server end of
    ''' ImDisk/Devio communication. Derived classes implement communication mechanisms and
    ''' use an object implementing <see>IDevioProvider</see> interface as storage backend
    ''' for I/O requests received from client.
    ''' </summary>
    Public MustInherit Class DevioServiceBase
        Implements IDisposable

        Private ServiceThread As Thread

        Private _DevioProvider As IDevioProvider

        ''' <summary>
        ''' IDevioProvider object used by this instance.
        ''' </summary>
        Public ReadOnly Property DevioProvider As IDevioProvider
            Get
                Return _DevioProvider
            End Get
        End Property

        ''' <summary>
        ''' Indicates whether DevioProvider will be automatically closed when this instance
        ''' is disposed.
        ''' </summary>
        Public ReadOnly OwnsProvider As Boolean

        ''' <summary>
        ''' Size of virtual disk device.
        ''' </summary>
        ''' <value>Size of virtual disk device.</value>
        ''' <returns>Size of virtual disk device.</returns>
        Public Overridable Property DiskSize As Long

        ''' <summary>
        ''' Offset in disk image where this virtual disk device begins.
        ''' </summary>
        ''' <value>Offset in disk image where this virtual disk device begins.</value>
        ''' <returns>Offset in disk image where this virtual disk device begins.</returns>
        Public Overridable Property Offset As Long

        ''' <summary>
        ''' Sector size of virtual disk device.
        ''' </summary>
        ''' <value>Sector size of virtual disk device.</value>
        ''' <returns>Sector size of virtual disk device.</returns>
        Public Overridable Property SectorSize As UInteger

        ''' <summary>
        ''' Description of service.
        ''' </summary>
        ''' <value>Description of service.</value>
        ''' <returns>Description of service.</returns>
        Public Overridable Property Description As String

        ''' <summary>
        ''' Event raised when service thread is ready to start accepting connection from a client.
        ''' </summary>
        Public Event ServiceReady As Action
        Protected Overridable Sub OnServiceReady()
            RaiseEvent ServiceReady()
        End Sub

        ''' <summary>
        ''' Event raised when service initialization fails.
        ''' </summary>
        Public Event ServiceInitFailed As Action
        Protected Overridable Sub OnServiceInitFailed()
            RaiseEvent ServiceInitFailed()
        End Sub

        ''' <summary>
        ''' Event raised when an ImDisk Virtual Disk Device is created by with this instance.
        ''' </summary>
        Public Event ImDiskCreated As Action
        Protected Overridable Sub OnImDiskCreated()
            RaiseEvent ImDiskCreated()
        End Sub

        ''' <summary>
        ''' Event raised when service thread exits.
        ''' </summary>
        Public Event ServiceShutdown As Action
        Protected Overridable Sub OnServiceShutdown()
            RaiseEvent ServiceShutdown()
        End Sub

        ''' <summary>
        ''' Event raised when an unhandled exception occurs in service thread and thread is about to terminate,
        ''' but before associated virtual disk device is forcefully removed, as specified by ForceRemoveImDiskDeviceOnCrash
        ''' property.
        ''' </summary>
        Public Event ServiceUnhandledException As UnhandledExceptionEventHandler
        Protected Overridable Sub OnServiceUnhandledException(e As UnhandledExceptionEventArgs)
            RaiseEvent ServiceUnhandledException(Me, e)
            If HasImDiskDevice AndAlso ForceRemoveImDiskDeviceOnCrash Then
                ImDiskAPI.ForceRemoveDevice(ImDiskDeviceNumber)
            End If
        End Sub

        ''' <summary>
        ''' Event raised to stop service thread. Service thread handle this event by preparing commnunication for
        ''' disconnection.
        ''' </summary>
        Protected Event StopServiceThread As Action
        Protected Overridable Sub OnStopServiceThread()
            RaiseEvent StopServiceThread()
        End Sub

        ''' <summary>
        ''' Creates a new service instance with enough data to later run a service that acts as server end in ImDisk/Devio
        ''' communication.
        ''' </summary>
        ''' <param name="DevioProvider">IDevioProvider object to that serves as storage backend for this service.</param>
        ''' <param name="OwnsProvider">Indicates whether DevioProvider object will be automatically closed when this
        ''' instance is disposed.</param>
        Protected Sub New(DevioProvider As IDevioProvider, OwnsProvider As Boolean)

            Me.OwnsProvider = OwnsProvider

            _DevioProvider = DevioProvider

            DiskSize = DevioProvider.Length

            SectorSize = DevioProvider.SectorSize

        End Sub

        ''' <summary>
        ''' Creates a device reader delegate used to directly read from device through this instance.
        ''' </summary>
        ''' <returns>A delegate that can be used to directly read from device through this instance.</returns>
        Protected Overridable Function GetDeviceReader() As DLL.ImDiskReadFileUnmanagedProc

            Return _
              Function(handle As IntPtr,
                       buffer As IntPtr,
                       offset As Int64,
                       numberOfBytesToRead As UInt32,
                       ByRef numberOfBytesRead As UInt32)

                  Try
                      numberOfBytesRead = CUInt(DevioProvider.Read(buffer, 0, CInt(numberOfBytesToRead), offset))
                      Return True

                  Catch
                      Return False

                  End Try

              End Function

        End Function

        ''' <summary>
        ''' Reads partition table and parses partition entry values into a collection of PARTITION_INFORMATION
        ''' structure objects.
        ''' </summary>
        ''' <returns>Collection of PARTITION_INFORMATION structures objects.</returns>
        Public Overridable Function GetPartitionInformation() As ReadOnlyCollection(Of NativeFileIO.Win32API.PARTITION_INFORMATION)

            Return ImDiskAPI.GetPartitionInformation(Nothing, GetDeviceReader(), SectorSize, Offset)

        End Function

        ''' <summary>
        ''' When overridden in a derived class, runs service that acts as server end in ImDisk/Devio communication. It will
        ''' first wait for a client to connect, then serve client I/O requests and when client finally requests service to
        ''' terminate, this method returns to caller. To run service in a worker thread that automatically disposes this
        ''' object after client disconnection, call StartServiceThread() instead.
        ''' </summary>
        Public MustOverride Sub RunService()

        ''' <summary>
        ''' Creates a worker thread where RunService() method is called. After that method exits, this instance is automatically
        ''' disposed.
        ''' </summary>
        Public Overridable Function StartServiceThread() As Boolean

            Using _
              ServiceReadyEvent As New EventWaitHandle(initialState:=False, mode:=EventResetMode.ManualReset),
              ServiceInitFailedEvent As New EventWaitHandle(initialState:=False, mode:=EventResetMode.ManualReset)

                Dim ServiceReadyHandler As New Action(AddressOf ServiceReadyEvent.Set)
                AddHandler ServiceReady, ServiceReadyHandler
                Dim ServiceInitFailedHandler As New Action(AddressOf ServiceInitFailedEvent.Set)
                AddHandler ServiceInitFailed, ServiceInitFailedHandler

                ServiceThread = New Thread(AddressOf ServiceThreadProcedure)
                ServiceThread.Start()
                WaitHandle.WaitAny({ServiceReadyEvent, ServiceInitFailedEvent})

                RemoveHandler ServiceReady, ServiceReadyHandler
                RemoveHandler ServiceInitFailed, ServiceInitFailedHandler

                If ServiceReadyEvent.WaitOne(0) Then
                    Return True
                Else
                    Return False
                End If

            End Using

        End Function

        Private Sub ServiceThreadProcedure()

            Try
                RunService()

            Finally
                Dispose()

            End Try

        End Sub

        ''' <summary>
        ''' Waits for service thread created by StartServiceThread() to exit. If no service thread
        ''' has been created or if it has already exit, this method returns immediately with a
        ''' value of True.
        ''' </summary>
        ''' <param name="timeout">Timeout value, or Timeout.Infinite to wait infinitely.</param>
        ''' <returns>Returns True if service thread has exit or no service thread has been
        ''' created, or False if timeout occured.</returns>
        Public Overridable Function WaitForServiceThreadExit(timeout As TimeSpan) As Boolean

            If ServiceThread IsNot Nothing AndAlso ServiceThread.IsAlive Then
                Return ServiceThread.Join(timeout)
            Else
                Return True
            End If

        End Function

        ''' <summary>
        ''' Waits for service thread created by StartServiceThread() to exit. If no service thread
        ''' has been created or if it has already exit, this method returns immediately.
        ''' </summary>
        Public Overridable Sub WaitForServiceThreadExit()

            If ServiceThread IsNot Nothing AndAlso ServiceThread.IsAlive Then
                ServiceThread.Join()
            End If

        End Sub

        ''' <summary>
        ''' Combines a call to StartServiceThread() with a call to ImDiskAPI to create a proxy type
        ''' ImDisk Virtual Disk Device that uses the started service as storage backend.
        ''' </summary>
        ''' <param name="Flags">Flags to pass to ImDiskAPI.CreateDevice() combined with fixed flag
        ''' values specific to this instance. Example of such fixed flag values are flags specifying
        ''' proxy operation and which proxy communication protocol to use, which therefore do not
        ''' need to be specified in this parameter. A common value to pass however, is ImDiskFlags.ReadOnly
        ''' to create a read-only virtual disk device.</param>
        ''' <param name="MountPoint">Optionally drive letter followed by a colon to associate a drive
        ''' letter to created virtual disk device. If this parameter is null/Nothing, the device is
        ''' created without a drive letter.</param>
        Public Overridable Sub StartServiceThreadAndMountImDisk(Flags As ImDiskFlags, MountPoint As String)

            If Not StartServiceThread() Then
                Throw New Exception("Service initialization failed.")
            End If

            Try
                ImDiskAPI.CreateDevice(DiskSize,
                                       0,
                                       0,
                                       SectorSize,
                                       Offset,
                                       Flags Or ImDiskAdditionalFlags Or ImDiskProxyModeFlags,
                                       ImDiskProxyObjectName,
                                       False,
                                       MountPoint,
                                       _ImDiskDeviceNumber,
                                       Nothing)

                OnImDiskCreated()

            Catch
                OnStopServiceThread()
                Throw

            End Try

        End Sub

        ''' <summary>
        ''' Dismounts an ImDisk Virtual Disk Device created by StartServiceThreadAndMountImDisk() and waits
        ''' for service thread of this instance to exit.
        ''' </summary>
        Public Overridable Sub DismountImDiskAndStopServiceThread()

            Dim i = 1
            Do
                Try
                    ImDiskAPI.RemoveDevice(_ImDiskDeviceNumber)
                    Exit Do

                Catch ex As Win32Exception When (
                  i < 40 AndAlso
                  ex.NativeErrorCode = NativeFileIO.Win32API.ERROR_ACCESS_DENIED)

                    i += 1
                    Thread.Sleep(100)
                    Continue Do

                End Try
            Loop

            WaitForServiceThreadExit()

        End Sub

        ''' <summary>
        ''' Additional flags that will be passed to ImDiskAPI.CreateDevice() in StartServiceThreadAndMountImDisk()
        ''' method. Default value of this property depends on derived class and which parameters are normally
        ''' needed for ImDisk to start communication with this service.
        ''' </summary>
        ''' <value>Default value of this property depends on derived class and which parameters are normally
        ''' needed for ImDisk to start communication with this service.</value>
        ''' <returns>Default value of this property depends on derived class and which parameters are normally
        ''' needed for ImDisk to start communication with this service.</returns>
        Public Overridable Property ImDiskAdditionalFlags As ImDiskFlags

        ''' <summary>
        ''' When overridden in a derived class, indicates additional flags that will be passed to
        ''' ImDiskAPI.CreateDevice() in StartServiceThreadAndMountImDisk() method. Value of this property depends
        ''' on derived class and which parameters are normally needed for ImDisk to start communication with this
        ''' service.
        ''' </summary>
        ''' <value>Default value of this property depends on derived class and which parameters are normally
        ''' needed for ImDisk to start communication with this service.</value>
        ''' <returns>Default value of this property depends on derived class and which parameters are normally
        ''' needed for ImDisk to start communication with this service.</returns>
        Protected MustOverride ReadOnly Property ImDiskProxyModeFlags As ImDiskFlags

        ''' <summary>
        ''' Object name that ImDisk Virtual Disk Driver can use to connect to this service.
        ''' </summary>
        ''' <value>Object name string.</value>
        ''' <returns>Object name that ImDisk Virtual Disk Driver can use to connect to this service.</returns>
        Protected MustOverride ReadOnly Property ImDiskProxyObjectName As String

        Private _ImDiskDeviceNumber As UInteger = UInteger.MaxValue

        ''' <summary>
        ''' After successful call to StartServiceThreadAndMountImDisk(), this property returns ImDisk device
        ''' number for created ImDisk Virtual Disk Device. This number can be used when calling ImDisk API
        ''' functions. If no ImDisk Virtual Disk Device has been created by this instance, an exception is
        ''' thrown. Use HasImDiskDevice property to find out if an ImDisk device has been created.
        ''' </summary>
        ''' <value>ImDisk device
        ''' number for created ImDisk Virtual Disk Device.</value>
        ''' <returns>ImDisk device
        ''' number for created ImDisk Virtual Disk Device.</returns>
        ''' <remarks></remarks>
        Public Overridable ReadOnly Property ImDiskDeviceNumber As UInteger
            Get
                If _ImDiskDeviceNumber = UInteger.MaxValue Then
                    Throw New IOException("No ImDisk Virtual Disk Device currently associated with this instance.")
                End If
                Return _ImDiskDeviceNumber
            End Get
        End Property

        ''' <summary>
        ''' Use HasImDiskDevice property to find out if an ImDisk device has been created in a call to
        ''' StartServiceThreadAndMountImDisk() method. Use ImDiskDeviceNumber property to find out ImDisk
        ''' device number for created device.
        ''' </summary>
        ''' <value>Returns True if an ImDisk Virtual Disk Device has been created, False otherwise.</value>
        ''' <returns>Returns True if an ImDisk Virtual Disk Device has been created, False otherwise.</returns>
        Public Overridable ReadOnly Property HasImDiskDevice As Boolean
            Get
                Return _ImDiskDeviceNumber <> UInteger.MaxValue
            End Get
        End Property

        ''' <summary>
        ''' Indicates whether ImDisk Virtual Disk Device created by this instance will be automatically
        ''' forcefully removed if a crash occurs in service thread of this instance. Default is True.
        ''' </summary>
        ''' <value>Indicates whether ImDisk Virtual Disk Device created by this instance will be automatically
        ''' forcefully removed if a crash occurs in service thread of this instance. Default is True.</value>
        ''' <returns>Indicates whether ImDisk Virtual Disk Device created by this instance will be automatically
        ''' forcefully removed if a crash occurs in service thread of this instance. Default is True.</returns>
        Public Overridable Property ForceRemoveImDiskDeviceOnCrash As Boolean = True

#Region "IDisposable Support"
        Private disposedValue As Boolean ' To detect redundant calls

        ' IDisposable
        Protected Overridable Sub Dispose(disposing As Boolean)
            If Not Me.disposedValue Then

                If disposing Then
                    ' TODO: dispose managed state (managed objects).

                End If

                ' TODO: free unmanaged resources (unmanaged objects) and override Finalize() below.
                ' TODO: set large fields to null.

                If _DevioProvider IsNot Nothing Then
                    If OwnsProvider Then
                        _DevioProvider.Dispose()
                    End If
                    _DevioProvider = Nothing
                End If

                OnStopServiceThread()

            End If
            Me.disposedValue = True
        End Sub

        ' TODO: override Finalize() only if Dispose(ByVal disposing As Boolean) above has code to free unmanaged resources.
        Protected Overrides Sub Finalize()
            ' Do not change this code.  Put cleanup code in Dispose(ByVal disposing As Boolean) above.
            Dispose(False)
            MyBase.Finalize()
        End Sub

        ''' <summary>
        ''' Releases all resources used by this instance.
        ''' </summary>
        Public Sub Dispose() Implements IDisposable.Dispose
            ' Do not change this code.  Put cleanup code in Dispose(ByVal disposing As Boolean) above.
            Dispose(True)
            GC.SuppressFinalize(Me)
        End Sub
#End Region

    End Class

End Namespace
