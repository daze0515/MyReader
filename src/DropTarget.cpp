#include "DropTarget.h"
#include <shellapi.h>
#include <cwctype>

HRESULT DropTarget::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  if (riid == IID_IUnknown || riid == IID_IDropTarget) {
    *ppv = static_cast<IDropTarget*>(this);
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

static bool EndsWithTxt(const std::wstring& path) {
  if (path.size() < 4) return false;
  std::wstring tail = path.substr(path.size() - 4);
  for (wchar_t& c : tail) c = (wchar_t)towlower(c);
  return tail == L".txt";
}

// 从 IDataObject 里取出 CF_HDROP 文件列表
bool DropTarget::ReadFiles(IDataObject* data, std::vector<std::wstring>& out) {
  out.clear();
  if (!data) return false;
  FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
  STGMEDIUM stg = {};
  if (FAILED(data->GetData(&fmt, &stg))) return false;
  HDROP drop = (HDROP)GlobalLock(stg.hGlobal);
  if (drop) {
    const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
      wchar_t buf[4096] = {};
      if (DragQueryFileW(drop, i, buf, 4096)) out.push_back(std::wstring(buf));
    }
    GlobalUnlock(stg.hGlobal);
  }
  ReleaseStgMedium(&stg);
  return !out.empty();
}

bool DropTarget::HasTxt(IDataObject* data) {
  std::vector<std::wstring> files;
  if (!ReadFiles(data, files)) return false;
  for (const std::wstring& f : files) {
    if (EndsWithTxt(f)) return true;
  }
  return false;
}

HRESULT DropTarget::DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) {
  const bool ok = HasTxt(data);
  if (effect) *effect = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
  if (ok != inside_) {
    inside_ = ok;
    if (onHint_) onHint_(ok);
  }
  return S_OK;
}

HRESULT DropTarget::DragOver(DWORD, POINTL, DWORD* effect) {
  if (effect) *effect = inside_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
  return S_OK;
}

HRESULT DropTarget::DragLeave() {
  if (inside_) {
    inside_ = false;
    if (onHint_) onHint_(false);
  }
  return S_OK;
}

HRESULT DropTarget::Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) {
  if (inside_) {
    inside_ = false;
    if (onHint_) onHint_(false);
  }
  std::vector<std::wstring> files;
  std::wstring first;
  if (ReadFiles(data, files)) {
    for (const std::wstring& f : files) {
      if (EndsWithTxt(f)) { first = f; break; }
    }
  }
  if (effect) *effect = first.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
  if (!first.empty() && onDrop_) onDrop_(first);
  return S_OK;
}
