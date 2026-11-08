#include "../src/Common.h"
#include "../src/Http.h"
#include "../src/Extract.h"
#include "../src/SiteAdapter.h"
#include <cstdio>

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc < 3) { printf("usage: http_test <url> <cookiefile> [ua]\n"); return 2; }
  std::string cookieRaw;
  if (!ReadFileBytes(U2W(argv[2]), cookieRaw)) { printf("cookie read failed\n"); return 1; }
  // cookie file: line1 url, line2 ua=..., line6 cookie header
  std::vector<std::string> lines = SplitLines(cookieRaw);
  std::string cookie, ua;
  for (auto& l : lines) {
    if (StartsWith(l, "ua=")) ua = l.substr(3);
    else if (l.find('=') != std::string::npos && l.find("; ") != std::string::npos) cookie = l;
  }
  if (argc > 3) ua = argv[3];
  SiteRegistry reg;
  reg.Load(L"");
  const SiteAdapter& site = reg.Fallback();

  std::wstring proxy = ResolveProxy(ProxyConfig{});
  printf("proxy=%s\nua_len=%zu cookie_len=%zu\n", W2U(proxy).c_str(), ua.size(), cookie.size());
  HttpResult r = HttpGet(U2W(argv[1]), U2W(ua), U2W(cookie), proxy);
  printf("ok=%d status=%d err=%s size=%zu\n", (int)r.ok, r.status, r.error.c_str(), r.body.size());
  printf("has_txtcontent0=%d has_lastread=%d has_endhtml=%d\n",
    (int)(r.body.find("txtcontent0") != std::string::npos),
    (int)(r.body.find("lastread.set(") != std::string::npos),
    (int)(r.body.find("</html>") != std::string::npos));
  BookMeta m;
  if (SiteParseLastRead(site, r.body, m)) printf("lastread OK title=%s author=%s\n", m.title.c_str(), m.author.c_str());
  else printf("lastread PARSE FAILED\n");
  Chapter c = SiteParseChapter(site, r.body);
  printf("chapter ok=%d title=%s paras=%zu\n", (int)c.ok, c.title.c_str(), c.paragraphs.size());
  if (!r.body.empty()) printf("tail200=%s\n", r.body.substr(r.body.size() > 200 ? r.body.size() - 200 : 0).c_str());
  return 0;
}
