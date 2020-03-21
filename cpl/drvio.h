EXTERN_C VOID
DoEvents(HWND hWnd);

EXTERN_C BOOL
MsgBoxPrintF(HWND hWnd, UINT uStyle, LPCWSTR lpMessage, ...);

EXTERN_C VOID
MsgBoxLastError(HWND hWnd, LPCWSTR Prefix);

EXTERN_C BOOL
ImDiskCreate(HWND hWnd,
	     PDISK_GEOMETRY DiskGeometry,
	     DWORD Flags,
	     LPCWSTR FileName,
	     BOOL NativePath,
	     LPWSTR MountPoint);

EXTERN_C BOOL
ImDiskRemove(HWND hWnd, DWORD DeviceNumber, LPCWSTR MountPoint);

EXTERN_C HANDLE
ImDiskOpenDeviceByName(PUNICODE_STRING FileName, DWORD AccessMode);

EXTERN_C HANDLE
ImDiskOpenDeviceByNumber(DWORD DeviceNumber, DWORD AccessMode);

EXTERN_C BOOL
ImDiskCheckDriverVersion(HANDLE Device);

