Imports Microsoft.Win32.SafeHandles
Imports System.ComponentModel

Namespace IO

  ''' <summary>
  ''' Provides wrappers for Win32 file API. This makes it possible to open everyting that
  ''' CreateFile() can open and get a FileStream based .NET wrapper around the file handle.
  ''' </summary>
  Public NotInheritable Class NativeFileIO

#Region "Win32 API"
    Public Class Win32API

      Public Const GENERIC_READ As UInt32 = &H80000000UI
      Public Const GENERIC_WRITE As UInt32 = &H40000000UI
      Public Const FILE_SHARE_READ As UInt32 = &H1UI
      Public Const FILE_SHARE_WRITE As UInt32 = &H2UI
      Public Const FILE_SHARE_DELETE As UInt32 = &H4UI
      Public Const FILE_READ_ATTRIBUTES As UInt32 = &H80UI
      Public Const FILE_ATTRIBUTE_NORMAL As UInt32 = &H80UI
      Public Const FILE_FLAG_OVERLAPPED As UInt32 = &H40000000UI
      Public Const OPEN_ALWAYS As UInt32 = 4UI
      Public Const OPEN_EXISTING As UInt32 = 3UI
      Public Const CREATE_ALWAYS As UInt32 = 2UI
      Public Const CREATE_NEW As UInt32 = 1UI
      Public Const TRUNCATE_EXISTING As UInt32 = 5UI
      Public Const ERROR_FILE_NOT_FOUND As UInt32 = 2UI
      Public Const ERROR_PATH_NOT_FOUND As UInt32 = 3UI
      Public Const ERROR_ACCESS_DENIED As UInt32 = 5UI

      Public Const FSCTL_GET_COMPRESSION As UInt32 = &H9003C
      Public Const FSCTL_SET_COMPRESSION As UInt32 = &H9C040
      Public Const COMPRESSION_FORMAT_NONE As UShort = 0US
      Public Const COMPRESSION_FORMAT_DEFAULT As UShort = 1US

      Public Const FSCTL_ALLOW_EXTENDED_DASD_IO As UInt32 = &H90083

      <StructLayout(LayoutKind.Sequential)> _
      Public Structure COMMTIMEOUTS
        Public ReadIntervalTimeout As UInt32
        Public ReadTotalTimeoutMultiplier As UInt32
        Public ReadTotalTimeoutConstant As UInt32
        Public WriteTotalTimeoutMultiplier As UInt32
        Public WriteTotalTimeoutConstant As UInt32
      End Structure

      Public Declare Auto Function GetCommTimeouts Lib "kernel32" ( _
        hFile As IntPtr, _
        <Out()> ByRef lpCommTimeouts As COMMTIMEOUTS) As Boolean

      Public Declare Auto Function SetCommTimeouts Lib "kernel32" ( _
        hFile As IntPtr, _
        <[In]()> ByRef lpCommTimeouts As COMMTIMEOUTS) As Boolean

      Public Declare Auto Function CreateFile Lib "kernel32" ( _
        lpFileName As String, _
        dwDesiredAccess As UInt32, _
        dwShareMode As UInt32, _
        lpSecurityAttributes As IntPtr, _
        dwCreationDisposition As UInt32, _
        dwFlagsAndAttributes As UInt32, _
        hTemplateFile As IntPtr) As IntPtr

      Public Declare Function GetFileSize Lib "kernel32" Alias "GetFileSizeEx" ( _
        hFile As IntPtr, _
        ByRef liFileSize As Int64) As Boolean

      Public Declare Function DeviceIoControl Lib "kernel32" Alias "DeviceIoControl" ( _
        hDevice As IntPtr, _
        dwIoControlCode As UInt32, _
        <MarshalAs(UnmanagedType.LPArray, SizeParamIndex:=3), [In]()> lpInBuffer As Byte(), _
        nInBufferSize As UInt32, _
        <MarshalAs(UnmanagedType.LPArray, SizeParamIndex:=5), Out()> lpOutBuffer As Byte(), _
        nOutBufferSize As UInt32, _
        ByRef lpBytesReturned As UInt32, _
        lpOverlapped As IntPtr) As Boolean

    End Class
#End Region

    ''' <summary>
    ''' Encapsulates call to a Win32 API function that returns a BOOL value indicating success
    ''' or failure and where an error value is available through a call to GetLastError() in case
    ''' of failure. If value True is passed to this method it does nothing. If False is passed,
    ''' it calls GetLastError(), converts error code to a HRESULT value and throws a managed
    ''' exception for that HRESULT.
    ''' </summary>
    ''' <param name="result">Return code from a Win32 API function call.</param>
    Public Shared Sub Win32Try(result As Boolean)

      If result = False Then
        Throw New Win32Exception
      End If

    End Sub

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle in a SafeFileHandle object.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="Overlapped">Specifies whether to request overlapped I/O.</param>
    Public Shared Function OpenFileHandle( _
      FileName As String, _
      DesiredAccess As FileAccess, _
      ShareMode As FileShare, _
      CreationDisposition As FileMode, _
      Overlapped As Boolean) As SafeFileHandle

      If String.IsNullOrEmpty(FileName) Then
        Throw New ArgumentNullException("FileName")
      End If

      Dim NativeDesiredAccess As UInt32 = Win32API.FILE_READ_ATTRIBUTES
      If (DesiredAccess And FileAccess.Read) = FileAccess.Read Then
        NativeDesiredAccess = NativeDesiredAccess Or Win32API.GENERIC_READ
      End If
      If (DesiredAccess And FileAccess.Write) = FileAccess.Write Then
        NativeDesiredAccess = NativeDesiredAccess Or Win32API.GENERIC_WRITE
      End If

      Dim NativeShareMode As UInt32 = 0
      If (ShareMode And FileShare.Read) = FileShare.Read Then
        NativeShareMode = NativeShareMode Or Win32API.FILE_SHARE_READ
      End If
      If (ShareMode And FileShare.Write) = FileShare.Write Then
        NativeShareMode = NativeShareMode Or Win32API.FILE_SHARE_WRITE
      End If
      If (ShareMode And FileShare.Delete) = FileShare.Delete Then
        NativeShareMode = NativeShareMode Or Win32API.FILE_SHARE_DELETE
      End If

      Dim NativeCreationDisposition As UInt32 = 0
      Select Case CreationDisposition
        Case FileMode.Create
          NativeCreationDisposition = Win32API.CREATE_ALWAYS
        Case FileMode.CreateNew
          NativeCreationDisposition = Win32API.CREATE_NEW
        Case FileMode.Open
          NativeCreationDisposition = Win32API.OPEN_EXISTING
        Case FileMode.OpenOrCreate
          NativeCreationDisposition = Win32API.OPEN_ALWAYS
        Case FileMode.Truncate
          NativeCreationDisposition = Win32API.TRUNCATE_EXISTING
        Case Else
          Throw New NotImplementedException
      End Select

      Dim NativeFlagsAndAttributes As UInt32 = Win32API.FILE_ATTRIBUTE_NORMAL
      If Overlapped Then
        NativeFlagsAndAttributes += Win32API.FILE_FLAG_OVERLAPPED
      End If

      Dim Handle As New SafeFileHandle(Win32API.CreateFile(FileName, _
                                                           NativeDesiredAccess, _
                                                           NativeShareMode, _
                                                           IntPtr.Zero, _
                                                           NativeCreationDisposition, _
                                                           NativeFlagsAndAttributes, _
                                                           IntPtr.Zero), _
                                       ownsHandle:=True)
      If Handle.IsInvalid Then
        Throw New Win32Exception
      End If

      Return Handle
    End Function

    ''' <summary>
    ''' Converts FileAccess flags to values legal in constructor call to FileStream class.
    ''' </summary>
    ''' <param name="Value">FileAccess values.</param>
    Private Shared Function GetFileStreamLegalAccessValue(Value As FileAccess) As FileAccess
      If Value = 0 Then
        Return FileAccess.Read
      Else
        Return Value
      End If
    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    Public Shared Function OpenFileStream( _
      FileName As String, _
      CreationDisposition As FileMode, _
      DesiredAccess As FileAccess, _
      ShareMode As FileShare) As FileStream

      Return New FileStream(OpenFileHandle(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped:=False), GetFileStreamLegalAccessValue(DesiredAccess))

    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="BufferSize">Buffer size to specify in constructor call to FileStream class.</param>
    Public Shared Function OpenFileStream( _
      FileName As String, _
      CreationDisposition As FileMode, _
      DesiredAccess As FileAccess, _
      ShareMode As FileShare, _
      BufferSize As Integer) As FileStream

      Return New FileStream(OpenFileHandle(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped:=False), GetFileStreamLegalAccessValue(DesiredAccess), BufferSize)

    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="BufferSize">Buffer size to specify in constructor call to FileStream class.</param>
    ''' <param name="Overlapped">Specifies whether to request overlapped I/O.</param>
    Public Shared Function OpenFileStream( _
      FileName As String, _
      CreationDisposition As FileMode, _
      DesiredAccess As FileAccess, _
      ShareMode As FileShare, _
      BufferSize As Integer, _
      Overlapped As Boolean) As FileStream

      Return New FileStream(OpenFileHandle(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped), GetFileStreamLegalAccessValue(DesiredAccess), BufferSize, Overlapped)

    End Function

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="Overlapped">Specifies whether to request overlapped I/O.</param>
    Public Shared Function OpenFileStream( _
      FileName As String, _
      CreationDisposition As FileMode, _
      DesiredAccess As FileAccess, _
      ShareMode As FileShare, _
      Overlapped As Boolean) As FileStream

      Return New FileStream(OpenFileHandle(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped), GetFileStreamLegalAccessValue(DesiredAccess), 1, Overlapped)

    End Function

    Private Shared Sub SetFileCompressionState(SafeFileHandle As SafeFileHandle, State As UShort)

      Dim Buffer As New BufferedBinaryWriter
      Buffer.Write(State)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        Win32Try(Win32API.DeviceIoControl(SafeFileHandle.DangerousGetHandle(), _
                                          Win32API.FSCTL_SET_COMPRESSION, _
                                          Buffer.ToArray(), _
                                          2, _
                                          Nothing, _
                                          Nothing, _
                                          Nothing, _
                                          Nothing))

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    Public Shared Function GetFileSize(SafeFileHandle As SafeFileHandle) As Int64

      Dim FileSize As Int64

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        Win32Try(Win32API.GetFileSize(SafeFileHandle.DangerousGetHandle(), FileSize))

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

      Return FileSize

    End Function

    Public Shared Sub CompressFile(SafeFileHandle As SafeFileHandle)

      SetFileCompressionState(SafeFileHandle, Win32API.COMPRESSION_FORMAT_DEFAULT)

    End Sub

    Public Shared Sub UncompressFile(SafeFileHandle As SafeFileHandle)

      SetFileCompressionState(SafeFileHandle, Win32API.COMPRESSION_FORMAT_NONE)

    End Sub

    Public Shared Sub AllowExtendedDASDIO(SafeFileHandle As SafeFileHandle)

      Dim HandleReferenced As Boolean
      Try
        SafeFileHandle.DangerousAddRef(HandleReferenced)
        If SafeFileHandle.IsInvalid OrElse SafeFileHandle.IsClosed Then
          Throw New ArgumentException("Handle is invalid")
        End If
        Win32Try(Win32API.DeviceIoControl(SafeFileHandle.DangerousGetHandle(), Win32API.FSCTL_ALLOW_EXTENDED_DASD_IO, Nothing, 0, Nothing, 0, 0, Nothing))

      Finally
        If HandleReferenced Then
          SafeFileHandle.DangerousRelease()
        End If

      End Try

    End Sub

    Private Sub New()

    End Sub

  End Class

End Namespace
