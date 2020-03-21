/*
    ImDisk Proxy Services.

    Copyright (C) 2005-2007 Olof Lagerkvist.

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use,
    copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following
    conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
    OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
    OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _INC_IMDPROXY_
#define _INC_IMDPROXY_

#if !defined(_WIN32) && !defined(_NTDDK_)
typedef long LONG;
typedef unsigned long ULONG;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned short WCHAR;
#endif

#define IMDPROXY_SVC                    L"ImDskSvc"
#define IMDPROXY_SVC_PIPE_DOSDEV_NAME   L"\\\\.\\PIPE\\" IMDPROXY_SVC
#define IMDPROXY_SVC_PIPE_NATIVE_NAME   L"\\Device\\NamedPipe\\" IMDPROXY_SVC

#define IMDPROXY_FLAG_RO                0x1

typedef enum _IMDPROXY_REQ
  {
    IMDPROXY_REQ_NULL,
    IMDPROXY_REQ_INFO,
    IMDPROXY_REQ_READ,
    IMDPROXY_REQ_WRITE,
    IMDPROXY_REQ_CONNECT,
    IMDPROXY_REQ_CLOSE,
  } IMDPROXY_REQ;

typedef struct _IMDPROXY_CONNECT_REQ
{
  ULONGLONG request_code;
  ULONGLONG flags;
  ULONGLONG length;
} IMDPROXY_CONNECT_REQ, *PIMDPROXY_CONNECT_REQ;

typedef struct _IMDPROXY_CONNECT_RESP
{
  ULONGLONG error_code;
  ULONGLONG object_ptr;
} IMDPROXY_CONNECT_RESP, *PIMDPROXY_CONNECT_RESP;

typedef struct _IMDPROXY_INFO_RESP
{
  ULONGLONG file_size;
  ULONGLONG req_alignment;
  ULONGLONG flags;
} IMDPROXY_INFO_RESP, *PIMDPROXY_INFO_RESP;

typedef struct _IMDPROXY_READ_REQ
{
  ULONGLONG request_code;
  ULONGLONG offset;
  ULONGLONG length;
} IMDPROXY_READ_REQ, *PIMDPROXY_READ_REQ;

typedef struct _IMDPROXY_READ_RESP
{
  ULONGLONG errorno;
  ULONGLONG length;
} IMDPROXY_READ_RESP, *PIMDPROXY_READ_RESP;

typedef struct _IMDPROXY_WRITE_REQ
{
  ULONGLONG request_code;
  ULONGLONG offset;
  ULONGLONG length;
} IMDPROXY_WRITE_REQ, *PIMDPROXY_WRITE_REQ;

typedef struct _IMDPROXY_WRITE_RESP
{
  ULONGLONG errorno;
  ULONGLONG length;
} IMDPROXY_WRITE_RESP, *PIMDPROXY_WRITE_RESP;

// For shared memory proxy communication only. Offset to data area in
// shared memory.
#define IMDPROXY_HEADER_SIZE 4096

#endif // _INC_IMDPROXY_
