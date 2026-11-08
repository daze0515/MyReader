#pragma once
#include <windows.h>
#include <ole2.h>
#include <functional>
#include <string>
#include <vector>

// 宿主窗口的 OLE 拖放目标：把拖进来的 .txt 路径交给回调。
// WebView2 默认自己吃掉文件拖放，所以关掉 AllowExternalDrop 之后，
// 由这个 IDropTarget 接管主窗口（必要时也接管 WebView2 的子窗口）。
class DropTarget : public IDropTarget {
 public:
  using DropFn = std::function<void(const std::wstring&)>;
  using HintFn = std::function<void(bool)>;

  DropTarget(DropFn onDrop, HintFn onHint)
      : onDrop_(std::move(onDrop)), onHint_(std::move(onHint)) {}

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
  ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
  ULONG STDMETHODCALLTYPE Release() override {
    LONG n = InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return (ULONG)n;
  }

  // IDropTarget
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override;
  HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override;
  HRESULT STDMETHODCALLTYPE DragLeave() override;
  HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override;

 private:
  bool HasTxt(IDataObject* data);
  static bool ReadFiles(IDataObject* data, std::vector<std::wstring>& out);

  LONG ref_ = 1;
  bool inside_ = false;
  DropFn onDrop_;
  HintFn onHint_;
};
