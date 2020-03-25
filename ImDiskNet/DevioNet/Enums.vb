Public Enum IMDPROXY_REQ As ULong

    ''' <summary>
    ''' No operation.
    ''' </summary>
    IMDPROXY_REQ_NULL

    ''' <summary>
    ''' Request information about I/O provider.
    ''' </summary>
    IMDPROXY_REQ_INFO

    ''' <summary>
    ''' Request to read data.
    ''' </summary>
    IMDPROXY_REQ_READ

    ''' <summary>
    ''' Request to write data.
    ''' </summary>
    IMDPROXY_REQ_WRITE

    ''' <summary>
    ''' Request to connect to serial port, TCP/IP host etc. Only used internally between ImDisk driver and helper service.
    ''' </summary>
    IMDPROXY_REQ_CONNECT

    ''' <summary>
    ''' Request to close proxy connection.
    ''' </summary>
    IMDPROXY_REQ_CLOSE

    ''' <summary>
    ''' Request to unmap allocation range, that is mark as not in use. Sent to proxy services that indicate support for this
    ''' request by setting IMDPROXY_FLAG_SUPPORTS_UNMAP flag in Flags response field. The request is sent in response to TRIM
    ''' requests sent by filesystem drivers.
    ''' </summary>
    IMDPROXY_REQ_UNMAP

    ''' <summary>
    ''' Request to fill a range with zeros. Sent to proxy services that indicate support for this request by setting
    ''' IMDPROXY_FLAG_SUPPORTS_ZERO flag in Flags response field. The request is sent to proxy when an all-zeros range is
    ''' written, or when FSCTL_SET_ZERO_DATA is received.
    ''' </summary>
    IMDPROXY_REQ_ZERO
End Enum

Public Enum IMDPROXY_FLAGS As ULong
    IMDPROXY_FLAG_NONE = 0UL
    IMDPROXY_FLAG_RO = 1UL
    IMDPROXY_FLAG_SUPPORTS_UNMAP = 2UL
    IMDPROXY_FLAG_SUPPORTS_ZERO = 4UL
End Enum

''' <summary>
''' Constants used in connection with ImDisk/Devio proxy communication.
''' </summary>
Public MustInherit Class IMDPROXY_CONSTANTS
    Private Sub New()
    End Sub

    ''' <summary>
    ''' Header size when communicating using a shared memory object.
    ''' </summary>
    Public Const IMDPROXY_HEADER_SIZE As Integer = 4096

    ''' <summary>
    ''' Default required alignment for I/O operations.
    ''' </summary>
    Public Const REQUIRED_ALIGNMENT As Integer = 1
End Class

<StructLayout(LayoutKind.Sequential)>
Public Structure IMDPROXY_CONNECT_REQ
    Public request_code As IMDPROXY_REQ
    Public flags As ULong
    Public length As ULong
End Structure

<StructLayout(LayoutKind.Sequential)>
Public Structure IMDPROXY_CONNECT_RESP
    Public error_code As ULong
    Public object_ptr As ULong
End Structure

''' <summary>
''' Message sent by proxy service after connection has been established. This
''' message indicates what features this proxy service supports and total size
''' of virtual image and alignment requirement for I/O requests.
''' </summary>
<StructLayout(LayoutKind.Sequential)>
Public Structure IMDPROXY_INFO_RESP

    ''' <summary>
    ''' Total size in bytes of virtual image
    ''' </summary>
    Public file_size As ULong

    ''' <summary>
    ''' Required alignment in bytes for I/O requests sent to this proxy service
    ''' </summary>
    Public req_alignment As ULong

    ''' <summary>
    ''' Flags from IMDPROXY_FLAGS enumeration
    ''' </summary>
    Public flags As IMDPROXY_FLAGS

End Structure

<StructLayout(LayoutKind.Sequential)>
Public Structure IMDPROXY_READ_REQ
    Public request_code As IMDPROXY_REQ
    Public offset As ULong
    Public length As ULong
End Structure

<StructLayout(LayoutKind.Sequential)>
Public Structure IMDPROXY_READ_RESP
    Public errorno As ULong
    Public length As ULong
End Structure

<StructLayout(LayoutKind.Sequential)>
Public Structure IMDPROXY_WRITE_REQ
    Public request_code As IMDPROXY_REQ
    Public offset As ULong
    Public length As ULong
End Structure

<StructLayout(LayoutKind.Sequential)>
Public Structure IMDPROXY_WRITE_RESP
    Public errorno As ULong
    Public length As ULong
End Structure
