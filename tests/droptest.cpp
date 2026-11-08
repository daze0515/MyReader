// DropTarget 单元测试：用合成的 IDataObject(CF_HDROP) 直接驱动 src/DropTarget.cpp，
// 覆盖真实鼠标拖放时走的那条代码路径（DragEnter / DragOver / DragLeave / Drop）。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/DropTarget.h"

static int g_pass = 0, g_fail = 0;
static void Check(bool ok, const char* name) {
  if (ok) { g_pass++; printf("  ok   %s/n", name); }
  else { g_fail++; printf("  FAIL %s/n", name); }
}

// 最小 IDataObject：只暴露 CF_HDROP
class DropData : public IDataObject {
 public:
  explicit DropData(std::vector<std::wstring> files, bool offer = true, DWORD tymed = TYMED_HGLOBAL)
      : files_(std::move(files)), offer_(offer), tymed_(tymed) {}
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDataObject) { *ppv = this; AddRef(); return S_OK; }
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG n = InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return (ULONG)n;
  }
  STDMETHODIMP GetData(FORMATETC* fmt, STGMEDIUM* stg) override {
    if (!fmt || !stg) return E_POINTER;
    if (!offer_ || fmt->cfFormat != CF_HDROP) return DV_E_FORMATETC;
    if (!(fmt->tymed & tymed_)) return DV_E_TYMED;
    size_t chars = 1;
    for (const auto& f : files_) chars += f.size() + 1;
    const size_t hdr = 20;  // DROPFILES: pFiles(4) pt(8) fNC(4) fWide(4)
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, hdr + chars * sizeof(wchar_t));
    if (!h) return E_OUTOFMEMORY;
    BYTE* base = (BYTE*)GlobalLock(h);
    *((DWORD*)(base + 0)) = (DWORD)hdr;
    *((BOOL*)(base + 16)) = TRUE;
    wchar_t* p = (wchar_t*)(base + hdr);
    for (const auto& f : files_) { memcpy(p, f.c_str(), (f.size() + 1) * sizeof(wchar_t)); p += f.size() + 1; }
    *p = 0;
    GlobalUnlock(h);
    stg->tymed = TYMED_HGLOBAL;
    stg->hGlobal = h;
    stg->pUnkForRelease = nullptr;
    return S_OK;
  }
  STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
  STDMETHODIMP QueryGetData(FORMATETC* fmt) override {
    if (!fmt) return E_POINTER;
    return (offer_ && fmt->cfFormat == CF_HDROP) ? S_OK : DV_E_FORMATETC;
  }
  STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override { if (out) out->ptd = nullptr; return E_NOTIMPL; }
  STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
  STDMETHODIMP EnumFormatEtc(DWORD, IEnumFORMATETC** pp) override { if (pp) *pp = nullptr; return E_NOTIMPL; }
  STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
  STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  STDMETHODIMP EnumDAdvise(IEnumSTATDATA** pp) override { if (pp) *pp = nullptr; return OLE_E_ADVISENOTSUPPORTED; }
 private:
  ~DropData() = default;
  LONG ref_ = 1;
  std::vector<std::wstring> files_;
  bool offer_;
  DWORD tymed_;
};

struct Recorder {
  std::wstring dropped;
  int dropCount = 0;
  std::vector<bool> hints;
  DropTarget* target = nullptr;

  void Attach() {
    target = new DropTarget([this](const std::wstring& p) { dropped = p; dropCount++; },
                            [this](bool on) { hints.push_back(on); });
  }
  ~Recorder() { if (target) target->Release(); }
};

int main() {
  OleInitialize(nullptr);
  printf("=== DROPTARGET SELFTEST ===/n");
  const POINTL pt = { 100, 100 };

  {  // 1. 单个 .txt：进入高亮、放下导入
    Recorder r; r.Attach();
    DropData* d = new DropData({ L"D:/books/novel.txt" });
    DWORD eff = 0xFFFFFFFF;
    Check(r.target->DragEnter(d, MK_LBUTTON, pt, &eff) == S_OK, "enter: S_OK");
    Check(eff == DROPEFFECT_COPY, "enter: effect=COPY");
    Check(r.hints.size() == 1 && r.hints[0] == true, "enter: hint(true)");
    DWORD eff2 = 0;
    Check(r.target->DragOver(MK_LBUTTON, pt, &eff2) == S_OK, "over: S_OK");
    Check(eff2 == DROPEFFECT_COPY, "over: effect=COPY");
    DWORD eff3 = 0;
    Check(r.target->Drop(d, MK_LBUTTON, pt, &eff3) == S_OK, "drop: S_OK");
    Check(eff3 == DROPEFFECT_COPY, "drop: effect=COPY");
    Check(r.dropCount == 1 && r.dropped == L"D:/books/novel.txt", "drop: path delivered");
    Check(r.hints.size() == 2 && r.hints[1] == false, "drop: hint(false)");
    d->Release();
  }

  {  // 2. 大小写后缀
    Recorder r; r.Attach();
    DropData* d = new DropData({ L"D:/books/NOVEL.TXT" });
    DWORD eff = 0;
    r.target->DragEnter(d, 0, pt, &eff);
    Check(eff == DROPEFFECT_COPY, "enter: .TXT uppercase accepted");
    DWORD e3 = 0;
    r.target->Drop(d, 0, pt, &e3);
    Check(r.dropCount == 1, "drop: .TXT uppercase delivered");
    d->Release();
  }

  {  // 3. 非 txt 拒绝
    Recorder r; r.Attach();
    DropData* d = new DropData({ L"D:/books/notes.md", L"D:/books/a.pdf" });
    DWORD eff = 0xFFFFFFFF;
    r.target->DragEnter(d, 0, pt, &eff);
    Check(eff == DROPEFFECT_NONE, "enter: non-txt -> NONE");
    Check(r.hints.empty(), "enter: non-txt -> no hint");
    DWORD e3 = 0xFFFFFFFF;
    r.target->Drop(d, 0, pt, &e3);
    Check(e3 == DROPEFFECT_NONE, "drop: non-txt -> NONE");
    Check(r.dropCount == 0, "drop: non-txt -> no callback");
    d->Release();
  }

  {  // 4. 多文件里挑出 .txt
    Recorder r; r.Attach();
    DropData* d = new DropData({ L"D:/a.md", L"D:/b.txt", L"D:/c.txt" });
    DWORD e3 = 0;
    r.target->Drop(d, 0, pt, &e3);
    Check(r.dropCount == 1 && r.dropped == L"D:/b.txt", "drop: first .txt wins");
    d->Release();
  }

  {  // 5. 离开窗口
    Recorder r; r.Attach();
    DropData* d = new DropData({ L"D:/x.txt" });
    DWORD eff = 0;
    r.target->DragEnter(d, 0, pt, &eff);
    Check(r.hints.size() == 1 && r.hints[0] == true, "leave: hint(true) before");
    r.target->DragLeave();
    Check(r.hints.size() == 2 && r.hints[1] == false, "leave: hint(false) after");
    DWORD eff2 = 0;
    r.target->DragOver(0, pt, &eff2);
    Check(eff2 == DROPEFFECT_NONE, "leave: over after leave -> NONE");
    d->Release();
  }

  {  // 6. 没有 CF_HDROP 的数据对象
    Recorder r; r.Attach();
    DropData* d = new DropData({ L"D:/x.txt" }, /*offer=*/false);
    DWORD eff = 0xFFFFFFFF;
    r.target->DragEnter(d, 0, pt, &eff);
    Check(eff == DROPEFFECT_NONE, "enter: no CF_HDROP -> NONE");
    DWORD e3 = 0;
    r.target->Drop(d, 0, pt, &e3);
    Check(r.dropCount == 0, "drop: no CF_HDROP -> no callback");
    d->Release();
  }

  {  // 7. tymed 不是 HGLOBAL
    Recorder r; r.Attach();
    DropData* d = new DropData({ L"D:/x.txt" }, true, TYMED_ISTREAM);
    DWORD eff = 0xFFFFFFFF;
    r.target->DragEnter(d, 0, pt, &eff);
    Check(eff == DROPEFFECT_NONE, "enter: wrong tymed -> NONE");
    d->Release();
  }

  {  // 8. 空文件列表
    Recorder r; r.Attach();
    DropData* d = new DropData({});
    DWORD eff = 0xFFFFFFFF;
    r.target->DragEnter(d, 0, pt, &eff);
    Check(eff == DROPEFFECT_NONE, "enter: empty list -> NONE");
    d->Release();
  }

  {  // 9. QueryInterface
    Recorder r; r.Attach();
    void* pv = nullptr;
    Check(r.target->QueryInterface(IID_IDropTarget, &pv) == S_OK && pv != nullptr, "QI: IDropTarget");
    if (pv) ((IUnknown*)pv)->Release();
    pv = nullptr;
    Check(r.target->QueryInterface(IID_IUnknown, &pv) == S_OK, "QI: IUnknown");
    if (pv) ((IUnknown*)pv)->Release();
    pv = nullptr;
    Check(r.target->QueryInterface(IID_IDataObject, &pv) == E_NOINTERFACE, "QI: IDataObject -> E_NOINTERFACE");
  }

  printf("PASS=%d FAIL=%d/n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
