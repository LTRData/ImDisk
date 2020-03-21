#include <unistd.h>
#include <syslog.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include "safeio.h"

int
safe_read(int fd, void *pdata, size_t size)
{
  char *data = (char*) pdata;
  size_t sizeleft = size;

  while (sizeleft > 0)
    {
      ssize_t sizedone = read(fd, data, sizeleft);
      if (sizedone == -1)
	{
	  syslog(LOG_ERR, "safe_read(): %m\n");
	  return 0;
	}

      if (sizedone == 0)
	return 0;

      sizeleft -= sizedone;
      data += sizedone;
    }

  return 1;
}

int
safe_write(int fd, const void *pdata, size_t size)
{
  const char *data = (char*) pdata;
  size_t sizeleft = size;

  while (sizeleft > 0)
    {
      ssize_t sizedone = write(fd, data, sizeleft);
      if (sizedone == -1)
	{
	  syslog(LOG_ERR, "safe_write(): %m\n");
	  return 0;
	}

      if (sizedone == 0)
	return 0;

      sizeleft -= sizedone;
      data += sizedone;
    }

  return 1;
}

