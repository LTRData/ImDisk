/*
    General I/O support routines.

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

#ifndef _SAFEIO_H
#define _SAFEIO_H

#ifdef __cplusplus
extern "C" {
#endif

int
safe_read(int fd, void *pdata, size_t size);

int
safe_write(int fd, const void *pdata, size_t size);

#ifdef _WIN32
__inline
ssize_t
pread(int d, void *buf, size_t nbytes, __int64 offset)
{
  if (_lseeki64(d, offset, SEEK_SET) != offset)
    return (ssize_t) -1;

  return read(d, buf, nbytes);
}

__inline
ssize_t
pwrite(int d, const void *buf, size_t nbytes, __int64 offset)
{
  if (_lseeki64(d, offset, SEEK_SET) != offset)
    return (ssize_t) -1;

  return write(d, buf, nbytes);
}
#endif

#ifdef __cplusplus
}
#endif

#endif
