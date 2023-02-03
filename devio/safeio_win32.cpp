/*
    General I/O support routines for WIN32 environments.

    Copyright (C) 2005-2023 Olof Lagerkvist.

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

#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <windows.h>
#include <winsock.h>

#include "..\inc\wio.hpp"

#include "devio_types.h"
#include "safeio.h"

WOverlapped Overlapped;

extern "C"
int
safe_read(SOCKET fd, void *pdata, safeio_size_t size)
{
  return Overlapped.BufRecv((HANDLE) fd, pdata, size) == (DWORD)size;
}

extern "C"
int
safe_write(SOCKET fd, const void *pdata, safeio_size_t size)
{
  return Overlapped.BufSend((HANDLE) fd, pdata, size);
}
