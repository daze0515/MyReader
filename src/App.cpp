#include "App.h"
#include <WebView2EnvironmentOptions.h>
#include <commdlg.h>
#include <shellapi.h>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static const wchar_t* kWindowClass = L"ReaderWndClass";
// 与 src/app.rc 里的 IDI_READER 保持一致
#define IDI_READER 101

App& App::Get() {
  static App instance;
  return instance;
}

static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
  return App::Get().HandleMessage(h, m, w, l);
}

bool App::Init(HINSTANCE hInst, int nCmdShow) {
  store_.Init(ModuleDir() + L"\\data");
  LoadSites();
  ApplyNetSettings();

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProcThunk;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = kWindowClass;
  wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_READER), IMAGE_ICON, 0, 0,
                                LR_DEFAULTSIZE | LR_SHARED);
  wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_READER), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
  if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  if (!wc.hIconSm) wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
  if (!RegisterClassExW(&wc)) return false;

  hwnd_ = CreateWindowExW(0, kWindowClass, L"阅读器", WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT, 1200, 880,
                          nullptr, nullptr, hInst, nullptr);
  if (!hwnd_) return false;
  // 类图标已经生效，这里再显式设一次，免得后面被 WebView2 或别的代码覆盖
  SendMessageW(hwnd_, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
  SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);

  ShowWindow(hwnd_, nCmdShow);
  UpdateWindow(hwnd_);
  CreateWebView();
  return true;
}

LRESULT App::HandleMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_SIZE:
      ResizeWebView();
      return 0;
    case WM_APP_JOB: {
      auto* fn = reinterpret_cast<std::function<void()>*>(l);
      if (fn) { (*fn)(); delete fn; }
      return 0;
    }
    case WM_TIMER:
      if (w == 1) { PollVerification(); return 0; }
      if (w == 2) { KillTimer(hwnd_, 2); StartNextFetch(); return 0; }
      if (w == 4) { PumpJob(); return 0; }
      if (w == 5) { KillTimer(hwnd_, 5); ProbeCoverImage(); return 0; }
      if (w == 3) {
        PageResult timeout;
        timeout.error = "浏览器抓取超时";
        FinishFetch(timeout);
        return 0;
      }
      break;
    case WM_DROPFILES:
      HandleDrop((HDROP)w);
      return 0;
    case WM_DESTROY:
      if (dropTarget_) { RevokeDragDrop(hwnd_); dropTarget_.Reset(); }
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(h, m, w, l);
}

void App::ResizeWebView() {
  if (!controller_) return;
  RECT rc = {};
  GetClientRect(hwnd_, &rc);
  controller_->put_Bounds(rc);
}

void App::CreateWebView() {
  std::wstring dataDir = ModuleDir() + L"\\data";
  std::wstring userData = dataDir + L"\\webview2";
  EnsureDir(dataDir);
  EnsureDir(userData);

  auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
  std::wstring args = L"--lang=zh-CN";
  const Settings& st = store_.settings();
  if (st.proxyMode == "manual" && !st.proxyManual.empty()) {
    args += L" --proxy-server=" + U2W(st.proxyManual);
  } else if (st.proxyMode == "none") {
    args += L" --no-proxy-server";
  }
  {
    wchar_t dbgPort[16] = {};
    DWORD n = GetEnvironmentVariableW(L"READER_DEBUG_PORT", dbgPort, 16);
    if (n > 0 && n < 16) args += L" --remote-debugging-port=" + std::wstring(dbgPort);
  }
  options->put_AdditionalBrowserArguments(args.c_str());

  CreateCoreWebView2EnvironmentWithOptions(
    nullptr, userData.c_str(), options.Get(),
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
      [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(hr) || !env) {
          MessageBoxW(hwnd_, L"WebView2 初始化失败，请确认已安装 WebView2 Runtime。",
                      L"阅读器", MB_ICONERROR);
          return S_OK;
        }
        env_ = env;
        env_->CreateCoreWebView2Controller(
          hwnd_,
          Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
              if (FAILED(hr2) || !c) return S_OK;
              controller_ = c;
              controller_->get_CoreWebView2(&webview_);
              webview_->get_Settings(&wvSettings_);
              if (wvSettings_) {
                wvSettings_->put_IsStatusBarEnabled(FALSE);
                wvSettings_->put_AreDefaultContextMenusEnabled(FALSE);
                wvSettings_->put_AreDevToolsEnabled(TRUE);
                wvSettings_->put_IsZoomControlEnabled(FALSE);
                ComPtr<ICoreWebView2Settings2> s2;
                if (SUCCEEDED(wvSettings_.As(&s2))) {
                  LPWSTR ua = nullptr;
                  s2->get_UserAgent(&ua);
                  if (ua) { ua_ = ua; CoTaskMemFree(ua); }
                }
              }

              ComPtr<ICoreWebView2_2> wv2;
              if (SUCCEEDED(webview_.As(&wv2))) wv2->get_CookieManager(&cookieMgr_);

              EventRegistrationToken tok = {};
              webview_->add_WebMessageReceived(
                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                  [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    LPWSTR json = nullptr;
                    if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                      OnWebMessage(json);
                      CoTaskMemFree(json);
                    }
                    return S_OK;
                  }).Get(), &tok);

              ResizeWebView();

              // 拖入 .txt：先关掉 WebView2 自己的外部拖放，再由宿主窗口的 IDropTarget 接管
              {
                ComPtr<ICoreWebView2Controller4> c4;
                const HRESULT hrQuery = controller_.As(&c4);
                const HRESULT hrAllow = SUCCEEDED(hrQuery) ? c4->put_AllowExternalDrop(FALSE) : hrQuery;
                char buf[128] = {};
                sprintf_s(buf, "AllowExternalDrop hr=0x%08lX (query=0x%08lX)",
                          (unsigned long)hrAllow, (unsigned long)hrQuery);
                DropDebug(buf);
                DragAcceptFiles(hwnd_, TRUE);  // 兜底：老式 WM_DROPFILES
                InstallDropTarget();
              }

              ComPtr<ICoreWebView2_3> wv3;
              if (SUCCEEDED(webview_.As(&wv3))) {
                std::wstring webDir = ResolveWebDir();
                wv3->SetVirtualHostNameToFolderMapping(
                  L"reader.local", webDir.c_str(),
                  COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                // 封面走本地缓存目录，书架就不会因为代理挂了变成一排占位图
                const std::wstring coversDir = CoversDir();
                EnsureDir(coversDir);
                wv3->SetVirtualHostNameToFolderMapping(
                  L"covers.local", coversDir.c_str(),
                  COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
              }
              CreateFetchView();
              webview_->Navigate(L"https://reader.local/index.html");
              return S_OK;
            }).Get());
        return S_OK;
      }).Get());
}

static bool FileExistsW(const std::wstring& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// web/ 目录探测：优先 exe 同级，其次开发态的上两级目录。
std::wstring App::ResolveWebDir() {
  const std::wstring base = ModuleDir();
  const std::wstring candidates[] = {
    base + L"\\web",
    base + L"\\..\\web",
    base + L"\\..\\..\\web",
  };
  for (const auto& c : candidates) {
    if (FileExistsW(c + L"\\index.html")) return c;
  }
  MessageBoxW(hwnd_,
    L"找不到界面文件 web\\index.html。\n\n"
    L"请把项目的 web 目录复制到 Reader.exe 同级目录下再启动。",
    L"阅读器", MB_ICONERROR | MB_OK);
  return candidates[0];
}

std::wstring App::ProxyUrl() {
  ProxyConfig cfg;
  const Settings& st = store_.settings();
  if (st.proxyMode == "manual") {
    cfg.mode = ProxyConfig::Mode::Manual;
    cfg.manual = U2W(st.proxyManual);
  } else if (st.proxyMode == "none") {
    cfg.mode = ProxyConfig::Mode::None;
  } else {
    cfg.mode = ProxyConfig::Mode::System;
  }
  return ResolveProxy(cfg);
}

void App::RunAsync(std::function<void()> work) {
  std::thread([work]() { work(); }).detach();
}

void App::PostToUi(std::function<void()> fn) {
  auto* p = new std::function<void()>(std::move(fn));
  if (!PostMessageW(hwnd_, WM_APP_JOB, 0, (LPARAM)p)) delete p;
}

static int IntervalMs(double sec) {
  if (sec < 0.8) sec = 0.8;
  if (sec > 5.0) sec = 5.0;
  return (int)(sec * 1000);
}

void App::ApplyNetSettings() {
  throttleMs_.store(IntervalMs(store_.settings().requestInterval));
}

void App::Throttle() {
  static std::mutex m;
  static std::chrono::steady_clock::time_point last =
      std::chrono::steady_clock::now() - std::chrono::seconds(30);
  std::lock_guard<std::mutex> lk(m);
  const auto minGap = std::chrono::milliseconds(throttleMs_.load());
  auto now = std::chrono::steady_clock::now();
  if (now - last < minGap) std::this_thread::sleep_for(minGap - (now - last));
  int jitter = (int)(minGap.count() / 4);
  if (jitter > 600) jitter = 600;
  if (jitter > 0) std::this_thread::sleep_for(std::chrono::milliseconds(rand() % jitter));
  last = std::chrono::steady_clock::now();
}

void App::PostJson(const Json& j) {
  if (!webview_) return;
  webview_->PostWebMessageAsJson(U2W(j.Dump()).c_str());
}

void App::SendStatus(const std::string& text, bool busy) {
  Json j = Json::Obj();
  j["type"] = Json::Str("status");
  j["text"] = Json::Str(text);
  j["busy"] = Json::Bool(busy);
  PostJson(j);
}

void App::SendError(const std::string& message) {
  Json j = Json::Obj();
  j["type"] = Json::Str("error");
  j["message"] = Json::Str(message);
  PostJson(j);
}

void App::SendSettings() {
  Json j = Json::Obj();
  j["type"] = Json::Str("settings");
  j["value"] = store_.settings().ToJson();
  PostJson(j);
}

void App::SetTitle(const std::wstring& t) {
  if (hwnd_) SetWindowTextW(hwnd_, t.c_str());
}

void App::SendMeta() {
  Json j = Json::Obj();
  j["type"] = Json::Str("meta");
  Json m = Json::Obj();
  const std::string localCover = CoverUrlFor(meta_.bookId);
  if (localCover.empty() && !isLocal_) EnsureCoverCached(meta_.bookId, meta_.cover);
  m["bookId"] = Json::Str(meta_.bookId);
  m["title"] = Json::Str(meta_.title);
  m["author"] = Json::Str(meta_.author);
  m["cover"] = Json::Str(localCover.empty() ? meta_.cover : localCover);
  m["category"] = Json::Str(meta_.category);
  m["status"] = Json::Str(meta_.status);
  m["url"] = Json::Str(bookUrl_);
  m["kind"] = Json::Str(isLocal_ ? "local" : "web");
  m["localPath"] = Json::Str(isLocal_ && txt_ ? W2U(txt_->PathW()) : std::string());
  j["meta"] = m;
  PostJson(j);
}

void App::SendBook(int resumeIndex, double resumeRatio) {
  Json j = Json::Obj();
  j["type"] = Json::Str("book");
  Json m = Json::Obj();
  const std::string localCover = CoverUrlFor(meta_.bookId);
  if (localCover.empty() && !isLocal_) EnsureCoverCached(meta_.bookId, meta_.cover);
  m["bookId"] = Json::Str(meta_.bookId);
  m["title"] = Json::Str(meta_.title);
  m["author"] = Json::Str(meta_.author);
  m["cover"] = Json::Str(localCover.empty() ? meta_.cover : localCover);
  m["category"] = Json::Str(meta_.category);
  m["status"] = Json::Str(meta_.status);
  m["url"] = Json::Str(bookUrl_);
  m["kind"] = Json::Str(isLocal_ ? "local" : "web");
  m["localPath"] = Json::Str(isLocal_ && txt_ ? W2U(txt_->PathW()) : std::string());
  j["meta"] = m;
  j["kind"] = Json::Str(isLocal_ ? "local" : "web");
  j["total"] = Json::Num((double)(int)toc_.size());
  j["resumeIndex"] = Json::Num(resumeIndex);
  j["resumeRatio"] = Json::Num(resumeRatio);
  Json arr = Json::Arr();
  for (auto& c : toc_) arr.Push(Json::Str(c.title));
  j["chapters"] = arr;
  PostJson(j);
}

void App::SendChapter(int index, const std::string& title,
                      const std::vector<std::string>& paragraphs, bool cached) {
  loadedOk_.insert(index);
  Json j = Json::Obj();
  j["type"] = Json::Str("chapter");
  j["index"] = Json::Num(index);
  j["title"] = Json::Str(title);
  j["total"] = Json::Num((double)(int)toc_.size());
  j["cached"] = Json::Bool(cached);
  Json arr = Json::Arr();
  for (auto& p : paragraphs) arr.Push(Json::Str(p));
  j["paragraphs"] = arr;
  PostJson(j);
}

void App::SendBooks() {
  std::vector<BookRecord> list = store_.books();
  std::sort(list.begin(), list.end(), [](const BookRecord& a, const BookRecord& b) {
    return a.updatedAt > b.updatedAt;
  });
  Json j = Json::Obj();
  j["type"] = Json::Str("books");
  Json arr = Json::Arr();
  for (auto& b : list) {
    // 有本地缓存就给本地图；没有就顺手排个后台任务去存一份（永远排在正文后面）
    const std::string local = CoverUrlFor(b.bookId);
    if (!local.empty()) b.cover = local;
    else if (b.kind != "local") EnsureCoverCached(b.bookId, b.cover);
    arr.Push(b.ToJson());
  }
  j["items"] = arr;
  PostJson(j);
}

static std::string UrlHost(const std::string& url) {
  size_t p = url.find("://");
  std::string rest = (p == std::string::npos) ? url : url.substr(p + 3);
  size_t e = rest.find_first_of("/?#");
  std::string host = (e == std::string::npos) ? rest : rest.substr(0, e);
  size_t at = host.rfind('@');
  if (at != std::string::npos) host = host.substr(at + 1);
  size_t colon = host.find(':');
  if (colon != std::string::npos) host = host.substr(0, colon);
  return ToLowerAscii(host);
}

static bool DomainMatches(const std::string& cookieDomain, const std::string& host) {
  std::string d = ToLowerAscii(cookieDomain);
  if (!d.empty() && d[0] == '.') d = d.substr(1);
  if (d.empty() || host.empty()) return false;
  if (host == d) return true;
  if (host.size() > d.size() &&
      host.compare(host.size() - d.size(), d.size(), d) == 0 &&
      host[host.size() - d.size() - 1] == '.') return true;
  return false;
}

// 通过 CDP 读 Cookie：CookieManager::GetCookies 不返回 HttpOnly，
// 而 Cloudflare 的 cf_clearance 正是 HttpOnly，必须走 DevTools 协议。
void App::RefreshCookies(const std::wstring& url, std::function<void()> done) {
  auto finished = std::make_shared<std::function<void()>>(std::move(done));
  if (!webview_) { if (*finished) (*finished)(); return; }

  const std::string host = UrlHost(W2U(url));
  webview_->CallDevToolsProtocolMethod(
    L"Network.getAllCookies", L"{}",
    Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
      [this, finished, host, url](HRESULT hr, LPCWSTR json) -> HRESULT {
        std::wstring header;
        std::string names;
        if (SUCCEEDED(hr) && json) {
          Json j;
          if (Json::Parse(W2U(json), j) && j.IsObj()) {
            const Json* arr = j.Get("cookies");
            if (arr && arr->IsArr()) {
              const int64_t now = (int64_t)time(nullptr);
              for (const auto& c : arr->arr) {
                if (!c.IsObj()) continue;
                std::string name = c.GetStr("name");
                std::string value = c.GetStr("value");
                if (name.empty()) continue;
                if (!DomainMatches(c.GetStr("domain"), host)) continue;
                const double expires = c.GetNum("expires", 0);
                if (expires > 0 && (int64_t)expires < now) continue;
                if (!header.empty()) header += L"; ";
                header += U2W(name) + L"=" + U2W(value);
                if (!names.empty()) names += ", ";
                names += name;
              }
            }
          }
        }
        cookieHeader_ = header;
        {
          std::wstring dbg = store_.DataDir() + L"\\debug";
          EnsureDir(dbg);
          WriteFileBytes(dbg + L"\\session.txt",
            "url=" + W2U(url) + "\nua=" + W2U(ua_) + "\nnames=" + names +
            "\nlen=" + std::to_string(header.size()) + "\n\n" + W2U(header));
        }
        if (*finished) (*finished)();
        return S_OK;
      }).Get());
}

const SiteAdapter& App::Site() const { return site_ ? *site_ : sites_.Fallback(); }

// 每次打开书都重读规则：改完 data\sites\*.json 重新打开这本书就生效，不用重启
void App::LoadSites() {
  const std::wstring dir = store_.DataDir() + L"\\sites";
  sites_.EnsureDefaultFiles(dir);
  sites_.Load(dir);
  const std::wstring dbg = store_.DataDir() + L"\\debug";
  EnsureDir(dbg);
  WriteFileBytes(dbg + L"\\sites.txt",
    "dir=" + W2U(dir) + "\ncount=" + std::to_string(sites_.count()) +
    "\n" + sites_.lastError());
}

void App::OpenBookUrl(const std::string& rawUrl) {
  std::string url = TrimAscii(rawUrl);
  if (url.empty()) { SendError("请输入网址"); return; }
  if (url.find("://") == std::string::npos) url = "https://" + url;

  LoadSites();
  const SiteAdapter* matched = sites_.Match(url);
  if (!matched) matched = &sites_.Fallback();
  site_ = matched;

  if (SiteBookId(*matched, url).empty()) {
    SendError("这个网址里找不到书号，请粘贴书籍目录页，例如 https://twkan.com/book/67581/index.html");
    return;
  }
  // 章节页 / 目录页 / 缺 index.html 都统一成目录页
  const std::string indexUrl = SiteIndexUrl(*matched, url);
  if (!indexUrl.empty()) url = indexUrl;

  bookUrl_ = url;
  toc_.clear();
  meta_ = BookMeta();
  currentIndex_ = -1;
  verifyRounds_ = 0;
  loadedOk_.clear();
  { std::lock_guard<std::mutex> lk(inflightMu_); healTried_.clear(); }
  isLocal_ = false;
  txt_.reset();
  StopJob("idle");
  SendStatus("正在获取目录…", true);
  FetchToc();
}

static void LogFetch(const std::wstring& dataDir, const std::string& url, const std::string& info) {
  std::wstring dbg = dataDir + L"\\debug";
  EnsureDir(dbg);
  WriteFileBytes(dbg + L"\\fetch.txt", "url=" + url + "\n" + info + "\n");
}

static const char* kVerifyFailed =
  "人机验证未通过。请在窗口里完成验证后点「重新验证」；若反复失败，换个代理节点或稍后再试。";

// ---------- 隐藏的抓取用 WebView2 ----------
// 站点开了 Cloudflare 之后，WinHTTP 即使带上浏览器里取来的 cf_clearance 也会被
// 403（CF 把放行绑在浏览器指纹上）。所以正文 / 目录统一用同一个 profile 的
// 隐藏 WebView2 去取：指纹、Cookie、代理都和用户看到的那次访问一致。
void App::CreateFetchView() {
  if (!env_ || fetchView_ || !hwnd_) return;

  static const wchar_t* kHostClass = L"ReaderFetchHost";
  HINSTANCE inst = GetModuleHandleW(nullptr);
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = kHostClass;
    RegisterClassExW(&wc);
    registered = true;
  }
  fetchHwnd_ = CreateWindowExW(0, kHostClass, L"", WS_POPUP, -32000, -32000, 1, 1,
                               hwnd_, nullptr, inst, nullptr);
  if (!fetchHwnd_) return;

  env_->CreateCoreWebView2Controller(
    fetchHwnd_,
    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
      [this](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
        if (FAILED(hr) || !c) return S_OK;
        fetchCtrl_ = c;
        c->get_CoreWebView2(&fetchView_);
        c->put_IsVisible(FALSE);
        RECT rc = { 0, 0, 1, 1 };
        c->put_Bounds(rc);

        ComPtr<ICoreWebView2Settings> fs;
        if (SUCCEEDED(fetchView_->get_Settings(&fs)) && fs) {
          fs->put_IsStatusBarEnabled(FALSE);
          fs->put_AreDefaultContextMenusEnabled(FALSE);
          fs->put_AreDevToolsEnabled(FALSE);
        }

        // 图片 / 视频 / 字体对正文没用，直接掐掉：省代理流量，页面也更快
        EventRegistrationToken rtok = {};
        fetchView_->add_WebResourceRequested(
          Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
              // 取封面时正文就是那张图本身，不能拦
              if (!fetchQueue_.empty() && fetchQueue_.front().image) return S_OK;
              ComPtr<ICoreWebView2WebResourceResponse> resp;
              if (env_ && SUCCEEDED(env_->CreateWebResourceResponse(nullptr, 204, L"No Content", L"", &resp))) {
                args->put_Response(resp.Get());
              }
              return S_OK;
            }).Get(), &rtok);
        fetchView_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_IMAGE);
        fetchView_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_MEDIA);
        fetchView_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_FONT);

        EventRegistrationToken ntok = {};
        fetchView_->add_NavigationCompleted(
          Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
              if (!fetchBusy_ || navGen_ != fetchGen_) return S_OK;
              BOOL success = FALSE;
              args->get_IsSuccess(&success);
              if (!success) {
                COREWEBVIEW2_WEB_ERROR_STATUS st = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                args->get_WebErrorStatus(&st);
                PageResult res;
                res.error = "浏览器加载失败(err=" + std::to_string((int)st) + ")";
                if (!fetchQueue_.empty()) LogFetch(store_.DataDir(), fetchQueue_.front().url, res.error);
                FinishFetch(res);
                return S_OK;
              }
              if (!fetchQueue_.empty() && fetchQueue_.front().image) {
                ProbeCoverImage();
                return S_OK;
              }
              const int gen = fetchGen_;
              fetchView_->ExecuteScript(
                L"document.documentElement.outerHTML",
                Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                  [this, gen](HRESULT ehr, LPCWSTR json) -> HRESULT {
                    if (gen != fetchGen_) return S_OK;
                    std::string html;
                    if (SUCCEEDED(ehr) && json) {
                      Json j;
                      if (Json::Parse(W2U(json), j) && j.t == Json::T::Str) html = j.str;
                    }
                    PageResult res;
                    res.ok = !html.empty();
                    res.status = res.ok ? 200 : 0;
                    if (!res.ok) res.error = "页面内容为空";
                    res.body = std::move(html);
                    if (!fetchQueue_.empty()) {
                      LogFetch(store_.DataDir(), fetchQueue_.front().url,
                               "ok size=" + std::to_string(res.body.size()) +
                               (SiteLooksLikeChallenge(Site(), res.body) ? " challenge=1" : " challenge=0"));
                    }
                    FinishFetch(res);
                    return S_OK;
                  }).Get());
              return S_OK;
            }).Get(), &ntok);
        // 抓取视图就绪后，把书架封面补一遍（此前可能因为没就绪没排上）
        if (coverSweepPending_) { coverSweepPending_ = false; SendBooks(); }
        return S_OK;
      }).Get());
}

void App::BrowserGet(const std::string& url, std::function<void(const PageResult&)> done) {
  FetchJob job;
  job.url = url;
  job.done = std::move(done);
  // 正文插到所有封面任务前面：用户翻页不能被后台补封面挡住
  size_t pos = fetchQueue_.size();
  for (size_t i = fetchBusy_ ? 1 : 0; i < fetchQueue_.size(); ++i) {
    if (fetchQueue_[i].image) { pos = i; break; }
  }
  fetchQueue_.insert(fetchQueue_.begin() + (ptrdiff_t)pos, std::move(job));
  StartNextFetch();
}

void App::BrowserGetImage(const std::string& url, const std::string& bookId) {
  FetchJob job;
  job.url = url;
  job.bookId = bookId;
  job.image = true;
  fetchQueue_.push_back(std::move(job));   // 封面永远排最后
  StartNextFetch();
}

void App::StartNextFetch() {
  if (!fetchView_ || fetchBusy_ || fetchQueue_.empty()) return;
  const auto gap = std::chrono::milliseconds(throttleMs_.load());
  const auto now = std::chrono::steady_clock::now();
  if (now - lastFetch_ < gap) {
    int wait = (int)std::chrono::duration_cast<std::chrono::milliseconds>(gap - (now - lastFetch_)).count();
    int jitter = (int)(gap.count() / 4);
    if (jitter > 600) jitter = 600;
    if (jitter > 0) wait += rand() % jitter;
    SetTimer(hwnd_, 2, wait > 10 ? wait : 10, nullptr);
    return;
  }
  KillTimer(hwnd_, 2);
  lastFetch_ = now;
  fetchBusy_ = true;
  navGen_ = ++fetchGen_;
  SetTimer(hwnd_, 3, 30000, nullptr);
  fetchView_->Navigate(U2W(fetchQueue_.front().url).c_str());
}

void App::FinishFetch(const PageResult& res) {
  if (!fetchBusy_) return;
  fetchBusy_ = false;
  ++fetchGen_;
  navGen_ = -1;
  KillTimer(hwnd_, 3);
  if (fetchQueue_.empty()) return;
  FetchJob job = std::move(fetchQueue_.front());
  fetchQueue_.pop_front();

  // 封面：图片文档可能还没解码完，隔一会儿再问一次；拿不到就放弃，等下次刷新书架重来
  if (job.image) {
    // 只有「图片还没解码好」才值得再问；导航本身失败（403/超时）就不再纠缠
    if (!res.ok && job.probed && job.tries < 8) {
      ++job.tries;
      fetchQueue_.push_front(std::move(job));
      SetTimer(hwnd_, 3, 15000, nullptr);
      SetTimer(hwnd_, 5, 350, nullptr);
      return;
    }
    if (res.ok) SaveCover(job.bookId, res.body);
    StartNextFetch();
    return;
  }

  if (!res.ok && res.status == 0 && job.tries < 2) {
    ++job.tries;
    fetchQueue_.push_front(std::move(job));
    StartNextFetch();
    return;
  }
  if (job.done) job.done(res);
  StartNextFetch();
}

void App::GetPage(const std::string& url, std::function<void(const PageResult&)> done) {
  if (fetchView_) { BrowserGet(url, std::move(done)); return; }

  // 兜底：隐藏浏览器还没就绪时退回 WinHTTP
  const std::wstring ua = ua_;
  const std::wstring cookie = cookieHeader_;
  const std::wstring proxy = ProxyUrl();
  RunAsync([this, url, ua, cookie, proxy, done]() {
    static const int kBackoff[3] = { 1000, 3000, 8000 };
    HttpResult r;
    for (int attempt = 0; attempt < 4; ++attempt) {
      Throttle();
      r = HttpGet(U2W(url), ua, cookie, proxy);
      if (r.ok) break;
      if (attempt < 3) std::this_thread::sleep_for(std::chrono::milliseconds(kBackoff[attempt]));
    }
    PageResult res;
    res.ok = r.ok;
    res.status = r.status;
    res.error = r.error;
    res.body = std::move(r.body);
    PostToUi([this, done, res]() { done(res); });
  });
}

// ---------------------------------------------------------------------------
// 封面本地缓存
// ---------------------------------------------------------------------------
std::wstring App::CoversDir() const { return store_.DataDir() + L"\\covers"; }

// 本地有就直接给本地地址，没有返回空串
std::string App::CoverUrlFor(const std::string& bookId) const {
  if (bookId.empty()) return "";
  const std::wstring p = CoversDir() + L"\\" + U2W(bookId) + L".jpg";
  if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return "";
  return "https://covers.local/" + bookId + ".jpg";
}

void App::EnsureCoverCached(const std::string& bookId, const std::string& url) {
  if (bookId.empty() || url.empty()) return;
  if (url.compare(0, 21, "https://covers.local/") == 0) return;   // 已经是本地图
  if (!fetchView_) { coverSweepPending_ = true; return; }   // 就绪后补一次
  if (!CoverUrlFor(bookId).empty()) return;
  if (!coverTried_.insert(bookId).second) return;   // 一次会话里每本书只试一次
  BrowserGetImage(url, bookId);
}

// 图片文档里 <img> 和文档同源，所以 canvas 不会被跨域污染，能直接取 dataURL
static const wchar_t* kCoverProbeScript =
  L"(function(){var i=document.images[0];"
  L"if(!i||!i.complete||!i.naturalWidth)return '';"
  L"try{var c=document.createElement('canvas');c.width=i.naturalWidth;c.height=i.naturalHeight;"
  L"var g=c.getContext('2d');g.drawImage(i,0,0);"
  L"return c.toDataURL('image/jpeg',0.92);}catch(e){return '';}})()";

void App::ProbeCoverImage() {
  if (!fetchView_ || fetchQueue_.empty() || !fetchQueue_.front().image) return;
  fetchBusy_ = true;                       // 借用同一条「当前任务」状态
  fetchQueue_.front().probed = true;
  const int gen = fetchGen_;
  fetchView_->ExecuteScript(
    kCoverProbeScript,
    Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
      [this, gen](HRESULT ehr, LPCWSTR json) -> HRESULT {
        if (gen != fetchGen_) return S_OK;
        std::string dataUrl;
        if (SUCCEEDED(ehr) && json) {
          Json j;
          if (Json::Parse(W2U(json), j) && j.t == Json::T::Str) dataUrl = j.str;
        }
        PageResult res;
        res.ok = !dataUrl.empty();
        res.status = res.ok ? 200 : 0;
        if (!res.ok) res.error = "封面还没就绪";
        res.body = std::move(dataUrl);
        FinishFetch(res);
        return S_OK;
      }).Get());
}

static int B64Val(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static std::string Base64Decode(const std::string& s) {
  std::string out;
  out.reserve(s.size() / 4 * 3);
  int buf = 0, bits = 0;
  for (unsigned char c : s) {
    if (c == '=') break;
    const int v = B64Val(c);
    if (v < 0) continue;                   // 换行之类直接跳过
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((char)((buf >> bits) & 0xFF));
    }
  }
  return out;
}

void App::SaveCover(const std::string& bookId, const std::string& dataUrl) {
  const size_t comma = dataUrl.find("base64,");
  if (comma == std::string::npos) return;
  const std::string bytes = Base64Decode(dataUrl.substr(comma + 7));
  if (bytes.size() < 512) return;          // 太小基本是错误页 / 空白图
  const std::wstring dir = CoversDir();
  EnsureDir(dir);
  if (!WriteFileBytes(dir + L"\\" + U2W(bookId) + L".jpg", bytes)) return;
  const std::wstring dbg = store_.DataDir() + L"\\debug";
  EnsureDir(dbg);
  WriteFileBytes(dbg + L"\\cover.txt",
                 "bookId=" + bookId + " bytes=" + std::to_string(bytes.size()) + "\n");
  SendBooks();                             // 书架立刻换成本地图
}

void App::FetchToc() {
  const std::string indexUrl = bookUrl_;
  const std::string listUrl = SiteTocListUrl(Site(), indexUrl);
  GetPage(indexUrl, [this, indexUrl, listUrl](const PageResult& r1) {
    if (!r1.ok) { if (!ApplyCachedToc()) SendError("网络错误：" + r1.error); return; }
    if (SiteLooksLikeChallenge(Site(), r1.body)) {
      if (verifyRounds_ >= 3) { if (!ApplyCachedToc()) SendError(kVerifyFailed); return; }
      BeginVerification(indexUrl, nullptr);
      return;
    }
    if (listUrl.empty()) { ApplyToc(indexUrl, r1.body, PageResult{}); return; }
    GetPage(listUrl, [this, indexUrl, r1](const PageResult& r2) {
      if (SiteLooksLikeChallenge(Site(), r2.body)) {
        if (verifyRounds_ >= 3) { if (!ApplyCachedToc()) SendError(kVerifyFailed); return; }
        BeginVerification(indexUrl, nullptr);
        return;
      }
      ApplyToc(indexUrl, r1.body, r2);
    });
  });
}

void App::ApplyToc(const std::string& indexUrl, const std::string& indexHtml,
                   const PageResult& listRes) {
  TocResult toc;
  if (!listRes.body.empty() && listRes.status == 200) toc = SiteParseToc(Site(), listRes.body, indexUrl);
  if (!toc.ok) {
    toc = SiteParseToc(Site(), indexHtml, indexUrl);
  } else {
    // 章节列表来自 AJAX，书名 / 作者只在索引页里，需要单独补一次
    BookMeta idxMeta;
    if (SiteParseBookMeta(Site(), indexHtml, idxMeta)) {
      if (toc.meta.title.empty()) toc.meta.title = idxMeta.title;
      if (toc.meta.author.empty()) toc.meta.author = idxMeta.author;
      if (toc.meta.cover.empty()) toc.meta.cover = idxMeta.cover;
      if (toc.meta.category.empty()) toc.meta.category = idxMeta.category;
      if (toc.meta.status.empty()) toc.meta.status = idxMeta.status;
    }
  }
  if (!toc.ok) { SendError("无法解析目录：" + toc.error); return; }

  toc_ = std::move(toc.chapters);
  meta_ = std::move(toc.meta);
  if (meta_.bookId.empty()) meta_.bookId = SiteBookId(Site(), indexUrl);
  store_.WriteToc(meta_.bookId, TocToJson());

  int resume = 0;
  double ratio = 0;
  if (BookRecord* rec = store_.Find(meta_.bookId)) {
    resume = rec->chapterIndex;
    ratio = rec->ratio;
    if (resume < 0 || resume >= (int)toc_.size()) resume = 0;
  }
  currentIndex_ = resume;
  lastRatio_ = ratio;
  SendBook(resume, ratio);
  SendCacheInfo();
  SendStatus("", false);
  FetchChapter(resume);
}

void App::FetchChapter(int index) {
  if (index < 0 || index >= (int)toc_.size()) {
    Json j = Json::Obj();
    j["type"] = Json::Str("end");
    PostJson(j);
    return;
  }
  if (isLocal_ && txt_) {
    std::vector<std::string> paras;
    txt_->GetChapter(index, paras);
    SendChapter(index, toc_[index].title, paras, true);
    return;
  }
  FetchChapterInternal(meta_.bookId, toc_[index].url, index, false, nullptr);
}

// silent=true 是后台任务（离线下载 / 导出补齐）：不往阅读页推消息，只落缓存。
void App::FetchChapterInternal(const std::string& bookId, const std::string& url, int index,
                               bool silent, std::function<void(bool)> done) {
  std::string cached;
  if (store_.ReadChapterCache(bookId, index, cached)) {
    Json cj;
    if (Json::Parse(cached, cj) && cj.IsObj()) {
      std::vector<std::string> paras;
      const Json* p = cj.Get("paragraphs");
      if (p && p->IsArr()) for (auto& x : p->arr) {
        if (x.t != Json::T::Str) continue;
        if (IsWatermarkLine(x.str)) continue;
        paras.push_back(x.str);
      }
      if (!paras.empty()) {
        if (!silent) SendChapter(index, cj.GetStr("title"), paras, true);
        if (done) done(true);
        return;
      }
    }
  }

  if (!silent) {
    std::lock_guard<std::mutex> lk(inflightMu_);
    if (inflight_.count(index)) return;
    inflight_.insert(index);
  }

  GetPage(url, [this, index, url, bookId, silent, done](const PageResult& r) {
    if (!silent) {
      std::lock_guard<std::mutex> lk(inflightMu_);
      inflight_.erase(index);
    }
    const bool challenge = SiteLooksLikeChallenge(Site(), r.body);
    if (!r.ok || challenge) {
      if (silent && challenge) {
        // 后台任务碰到人机验证：不逐章弹验证窗口，交给 PumpJob 统一熔断
        jobChallengeSeen_ = true;
        if (done) done(false);
        return;
      }
      bool canHeal = false;
      {
        std::lock_guard<std::mutex> lk(inflightMu_);
        if (!healTried_.count(index)) { healTried_.insert(index); canHeal = true; }
      }
      if (canHeal) {
        if (!silent) SendStatus("站点要求人机验证，请在窗口里完成验证", true);
        BeginVerification(url, [this, bookId, url, index, silent, done]() {
          FetchChapterInternal(bookId, url, index, silent, done);
        });
        return;
      }
      {
        std::wstring dbg = store_.DataDir() + L"\\debug";
        EnsureDir(dbg);
        WriteFileBytes(dbg + L"\\chapter_fail.txt",
          "url=" + url + "\nok=" + (r.ok ? "1" : "0") +
          "\nstatus=" + std::to_string(r.status) + "\nerror=" + r.error +
          "\nvia=browser\n\n" + r.body.substr(0, 6000));
      }
      if (!silent) {
        Json j = Json::Obj();
        j["type"] = Json::Str("chapterError");
        j["index"] = Json::Num(index);
        j["message"] = Json::Str(r.ok ? "站点拦截（人机验证未通过），请点「重新验证」"
                                       : ("网络错误：" + r.error));
        PostJson(j);
      }
      if (done) done(false);
      return;
    }
    Chapter ch = SiteParseChapter(Site(), r.body);
    if (!ch.ok) {
      if (!silent) {
        Json j = Json::Obj();
        j["type"] = Json::Str("chapterError");
        j["index"] = Json::Num(index);
        j["message"] = Json::Str("正文解析失败：" + ch.error);
        PostJson(j);
      }
      if (done) done(false);
      return;
    }
    if (bookId == meta_.bookId) {
      BookMeta cm;
      if (SiteParseLastRead(Site(), r.body, cm)) {
        bool changed = false;
        if (meta_.title.empty() && !cm.title.empty()) { meta_.title = cm.title; changed = true; }
        if (meta_.author.empty() && !cm.author.empty()) { meta_.author = cm.author; changed = true; }
        if (meta_.cover.empty() && !cm.cover.empty()) { meta_.cover = cm.cover; changed = true; }
        if (meta_.category.empty() && !cm.category.empty()) { meta_.category = cm.category; changed = true; }
        if (meta_.status.empty() && !cm.status.empty()) { meta_.status = cm.status; changed = true; }
        if (changed) SendMeta();
      }
      {
        std::wstring dbg = store_.DataDir() + L"\\debug";
        EnsureDir(dbg);
        WriteFileBytes(dbg + L"\\meta.txt",
          "index=" + std::to_string(index) + " body=" + std::to_string(r.body.size()) +
          "\nlastread=" + (SiteParseLastRead(Site(), r.body, cm) ? "1" : "0") +
          "\ncm.title=" + cm.title + "\nmeta_.title=" + meta_.title +
          "\nmeta_.author=" + meta_.author);
      }
    }
    Json cj = Json::Obj();
    cj["title"] = Json::Str(ch.title);
    Json arr = Json::Arr();
    for (auto& p : ch.paragraphs) arr.Push(Json::Str(p));
    cj["paragraphs"] = arr;
    store_.WriteChapterCache(bookId, index, cj.Dump());
    if (bookId == meta_.bookId) SendCacheInfo();
    if (!silent) SendChapter(index, ch.title, ch.paragraphs);
    if (done) done(true);
  });
}

void App::BeginVerification(const std::string& url, std::function<void()> after) {
  verifying_ = true;
  verifyClear_ = 0;
  ++verifyRounds_;
  verifyOrigin_ = url;
  verifyAfter_ = std::move(after);
  SendStatus("站点要求人机验证，请在窗口里完成验证（通过后自动继续）", true);
  if (webview_) webview_->Navigate(U2W(url).c_str());
  SetTimer(hwnd_, 1, 1500, nullptr);
}

void App::PollVerification() {
  if (!webview_) return;
  static const wchar_t* kScript =
    L"(function(){var b=document.getElementById('__rd_banner');"
    L"if(!b&&document.body){b=document.createElement('div');b.id='__rd_banner';"
    L"b.style.cssText='position:fixed;z-index:2147483647;left:0;right:0;top:0;padding:10px 16px;"
    L"background:#1f6feb;color:#fff;font:14px/1.6 system-ui,Segoe UI,sans-serif;text-align:center;"
    L"box-shadow:0 2px 10px rgba(0,0,0,.35)';"
    L"b.textContent='阅读器：请先完成页面上的人机验证，通过后会自动进入阅读模式';"
    L"document.body.appendChild(b);}return document.title;})()";

  webview_->ExecuteScript(
    kScript,
    Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
      [this](HRESULT hr, LPCWSTR result) -> HRESULT {
        std::string title;
        if (SUCCEEDED(hr) && result) {
          Json j;
          if (Json::Parse(W2U(result), j) && j.t == Json::T::Str) title = j.str;
        }
        std::string lower = ToLowerAscii(title);
        bool challenge = title.empty() ||
                         title.find("请稍候") != std::string::npos ||
                         lower.find("just a moment") != std::string::npos ||
                         lower.find("attention required") != std::string::npos;
        if (!challenge) {
          ++verifyClear_;
          if (verifyClear_ >= 2) {
            KillTimer(hwnd_, 1);
            verifying_ = false;
            verifyRounds_ = 0;
            std::string origin = verifyOrigin_;
            size_t pos = origin.find("/book/");
            std::wstring cookieUrl = U2W(pos == std::string::npos ? origin : origin.substr(0, pos + 1));
            std::function<void()> after = verifyAfter_;
            verifyAfter_ = nullptr;
            if (webview_) webview_->Navigate(L"https://reader.local/index.html");
            SendStatus("验证通过，继续加载…", true);
            RefreshCookies(cookieUrl, [this, after]() {
              if (after) after(); else FetchToc();
            });
          }
        } else {
          verifyClear_ = 0;
        }
        return S_OK;
      }).Get());
}

void App::OnWebMessage(const std::wstring& rawJson) {
  Json j;
  if (!Json::Parse(W2U(rawJson), j) || !j.IsObj()) return;
  std::string type = j.GetStr("type");

  if (type == "ready") {
    SendSettings();
    SendBooks();
    RefreshCookies(L"https://twkan.com/", nullptr);
    if (!toc_.empty() && !verifying_ && currentIndex_ >= 0) {
      SendBook(currentIndex_, lastRatio_);
      FetchChapter(currentIndex_);
    }
  } else if (type == "open") {
    OpenBookUrl(j.GetStr("url"));
  } else if (type == "needChapter") {
    FetchChapter((int)j.GetNum("index", -1));
  } else if (type == "progress") {
    if (meta_.bookId.empty()) return;
    const int idx = (int)j.GetNum("index", -1);
    if (idx < 0 || idx >= (int)toc_.size()) return;
    if (!loadedOk_.count(idx)) return;   // 加载失败 / 还没出来的章节不记进度，免得把书签冲掉
    BookRecord& b = store_.Upsert(meta_.bookId);
    b.title = meta_.title;
    // 元信息来自章节页 lastread，走缓存或索引页时可能拿不到，别把书架里已有的值冲掉
    if (!meta_.author.empty()) b.author = meta_.author;
    if (!meta_.cover.empty()) b.cover = meta_.cover;
    if (!meta_.category.empty()) b.category = meta_.category;
    if (!meta_.status.empty()) b.status = meta_.status;
    b.url = bookUrl_;
    b.total = (int)toc_.size();
    b.chapterIndex = (int)j.GetNum("index", b.chapterIndex);
    b.ratio = j.GetNum("ratio", b.ratio);
    lastRatio_ = b.ratio;
    b.updatedAt = (int64_t)time(nullptr);
    currentIndex_ = b.chapterIndex;
    store_.SaveBookshelf();
  } else if (type == "setSetting") {
    const Json* v = j.Get("value");
    if (v && v->IsObj()) {
      store_.settings().FromJson(*v);
      store_.SaveSettings();
      ApplyNetSettings();
      SendSettings();
    }
  } else if (type == "listBooks") {
    SendBooks();
  } else if (type == "openBook") {
    std::string id = j.GetStr("bookId");
    if (BookRecord* b = store_.Find(id)) {
      if (b->kind == "local") OpenLocalBook(*b);
      else OpenBookUrl(b->url);
    }
  } else if (type == "importTxt") {
    ImportTxtDialog();
  } else if (type == "relocateBook") {
    RelocateBook(j.GetStr("bookId"));
  } else if (type == "rescanTxt") {
    RescanLocalBook(j.GetStr("encoding", "auto"), j.GetStr("regex"));
  } else if (type == "download") {
    if (isLocal_) { SendError("本地 TXT 不用下载"); return; }
    if (toc_.empty() || meta_.bookId.empty()) { SendError("先打开一本书"); return; }
    std::vector<int> idx = BuildRange(j, (int)toc_.size(), currentIndex_ < 0 ? 0 : currentIndex_);
    if (idx.empty()) { SendError("下载范围为空"); return; }
    store_.WriteToc(meta_.bookId, TocToJson());
    jobBookId_ = meta_.bookId;
    jobToc_ = toc_;
    StartJob(false, idx, j);
  } else if (type == "clearCache") {
    ClearCache(j);
  } else if (type == "downloadPause") {
    StopJob("paused");
  } else if (type == "exportTxt") {
    BeginExport(j);
  } else if (type == "exportCancel") {
    StopJob("idle");
  } else if (type == "removeBook") {
    std::string id = j.GetStr("bookId");
    auto& list = store_.books();
    for (size_t i = 0; i < list.size(); ++i) {
      if (list[i].bookId == id) { list.erase(list.begin() + (ptrdiff_t)i); break; }
    }
    store_.SaveBookshelf();
    SendBooks();
  } else if (type == "title") {
    SetTitle(U2W(j.GetStr("text", "阅读器")));
  } else if (type == "retryVerify") {
    if (!bookUrl_.empty()) {
      verifyRounds_ = 0;
      SendStatus("正在获取目录…", true);
      FetchToc();
    }
  } else if (type == "fullscreen") {
    ToggleFullscreen();
  }
}

// ---------- 本地 TXT：文件对话框与导入 ----------
static const wchar_t* kTxtFilter = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";

std::wstring App::PickOpenFile() {
  wchar_t buf[4096] = {};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd_;
  ofn.lpstrFilter = kTxtFilter;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = 4096;
  ofn.lpstrTitle = L"选择 TXT 小说文件";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetOpenFileNameW(&ofn)) return L"";
  return std::wstring(buf);
}

std::wstring App::PickSaveFile(const std::string& defaultName) {
  wchar_t buf[4096] = {};
  std::wstring name = U2W(defaultName);
  if (name.size() > 180) name = name.substr(0, 180);
  wcsncpy_s(buf, name.c_str(), _TRUNCATE);
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd_;
  ofn.lpstrFilter = kTxtFilter;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = 4096;
  ofn.lpstrDefExt = L"txt";
  ofn.lpstrTitle = L"导出为 TXT";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetSaveFileNameW(&ofn)) return L"";
  return std::wstring(buf);
}

// 同一个文件（路径）永远得到同一个 bookId，重复导入不会多出一本
static std::string LocalBookId(const std::wstring& path) {
  const std::string u = W2U(path);
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : u) { h ^= c; h *= 1099511628211ull; }
  char buf[40] = {};
  sprintf_s(buf, "local-%016llx", (unsigned long long)h);
  return buf;
}

void App::ImportTxtDialog() {
  std::wstring path = PickOpenFile();
  if (path.empty()) return;
  ImportTxt(path, "auto", "", true);
}

void App::ImportTxt(const std::wstring& path, const std::string& enc, const std::string& regex, bool openAfter) {
  auto book = std::make_unique<TxtBook>();
  std::string err;
  TxtSplitRule rule;
  rule.encoding = enc.empty() ? "auto" : enc;
  rule.regex = regex;
  if (!book->Load(path, rule, err)) { SendError("导入失败：" + err); return; }

  const std::string id = LocalBookId(path);
  BookRecord& b = store_.Upsert(id);
  b.kind = "local";
  b.localPath = W2U(path);
  b.txtEncoding = rule.encoding;
  b.txtRegex = regex;
  b.title = book->Title();
  b.author = "本地文件";
  b.category = "本地 TXT";
  b.status = "";
  b.cover = "";
  b.url = "file:///" + b.localPath;
  b.total = (int)book->Chapters().size();
  if (b.chapterIndex < 0 || b.chapterIndex >= b.total) b.chapterIndex = 0;
  b.updatedAt = (int64_t)time(nullptr);
  store_.SaveBookshelf();
  SendBooks();

  Json t = Json::Obj();
  t["type"] = Json::Str("toast");
  t["text"] = Json::Str("已导入《" + b.title + "》，识别到 " + std::to_string(b.total) +
                        " 章（编码 " + book->Encoding() + "）");
  PostJson(t);
  if (openAfter) OpenLocalBook(b);
}

void App::OpenLocalBook(const BookRecord& rec) {
  isLocal_ = true;
  txt_.reset();
  toc_.clear();
  meta_ = BookMeta();
  currentIndex_ = -1;
  verifyRounds_ = 0;
  loadedOk_.clear();
  { std::lock_guard<std::mutex> lk(inflightMu_); healTried_.clear(); }
  StopJob("idle");

  auto book = std::make_unique<TxtBook>();
  std::string err;
  TxtSplitRule rule;
  rule.encoding = rec.txtEncoding.empty() ? "auto" : rec.txtEncoding;
  rule.regex = rec.txtRegex;
  if (!book->Load(U2W(rec.localPath), rule, err)) {
    SendError("打开失败：" + err);
    Json j = Json::Obj();
    j["type"] = Json::Str("needRelocate");
    j["bookId"] = Json::Str(rec.bookId);
    j["path"] = Json::Str(rec.localPath);
    PostJson(j);
    return;
  }
  txt_ = std::move(book);
  bookUrl_ = "file:///" + rec.localPath;
  meta_.bookId = rec.bookId;
  meta_.title = txt_->Title();
  meta_.author = "本地文件";
  meta_.category = "本地 TXT";
  for (const auto& c : txt_->Chapters()) {
    TocEntry e;
    e.title = c.title;
    toc_.push_back(std::move(e));
  }

  int resume = rec.chapterIndex;
  double ratio = rec.ratio;
  if (resume < 0 || resume >= (int)toc_.size()) { resume = 0; ratio = 0; }
  currentIndex_ = resume;
  lastRatio_ = ratio;
  SendMeta();
  SendBook(resume, ratio);
  SendTxtInfo();
  SendStatus("", false);
  FetchChapter(resume);
}

void App::SendTxtInfo() {
  if (!isLocal_ || !txt_) return;
  Json j = Json::Obj();
  j["type"] = Json::Str("txtInfo");
  j["bookId"] = Json::Str(meta_.bookId);
  j["path"] = Json::Str(W2U(txt_->PathW()));
  j["encoding"] = Json::Str(txt_->Encoding());
  j["requested"] = Json::Str(txt_->RequestedEncoding());
  j["regex"] = Json::Str(txt_->Rule());
  j["chapters"] = Json::Num((double)(int)txt_->Chapters().size());
  Json arr = Json::Arr();
  const auto& chs = txt_->Chapters();
  for (size_t i = 0; i < chs.size() && i < 6; ++i) arr.Push(Json::Str(chs[i].title));
  j["samples"] = arr;
  PostJson(j);
}

void App::RescanLocalBook(const std::string& enc, const std::string& regex) {
  if (!isLocal_ || !txt_) { SendError("当前不是本地 TXT 书"); return; }
  TxtSplitRule rule;
  rule.encoding = enc.empty() ? "auto" : enc;
  rule.regex = regex;
  txt_->Resplit(rule);

  BookRecord* rec = store_.Find(meta_.bookId);
  if (rec) {
    rec->txtEncoding = rule.encoding;
    rec->txtRegex = regex;
    rec->total = (int)txt_->Chapters().size();
    if (rec->chapterIndex >= rec->total) rec->chapterIndex = 0;
    store_.SaveBookshelf();
  }

  toc_.clear();
  for (const auto& c : txt_->Chapters()) {
    TocEntry e;
    e.title = c.title;
    toc_.push_back(std::move(e));
  }
  loadedOk_.clear();
  const int resume = (rec && rec->chapterIndex >= 0 && rec->chapterIndex < (int)toc_.size())
                       ? rec->chapterIndex : 0;
  currentIndex_ = resume;
  lastRatio_ = 0;
  SendMeta();
  SendTxtInfo();
  SendBook(resume, 0);
  SendStatus("已重新识别：" + std::to_string(toc_.size()) + " 章", false);
  FetchChapter(resume);
}

void App::RelocateBook(const std::string& bookId) {
  BookRecord* rec = store_.Find(bookId);
  if (!rec) return;
  std::wstring path = PickOpenFile();
  if (path.empty()) return;
  auto book = std::make_unique<TxtBook>();
  std::string err;
  TxtSplitRule rule;
  rule.encoding = rec->txtEncoding.empty() ? "auto" : rec->txtEncoding;
  rule.regex = rec->txtRegex;
  if (!book->Load(path, rule, err)) { SendError("重新定位失败：" + err); return; }
  rec->kind = "local";
  rec->localPath = W2U(path);
  rec->title = book->Title();
  rec->total = (int)book->Chapters().size();
  if (rec->chapterIndex >= rec->total) rec->chapterIndex = 0;
  rec->updatedAt = (int64_t)time(nullptr);
  store_.SaveBookshelf();
  SendBooks();
  OpenLocalBook(*rec);
}

// 拖放诊断：关键 HRESULT 与子窗口类名落盘，方便排查「拖进去没反应」
void App::DropDebug(const std::string& line) {
  dropLog_ += line + "\n";
  std::wstring dbg = store_.DataDir() + L"\\debug";
  EnsureDir(dbg);
  WriteFileBytes(dbg + L"\\drop.txt", dropLog_);
}

// 在主窗口（以及 WebView2 的子窗口）上注册 IDropTarget，接管文件拖放
void App::InstallDropTarget() {
  if (!dropTarget_) {
    dropTarget_ = new DropTarget(
      [this](const std::wstring& path) {
        DropDebug("drop path=" + W2U(path));
        ImportTxt(path, "auto", "", true);
      },
      [this](bool on) {
        Json j = Json::Obj();
        j["type"] = Json::Str("dragging");
        j["on"] = Json::Bool(on);
        PostJson(j);
      });
  }

  const HRESULT hrMain = RegisterDragDrop(hwnd_, dropTarget_.Get());
  char buf[128] = {};
  sprintf_s(buf, "RegisterDragDrop(hwnd) hr=0x%08lX", (unsigned long)hrMain);
  DropDebug(buf);

  // WebView2 的子窗口默认自己吃掉拖放；要是它还占着，就抢过来
  EnumChildWindows(
    hwnd_,
    [](HWND child, LPARAM lp) -> BOOL {
      App* self = reinterpret_cast<App*>(lp);
      wchar_t cls[128] = {};
      GetClassNameW(child, cls, 128);
      HRESULT hr = RegisterDragDrop(child, self->dropTarget_.Get());
      if (hr == DRAGDROP_E_ALREADYREGISTERED) {
        RevokeDragDrop(child);
        hr = RegisterDragDrop(child, self->dropTarget_.Get());
      }
      char line[320] = {};
      sprintf_s(line, "child %ls hr=0x%08lX", cls, (unsigned long)hr);
      self->DropDebug(line);
      return TRUE;
    },
    reinterpret_cast<LPARAM>(this));
}

void App::HandleDrop(HDROP drop) {
  const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  std::wstring first;
  for (UINT i = 0; i < n; ++i) {
    wchar_t buf[4096] = {};
    if (!DragQueryFileW(drop, i, buf, 4096)) continue;
    std::wstring p = buf;
    std::wstring low = p;
    for (auto& ch : low) ch = (wchar_t)towlower(ch);
    if (low.size() >= 4 && low.compare(low.size() - 4, 4, L".txt") == 0) { first = p; break; }
  }
  DragFinish(drop);
  if (first.empty()) { SendError("只支持拖入 .txt 文件"); return; }
  ImportTxt(first, "auto", "", true);
}

std::string App::TocToJson() const {
  Json j = Json::Obj();
  Json m = Json::Obj();
  m["title"] = Json::Str(meta_.title);
  m["author"] = Json::Str(meta_.author);
  m["cover"] = Json::Str(meta_.cover);   // 落盘存远端地址，本地图按 bookId 现算
  m["category"] = Json::Str(meta_.category);
  m["status"] = Json::Str(meta_.status);
  m["url"] = Json::Str(bookUrl_);
  j["meta"] = m;
  Json arr = Json::Arr();
  for (const auto& c : toc_) {
    Json e = Json::Obj();
    e["title"] = Json::Str(c.title);
    e["url"] = Json::Str(c.url);
    e["words"] = Json::Str(c.words);
    e["updated"] = Json::Str(c.updated);
    arr.Push(e);
  }
  j["chapters"] = arr;
  return j.Dump();
}

// 断网 / 站点不可达时用本地 toc.json 继续（离线下载过就能读）
bool App::ApplyCachedToc() {
  const std::string id = SiteBookId(Site(), bookUrl_);
  if (id.empty()) return false;
  std::string text;
  if (!store_.ReadToc(id, text)) return false;
  Json j;
  if (!Json::Parse(text, j) || !j.IsObj()) return false;
  const Json* chs = j.Get("chapters");
  if (!chs || !chs->IsArr() || chs->arr.empty()) return false;

  std::vector<TocEntry> loaded;
  for (const auto& e : chs->arr) {
    TocEntry t;
    t.title = e.GetStr("title");
    t.url = e.GetStr("url");
    t.words = e.GetStr("words");
    t.updated = e.GetStr("updated");
    if (t.title.empty()) continue;
    loaded.push_back(std::move(t));
  }
  if (loaded.empty()) return false;
  toc_ = std::move(loaded);

  meta_ = BookMeta();
  meta_.bookId = id;
  if (const Json* m = j.Get("meta")) {
    if (m->IsObj()) {
      meta_.title = m->GetStr("title");
      meta_.author = m->GetStr("author");
      meta_.cover = m->GetStr("cover");
      meta_.category = m->GetStr("category");
      meta_.status = m->GetStr("status");
    }
  }
  if (meta_.title.empty()) meta_.title = id;

  int resume = 0;
  double ratio = 0;
  if (BookRecord* rec = store_.Find(id)) {
    resume = rec->chapterIndex;
    ratio = rec->ratio;
    if (resume < 0 || resume >= (int)toc_.size()) { resume = 0; ratio = 0; }
  }
  currentIndex_ = resume;
  lastRatio_ = ratio;
  SendBook(resume, ratio);
  SendCacheInfo();
  SendStatus("离线模式：使用本地缓存的目录", false);
  FetchChapter(resume);
  return true;
}

std::vector<int> App::BuildRange(const Json& o, int total, int cur) const {
  std::vector<int> idx;
  if (total <= 0) return idx;
  const std::string scope = o.GetStr("scope", "all");
  int from = 0, to = total - 1;
  if (scope == "next") {
    from = cur;
    to = cur + (int)o.GetNum("next", 50) - 1;
  } else if (scope == "range") {
    from = (int)o.GetNum("from", 1) - 1;
    to = (int)o.GetNum("to", total) - 1;
  }
  if (from < 0) from = 0;
  if (to > total - 1) to = total - 1;
  for (int i = from; i <= to; ++i) idx.push_back(i);
  return idx;
}

static std::string SizeText(long long bytes) {
  if (bytes >= 1024 * 1024) {
    return std::to_string(bytes / 1048576) + "." +
           std::to_string((bytes % 1048576) * 10 / 1048576) + " MB";
  }
  if (bytes >= 1024) return std::to_string(bytes / 1024) + " KB";
  return std::to_string(bytes) + " B";
}

// 把「这本书已经缓存了哪些章」推给下载面板（1-based，压成连续区间，带字节数）
void App::SendCacheInfo() {
  Json j = Json::Obj();
  j["type"] = Json::Str("cacheInfo");
  j["bookId"] = Json::Str(meta_.bookId);
  j["total"] = Json::Num((double)(int)toc_.size());
  Json ranges = Json::Arr();
  int count = 0;
  long long bytes = 0;
  if (!isLocal_ && !meta_.bookId.empty()) {
    const std::vector<Store::CachedChapter> list = store_.CachedChapters(meta_.bookId);
    for (size_t i = 0; i < list.size();) {
      size_t k = i;
      long long span = list[i].bytes;
      while (k + 1 < list.size() && list[k + 1].index == list[k].index + 1) {
        ++k;
        span += list[k].bytes;
      }
      Json r = Json::Arr();
      r.Push(Json::Num((double)(list[i].index + 1)));
      r.Push(Json::Num((double)(list[k].index + 1)));
      r.Push(Json::Num((double)span));
      ranges.Push(r);
      count += (int)(k - i + 1);
      bytes += span;
      i = k + 1;
    }
  }
  j["count"] = Json::Num((double)count);
  j["bytes"] = Json::Num((double)bytes);
  j["ranges"] = ranges;
  PostJson(j);
}

// 清理已下载的章节缓存。目录（toc.json）保留，清完照样能离线打开这本书
void App::ClearCache(const Json& o) {
  if (isLocal_ || meta_.bookId.empty()) { SendError("本地 TXT 没有章节缓存"); return; }
  if (jobRunning_) { SendError("有下载任务在进行中，先暂停再清理"); return; }
  const std::string scope = o.GetStr("scope", "all");
  const std::vector<Store::CachedChapter> all = store_.CachedChapters(meta_.bookId);
  std::vector<int> kill;
  long long bytes = 0;
  if (scope == "range") {
    int from = (int)o.GetNum("from", 1);
    int to = (int)o.GetNum("to", 0);
    if (from < 1) from = 1;
    if (to < from) { SendError("清理范围为空"); return; }
    for (const auto& c : all) {
      const int n = c.index + 1;
      if (n >= from && n <= to) { kill.push_back(c.index); bytes += c.bytes; }
    }
  } else {
    for (const auto& c : all) { kill.push_back(c.index); bytes += c.bytes; }
  }
  if (kill.empty()) { SendError("这个范围里没有已缓存的章节"); return; }
  int failed = 0;
  for (int i : kill) {
    if (!DeleteFileW(store_.ChapterPath(meta_.bookId, i).c_str())) ++failed;
  }
  SendCacheInfo();
  const int done = (int)kill.size() - failed;
  Json t = Json::Obj();
  t["type"] = Json::Str("toast");
  t["text"] = Json::Str("已清理 " + std::to_string(done) + " 章缓存（释放 " + SizeText(bytes) + "）" +
                        (failed ? "，有 " + std::to_string(failed) + " 个文件删不掉" : ""));
  PostJson(t);
  SendStatus("已清理 " + std::to_string(done) + " 章本地缓存", false);
}

void App::SendJobState(const std::string& state) {
  Json j = Json::Obj();
  j["type"] = Json::Str("job");
  j["kind"] = Json::Str(jobExport_ ? "export" : "download");
  j["state"] = Json::Str(state);
  j["done"] = Json::Num((double)jobDone_);
  j["total"] = Json::Num((double)jobTotal_);
  j["queued"] = Json::Num((double)(int)jobQueue_.size());
  PostJson(j);
}

void App::StartJob(bool exportJob, const std::vector<int>& indices, const Json& opts) {
  if (jobRunning_) { SendError("已有任务在进行中，请先暂停"); return; }
  jobExport_ = exportJob;
  jobQueue_ = indices;
  jobTotal_ = (int)indices.size();
  jobDone_ = 0;
  exOpts_ = opts;
  jobFailStreak_ = 0;
  jobFailTotal_ = 0;
  jobPenaltyMs_ = 0;
  jobChallengeSeen_ = false;
  // 每章之间的间隔完全由用户设定（工具 → 离线下载 → 每章间隔），不再按章数自动加码
  const int n = (int)indices.size();
  jobGapMs_ = (int)(store_.settings().downloadGap * 1000.0 + 0.5);
  if (jobGapMs_ < 0) jobGapMs_ = 0;
  if (jobGapMs_ > 60000) jobGapMs_ = 60000;
  jobLastAt_ = GetTickCount();
  jobRunning_ = true;
  SendJobState("running");
  {
    // 实际节奏还要受「请求间隔」这个全局下限约束，报最大值才不会骗人
    const int gap = jobGapMs_ > throttleMs_.load() ? jobGapMs_ : throttleMs_.load();
    SendStatus("离线下载 " + std::to_string(n) + " 章：每章约 " + std::to_string(gap / 1000) + "." +
               std::to_string((gap % 1000) / 100) + " 秒", false);
  }
  SetTimer(hwnd_, 4, 250, nullptr);
  PumpJob();
}

void App::StopJob(const std::string& state) {
  if (!jobRunning_) return;
  jobRunning_ = false;
  jobQueue_.clear();
  KillTimer(hwnd_, 4);
  SendJobState(state);
}

// 熔断：任务先停下来，剩余章节留在队列里，等用户处理完再点「开始下载」继续
void App::PauseJobByGuard(const std::string& reason) {
  if (!jobRunning_) return;
  jobRunning_ = false;
  KillTimer(hwnd_, 4);
  SendJobState("paused");
  SendStatus(reason, false);
  Json t = Json::Obj();
  t["type"] = Json::Str("toast");
  t["text"] = Json::Str(reason);
  PostJson(t);
}

// 串行推进：先让用户正在读的章节抓完，再抓后台任务的一章
void App::PumpJob() {
  if (!jobRunning_) return;
  if (fetchBusy_ || !fetchQueue_.empty()) return;
  // 每章之间至少隔 jobGapMs_（用户设定）+ 失败惩罚；从上一章抓完的时刻算起
  const int wait = jobGapMs_ + jobPenaltyMs_;
  if (wait > 0) {
    const int elapsed = (int)(GetTickCount() - jobLastAt_);
    if (elapsed < wait) {
      const int remain = wait - elapsed;
      SetTimer(hwnd_, 4, remain > 10 ? remain : 10, nullptr);
      return;
    }
  }
  jobPenaltyMs_ = 0;
  while (true) {
    if (jobQueue_.empty()) {
      jobRunning_ = false;
      KillTimer(hwnd_, 4);
      SendJobState("done");
      if (jobExport_) {
        RunExportWrite();
      } else {
        std::string msg = "离线下载完成，共 " + std::to_string(jobDone_) + " 章";
        if (jobFailTotal_ > 0) msg += "（失败 " + std::to_string(jobFailTotal_) + " 章）";
        SendStatus(msg, false);
      }
      return;
    }
    const int idx = jobQueue_.front();
    jobQueue_.erase(jobQueue_.begin());
    std::string cached;
    if (store_.ReadChapterCache(jobBookId_, idx, cached)) { ++jobDone_; continue; }
    const std::string url = (idx < (int)jobToc_.size()) ? jobToc_[idx].url : std::string();
    if (url.empty()) { ++jobDone_; continue; }
    FetchChapterInternal(jobBookId_, url, idx, true, [this](bool ok) {
      ++jobDone_;
      jobLastAt_ = GetTickCount();
      // 中途按了暂停 / 取消：别再往前端推 running，否则「开始下载」会被卡成禁用
      if (!jobRunning_) return;
      if (ok) {
        jobFailStreak_ = 0;
      } else {
        ++jobFailStreak_;
        ++jobFailTotal_;
        if (jobChallengeSeen_) {
          jobChallengeSeen_ = false;
          PauseJobByGuard("站点要求人机验证，任务已暂停：请在窗口里过验证，再点「开始下载」继续");
          return;
        }
        if (jobFailStreak_ >= 3) {
          PauseJobByGuard("连续 " + std::to_string(jobFailStreak_) + " 章抓取失败，已自动暂停（本次失败共 " +
                          std::to_string(jobFailTotal_) + " 章）。隔一会儿再继续更稳妥");
          return;
        }
        // 失败后把下一章的间隔拉长：3s → 10s → 30s
        static const int kPenalty[3] = { 3000, 10000, 30000 };
        const int p = kPenalty[jobFailStreak_ - 1];
        if (p > jobPenaltyMs_) jobPenaltyMs_ = p;
      }
      SendJobState("running");
      PumpJob();
    });
    SendJobState("running");
    return;
  }
}

bool App::LoadBookContext(const std::string& bookId, ExportCtx& ctx) {
  ctx = ExportCtx();
  const std::string id = bookId.empty() ? meta_.bookId : bookId;
  if (!id.empty() && id == meta_.bookId && !toc_.empty()) {
    ctx.valid = true;
    ctx.bookId = meta_.bookId;
    ctx.title = meta_.title;
    ctx.local = isLocal_;
    ctx.toc = toc_;
    if (isLocal_ && txt_) {
      ctx.localPath = txt_->PathW();
      ctx.txtEncoding = txt_->RequestedEncoding();
      ctx.txtRegex = txt_->Rule();
    }
    return true;
  }

  BookRecord* rec = store_.Find(id);
  if (!rec) return false;
  ctx.valid = true;
  ctx.bookId = rec->bookId;
  ctx.title = rec->title;
  ctx.local = (rec->kind == "local");
  ctx.localPath = U2W(rec->localPath);
  ctx.txtEncoding = rec->txtEncoding;
  ctx.txtRegex = rec->txtRegex;

  std::string tocText;
  if (store_.ReadToc(rec->bookId, tocText)) {
    Json j;
    if (Json::Parse(tocText, j) && j.IsObj()) {
      if (const Json* chs = j.Get("chapters")) {
        if (chs->IsArr()) for (const auto& e : chs->arr) {
          TocEntry t;
          t.title = e.GetStr("title");
          t.url = e.GetStr("url");
          if (t.title.empty()) continue;
          ctx.toc.push_back(std::move(t));
        }
      }
      if (ctx.title.empty()) {
        if (const Json* m = j.Get("meta")) ctx.title = m->GetStr("title");
      }
    }
  }
  if (ctx.toc.empty() && ctx.local) {
    TxtBook book;
    std::string err;
    TxtSplitRule rule;
    rule.encoding = ctx.txtEncoding;
    rule.regex = ctx.txtRegex;
    if (book.Load(ctx.localPath, rule, err)) {
      for (const auto& c : book.Chapters()) {
        TocEntry t;
        t.title = c.title;
        ctx.toc.push_back(std::move(t));
      }
      if (ctx.title.empty()) ctx.title = book.Title();
    }
  }
  return !ctx.toc.empty();
}

void App::BeginExport(const Json& opts) {
  if (jobRunning_) { SendError("有任务在进行中，请先暂停"); return; }
  ExportCtx ctx;
  if (!LoadBookContext(opts.GetStr("bookId"), ctx)) {
    SendError("导出失败：没有这本书的目录，先在阅读器里打开一次再导出");
    return;
  }
  const int total = (int)ctx.toc.size();
  const std::string scope = opts.GetStr("scope", "all");
  int from = 0, to = total - 1;
  if (scope == "range") {
    from = (int)opts.GetNum("from", 1) - 1;
    to = (int)opts.GetNum("to", total) - 1;
  }
  if (from < 0) from = 0;
  if (to > total - 1) to = total - 1;
  if (to < from) { SendError("导出范围为空"); return; }

  std::vector<int> idx;
  for (int i = from; i <= to; ++i) {
    if (scope == "cached" && !ctx.local) {
      std::string c;
      if (!store_.ReadChapterCache(ctx.bookId, i, c)) continue;
    }
    idx.push_back(i);
  }
  if (idx.empty()) { SendError("这个范围里没有可导出的章节"); return; }

  std::string name = ctx.title.empty() ? std::string("导出") : ctx.title;
  name += ".txt";
  std::wstring out = PickSaveFile(name);
  if (out.empty()) return;

  exOutPath_ = out;
  exCtx_ = ctx;
  exOpts_ = opts;
  jobBookId_ = ctx.bookId;
  jobToc_ = ctx.toc;

  std::vector<int> missing;
  if (opts.GetBool("fill", false) && !ctx.local) {
    for (int i : idx) {
      std::string c;
      if (!store_.ReadChapterCache(ctx.bookId, i, c)) missing.push_back(i);
    }
  }
  if (!missing.empty()) {
    SendStatus("先补齐 " + std::to_string(missing.size()) + " 个未缓存章节…", true);
    StartJob(true, missing, opts);
    return;
  }
  RunExportWrite();
}

bool App::GatherChapters(const ExportCtx& ctx, const std::vector<int>& indices,
                         std::vector<ExportChapter>& out, int& skipped) {
  out.clear();
  skipped = 0;
  if (ctx.local) {
    TxtBook temp;
    const TxtBook* live = (txt_ && txt_->PathW() == ctx.localPath) ? txt_.get() : nullptr;
    if (!live) {
      TxtSplitRule rule;
      rule.encoding = ctx.txtEncoding;
      rule.regex = ctx.txtRegex;
      std::string err;
      if (!temp.Load(ctx.localPath, rule, err)) return false;
      live = &temp;
    }
    const auto& chs = live->Chapters();
    for (int i : indices) {
      if (i < 0 || i >= (int)chs.size()) { ++skipped; continue; }
      ExportChapter c;
      c.title = chs[i].title;
      live->GetChapter(i, c.paragraphs);
      out.push_back(std::move(c));
    }
    return true;
  }

  for (int i : indices) {
    std::string cached;
    if (!store_.ReadChapterCache(ctx.bookId, i, cached)) { ++skipped; continue; }
    Json cj;
    if (!Json::Parse(cached, cj) || !cj.IsObj()) { ++skipped; continue; }
    ExportChapter c;
    c.title = cj.GetStr("title");
    if (c.title.empty() && i < (int)ctx.toc.size()) c.title = ctx.toc[i].title;
    if (const Json* p = cj.Get("paragraphs")) {
      if (p->IsArr()) for (const auto& x : p->arr) {
        if (x.t != Json::T::Str) continue;
        if (IsWatermarkLine(x.str)) continue;
        c.paragraphs.push_back(x.str);
      }
    }
    if (c.paragraphs.empty()) { ++skipped; continue; }
    out.push_back(std::move(c));
  }
  return true;
}

void App::RunExportWrite() {
  if (!exCtx_.valid) { SendError("导出上下文已失效，请重新导出"); return; }
  const ExportCtx& ctx = exCtx_;
  const int total = (int)ctx.toc.size();
  const std::string scope = exOpts_.GetStr("scope", "all");
  int from = 0, to = total - 1;
  if (scope == "range") {
    from = (int)exOpts_.GetNum("from", 1) - 1;
    to = (int)exOpts_.GetNum("to", total) - 1;
  }
  if (from < 0) from = 0;
  if (to > total - 1) to = total - 1;

  std::vector<int> idx;
  for (int i = from; i <= to; ++i) {
    if (scope == "cached" && !ctx.local) {
      std::string c;
      if (!store_.ReadChapterCache(ctx.bookId, i, c)) continue;
    }
    idx.push_back(i);
  }

  std::vector<ExportChapter> chapters;
  int skipped = 0;
  if (!GatherChapters(ctx, idx, chapters, skipped)) { SendError("导出失败：读取章节内容出错"); return; }
  if (chapters.empty()) { SendError("没有可导出的内容"); return; }

  TextEncoding enc = TextEncoding::Utf8Bom;
  ParseTextEncoding(exOpts_.GetStr("encoding", "utf8bom"), enc);
  const std::string merged = BuildMergedText(chapters, exOpts_.GetBool("includeTitles", true),
                                             exOpts_.GetBool("simplified", true));
  const std::string bytes = EncodeText(merged, enc);
  std::string err;
  if (!WriteTextFile(exOutPath_, bytes, err)) { SendError("导出失败：" + err); return; }

  Json j = Json::Obj();
  j["type"] = Json::Str("exportDone");
  j["path"] = Json::Str(W2U(exOutPath_));
  j["chapters"] = Json::Num((double)chapters.size());
  j["skipped"] = Json::Num((double)skipped);
  j["bytes"] = Json::Num((double)bytes.size());
  j["encoding"] = Json::Str(TextEncodingId(enc));
  PostJson(j);
  SendStatus("已导出 " + std::to_string(chapters.size()) + " 章" +
             (skipped ? ("（跳过 " + std::to_string(skipped) + " 章）") : std::string()), false);
}

void App::ToggleFullscreen() {
  if (!hwnd_) return;
  if (!fullscreen_) {
    savedStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    GetWindowRect(hwnd_, &savedRect_);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) return;
    SetWindowLongPtrW(hwnd_, GWL_STYLE, savedStyle_ & ~(WS_CAPTION | WS_THICKFRAME));
    SetWindowPos(hwnd_, nullptr, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_NOZORDER | SWP_FRAMECHANGED);
    fullscreen_ = true;
  } else {
    SetWindowLongPtrW(hwnd_, GWL_STYLE, savedStyle_);
    SetWindowPos(hwnd_, nullptr, savedRect_.left, savedRect_.top,
                 savedRect_.right - savedRect_.left,
                 savedRect_.bottom - savedRect_.top,
                 SWP_NOZORDER | SWP_FRAMECHANGED);
    fullscreen_ = false;
  }
  ResizeWebView();
}
