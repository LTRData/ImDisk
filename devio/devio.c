/*
    Server end for ImDisk Virtual Disk Driver.

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

//#define DEBUG

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>

#ifdef _WIN32

#include <windows.h>
#include <winsock.h>
#include <winioctl.h>
#include <io.h>

__inline
BOOL
OemPrintF(FILE *Stream, LPCSTR Message, ...)
{
  va_list param_list;
  LPSTR lpBuf = NULL;

  va_start(param_list, Message);

  if (!FormatMessageA(78 |
		      FORMAT_MESSAGE_ALLOCATE_BUFFER |
		      FORMAT_MESSAGE_FROM_STRING, Message, 0, 0,
		      (LPSTR) &lpBuf, 0, &param_list))
    return FALSE;

  CharToOemA(lpBuf, lpBuf);
  fprintf(Stream, "%s\n", lpBuf);
  LocalFree(lpBuf);
  return TRUE;
}

__inline
void
syslog(FILE *Stream, LPCSTR Message, ...)
{
  va_list param_list;
  LPSTR MsgBuf = NULL;

  va_start(param_list, Message);

  if (strstr(Message, "%m") != NULL)
    {
      if (FormatMessage(FORMAT_MESSAGE_MAX_WIDTH_MASK |
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(), 0, (LPSTR) &MsgBuf, 0, NULL))
	CharToOemA(MsgBuf, MsgBuf);
    }

  if (MsgBuf != NULL)
    {
      vfprintf(Stream, Message, param_list);
      fprintf(Stream, "%s\n", MsgBuf);

      LocalFree(MsgBuf);
    }
  else
    vfprintf(Stream, Message, param_list);

  return;
}

#define LOG_ERR   stderr

typedef size_t ssize_t;
typedef __int64 off_t_64;

#define ULL_FMT   "%I64u"

#else  // Unix

#include <syslog.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define closesocket(s) close(s)
#ifndef O_BINARY
#define O_BINARY       0
#endif
#define ULL_FMT   "%llu"

typedef off_t off_t_64;

#endif

#include "../inc/imdproxy.h"
#include "safeio.h"

#ifndef O_DIRECT
#define O_DIRECT 0
#endif

#ifndef O_FSYNC
#define O_FSYNC 0
#endif

#define DEF_REQ_ALIGNMENT 512
#define DEF_BUFFER_SIZE (16 << 20)

#ifdef DEBUG
#define dbglog(x) syslog x
#else
#define dbglog(x)
#endif

int fd = -1;
int sd = -1;
void *buf = NULL;
unsigned long buffer_size = DEF_BUFFER_SIZE;
unsigned long long offset = 0;
IMDPROXY_INFO_RESP devio_info = { 0 };

int
send_info()
{
  return safe_write(sd, &devio_info, sizeof devio_info);
}

int
read_data()
{
  IMDPROXY_READ_REQ req_block = { 0 };
  IMDPROXY_READ_RESP resp_block = { 0 };
  size_t size;
  ssize_t readdone;

  if (!safe_read(sd, &req_block.offset,
		 sizeof(req_block) - sizeof(req_block.request_code)))
    {
      syslog(LOG_ERR, "Error reading request header.\n");
      return 0;
    }

  size = (size_t)
    (req_block.length < buffer_size ? req_block.length : buffer_size);

  dbglog((LOG_ERR, "read request " ULL_FMT " bytes at " ULL_FMT ".\n",
	  req_block.length, req_block.offset));

  memset(buf, 0, size);

  readdone =
    pread(fd, buf, (size_t) size, (off_t_64) (offset + req_block.offset));
  if (readdone == -1)
    {
      resp_block.errorno = errno;
      resp_block.length = 0;
      syslog(LOG_ERR, "Device read: %m\n");
    }
  else
    {
      resp_block.errorno = 0;
      resp_block.length = size;

      if (req_block.length != readdone)
	syslog(LOG_ERR,
	       "Partial read at " ULL_FMT ": Got %u, req " ULL_FMT ".\n",
	       (offset + req_block.offset), readdone, req_block.length);
    }

  dbglog((LOG_ERR, "read done reporting/sending " ULL_FMT " bytes.\n",
	  resp_block.length));

  if (!safe_write(sd, &resp_block, sizeof resp_block))
    {
      syslog(LOG_ERR, "Warning: I/O stream incosistency.\n");
      return 0;
    }

  if (resp_block.errorno == 0)
    if (!safe_write(sd, buf, (size_t) resp_block.length))
      {
	syslog(LOG_ERR, "Error sending read response to caller.\n");
	return 0;
      }

  return 1;
}

int
write_data()
{
  IMDPROXY_WRITE_REQ req_block = { 0 };
  IMDPROXY_WRITE_RESP resp_block = { 0 };

  if (!safe_read(sd, &req_block.offset,
		 sizeof(req_block) - sizeof(req_block.request_code)))
    return 0;

  dbglog((LOG_ERR, "write request " ULL_FMT " bytes at %llu.\n",
	  req_block.length, req_block.offset));

  if (req_block.length > buffer_size)
    {
      syslog(LOG_ERR, "Too big block write requested: %u bytes.\n",
	     (int)req_block.length);
      return 0;
    }

  if (!safe_read(sd, buf, (size_t) req_block.length))
    {
      syslog(LOG_ERR, "Warning: I/O stream inconsistency.\n");

      return 0;
    }

  if (devio_info.flags & IMDPROXY_FLAG_RO)
    {
      resp_block.errorno = EBADF;
      resp_block.length = 0;
      syslog(LOG_ERR, "Device write attempt on read-only device.\n");
    }
  else
    {
      ssize_t writedone =
	pwrite(fd, buf, (size_t) req_block.length,
	       (off_t_64) (offset + req_block.offset));
      if (writedone == -1)
	{
	  resp_block.errorno = errno;
	  resp_block.length = writedone;
	  syslog(LOG_ERR, "Device write: %m\n");
	}
      else
	{
	  resp_block.errorno = 0;
	  resp_block.length = writedone;
	}

      if (req_block.length != resp_block.length)
	syslog
	  (LOG_ERR, "Partial write at " ULL_FMT ".\n",
	   (offset + req_block.offset));

      dbglog((LOG_ERR, "write done reporting/sending " ULL_FMT " bytes.\n",
	      resp_block.length));
    }

  if (!safe_write(sd, &resp_block, sizeof resp_block))
    {
      syslog(LOG_ERR, "Error sending write response to caller.\n");

      return 0;
    }

  return 1;
}

int
main(int argc, char **argv)
{
  ULONGLONG req = 0;
  unsigned short port = 0;
  int i;
  struct sockaddr_in saddr = { 0 };

#ifdef _WIN32
  WSADATA wsadata;
  WSAStartup(0x0101, &wsadata);
#endif

  if (argc >= 4)
    if (strcmp(argv[1], "-r") == 0)
      {
	devio_info.flags |= IMDPROXY_FLAG_RO;
	argv++;
	argc--;
      }

  if ((argc < 3) | (argc > 7))
    {
      fprintf(stderr,
	      "Usage:\n"
	      "%s [-r] tcp-port|commdev diskdev [blocks] [offset] [alignm] [buffersize]\n"
	      "\n"
	      "Default number of blocks is 0. When running on Windows the program will try to\n"
	      "get the size of the image file or partition automatically in this case,\n"
	      "otherwise the client must know the exact size without help from this service.\n"
	      "Default alignment is %u bytes.\n"
	      "Default buffer size is %u bytes.\n",
	      argv[0],
	      DEF_REQ_ALIGNMENT,
	      DEF_BUFFER_SIZE);
      return -1;
    }

  port = (u_short) strtoul(argv[1], NULL, 0);

  if (devio_info.flags & IMDPROXY_FLAG_RO)
    fd = open(argv[2], O_BINARY | O_DIRECT | O_FSYNC | O_RDONLY);
  else
    fd = open(argv[2], O_BINARY | O_DIRECT | O_FSYNC | O_RDWR);
  if (fd == -1)
    {
      syslog(LOG_ERR, "Device open failed on '%s': %m\n", argv[2]);
      return 1;
    }

  dbglog((LOG_ERR, "Successfully opened device '%s'.\n", argv[2]));

  if (argc > 3)
    {
      char suf = 0;
      if (sscanf(argv[3], ULL_FMT "%c", &devio_info.file_size, &suf) == 2)
	switch (suf)
	  {
	  case 'T':
	    devio_info.file_size <<= 10;
	  case 'G':
	    devio_info.file_size <<= 10;
	  case 'M':
	    devio_info.file_size <<= 10;
	  case 'K':
	    devio_info.file_size <<= 10;
	  case 'B':
	    break;
	  case 't':
	    devio_info.file_size *= 1000;
	  case 'g':
	    devio_info.file_size *= 1000;
	  case 'm':
	    devio_info.file_size *= 1000;
	  case 'k':
	    devio_info.file_size *= 1000;
	  case 'b':
	    break;
	  default:
	    syslog(LOG_ERR, "Unsupported size suffix: %c\n", suf);
	  }
      else
	devio_info.file_size <<= 9;
    }

#ifdef _WIN32
  {
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    BY_HANDLE_FILE_INFORMATION by_handle_file_info;

    // If not regular disk file, try to lock volume using FSCTL operation.
    if (!GetFileInformationByHandle(h, &by_handle_file_info))
      {
	DWORD dw;
	FlushFileBuffers(h);
	if (DeviceIoControl(h, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &dw, NULL))
	  {
	    if (!DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0,
				 &dw, NULL))
	      {
		syslog(LOG_ERR, "Cannot dismount filesystem on %s.\n",
		       argv[2]);

		if (~devio_info.flags & IMDPROXY_FLAG_RO)
		  return 9;
	      }
	  }
	else
	  switch (GetLastError())
	    {
	    case ERROR_NOT_SUPPORTED:
	    case ERROR_INVALID_FUNCTION:
	    case ERROR_INVALID_HANDLE:
	    case ERROR_INVALID_PARAMETER:
	      break;

	    default:
	      {
		syslog(LOG_ERR, "Cannot dismount filesystem on %s.\n",
		       argv[2]);

		if (~devio_info.flags & IMDPROXY_FLAG_RO)
		  return 9;
	      }
	    }

	if (devio_info.file_size == 0)
	  {
	    PARTITION_INFORMATION partition_info = { 0 };
	    DWORD dw;

	    if (!DeviceIoControl(h, IOCTL_DISK_GET_PARTITION_INFO, NULL, 0,
				 &partition_info, sizeof(partition_info), &dw,
				 NULL))
	      syslog(LOG_ERR, "Cannot determine size of image/partition.\n");
	    else
	      devio_info.file_size = partition_info.PartitionLength.QuadPart;
	  }
      }
    else if (devio_info.file_size == 0)
      {
	LARGE_INTEGER file_size;
	file_size.HighPart = by_handle_file_info.nFileSizeHigh;
	file_size.LowPart = by_handle_file_info.nFileSizeLow;

	devio_info.file_size = file_size.QuadPart;
      }
  }
#endif

  if (argc > 4)
    {
      char suf = 0;
      if (sscanf(argv[4], ULL_FMT "%c", &offset, &suf) == 2)
	switch (suf)
	  {
	  case 'T':
	    offset <<= 10;
	  case 'G':
	    offset <<= 10;
	  case 'M':
	    offset <<= 10;
	  case 'K':
	    offset <<= 10;
	  case 'B':
	    break;
	  case 't':
	    offset *= 1000;
	  case 'g':
	    offset *= 1000;
	  case 'm':
	    offset *= 1000;
	  case 'k':
	    offset *= 1000;
	  case 'b':
	    break;
	  default:
	    syslog(LOG_ERR, "Unsupported size suffix: %c\n", suf);
	  }
      else
	offset <<= 9;
    }

  if (argc > 5)
    sscanf(argv[4], ULL_FMT, &devio_info.req_alignment);
  else
    devio_info.req_alignment = DEF_REQ_ALIGNMENT;

  if (argc > 6)
    sscanf(argv[5], "%lu", &buffer_size);

  buf = malloc(buffer_size);
  if (buf == NULL)
    {
      syslog(LOG_ERR, "malloc() failed: %m\n");
      return 2;
    }

  if (port != 0)
    {
      int ssd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (ssd == -1)
	{
	  syslog(LOG_ERR, "socket() failed: %m\n");
	  return 2;
	}

      saddr.sin_family = AF_INET;
      saddr.sin_addr.s_addr = INADDR_ANY;
      saddr.sin_port = htons(port);

      if (bind(ssd, (struct sockaddr*) &saddr, sizeof saddr) == -1)
	{
	  syslog(LOG_ERR, "bind() failed port %u: %m\n", (unsigned int) port);
	  return 2;
	}

      i = sizeof saddr;
      if (getsockname(ssd, (struct sockaddr*) &saddr, &i) == -1)
	{
	  syslog(LOG_ERR, "getsockname() failed: %m\n");
	  return 2;
	}

      if (listen(ssd, 1) == -1)
	{
	  syslog(LOG_ERR, "listen() failed for %s:%u: %m\n",
		 inet_ntoa(saddr.sin_addr),
		 (unsigned int) ntohs(saddr.sin_port));
	  return 2;
	}

      dbglog((LOG_ERR, "Waiting for connection on %s:%u.\n",
	      inet_ntoa(saddr.sin_addr),
	      (unsigned int) ntohs(saddr.sin_port)));

      i = sizeof saddr;
      sd = accept(ssd, (struct sockaddr*) &saddr, &i);
      if (sd == -1)
	{
	  syslog(LOG_ERR, "accept() failed port %u: %m\n",
		 (unsigned int) port);
	  return 2;
	}

      closesocket(ssd);

      dbglog((LOG_ERR, "Got connection from %s:%u.\n",
	      inet_ntoa(saddr.sin_addr),
	      (unsigned int) ntohs(saddr.sin_port)));
    }
  else if (strcmp(argv[1], "-") == 0)
    {
#ifdef _WIN32
      sd = (SOCKET) GetStdHandle(STD_INPUT_HANDLE);
#else
      sd = 0;
#endif

      dbglog((LOG_ERR, "Using stdin as comm device.\n"));
    }
  else
    {
      sd = open(argv[1], O_RDWR | O_DIRECT | O_FSYNC);
      if (sd == -1)
	{
	  syslog(LOG_ERR, "File open failed on '%s': %m\n", argv[1]);
	  return 1;
	}

      dbglog((LOG_ERR, "Successfully opened device '%s'.\n", argv[1]));
    }

  i = 1;
  if (setsockopt(sd, IPPROTO_TCP, TCP_NODELAY, (const char*) &i, sizeof i))
    syslog(LOG_ERR, "setsockopt(..., TCP_NODELAY): %m\n");

  for (;;)
    {
      if (!safe_read(sd, &req, sizeof(req)))
	return 0;

      switch (req)
	{
	case IMDPROXY_REQ_INFO:
	  if (!send_info())
	    return 1;
	  break;

	case IMDPROXY_REQ_READ:
	  if (!read_data())
	    return 1;
	  break;

	case IMDPROXY_REQ_WRITE:
	  if (!write_data())
	    return 1;
	  break;

	default:
	  req = ENODEV;
	  if (write(sd, &req, 4) != 4)
	    {
	      syslog(LOG_ERR, "stdout: %m\n");
	      return 1;
	    }
	}
    }
}
