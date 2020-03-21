EXTERN_C VOID
DoEvents(HWND hWnd);

EXTERN_C INT_PTR CALLBACK
StatusDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

EXTERN_C INT_PTR CALLBACK
NewDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

EXTERN_C BOOL
MsgBoxPrintF(HWND hWnd, UINT uStyle, LPCWSTR lpTitle, LPCWSTR lpMessage, ...);

EXTERN_C VOID
MsgBoxLastError(HWND hWnd, LPCWSTR Prefix);

EXTERN_C HANDLE
ImDiskOpenDeviceByName(PUNICODE_STRING FileName, DWORD AccessMode);

EXTERN_C HANDLE
ImDiskOpenDeviceByNumber(DWORD DeviceNumber, DWORD AccessMode);

EXTERN_C BOOL
ImDiskCheckDriverVersion(HANDLE Device);
