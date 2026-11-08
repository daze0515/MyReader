// 只读探测：Reader 窗口上 OLE 拖放目标的注册情况。
// 只读窗口属性，绝不跨进程调用指针，所以是安全的。
#include <windows.h>
#include <cstdio>
static const wchar_t* kProp = L"OleDropTargetInterface";
static HWND g_main = nullptr;
static BOOL CALLBACK FindMain(HWND h, LPARAM) {
  wchar_t cls[256] = {};
  GetClassNameW(h, cls, 256);
  if (wcscmp(cls, L"ReaderWndClass") == 0) { g_main = h; return FALSE; }
  return TRUE;
}
static BOOL CALLBACK ShowChild(HWND c, LPARAM) {
  wchar_t cls[256] = {};
  GetClassNameW(c, cls, 256);
  void* dt = (void*)GetPropW(c, kProp);
  printf("  child %-26ls drop=%s\n", cls, dt ? "YES" : "no");
  return TRUE;
}
int main() {
  EnumWindows(FindMain, 0);
  if (!g_main) { printf("NO_WINDOW\n"); return 1; }
  printf("main  %-26ls drop=%s\n", L"ReaderWndClass", GetPropW(g_main, kProp) ? "YES" : "no");
  EnumChildWindows(g_main, ShowChild, 0);
  return 0;
}
