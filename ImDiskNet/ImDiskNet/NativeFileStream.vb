Namespace IO

  ''' <summary>
  ''' Extends FileStream class with constructors to open a file using Win32 API CreateFile() function. This makes it
  ''' possible to open everyting that CreateFile() can open and get a FileStream based .NET wrapper around the file handle.
  ''' </summary>
  Public Class NativeFileStream
    Inherits FileStream

    Protected Const GENERIC_READ As UInt32 = &H80000000UI
    Protected Const GENERIC_WRITE As UInt32 = &H40000000UI
    Protected Const FILE_SHARE_READ As UInt32 = &H1UI
    Protected Const FILE_SHARE_WRITE As UInt32 = &H2UI
    Protected Const FILE_SHARE_DELETE As UInt32 = &H4UI
    Protected Const FILE_READ_ATTRIBUTES As UInt32 = &H80UI
    Protected Const FILE_ATTRIBUTE_NORMAL As UInt32 = &H80UI
    Protected Const FILE_FLAG_OVERLAPPED As UInt32 = &H40000000UI
    Protected Const OPEN_ALWAYS As UInt32 = 4UI
    Protected Const OPEN_EXISTING As UInt32 = 3UI
    Protected Const CREATE_ALWAYS As UInt32 = 2UI
    Protected Const CREATE_NEW As UInt32 = 1UI
    Protected Const TRUNCATE_EXISTING As UInt32 = 5UI
    Protected Const ERROR_FILE_NOT_FOUND As UInt32 = 2UI
    Protected Const ERROR_PATH_NOT_FOUND As UInt32 = 3UI
    Protected Const ERROR_ACCESS_DENIED As UInt32 = 5UI

    Private Declare Auto Function CreateFile Lib "kernel32" (
      ByVal lpFileName As String,
      ByVal dwDesiredAccess As UInt32,
      ByVal dwShareMode As UInt32,
      ByVal lpSecurityAttributes As IntPtr,
      ByVal dwCreationDisposition As UInt32,
      ByVal dwFlagsAndAttributes As UInt32,
      ByVal hTemplateFile As IntPtr) As IntPtr

    Protected Declare Function DeviceIoControl Lib "kernel32" Alias "DeviceIoControl" (
      ByVal hDevice As IntPtr,
      ByVal dwIoControlCode As UInt32,
      ByVal lpInBuffer As IntPtr,
      ByVal nInBufferSize As UInt32,
      ByVal lpOutBuffer As IntPtr,
      ByVal nOutBufferSize As UInt32,
      ByVal lpBytesReturned As UInt32,
      <MarshalAs(UnmanagedType.LPStruct)> ByRef lpOverlapped As OVERLAPPED) As Int32

    Protected Structure OVERLAPPED
      Public Internal As IntPtr
      Public InternalHigh As IntPtr
      Public offset As UInt32
      Public OffsetHigh As UInt32
      Public hEvent As IntPtr
    End Structure

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle in a SafeFileHandle object.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="Overlapped">Specifies whether to request overlapped I/O.</param>
    Protected Shared Function NativeOpenFile(
      ByVal FileName As String,
      ByVal DesiredAccess As FileAccess,
      ByVal ShareMode As FileShare,
      ByVal CreationDisposition As FileMode,
      ByVal Overlapped As Boolean) As SafeFileHandle

      If String.IsNullOrEmpty(FileName) Then
        Throw New ArgumentNullException("FileName")
      End If

      Dim NativeDesiredAccess As UInt32 = FILE_READ_ATTRIBUTES
      If (DesiredAccess And FileAccess.Read) = FileAccess.Read Then
        NativeDesiredAccess = NativeDesiredAccess Or GENERIC_READ
      End If
      If (DesiredAccess And FileAccess.Write) = FileAccess.Write Then
        NativeDesiredAccess = NativeDesiredAccess Or GENERIC_WRITE
      End If

      Dim NativeShareMode As UInt32 = 0
      If (ShareMode And FileShare.Read) = FileShare.Read Then
        NativeShareMode = NativeShareMode Or FILE_SHARE_READ
      End If
      If (ShareMode And FileShare.Write) = FileShare.Write Then
        NativeShareMode = NativeShareMode Or FILE_SHARE_WRITE
      End If
      If (ShareMode And FileShare.Delete) = FileShare.Delete Then
        NativeShareMode = NativeShareMode Or FILE_SHARE_DELETE
      End If

      Dim NativeCreationDisposition As UInt32 = 0
      Select Case CreationDisposition
        Case FileMode.Create
          NativeCreationDisposition = CREATE_ALWAYS
        Case FileMode.CreateNew
          NativeCreationDisposition = CREATE_NEW
        Case FileMode.Open
          NativeCreationDisposition = OPEN_EXISTING
        Case FileMode.OpenOrCreate
          NativeCreationDisposition = OPEN_ALWAYS
        Case FileMode.Truncate
          NativeCreationDisposition = TRUNCATE_EXISTING
        Case Else
          Throw New NotImplementedException
      End Select

      Dim NativeFlagsAndAttributes As UInt32 = FILE_ATTRIBUTE_NORMAL
      If Overlapped Then
        NativeFlagsAndAttributes += FILE_FLAG_OVERLAPPED
      End If

      Dim Handle As New SafeFileHandle(CreateFile(FileName,
                                                  NativeDesiredAccess,
                                                  NativeShareMode,
                                                  IntPtr.Zero,
                                                  NativeCreationDisposition,
                                                  NativeFlagsAndAttributes,
                                                  IntPtr.Zero),
                                       ownsHandle:=True)
      If Handle.IsInvalid Then
        Marshal.ThrowExceptionForHR(Marshal.GetHRForLastWin32Error())
      End If

      Return Handle
    End Function

    ''' <summary>
    ''' Converts FileAccess flags to values legal in constructor call to FileStream class.
    ''' </summary>
    ''' <param name="Value">FileAccess values.</param>
    Private Shared Function GetLegalFileAccessValue(ByVal Value As FileAccess) As FileAccess
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
    Public Sub New(
      ByVal FileName As String,
      ByVal CreationDisposition As FileMode,
      ByVal DesiredAccess As FileAccess,
      ByVal ShareMode As FileShare)

      MyBase.New(NativeOpenFile(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped:=False), GetLegalFileAccessValue(DesiredAccess))

    End Sub

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="BufferSize">Buffer size to specify in constructor call to FileStream class.</param>
    Public Sub New(
      ByVal FileName As String,
      ByVal CreationDisposition As FileMode,
      ByVal DesiredAccess As FileAccess,
      ByVal ShareMode As FileShare,
      ByVal BufferSize As Integer)

      MyBase.New(NativeOpenFile(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped:=False), GetLegalFileAccessValue(DesiredAccess), BufferSize)

    End Sub

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="BufferSize">Buffer size to specify in constructor call to FileStream class.</param>
    ''' <param name="Overlapped">Specifies whether to request overlapped I/O.</param>
    Public Sub New(
      ByVal FileName As String,
      ByVal CreationDisposition As FileMode,
      ByVal DesiredAccess As FileAccess,
      ByVal ShareMode As FileShare,
      ByVal BufferSize As Integer,
      ByVal Overlapped As Boolean)

      MyBase.New(NativeOpenFile(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped), GetLegalFileAccessValue(DesiredAccess), BufferSize, Overlapped)

    End Sub

    ''' <summary>
    ''' Calls Win32 API CreateFile() function and encapsulates returned handle.
    ''' </summary>
    ''' <param name="FileName">Name of file to open.</param>
    ''' <param name="DesiredAccess">File access to request.</param>
    ''' <param name="ShareMode">Share mode to request.</param>
    ''' <param name="CreationDisposition">Open/creation mode.</param>
    ''' <param name="Overlapped">Specifies whether to request overlapped I/O.</param>
    Public Sub New(
      ByVal FileName As String,
      ByVal CreationDisposition As FileMode,
      ByVal DesiredAccess As FileAccess,
      ByVal ShareMode As FileShare,
      ByVal Overlapped As Boolean)

      MyBase.New(NativeOpenFile(FileName, DesiredAccess, ShareMode, CreationDisposition, Overlapped), GetLegalFileAccessValue(DesiredAccess), 0, Overlapped)

    End Sub

    ''' <summary>
    ''' Send constructor call through to FileStream class.
    ''' </summary>
    ''' <param name="Handle">Existing SafeFileHandle to encapsulate.</param>
    ''' <param name="AccessMode">File access to request.</param>
    Protected Sub New(
      ByVal Handle As SafeFileHandle,
      ByVal AccessMode As FileAccess)

      MyBase.New(Handle, GetLegalFileAccessValue(AccessMode))

    End Sub

  End Class

End Namespace
