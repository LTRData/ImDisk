/*
  General I/O support routines for POSIX environments.

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

#include <unistd.h>
#include <syslog.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include "devio_types.h"
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
