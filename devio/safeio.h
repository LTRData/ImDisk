/*
General I/O support routines.

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

#ifndef _INC_SAFEIO_
#define _INC_SAFEIO_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
    __inline int pread(int d, void *buf, int nbytes, __int64 offset)
    {
        int rc;

        if (_lseeki64(d, offset, SEEK_SET) != offset)
        {
            return -1;
        }

        rc = _read(d, buf, nbytes);

        if ((nbytes > 0) && (rc >= 0) && (rc < nbytes))
        {
            memset((char*)buf + rc, 0, nbytes - rc);
            rc = nbytes;
        }

        return rc;
    }

    __inline int pwrite(int d, const void *buf, int nbytes, __int64 offset)
    {
        int rc;

        if (_lseeki64(d, offset, SEEK_SET) != offset)
        {
            return -1;
        }

        rc = _write(d, buf, nbytes);

        if ((rc >= 0) || (errno != 28))
        {
            return rc;
        }

        fprintf(stderr, "Extending file to %I64u bytes.\n", offset + nbytes);

        if (_lseeki64(d, offset + nbytes, SEEK_SET) != (offset + nbytes))
        {
            return -1;
        }

        if (!SetEndOfFile((HANDLE)_get_osfhandle(d)))
        {
            errno = 28;
            return -1;
        }

        if (_lseeki64(d, offset, SEEK_SET) != offset)
        {
            return -1;
        }

        return _write(d, buf, nbytes);
    }

#endif

    int safe_read(SOCKET fd, void *pdata, safeio_size_t size);

    int safe_write(SOCKET fd, const void *pdata, safeio_size_t size);

#ifdef __cplusplus
}
#endif

#endif // _INC_SAFEIO_
