#include "App.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
  // 必须是 OleInitialize（而不是 CoInitializeEx）：它才会把 OLE 的拖放/剪贴板子系统一起初始化，
  // 否则 RegisterDragDrop 会失败（实测返回 E_OUTOFMEMORY），窗口就收不到文件拖放。
  const HRESULT hrOle = OleInitialize(nullptr);
  if (!App::Get().Init(hInst, nCmdShow)) {
    if (SUCCEEDED(hrOle)) OleUninitialize();
    return 1;
  }
  MSG msg = {};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (SUCCEEDED(hrOle)) OleUninitialize();
  return 0;
}
