#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

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

  if (!safe_read(sd, &req_block.offset,
		 sizeof(req_block) - sizeof(req_block.request_code)))
    {
      syslog(LOG_ERR, "Error reading request header.\n");
      return 0;
    }

  size_t size = req_block.length < buffer_size ?
    req_block.length : buffer_size;

  dbglog((LOG_ERR, "read request %llu bytes at %llu.\n", req_block.length,
	  req_block.offset));

  ssize_t readdone = pread(fd, buf, size, offset + req_block.offset);
  if (readdone == -1)
    {
      resp_block.errorno = errno;
      resp_block.length = 0;
      syslog(LOG_ERR, "Device read: %m\n");
    }
  else
    {
      resp_block.errorno = 0;
      resp_block.length = readdone;
    }

  dbglog((LOG_ERR, "read done reporting/sending %llu bytes.\n",
	  resp_block.length));

  if (!safe_write(sd, &resp_block, sizeof resp_block))
    {
      syslog(LOG_ERR, "Warning: I/O stream incosistency.\n");
      return 0;
    }

  if (resp_block.errorno == 0)
    if (!safe_write(sd, buf, resp_block.length))
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

  dbglog((LOG_ERR, "write request %llu bytes at %llu.\n", req_block.length,
	  req_block.offset));

  if (req_block.length > buffer_size)
    {
      syslog(LOG_ERR, "Too big block write requested: %u bytes.\n",
	     (int)req_block.length);
      return 0;
    }

  if (!safe_read(sd, buf, req_block.length))
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
	pwrite(fd, buf, req_block.length, offset + req_block.offset);
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

      dbglog((LOG_ERR, "write done reporting/sending %llu bytes.\n",
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
	      "Default number of blocks is 0. This can only be used if the client knows the\n"
	      "size without help from this service.\n"
	      "Default alignment is %u bytes.\n"
	      "Default buffer size is %u bytes.\n",
	      argv[0],
	      DEF_REQ_ALIGNMENT,
	      DEF_BUFFER_SIZE);
      return -1;
    }

  port = strtoul(argv[1], NULL, 0);

  if (devio_info.flags & IMDPROXY_FLAG_RO)
    fd = open(argv[2], O_RDONLY | O_DIRECT | O_FSYNC);
  else
    fd = open(argv[2], O_RDWR | O_DIRECT | O_FSYNC);
  if (fd == -1)
    {
      syslog(LOG_ERR, "Device open failed: %m\n");
      return 1;
    }

  if (argc > 3)
    {
      char suf = 0;
      if (sscanf(argv[3], "%llu%c", &devio_info.file_size, &suf) == 2)
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

  if (argc > 4)
    {
      char suf = 0;
      if (sscanf(argv[4], "%llu%c", &offset, &suf) == 2)
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
    sscanf(argv[4], "%llu", &devio_info.req_alignment);
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

      close(ssd);

      dbglog((LOG_ERR, "Got connection from %s:%u.\n",
	      inet_ntoa(saddr.sin_addr),
	      (unsigned int) ntohs(saddr.sin_port)));
    }
  else
    {
      sd = open(argv[1], O_RDWR | O_DIRECT | O_FSYNC);
      if (sd == -1)
	{
	  syslog(LOG_ERR, "File open failed: %m\n");
	  return 1;
	}
    }

  i = 1;
  if (setsockopt(sd, IPPROTO_TCP, TCP_NODELAY, &i, sizeof i))
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
