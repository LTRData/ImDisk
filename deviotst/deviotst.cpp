#include <windows.h>
#include <winsock.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>

#include "../inc/imdproxy.h"
#include "../inc/wio.hpp"

__inline BOOL
oem_printf(FILE *stream, LPCSTR lpMessage, ...)
{
  va_list param_list;
  LPSTR lpBuf = NULL;

  va_start(param_list, lpMessage);

  if (!FormatMessageA(78 |
		      FORMAT_MESSAGE_ALLOCATE_BUFFER |
		      FORMAT_MESSAGE_FROM_STRING, lpMessage, 0, 0,
		      (LPSTR) &lpBuf, 0, &param_list))
    return FALSE;

  CharToOemA(lpBuf, lpBuf);
  fprintf(stream, "%s", lpBuf);
  LocalFree(lpBuf);
  fflush(stream);
  return TRUE;
}

void win_perror(LPCSTR __errmsg)
{
  LPSTR errmsg = NULL;
  int __win_errno = GetLastError();

  FormatMessageA(FORMAT_MESSAGE_MAX_WIDTH_MASK |
		 FORMAT_MESSAGE_FROM_SYSTEM |
		 FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, __win_errno,
		 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		 (LPSTR)&errmsg, 0, NULL);

  if (__errmsg ? !!*__errmsg : FALSE)
    if (errmsg != NULL)
      oem_printf(stderr, "%1: %2%n", __errmsg, errmsg);
    else
      fprintf(stderr, "%s: Win32 error %u\n", __errmsg, (DWORD) __win_errno);
  else
    if (errmsg != NULL)
      oem_printf(stderr, "%1%n", errmsg);
    else
      fprintf(stderr, "Win32 error %u\n", (DWORD) __win_errno);

  if (errmsg != NULL)
    LocalFree(errmsg);
}

int __cdecl
wmain(int argc, LPWSTR *argv)
{
  if (argc < 2)
    {
      fputs("Usage:\r\n"
	    "deviotst host[:port] [info]\r\n"
	    "deviotst host[:port] read|write offset size\r\n",
	    stderr);

      return 1;
    }

  WSADATA wd;
  if (WSAStartup(0x0101, &wd))
    {
      fputs("Winsock initialization failure.\r\n", stderr);
      return -1;
    }

  HANDLE hIO = INVALID_HANDLE_VALUE;

  LPWSTR hostname = wcstok(argv[1], L":");
  LPWSTR port = wcstok(NULL, L"");
  if (port == NULL)
    port = L"9000";

  hIO = (HANDLE) ConnectTCP(hostname, port);
  if (hIO == INVALID_HANDLE_VALUE)
    {
      win_perror("Connect failed");
      return 1;
    }

  BOOL b = TRUE;
  if (setsockopt((SOCKET) hIO, IPPROTO_TCP, TCP_NODELAY, (LPCSTR) &b,
		 sizeof b))
    {
      win_perror("setsockopt failed");
      return 1;
    }

  WOverlapped Overlapped;
  HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (argc < 4)
    {
      ULONGLONG req = IMDPROXY_REQ_INFO;
      if (!Overlapped.BufSend(hIO, &req, sizeof(req)))
	{
	  win_perror("Send failed");
	  return 1;
	}

      IMDPROXY_INFO_RESP info_resp = { 0 };
      if (Overlapped.BufRecv(hIO, &info_resp, sizeof info_resp) !=
	  sizeof info_resp)
	{
	  win_perror("Receive failed");
	  return 1;
	}

      fprintf(stderr,
	      "Size: %I64u bytes, flags: %I64u, req alignm: %I64u bytes.\n",
	      info_resp.file_size,
	      info_resp.flags,
	      info_resp.req_alignment);
    }
  else if (_wcsicmp(argv[2], L"read") == 0)
    {
      IMDPROXY_READ_REQ read_req;
      read_req.request_code = IMDPROXY_REQ_READ;
      swscanf(argv[3], L"%I64u", &read_req.offset);
      swscanf(argv[4], L"%I64u", &read_req.length);

      char *buf = (char *) malloc((SIZE_T) read_req.length);
      if (buf == NULL)
	{
	  win_perror("Allocation failed");
	  return 1;
	}

      if (!Overlapped.BufSend(hIO, &read_req, sizeof read_req))
	{
	  win_perror("Send failed");
	  return 1;
	}

      IMDPROXY_READ_RESP read_resp = { 0 };
      if (Overlapped.BufRecv(hIO, &read_resp, sizeof read_resp) !=
	  sizeof read_resp)
	{
	  win_perror("Receive failed");
	  return 1;
	}

      if (read_resp.errorno != 0)
	{
	  fprintf(stderr, "Device I/O error: %s\n",
		  strerror((int) read_resp.errorno));
	  return 1;
	}

      if (Overlapped.BufRecv(hIO, buf, (DWORD) read_resp.length) !=
	  read_resp.length)
	{
	  win_perror("Receive failed");
	  return 1;
	}

      DWORD dw;
      if (!WriteFile(hStdOut, buf, (DWORD) read_resp.length, &dw, NULL))
	{
	  win_perror("Write failed");
	  return 1;
	}

      if (dw != read_resp.length)
	{
	  win_perror("Write failed");
	  return 1;
	}
    }
  else if (_wcsicmp(argv[2], L"write") == 0)
    {
      IMDPROXY_WRITE_REQ write_req;
      write_req.request_code = IMDPROXY_REQ_WRITE;
      swscanf(argv[3], L"%I64u", &write_req.offset);
      swscanf(argv[4], L"%I64u", &write_req.length);

      char *buf = (char *) malloc((SIZE_T) write_req.length);
      if (buf == NULL)
	{
	  win_perror("Allocation failed");
	  return 1;
	}

      DWORD dw;
      if (!ReadFile(hStdIn, buf, (DWORD) write_req.length, &dw, NULL))
	{
	  win_perror("Read failed");
	  return 1;
	}

      if (dw != write_req.length)
	fprintf(stderr, "Warning: Requested %u got %u bytes on stdin.\n",
		(DWORD) write_req.length, dw);

      if (!Overlapped.BufSend(hIO, &write_req, sizeof write_req))
	{
	  win_perror("Send failed");
	  return 1;
	}

      if (!Overlapped.BufSend(hIO, buf, (DWORD) write_req.length))
	{
	  win_perror("Send failed");
	  return 1;
	}

      IMDPROXY_WRITE_RESP write_resp = { 0 };
      if (Overlapped.BufRecv(hIO, &write_resp, sizeof write_resp) !=
	  sizeof write_resp)
	{
	  win_perror("Receive failed");
	  return 1;
	}

      if (write_resp.errorno != 0)
	{
	  fprintf(stderr, "Device I/O error: %s\n",
		  strerror((int) write_resp.errorno));
	  return 1;
	}
    }

  return 0;
}

extern "C"
__declspec(noreturn)
void __cdecl
wmainCRTStartup()
{
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLine(), &argc);

  if (argv == NULL)
    {
      MessageBoxA(NULL, "This program requires Windows NT/2000/XP.", "ImDisk",
		  MB_ICONSTOP);
      ExitProcess((UINT)-1);
    }

  exit(wmain(argc, argv));
}
