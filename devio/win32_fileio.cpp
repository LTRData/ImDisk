#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <io.h>

#include "devio_types.h"
#include "devio.h"

#define CONSOLE_MESSAGES

#ifdef CONSOLE_MESSAGES
#define DbgMsg(m) puts(m)
#else
#define DbgMsg(m)
#endif

#define ErrMsg(m) fprintf(stderr, "%s\n", m)

safeio_ssize_t __cdecl
dllread(void *fd, void *buf, safeio_size_t size, off_t_64 offset)
{
	HANDLE strm = (HANDLE)fd;

	if (!SetFilePointerEx(strm, *(PLARGE_INTEGER)&offset, NULL, FILE_BEGIN))
	{
		return -1;
	}

	DWORD readbytes;
	if (ReadFile(strm, buf, size, &readbytes, NULL))
	{
		return readbytes;
	}
	else
	{
		return -1;
	}
}

safeio_ssize_t __cdecl
dllwrite(void *fd, void *buf, safeio_size_t size, off_t_64 offset)
{
	HANDLE strm = (HANDLE)fd;

	if (!SetFilePointerEx(strm, *(PLARGE_INTEGER)&offset, NULL, FILE_BEGIN))
	{
		return -1;
	}

	DWORD readbytes;
	if (WriteFile(strm, buf, size, &readbytes, NULL))
	{
		return readbytes;
	}
	else
	{
		return -1;
	}
}

int	__cdecl
dllclose(void *fd)
{
	if (CloseHandle(fd))
	{
		return 0;
	}
	else
	{
		return -1;
	}
}

void * __cdecl
dllopen(
const char *str,
int read_only,
dllread_proc *dllread_ptr, 
dllwrite_proc *dllwrite_ptr, 
dllclose_proc *dllclose_ptr,
off_t_64 *size)
{
	*dllread_ptr = dllread;
	*dllwrite_ptr = dllwrite;
	*dllclose_ptr = dllclose;

	HANDLE handle = CreateFile(
		str, read_only != 0 ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_EXISTING, 0, NULL);

	if (handle == INVALID_HANDLE_VALUE)
		return NULL;

	if (!GetFileSizeEx(handle, (PLARGE_INTEGER)size))
	{
		CloseHandle(handle);
		return NULL;
	}

	return handle;
}
