/*
    ImDisk Proxy Service.

    Copyright (C) 2005-2006 Olof Lagerkvist.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _IMDPROXY_H
#define _IMDPROXY_H

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

#define IMDPROXY_SWITCH_BUFFER_SIZE     (1024 << 10)

typedef enum _IMDPROXY_REQ
  {
    IMDPROXY_REQ_NULL,
    IMDPROXY_REQ_INFO,
    IMDPROXY_REQ_READ,
    IMDPROXY_REQ_WRITE,
    IMDPROXY_REQ_CONNECT,
  } IMDPROXY_REQ;

typedef struct _IMDPROXY_CONNECT_REQ
{
  ULONGLONG request_code;
  ULONGLONG flags;
  ULONGLONG length;
} IMDPROXY_CONNECT_REQ, *PIMDPROXY_CONNECT_REQ;

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

#endif
