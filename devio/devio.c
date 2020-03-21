/*
    Server end for ImDisk Virtual Disk Driver.

    Copyright (C) 2005-2008 Olof Lagerkvist.

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

#define DEBUG

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

  fflush(Stream);
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
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define closesocket(s) close(s)
#ifndef O_BINARY
#define O_BINARY       0
#endif
#define ULL_FMT   "%llu"

typedef off_t off_t_64;

#define _lseeki64 lseek

#endif

#include "../inc/imdproxy.h"
#include "safeio.h"

#ifndef O_DIRECT
#define O_DIRECT 0
#endif

#ifndef O_FSYNC
#define O_FSYNC 0
#endif

#define DEF_BUFFER_SIZE (16 << 20)

#ifdef DEBUG
#define dbglog(x) syslog x
#else
#define dbglog(x)
#endif

typedef union _LONGLONG_SWAP
{
  long long v64;
  struct
  {
    long v1;
    long v2;
  } v32;
} longlongswap;

int fd = -1;
int sd = -1;
void *buf = NULL;
void *buf2 = NULL;
u_long buffer_size = DEF_BUFFER_SIZE;
off_t_64 offset = 0;
IMDPROXY_INFO_RESP devio_info = { 0 };
char vhd_mode = 0;

struct _VHD_INFO
{
  struct _VHD_FOOTER
  {
    char Cookie[8];
    u_long Features;
    u_long FileFormatVersion;
    off_t_64 DataOffset;
    u_long TimeStamp;
    u_long CreatorApplication;
    u_long CreatorVersion;
    u_long CreatorHostOS;
    off_t_64 OriginalSize;
    off_t_64 CurrentSize;
    u_long DiskGeometry;
    u_long DiskType;
    u_long Checksum;
    char UniqueID[16];
    char SavedState;
    char Padding[427];
  } Footer;

  struct _VHD_HEADER
  {
    char Cookie[8];
    off_t_64 DataOffset;
    off_t_64 TableOffset;
    u_long HeaderVersion;
    u_long MaxTableEntries;
    u_long BlockSize;
    u_long Checksum;
    char ParentUniqueID[16];
    u_long ParentTimeStamp;
    u_long Reserved1;
    wchar_t ParentName[256];
    struct _VHD_PARENT_LOCATOR
    {
      u_long PlatformCode;
      u_long PlatformDataSpace;
      u_long PlatformDataLength;
      u_long Reserved1;
      off_t_64 PlatformDataOffset;
    } ParentLocator[8];
    char Padding[256];
  } Header;

} vhd_info = { { { 0 } } };

u_long block_size = 0;
u_int sector_size = 512;
u_long table_offset = 0;
short block_shift = 0;
short sector_shift = 0;
void *geometry = &vhd_info.Footer.DiskGeometry;
off_t_64 current_size = 0;

int
send_info()
{
  return safe_write(sd, &devio_info, sizeof devio_info);
}

ssize_t
vhd_read(void *buf, size_t size, off_t_64 offset)
{
  off_t_64 block_number;
  off_t_64 data_offset;
  size_t in_block_offset;
  u_long block_offset;
  size_t first_size = size;
  off_t_64 second_offset = 0;
  size_t second_size = 0;
  ssize_t readdone;

  dbglog((LOG_ERR, "vhd_read: Request " ULL_FMT " bytes at " ULL_FMT ".\n",
	  (off_t_64)size, (off_t_64)offset));

  if (offset + size > current_size)
    return 0;

  block_number = (size_t) (offset >> block_shift);
  data_offset = table_offset + (block_number << 2);
  in_block_offset = (size_t) (offset & (block_size - 1));
  if (first_size + in_block_offset > block_size)
    {
      first_size = block_size - in_block_offset;
      second_size = size - first_size;
      second_offset = offset + first_size;
    }

  readdone = pread(fd, &block_offset, sizeof(block_offset), data_offset);
  if (readdone != sizeof(block_offset))
    {
      if (errno == 0)
	errno = E2BIG;

      return (ssize_t) -1;
    }

  memset(buf, 0, size);

  if (block_offset == 0xFFFFFFFF)
    readdone = first_size;
  else
    {
      block_offset = ntohl(block_offset);

      data_offset =
	(((off_t_64) block_offset) << sector_shift) + sector_size +
	in_block_offset;

      readdone = pread(fd, buf, (size_t) first_size, data_offset);
      if (readdone == -1)
	return (ssize_t) -1;
    }

  if (second_size > 0)
    {
      size_t second_read =
	vhd_read((char*) buf + first_size, second_size, second_offset);

      if (second_read == -1)
	return (ssize_t) -1;

      readdone += second_read;
    }

  return readdone;
}

ssize_t
vhd_write(void *buf, size_t size, off_t_64 offset)
{
  off_t_64 block_number;
  off_t_64 data_offset;
  size_t in_block_offset;
  u_long block_offset;
  size_t first_size = size;
  off_t_64 second_offset = 0;
  size_t second_size = 0;
  ssize_t readdone;
  ssize_t writedone;
  off_t_64 bitmap_offset;
  size_t first_size_nqwords;

  dbglog((LOG_ERR, "vhd_write: Request " ULL_FMT " bytes at " ULL_FMT ".\n",
	  (off_t_64)size, (off_t_64)offset));

  if (offset + size > current_size)
    return 0;

  block_number = offset >> block_shift;
  data_offset = table_offset + (block_number << 2);
  in_block_offset = (size_t) (offset & (block_size - 1));
  if (first_size + in_block_offset > block_size)
    {
      first_size = block_size - in_block_offset;
      second_size = size - first_size;
      second_offset = offset + first_size;
    }
  first_size_nqwords = (first_size + 7) >> 3;

  readdone = pread(fd, &block_offset, sizeof(block_offset), data_offset);
  if (readdone != sizeof(block_offset))
    {
      if (errno == 0)
	errno = E2BIG;

      return (ssize_t) -1;
    }

  // Alocate a new block if not already defined
  if (block_offset == 0xFFFFFFFF)
    {
      off_t_64 block_offset_bytes;
      char *new_block_buf;
      long long *buf_ptr;

      // First check if new block is all zeroes, in that case don't allocate
      // a new block in the vhd file
      for (buf_ptr = (long long*)buf;
	   (buf_ptr < (long long*)buf + first_size_nqwords) & (*buf_ptr == 0);
	   buf_ptr++);
      if (buf_ptr >= (long long*)buf + first_size_nqwords)
	{
	  dbglog((LOG_ERR, "vhd_write: New empty block not added to vhd file "
		  "backing " ULL_FMT " bytes at " ULL_FMT ".\n",
		  (off_t_64)first_size, (off_t_64)offset));

	  writedone = first_size;

	  if (second_size > 0)
	    {
	      size_t second_write =
		vhd_write((char*) buf + first_size, second_size,
			  second_offset);

	      if (second_write == -1)
		return (ssize_t) -1;

	      writedone += second_write;
	    }

	  return writedone;
	}

      dbglog((LOG_ERR, "vhd_write: Adding new block to vhd file backing "
	      ULL_FMT " bytes at " ULL_FMT ".\n",
	      (off_t_64)first_size, (off_t_64)offset));

      new_block_buf = (char *)
	malloc(sector_size + block_size + sizeof(vhd_info.Footer));
      if (new_block_buf == NULL)
	return (ssize_t) -1;

      // New block is placed where the footer currently is
      block_offset_bytes =
	_lseeki64(fd, -(off_t_64) sizeof(vhd_info.Footer), SEEK_END);
      if (block_offset_bytes == -1)
	{
	  free(new_block_buf);
	  return (ssize_t) -1;
	}

      // Store pointer to new block start sector in BAT
      block_offset = htonl((u_long) (block_offset_bytes >> sector_shift));
      readdone = pwrite(fd, &block_offset, sizeof(block_offset), data_offset);
      if (readdone != sizeof(block_offset))
	{
	  free(new_block_buf);

	  if (errno == 0)
	    errno = E2BIG;

	  return (ssize_t) -1;
	}

      // Initialize new block with zeroes followed by the new footer
      memset(new_block_buf, 0, sector_size + block_size);
      memcpy(new_block_buf + sector_size + block_size, &vhd_info.Footer,
	     sizeof(vhd_info.Footer));

      readdone = pwrite(fd, new_block_buf,
			sector_size + block_size + sizeof(vhd_info.Footer),
			block_offset_bytes);
      if (readdone != sector_size + block_size + sizeof(vhd_info.Footer))
	{
	  free(new_block_buf);

	  if (errno == 0)
	    errno = E2BIG;

	  return (ssize_t) -1;
	}

      free(new_block_buf);
    }

  block_offset = ntohl(block_offset);
  data_offset = (((off_t_64) block_offset) << sector_shift) + sector_size +
    in_block_offset;

  writedone = pwrite(fd, buf, (size_t) first_size, data_offset);
  if (writedone == -1)
    return (ssize_t) -1;

  bitmap_offset = ((off_t_64) block_offset << sector_shift) +
    (in_block_offset >> 3);
  memset(buf2, 0xFF, first_size_nqwords);

  readdone = pwrite(fd, buf2, first_size_nqwords, bitmap_offset);
  if (readdone != first_size_nqwords)
    {
      if (errno == 0)
	errno = E2BIG;

      return (ssize_t) -1;
    }

  if (second_size > 0)
    {
      size_t second_write =
	vhd_write((char*) buf + first_size, second_size, second_offset);

      if (second_write == -1)
	return (ssize_t) -1;

      writedone += second_write;
    }

  return writedone;
}

ssize_t
dev_read(void *buf, size_t size, off_t_64 offset)
{
  if (vhd_mode)
    return vhd_read(buf, size, offset);
  else
    return pread(fd, buf, size, offset);
}

ssize_t
dev_write(void *buf, size_t size, off_t_64 offset)
{
  if (vhd_mode)
    return vhd_write(buf, size, offset);
  else
    return pwrite(fd, buf, size, offset);
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

  dbglog((LOG_ERR, "read request " ULL_FMT " bytes at " ULL_FMT " + "
	  ULL_FMT " = " ULL_FMT ".\n",
	  req_block.length, req_block.offset, offset,
	  req_block.offset + offset));

  memset(buf, 0, size);

  readdone =
    dev_read(buf, (size_t) size, (off_t_64) (offset + req_block.offset));
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

  dbglog((LOG_ERR, "write request " ULL_FMT " bytes at " ULL_FMT " + "
	  ULL_FMT " = " ULL_FMT ".\n",
	  req_block.length, req_block.offset, offset,
	  req_block.offset + offset));

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
      ssize_t writedone = dev_write(buf, (size_t) req_block.length,
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
	syslog(LOG_ERR, "Partial write at " ULL_FMT ".\n",
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
  ssize_t readdone;
  int partition_number = 0;
  char mbr[512];

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
	      "%s [-r] tcp-port|commdev diskdev [partitionnumber] [alignm] [buffersize]\n"
	      "\n"
	      "Default number of blocks is 0. When running on Windows the program will try to\n"
	      "get the size of the image file or partition automatically in this case,\n"
	      "otherwise the client must know the exact size without help from this service.\n"
	      "Default alignment is %u bytes.\n"
	      "Default buffer size is %i bytes.\n",
	      argv[0],
	      argv[0],
	      sector_size,
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
      syslog(LOG_ERR, "Failed to open '%s': %m\n", argv[2]);
      return 1;
    }

  printf("Successfully opened '%s'.\n", argv[2]);

  // Autodetect Microsoft .vhd files
  readdone = pread(fd, &vhd_info, (size_t) sizeof(vhd_info), 0);
  if ((readdone == sizeof(vhd_info)) &
      ((strncmp(vhd_info.Header.Cookie, "cxsparse", 8) == 0) &
       (strncmp(vhd_info.Footer.Cookie, "conectix", 8) == 0)))
    {
      // Calculate vhd shifts
      ((longlongswap*)&current_size)->v32.v1 =
	ntohl(((longlongswap*)&vhd_info.Footer.CurrentSize)->v32.v2);
      ((longlongswap*)&current_size)->v32.v2 =
	ntohl(((longlongswap*)&vhd_info.Footer.CurrentSize)->v32.v1);

      ((longlongswap*)&table_offset)->v32.v1 =
	ntohl(((longlongswap*)&vhd_info.Header.TableOffset)->v32.v2);
      ((longlongswap*)&table_offset)->v32.v2 =
	ntohl(((longlongswap*)&vhd_info.Header.TableOffset)->v32.v1);

      sector_size = (int)
	(current_size / ntohs(*(u_short*) geometry) /
	 ((u_char*) geometry)[2] / ((u_char*) geometry)[3]);

      block_size = ntohl(vhd_info.Header.BlockSize);

      for (block_shift = 0;
	   (block_shift < 64) &
	     ((((u_long) 1) << block_shift) != block_size);
	   block_shift++);

      devio_info.file_size = current_size;

      vhd_mode = 1;

      puts("Detected dynamically expanding Microsoft .vhd image file format.");
    }

  for (sector_shift = 0;
       (sector_shift < 64) & ((((u_long) 1) << sector_shift) != sector_size);
       sector_shift++);

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
      else if ((devio_info.file_size >= 0) & (devio_info.file_size <= 8))
	{
	  partition_number = (int) devio_info.file_size;
	  devio_info.file_size = current_size;
	}
      else
	devio_info.file_size <<= 9;
    }
  else
    partition_number = 1;

#ifdef _WIN32
  if (devio_info.file_size == 0)
    {
      HANDLE h = (HANDLE) _get_osfhandle(fd);
      BY_HANDLE_FILE_INFORMATION by_handle_file_info;

      // If not regular disk file, try to lock volume using FSCTL operation.
      if (!GetFileInformationByHandle(h, &by_handle_file_info))
	{
	  DWORD dw;
	  FlushFileBuffers(h);
	  if (DeviceIoControl(h, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0,
			      &dw, NULL))
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
				   &partition_info, sizeof(partition_info),
				   &dw, NULL))
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
#else
  if (devio_info.file_size == 0)
    {
      struct stat file_stat = { 0 };
      if (fstat(fd, &file_stat) == 0)
	devio_info.file_size = file_stat.st_size;
      else
	syslog(LOG_ERR, "Cannot determine size of image/partition: %m\n");
    }
#endif

  if (current_size == 0)
    current_size = devio_info.file_size;

  if (devio_info.file_size != 0)
    printf("Image size used: " ULL_FMT " bytes.\n", devio_info.file_size);

  if ((partition_number >= 1) & (partition_number <= 8))
    {
      if (dev_read(mbr, 512, 0) == -1)
	syslog(LOG_ERR, "Error reading device: %m\n");
      else if ((*(u_short*)(mbr + 0x01FE) == 0xAA55) &
	       (*(u_char*)(mbr + 0x01BD) == 0x00))
	{
	  puts("Detected a master boot record at sector 0.");

	  if ((partition_number >=5) & (partition_number <= 8))
	    {
	      int i = 0;
	      offset = 0;
	      for (i = 0; i < 4; i++)
		switch (*(mbr + 512 - 66 + (i << 4) + 4))
		  {
		  case 0x05: case 0x0F:
		    offset = (off_t_64)
		      (*(u_int*)(mbr + 512 - 66 + (i << 4) + 8)) <<
		      sector_shift;
		    if (dev_read(mbr, 512, offset) == 512)
		      if ((*(u_short*)(mbr + 0x01FE) == 0xAA55) &
			  (*(u_char*)(mbr + 0x01BD) == 0x00))
			partition_number -= 4;
		  }
	    }

	  if ((partition_number >=1) & (partition_number <= 4))
	    {
	      offset = (off_t_64)
		(*(u_int*)(mbr + 512 - 66 + ((partition_number-1) << 4) + 8))
		<< sector_shift;
	      devio_info.file_size = (off_t_64)
		(*(u_int*)(mbr + 512 - 66 + ((partition_number-1) << 4) + 12))
		<< sector_shift;

	      if ((offset == 0) | (devio_info.file_size == 0) |
		  ((current_size != 0) &
		   (offset + (off_t_64) devio_info.file_size > current_size)))
		{
		  syslog(LOG_ERR,
			 "Partition %i not found.\n", partition_number);
		  return 1;
		}

	      printf("Using partition %i.\n", partition_number);
	    }
	}
    }

  if (offset == 0)
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

	argc--;
	argv++;
      }

  if (argc > 4)
    sscanf(argv[4], ULL_FMT, &devio_info.req_alignment);
  else
    devio_info.req_alignment = sector_size;

  if (argc > 5)
    sscanf(argv[5], "%lu", &buffer_size);

  printf("Total size: " ULL_FMT " bytes. Using " ULL_FMT " bytes from offset "
	 ULL_FMT ".\n",
	 current_size, devio_info.file_size, offset);

  buf = malloc(buffer_size);
  buf2 = malloc(buffer_size);
  if ((buf == NULL) | (buf2 == NULL))
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

      printf("Waiting for connection on port %u. Press Ctrl+C to cancel.\n",
	     (unsigned int) ntohs(saddr.sin_port));

      i = sizeof saddr;
      sd = accept(ssd, (struct sockaddr*) &saddr, &i);
      if (sd == -1)
	{
	  syslog(LOG_ERR, "accept() failed port %u: %m\n",
		 (unsigned int) port);
	  return 2;
	}

      closesocket(ssd);

      printf("Got connection from %s:%u.\n",
	     inet_ntoa(saddr.sin_addr),
	     (unsigned int) ntohs(saddr.sin_port));
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

      printf("Waiting for I/O on device '%s'.\n", argv[1]);
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
