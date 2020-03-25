#ifdef __cplusplus
extern "C" {
#endif

  VOID
  WINAPI
  DoEvents(HWND hWnd);

  INT_PTR
  CALLBACK
  StatusDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

  INT_PTR
  CALLBACK
  NewDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

  BOOL
  WINAPI
  ImDiskInteractiveCheckSave(HWND hWnd, HANDLE device);

  extern
  ULONGLONG
  APIFlags;

#ifdef __cplusplus
}
#endif
