/*
    I/O Packet Forwarder Service for the ImDisk Virtual Disk Driver for
    Windows NT/2000/XP.
    This service redirects I/O requests sent to the ImDisk Virtual Disk Driver
    to another computer through a serial communication interface or by opening
    a TCP/IP connection.

    Copyright (C) 2005-2006 Olof Lagerkvist.

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

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0333
#endif

#include <windows.h>
#include <winioctl.h>
#include <ntsecapi.h>
#include <winsock.h>

#include <stdlib.h>
#include <process.h>

#include "..\inc\imdisk.h"
#include "..\inc\imdproxy.h"
#include "..\inc\wio.hpp"

#pragma comment(linker, "-subsystem:windows,3.51")

SERVICE_STATUS ImDiskSvcStatus;  
SERVICE_STATUS_HANDLE ImDiskSvcStatusHandle; 
HANDLE ImDiskSvcStopEvent = NULL;

// Define DEBUG if you want debug output.
//#define DEBUG

#ifndef DEBUG

#define KdPrint(x)
#define KdPrintLastError(x)

#else

#define KdPrint(x)          DbgPrintF          x
#define KdPrintLastError(x) DbgPrintLastError  x

BOOL
DbgPrintF(LPCSTR Message, ...)
{
  va_list param_list;
  LPSTR lpBuf = NULL;

  va_start(param_list, Message);

  if (!FormatMessageA(FORMAT_MESSAGE_MAX_WIDTH_MASK |
		      FORMAT_MESSAGE_ALLOCATE_BUFFER |
		      FORMAT_MESSAGE_FROM_STRING, Message, 0, 0,
		      (LPSTR) &lpBuf, 0, &param_list))
    return FALSE;

  OutputDebugStringA(lpBuf);
  LocalFree(lpBuf);
  return TRUE;
}

void
DbgPrintLastError(LPCSTR Prefix)
{
  LPWSTR MsgBuf;

  FormatMessageA(FORMAT_MESSAGE_MAX_WIDTH_MASK |
		 FORMAT_MESSAGE_ALLOCATE_BUFFER |
		 FORMAT_MESSAGE_FROM_SYSTEM |
		 FORMAT_MESSAGE_IGNORE_INSERTS,
		 NULL, GetLastError(), 0, (LPSTR) &MsgBuf, 0, NULL);

  DbgPrintF("ImDskSvc: %1: %2%n", Prefix, MsgBuf);

  LocalFree(MsgBuf);
}
#endif

class ImDiskSvcServerSession
{
  HANDLE hPipe;
  WOverlapped Overlapped;

  DWORD
  CALLBACK
  Thread()
  {
    IMDPROXY_CONNECT_REQ ConnectReq;

    KdPrint(("ImDskSvc: Thread created.%n"));

    if (Overlapped.BufRecv(hPipe, &ConnectReq.request_code,
			   sizeof ConnectReq.request_code) !=
	sizeof ConnectReq.request_code)
      {
	KdPrintLastError(("Overlapped.BufRecv() failed"));

	delete this;
	return 0;
      }

    if (ConnectReq.request_code != IMDPROXY_REQ_CONNECT)
      {
	delete this;
	return 0;
      }

    if (Overlapped.BufRecv(hPipe,
			   ((PUCHAR) &ConnectReq) +
			   sizeof(ConnectReq.request_code),
			   sizeof(ConnectReq) -
			   sizeof(ConnectReq.request_code)) !=
	sizeof(ConnectReq) -
	sizeof(ConnectReq.request_code))
      {
	KdPrintLastError(("Overlapped.BufRecv() failed"));

	delete this;
	return 0;
      }

    if ((ConnectReq.length == 0) | (ConnectReq.length > 520))
      {
	KdPrint(("ImDskSvc: Bad connection string length received (%1!u!).%n",
		 ConnectReq.length));

	delete this;
	return 0;
      }

    LPWSTR ConnectionString = (LPWSTR) malloc((size_t) ConnectReq.length + 2);
    if (ConnectionString == NULL)
      {
	KdPrintLastError(("malloc() failed"));

	delete this;
	return 0;
      }

    if (Overlapped.BufRecv(hPipe, ConnectionString,
			   (DWORD) ConnectReq.length) !=
	ConnectReq.length)
      {
	KdPrintLastError(("Overlapped.BufRecv() failed"));

	free(ConnectionString);
	delete this;
	return 0;
      }

    IMDPROXY_CONNECT_RESP connect_resp = { 0 };

    ConnectionString[ConnectReq.length / sizeof *ConnectionString] = 0;

    HANDLE hTarget;
    switch (IMDISK_PROXY_TYPE(ConnectReq.flags))
      {
      case IMDISK_PROXY_TYPE_COMM:
	{
	  LPWSTR FileName = wcstok(ConnectionString, L": ");

	  KdPrint(("ImDskSvc: Connecting to '%1!ws!'.%n", FileName));

	  hTarget = CreateFile(FileName,
			       GENERIC_READ | GENERIC_WRITE,
			       0,
			       NULL,
			       OPEN_EXISTING,
			       0,
			       NULL);

	  if (hTarget == INVALID_HANDLE_VALUE)
	    {
	      connect_resp.error_code = GetLastError();

	      KdPrintLastError(("CreateFile() failed"));
	      free(ConnectionString);

	      Overlapped.BufSend(hPipe, &connect_resp, sizeof connect_resp);

	      delete this;
	      return 0;
	    }

	  LPWSTR DCBAndTimeouts = wcstok(NULL, L"");
	  if (DCBAndTimeouts != NULL)
	    {
	      DCB dcb = { 0 };
	      COMMTIMEOUTS timeouts = { 0 };

	      if (DCBAndTimeouts[0] == L' ')
		++DCBAndTimeouts;

	      KdPrint(("ImDskSvc: Configuring '%1!ws!'.%n", DCBAndTimeouts));

	      GetCommState(hTarget, &dcb);
	      GetCommTimeouts(hTarget, &timeouts);
	      BuildCommDCBAndTimeouts(DCBAndTimeouts, &dcb, &timeouts);
	      SetCommState(hTarget, &dcb);
	      SetCommTimeouts(hTarget, &timeouts);
	    }

	  KdPrint(("ImDskSvc: Connected to '%1!ws!' and configured.%n",
		   FileName));

	  break;
	}

      case IMDISK_PROXY_TYPE_TCP:
	{
	  LPWSTR ServerName = wcstok(ConnectionString, L":");
	  LPWSTR PortName = wcstok(NULL, L"");

	  if (PortName == NULL)
	    PortName = L"9000";

	  KdPrint(("ImDskSvc: Connecting to '%1!ws!:%2!ws!'.%n",
		   ServerName, PortName));

	  hTarget = (HANDLE) ConnectTCP(ServerName, PortName);
	  if (hTarget == INVALID_HANDLE_VALUE)
	    {
	      connect_resp.error_code = GetLastError();

	      KdPrintLastError(("ConnectTCP() failed"));
	      free(ConnectionString);

	      Overlapped.BufSend(hPipe, &connect_resp, sizeof connect_resp);

	      delete this;
	      return 0;
	    }

	  bool b = true;
	  setsockopt((SOCKET) hTarget, IPPROTO_TCP, TCP_NODELAY, (LPCSTR) &b,
		     sizeof b);

	  KdPrint(("ImDskSvc: Connected to '%1!ws!:%2!ws!' and configured.%n",
		   ServerName, PortName));

	  break;
	}

      default:
	KdPrint(("ImDskSvc: Unsupported connection type (%1!#x!).%n",
		 IMDISK_PROXY_TYPE(ConnectReq.flags)));

	connect_resp.error_code = (ULONGLONG) -1;
	Overlapped.BufSend(hPipe, &connect_resp, sizeof connect_resp);

	free(ConnectionString);
	delete this;
	return 0;
      }

    free(ConnectionString);

    HANDLE hDriver = CreateFile(IMDISK_CTL_DOSDEV_NAME,
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_EXISTING,
				0,
				NULL);

    if (hDriver == INVALID_HANDLE_VALUE)
      {
	connect_resp.error_code = GetLastError();

	KdPrintLastError(("Opening ImDiskCtl device failed"));
	CloseHandle(hTarget);

	Overlapped.BufSend(hPipe, &connect_resp, sizeof connect_resp);

	delete this;
	return 0;
      }

    DWORD dw;
    if (!DeviceIoControl(hDriver,
			 IOCTL_IMDISK_REFERENCE_HANDLE,
			 &hTarget,
			 sizeof hTarget,
			 &connect_resp.object_ptr,
			 sizeof connect_resp.object_ptr,
			 &dw,
			 NULL))
      {
	connect_resp.error_code = GetLastError();

	KdPrintLastError(("IOCTL_IMDISK_REFERENCE_HANDLE failed"));
	CloseHandle(hDriver);
	CloseHandle(hTarget);

	Overlapped.BufSend(hPipe, &connect_resp, sizeof connect_resp);

	delete this;
	return 0;
      }

    CloseHandle(hDriver);

    Overlapped.BufSend(hPipe, &connect_resp, sizeof connect_resp);

    // This will fail when driver closes the pipe, but that just indicates that
    // we should shut down this connection.
    Overlapped.BufRecv(hPipe, &dw, sizeof dw);

    KdPrint(("ImDskSvc: Cleaning up.%n"));

    CloseHandle(hTarget);

    delete this;
    return 1;
  }

  ~ImDiskSvcServerSession()
  {
    if (hPipe != INVALID_HANDLE_VALUE)
      CloseHandle(hPipe);
  }

public:
  bool
  Connect()
  {
    if (!ConnectNamedPipe(hPipe, &Overlapped))
      if (GetLastError() != ERROR_IO_PENDING)
	{
	  ImDiskSvcStatus.dwWin32ExitCode = GetLastError();
	  delete this;
	  return false;
	}

    HANDLE hWaitObjects[] =
      {
	Overlapped.hEvent,
	ImDiskSvcStopEvent
      };

    switch (WaitForMultipleObjects(sizeof(hWaitObjects) /
				   sizeof(*hWaitObjects),
				   hWaitObjects,
				   FALSE,
				   INFINITE))
      {
      case WAIT_OBJECT_0:
	{
	  union
	  {
	    DWORD (CALLBACK ImDiskSvcServerSession::*Member)();
	    UINT (CALLBACK *Static)(LPVOID);
	  } ThreadFunction;
	  ThreadFunction.Member = &ImDiskSvcServerSession::Thread;

	  KdPrint(("ImDskSvc: Creating thread.%n"));

	  UINT id;
	  HANDLE hThread = (HANDLE)
	    _beginthreadex(NULL, 0, ThreadFunction.Static, this, 0, &id);
	  if (hThread == NULL)
	    {
	      ImDiskSvcStatus.dwWin32ExitCode = GetLastError();
	      delete this;
	      return false;
	    }

	  CloseHandle(hThread);
	  ImDiskSvcStatus.dwWin32ExitCode = NO_ERROR;
	  return true;
	}

      case WAIT_OBJECT_0 + 1:
	ImDiskSvcStatus.dwWin32ExitCode = NO_ERROR;
	delete this;
	return false;

      default:
	ImDiskSvcStatus.dwWin32ExitCode = GetLastError();
	delete this;
	return false;
      }
  }

  ImDiskSvcServerSession()
  {
    hPipe = CreateNamedPipe(IMDPROXY_SVC_PIPE_DOSDEV_NAME,
			    PIPE_ACCESS_DUPLEX |
			    FILE_FLAG_WRITE_THROUGH |
			    FILE_FLAG_OVERLAPPED,
			    0,
			    PIPE_UNLIMITED_INSTANCES,
			    0,
			    0,
			    0,
			    NULL);
  }

  bool
  IsOk()
  {
    if (this == NULL)
      return false;

    return 
      (hPipe != NULL) &
      (hPipe != INVALID_HANDLE_VALUE) &
      (Overlapped.hEvent != NULL);
  }
};

VOID
CALLBACK
ImDiskSvcCtrlHandler(DWORD Opcode)  
{ 
  if (Opcode == SERVICE_CONTROL_STOP)
    {
      SetEvent(ImDiskSvcStopEvent);

      ImDiskSvcStatus.dwWin32ExitCode = NO_ERROR;
      ImDiskSvcStatus.dwCurrentState = SERVICE_STOP_PENDING;
      ImDiskSvcStatus.dwCheckPoint = 0;
      ImDiskSvcStatus.dwWaitHint = 0;

      if (!SetServiceStatus(ImDiskSvcStatusHandle, &ImDiskSvcStatus))
	{
	  KdPrintLastError(("SetServiceStatus() failed"));
	}

      return;
    }

  SetServiceStatus(ImDiskSvcStatusHandle, &ImDiskSvcStatus);
} 

VOID
CALLBACK
ImDiskSvcStart(DWORD, LPWSTR *)
{
  ImDiskSvcStatus.dwServiceType = SERVICE_WIN32;
  ImDiskSvcStatus.dwCurrentState = SERVICE_START_PENDING;
  ImDiskSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  ImDiskSvcStatus.dwWin32ExitCode = NO_ERROR;
  ImDiskSvcStatus.dwServiceSpecificExitCode = 0;
  ImDiskSvcStatus.dwCheckPoint = 0;
  ImDiskSvcStatus.dwWaitHint = 0;
 
  ImDiskSvcStatusHandle = RegisterServiceCtrlHandler(IMDPROXY_SVC,
						     ImDiskSvcCtrlHandler);
  if (ImDiskSvcStatusHandle == (SERVICE_STATUS_HANDLE) 0)
    { 
      KdPrintLastError(("RegisterServiceCtrlHandler() failed"));
      return; 
    } 

  ImDiskSvcStatus.dwCurrentState = SERVICE_RUNNING;
  SetServiceStatus(ImDiskSvcStatusHandle, &ImDiskSvcStatus);

  for (;;)
    {
      if (WaitForSingleObject(ImDiskSvcStopEvent, 0) != WAIT_TIMEOUT)
	{
	  ImDiskSvcStatus.dwWin32ExitCode = NO_ERROR;
	  break;
	}

      ImDiskSvcServerSession *ServerSession = new ImDiskSvcServerSession;
      if (!ServerSession->IsOk())
	{
	  KdPrintLastError(("Pipe initialization failed"));
	  break;
	}

      if (!ServerSession->Connect())
	{
	  if (ImDiskSvcStatus.dwWin32ExitCode != NO_ERROR)
	    {	 
	      KdPrintLastError(("Pipe connect failed"));
	    }
	  break;
	}
    }

  ImDiskSvcStatus.dwCurrentState = SERVICE_STOPPED;
  ImDiskSvcStatus.dwControlsAccepted = 0;
  SetServiceStatus(ImDiskSvcStatusHandle, &ImDiskSvcStatus);
}

extern "C"
void
__declspec(noreturn)
Entry()
{
  WSADATA wsadata;
  WSAStartup(0x0101, &wsadata);

  SERVICE_TABLE_ENTRY ServiceTable[] =
    {
      { IMDPROXY_SVC, ImDiskSvcStart },
      { NULL, NULL }
    };

  ImDiskSvcStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (ImDiskSvcStopEvent == NULL)
    {
      KdPrintLastError(("CreateEvent() failed"));
      ExitProcess(0);
    }

  if (!StartServiceCtrlDispatcher(ServiceTable))
    {
      MessageBoxA(NULL, "This program can only run as a Windows NT service.",
		  "ImDisk Service",
		  MB_ICONSTOP | MB_TASKMODAL);

      ExitProcess(0);
    }

  SetEvent(ImDiskSvcStopEvent);
  ExitThread(1);
}
