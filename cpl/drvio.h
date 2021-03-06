#ifdef __cplusplus
extern "C" {
#endif

    INT_PTR
        CALLBACK
        StatusDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    INT_PTR
        CALLBACK
        NewDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    BOOL
        WINAPI
        ImDiskInteractiveCheckSave(HWND hWnd, HANDLE device);

  LONG
      WINAPI
      ImDiskShowCPlAppletElevated(HWND hWnd);

  VOID
      WINAPI
      ImDiskRelaunchElevated(HWND hWnd, LPCSTR DllFunction,
          LPCSTR CommandLine, int nCmdShow);

  VOID
      WINAPI
      ImDiskRelaunchElevatedW(HWND hWnd, LPCWSTR DllFunction,
          LPCWSTR CommandLine, int nCmdShow);

  /**
  This function is a quick perror-style way of displaying an error message
  for the last failed Windows API call.

  hWndParent   Parent window for the MessageBox call.

  Prefix       Text to print before the error message string.
  */
  VOID
      WINAPI
      MsgBoxLastError(IN HWND hWndParent OPTIONAL,
          IN LPCWSTR Prefix);

  extern
  ULONGLONG
  APIFlags;

#ifdef __cplusplus
}
#endif
