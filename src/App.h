#pragma once
#include "Common.h"
#include "Export.h"
#include "Extract.h"
#include "SiteAdapter.h"
#include "Http.h"
#include "Json.h"
#include "Store.h"
#include "DropTarget.h"
#include "TxtBook.h"
#include <shellapi.h>
#include <ole2.h>
#include <wrl.h>
#include <WebView2.h>
#include <mutex>
#include <set>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>

#define WM_APP_JOB (WM_APP + 1)

struct PageResult {
  bool ok = false;
  int status = 0;
  std::string error;
  std::string body;
};


// 导出时用到的书籍上下文（支持从书架直接导出，不打断当前阅读）
struct ExportCtx {
  bool valid = false;
  bool local = false;
  std::string bookId;
  std::string title;
  std::wstring localPath;
  std::string txtEncoding = "auto";
  std::string txtRegex;
  std::vector<TocEntry> toc;
};

class App {
public:
  static App& Get();
  bool Init(HINSTANCE hInst, int nCmdShow);
  LRESULT HandleMessage(HWND h, UINT m, WPARAM w, LPARAM l);

private:
  void CreateWebView();
  void OnWebMessage(const std::wstring& rawJson);
  void ResizeWebView();

  void OpenBookUrl(const std::string& url);
  void FetchToc();
  bool ApplyCachedToc();
  void ApplyToc(const std::string& indexUrl, const std::string& indexHtml, const PageResult& listRes);
  void FetchChapter(int index);
  void FetchChapterInternal(const std::string& bookId, const std::string& url, int index,
                            bool silent, std::function<void(bool)> done);
  void BeginVerification(const std::string& url, std::function<void()> after);
  void PollVerification();
  void RefreshCookies(const std::wstring& url, std::function<void()> done);

  // 抓取统一走隐藏的 WebView2：站点上了 Cloudflare 后，WinHTTP 即使带上
  // cf_clearance 也会被 403（CF 把放行绑在浏览器指纹上），只有浏览器能过。
  void CreateFetchView();
  void GetPage(const std::string& url, std::function<void(const PageResult&)> done);
  void BrowserGet(const std::string& url, std::function<void(const PageResult&)> done);
  void StartNextFetch();
  void FinishFetch(const PageResult& res);

  // ---- 封面本地缓存 ----
  // 封面图大多是 CF 后面的图片，WinHTTP 拿不到（403），所以借用同一个隐藏
  // WebView2：直接导航到图片 URL，Chrome 会包一层同源 <img> 文档，再画进
  // canvas 取 dataURL 就绕开了跨域污染。缓存到 data/covers/{bookId}.jpg，
  // 前端通过虚拟主机 covers.local 读本地文件，断网 / 关代理也是好图。
  std::wstring CoversDir() const;
  std::string CoverUrlFor(const std::string& bookId) const;
  void EnsureCoverCached(const std::string& bookId, const std::string& url);
  void BrowserGetImage(const std::string& url, const std::string& bookId);
  void ProbeCoverImage();
  void SaveCover(const std::string& bookId, const std::string& dataUrl);

  // ---- 本地 TXT ----
  void ImportTxtDialog();
  void ImportTxt(const std::wstring& path, const std::string& enc, const std::string& regex, bool openAfter);
  void OpenLocalBook(const BookRecord& rec);
  void RescanLocalBook(const std::string& enc, const std::string& regex);
  void RelocateBook(const std::string& bookId);
  void SendTxtInfo();

  // ---- 离线下载 / 导出（共用一条串行任务，给用户正在读的章节让路）----
  void StartJob(bool exportJob, const std::vector<int>& indices, const Json& opts);
  void PumpJob();
  void StopJob(const std::string& state);
  void PauseJobByGuard(const std::string& reason);
  void SendJobState(const std::string& state);
  void SendCacheInfo();
  void ClearCache(const Json& o);
  void RunExportWrite();
  void BeginExport(const Json& opts);
  bool GatherChapters(const struct ExportCtx& ctx, const std::vector<int>& indices,
                      std::vector<ExportChapter>& out, int& skipped);
  bool LoadBookContext(const std::string& bookId, struct ExportCtx& ctx);
  std::string TocToJson() const;
  std::vector<int> BuildRange(const Json& o, int total, int cur) const;

  std::wstring PickOpenFile();
  std::wstring PickSaveFile(const std::string& defaultName);
  void HandleDrop(HDROP drop);
  void InstallDropTarget();
  void DropDebug(const std::string& line);

  void SendStatus(const std::string& text, bool busy);
  void SendError(const std::string& message);
  void SendSettings();
  void SendMeta();
  void SendBook(int resumeIndex, double resumeRatio);
  void SendChapter(int index, const std::string& title,
                   const std::vector<std::string>& paragraphs, bool cached = false);
  void SendBooks();
  void PostJson(const Json& j);

  void LoadSites();
  const SiteAdapter& Site() const;

  std::wstring ProxyUrl();
  std::wstring ResolveWebDir();
  void RunAsync(std::function<void()> work);
  void PostToUi(std::function<void()> fn);
  void Throttle();
  void ApplyNetSettings();
  void SetTitle(const std::wstring& t);
  void ToggleFullscreen();

  HWND hwnd_ = nullptr;
  Microsoft::WRL::ComPtr<ICoreWebView2Environment> env_;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
  Microsoft::WRL::ComPtr<ICoreWebView2Settings> wvSettings_;
  Microsoft::WRL::ComPtr<ICoreWebView2CookieManager> cookieMgr_;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> fetchCtrl_;
  Microsoft::WRL::ComPtr<ICoreWebView2> fetchView_;
  HWND fetchHwnd_ = nullptr;

  Microsoft::WRL::ComPtr<IDropTarget> dropTarget_;
  std::string dropLog_;

  std::wstring ua_;
  std::wstring cookieHeader_;
  Store store_;
  SiteRegistry sites_;
  const SiteAdapter* site_ = nullptr;

  std::string bookUrl_;
  BookMeta meta_;
  std::vector<TocEntry> toc_;
  int currentIndex_ = -1;
  int verifyClear_ = 0;
  int verifyRounds_ = 0;
  bool verifying_ = false;
  std::string verifyOrigin_;
  std::mutex inflightMu_;
  std::set<int> inflight_;
  std::set<int> healTried_;
  std::set<int> loadedOk_;
  std::function<void()> verifyAfter_;

  // 本地 TXT 状态（原地引用，不复制文件）
  std::unique_ptr<TxtBook> txt_;
  bool isLocal_ = false;

  struct FetchJob {
    std::string url;
    int tries = 0;
    bool image = false;   // true = 取封面：回来的是 dataURL，不是 HTML
    bool probed = false;  // 已经问过一次图片文档（没解码好就等会儿再问）
    std::string bookId;   // 封面任务用：存成 data/covers/{bookId}.jpg
    std::function<void(const PageResult&)> done;
  };
  std::deque<FetchJob> fetchQueue_;
  bool fetchBusy_ = false;
  int fetchGen_ = 0;
  int navGen_ = -1;
  std::set<std::string> coverTried_;   // 试过的书号，一次会话里不反复重试
  bool coverSweepPending_ = false;     // 抓取视图还没就绪时，就绪后补一次书架刷新
  std::chrono::steady_clock::time_point lastFetch_ = std::chrono::steady_clock::now();

  bool fullscreen_ = false;
  RECT savedRect_ = {};
  LONG_PTR savedStyle_ = 0;
  double lastRatio_ = 0;
  std::atomic<int> throttleMs_{1200};

  // 任务（离线下载 / 导出补齐）状态
  bool jobRunning_ = false;
  bool jobExport_ = false;
  std::vector<int> jobQueue_;
  int jobTotal_ = 0;
  int jobDone_ = 0;
  std::string jobBookId_;
  std::vector<TocEntry> jobToc_;
  // 批量任务的自我保护：连续失败就熔断，失败后逐章拉长间隔
  int jobFailStreak_ = 0;
  int jobFailTotal_ = 0;
  int jobPenaltyMs_ = 0;
  int jobGapMs_ = 0;      // 用户设定的每章间隔
  DWORD jobLastAt_ = 0;   // 上一章抓完的时刻，用来卡每章间隔
  bool jobChallengeSeen_ = false;

  // 导出上下文
  ExportCtx exCtx_;
  std::wstring exOutPath_;
  Json exOpts_;
};

