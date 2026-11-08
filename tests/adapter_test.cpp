// 站点适配器单元测试：规则解析、认站、URL 模板、选择器子集，
// 以及最要紧的一条 —— 行为真的由 JSON 里的规则决定。
#include "../src/Common.h"
#include "../src/Extract.h"
#include "../src/SiteAdapter.h"
#include <cstdio>
#include <string>

static int g_pass = 0, g_fail = 0;
static void Check(bool ok, const char* name) {
  if (ok) { g_pass++; printf("  ok   %s\n", name); }
  else { g_fail++; printf("  FAIL %s\n", name); }
}

int main() {
  SetConsoleOutputCP(CP_UTF8);
  printf("=== SITEADAPTER SELFTEST ===\n");
  std::string err;

  // 1. 内置默认规则
  SiteAdapter d;
  Check(ParseSiteAdapterJson(BuiltinSiteJson(), d, &err), "builtin json parses");
  Check(d.id == "twkan", "builtin id=twkan");
  Check(!d.chapterContent.empty(), "builtin has content selectors");
  Check(!d.hosts.empty(), "builtin has hosts");

  // 2. 空目录：不加载任何外部规则，但兜底规则仍然可用
  SiteRegistry empty;
  empty.Load(L"");
  Check(empty.count() == 0, "empty dir -> 0 file adapters");
  Check(empty.Match("https://twkan.com/book/67581/index.html") != nullptr,
        "empty dir -> builtin rule still matches twkan");
  Check(empty.Match("https://example.org/book/1") == nullptr,
        "unknown host -> no match (caller falls back)");
  Check(empty.Fallback().id == "twkan", "fallback is builtin twkan");

  const SiteAdapter& b = empty.Fallback();

  // 3. URL 规则
  Check(SiteHost("https://twkan.com/book/1/x") == "twkan.com", "host");
  Check(SiteHost("https://WWW.Twkan.com:443/book/1/x") == "www.twkan.com", "host lowercased, port stripped");
  Check(SiteOrigin("https://twkan.com/book/1/x?y=1") == "https://twkan.com", "origin");
  Check(SiteBookId(b, "https://twkan.com/book/67581/index.html") == "67581", "bookId from /book/");
  Check(SiteBookId(b, "https://twkan.com/txt/67581/123") == "67581", "bookId from /txt/");
  Check(SiteBookId(b, "https://twkan.com/nope") == "", "bookId missing -> empty");
  Check(SiteTocListUrl(b, "https://twkan.com/book/67581/index.html") ==
        "https://twkan.com/ajax_novels/chapterlist/67581.html", "tocListUrl template");
  Check(SiteIndexUrl(b, "https://twkan.com/txt/67581/123") ==
        "https://twkan.com/book/67581/index.html", "indexUrl template");

  // 4. 真样本：目录页 / 章节页
  std::string idx;
  if (ReadFileBytes(L"testdata/twkan_book_index.html", idx)) {
    TocResult t = SiteParseToc(b, idx, "https://twkan.com/book/67581/index.html");
    printf("  (toc chapters=%zu)\n", t.chapters.size());
    Check(t.ok, "toc: ok");
    Check(t.chapters.size() > 20, "toc: chapters found on index page");
    Check(!t.chapters.empty() && t.chapters[0].url.find("/txt/") != std::string::npos,
          "toc: chapter url points at /txt/");
    Check(t.meta.bookId == "67581", "toc: bookId from base url");
    Check(!t.meta.title.empty(), "toc: title from <title>");
  } else {
    printf("  skip twkan index sample (testdata not present)\n");
  }

  std::string ajax;
  if (ReadFileBytes(L"testdata/twkan_chapterlist_ajax.html", ajax)) {
    TocResult t2 = SiteParseToc(b, ajax, "https://twkan.com/book/67581/index.html");
    printf("  (ajax chapters=%zu)\n", t2.chapters.size());
    Check(t2.ok, "ajax list: ok");
    Check(t2.chapters.size() > 500, "ajax list: chapters > 500");
  } else {
    printf("  skip twkan ajax sample (testdata not present)\n");
  }

  std::string chap;
  const bool haveChap = ReadFileBytes(L"testdata/twkan_chapter_sample.html", chap);
  if (haveChap) {
    Chapter c = SiteParseChapter(b, chap);
    printf("  (chapter title=%s paras=%zu)\n", c.title.c_str(), c.paragraphs.size());
    Check(c.ok, "chapter: ok");
    Check(!c.title.empty(), "chapter: title");
    Check(c.paragraphs.size() > 5, "chapter: paragraphs > 5");
    BookMeta m;
    Check(SiteParseLastRead(b, chap, m) && !m.title.empty(), "chapter: lastread meta");
  } else {
    printf("  skip twkan chapter sample (testdata not present)\n");
  }

  // 5. 选择器子集（自造 HTML，可控）
  {
    const std::string h =
      "<html><body><h1>标题</h1>"
      "<div id='c'><p>第一章 开始</p><div class='ad'>广告</div><p>正文一</p></div>"
      "<div class='ad'>容器外的广告</div></body></html>";
    SiteAdapter s;
    s.id = "t";
    s.hosts.push_back("t");
    s.chapterTitle = "h1";
    s.chapterContent.push_back("#c");
    s.chapterBlockTags = { "p", "div" };
    s.chapterBreakTags = { "br" };
    s.dropLeadingHeading = false;

    Chapter c1 = SiteParseChapter(s, h);
    Check(c1.title == "标题", "selector: #id + tag");
    Check(c1.paragraphs.size() == 3, "selector: content container respected (3 paras)");
    Check(!c1.paragraphs.empty() && c1.paragraphs[0] == "第一章 开始",
          "dropLeadingHeading=false keeps heading");

    s.dropLeadingHeading = true;
    Check(SiteParseChapter(s, h).paragraphs.size() == 2, "dropLeadingHeading=true drops heading");

    s.chapterStrip.push_back(".ad");
    Chapter c3 = SiteParseChapter(s, h);
    Check(c3.paragraphs.size() == 1 && c3.paragraphs[0] == "正文一", "strip: .class removed");

    s.chapterStrip.clear();
    s.chapterContent = { "#missing", "#c" };
    Check(SiteParseChapter(s, h).ok, "content selectors tried in order");

    s.chapterContent = { "#missing" };
    Check(!SiteParseChapter(s, h).ok, "all content selectors miss -> not ok");
  }

  // 6. 目录加载：坏文件不影响好文件
  {
    const std::wstring dir = L"build/test_sites";
    EnsureDir(dir);
    WriteFileBytes(dir + L"/aaa.json",
      "{\"id\":\"aaa\",\"name\":\"A\",\"match\":{\"hosts\":[\"a.com\"]},"
      "\"chapter\":{\"content\":[\"#x\"]}}");
    WriteFileBytes(dir + L"/bbb.json", "{ this is not json");
    SiteRegistry r;
    r.Load(dir);
    Check(r.count() == 1, "one good file loaded");
    Check(!r.lastError().empty(), "broken file reported in lastError");
    const SiteAdapter* a = r.Match("https://a.com/book/1/index.html");
    Check(a != nullptr && a->id == "aaa", "hosts match");
    Check(r.Match("https://b.com/book/1/index.html") == nullptr, "other host -> no match");
    Check(r.Match("https://a.com/whatever") != nullptr, "empty pathContains -> path not restricted");
  }

  // 7. 校验：缺 id / 缺 match / 缺 content 都要拒绝
  {
    SiteAdapter tmp;
    Check(!ParseSiteAdapterJson("{ broken", tmp, &err), "reject: bad json");
    Check(!err.empty(), "reject: error message set");
    Check(!ParseSiteAdapterJson("{\"name\":\"x\",\"match\":{\"hosts\":[\"a\"]},\"chapter\":{\"content\":[\"#a\"]}}", tmp, &err),
          "reject: no id");
    Check(!ParseSiteAdapterJson("{\"id\":\"x\",\"chapter\":{\"content\":[\"#a\"]}}", tmp, &err),
          "reject: no match");
    Check(!ParseSiteAdapterJson("{\"id\":\"x\",\"match\":{\"hosts\":[\"a\"]}}", tmp, &err),
          "reject: no chapter.content");
    Check(ParseSiteAdapterJson("{\"id\":\"x\",\"match\":{\"pathContains\":[\"/b/\"]},\"chapter\":{\"content\":[\"#a\"]}}", tmp, &err),
          "accept: pathContains-only match");
  }

  // 8. 首次运行把内置规则落盘，方便直接改
  {
    const std::wstring dir = L"build/test_sites2";
    DeleteFileW((dir + L"/twkan.json").c_str());  // 让这条断言在重复跑时也成立
    DeleteFileW((dir + L"/69shuba.json").c_str());
    SiteRegistry r;
    Check(r.EnsureDefaultFiles(dir) == 2, "both default files written");
    Check(r.EnsureDefaultFiles(dir) == 0, "second call is a no-op");
    r.Load(dir);
    Check(r.count() == 2, "both rules loaded from disk");
    Check(r.Match("https://twkan.com/book/67581/index.html") != nullptr, "loaded rule matches twkan");
    Check(r.Match("https://www.69shuba.com/book/55199.htm") != nullptr, "loaded rule matches 69shuba");
  }

  // 9. 最要紧的一条：行为由 JSON 决定
  if (haveChap) {
    const std::wstring dir = L"build/test_sites3";
    EnsureDir(dir);
    WriteFileBytes(dir + L"/fake.json",
      "{\"id\":\"fake\",\"name\":\"Fake\",\"match\":{\"hosts\":[\"twkan.com\"]},"
      "\"urls\":{\"bookIdMarkers\":[\"/book/\"]},"
      "\"chapter\":{\"content\":[\"#definitely-not-here\"]}}");
    SiteRegistry r;
    r.Load(dir);
    const SiteAdapter* f = r.Match("https://twkan.com/book/67581/index.html");
    Check(f != nullptr && f->id == "fake", "custom json wins the match");
    Check(SiteParseChapter(b, chap).ok, "builtin rule parses the chapter");
    Check(!SiteParseChapter(*f, chap).ok, "swapped content selector breaks it -> rules come from json");
  }

  // 10. 69shuba.com：真样本目录页 / 章节页 / og 元信息
  {
    SiteRegistry r;
    r.Load(L"build/test_sites_none");
    const SiteAdapter* s = r.Match("https://www.69shuba.com/book/55199.htm");
    Check(s != nullptr && s->id == "69shuba", "69shuba: builtin rule matches");
    if (s) {
      Check(SiteBookId(*s, "https://www.69shuba.com/book/55199.htm") == "55199",
            "69shuba: bookId from /book/x.htm");
      Check(SiteBookId(*s, "https://www.69shuba.com/txt/55199/40940617") == "55199",
            "69shuba: bookId from /txt/");
      Check(SiteIndexUrl(*s, "https://www.69shuba.com/txt/55199/40940617") ==
            "https://www.69shuba.com/book/55199.htm", "69shuba: indexUrl -> 详情页");
      Check(SiteTocListUrl(*s, "https://www.69shuba.com/book/55199.htm") ==
            "https://www.69shuba.com/book/55199/", "69shuba: tocListUrl -> 完整目录页");
    }

    std::string idx, tocHtml;
    if (s && ReadFileBytes(L"testdata/shuba_book_index.html", idx) &&
        ReadFileBytes(L"testdata/shuba_book_toc.html", tocHtml)) {
      TocResult t = SiteParseToc(*s, tocHtml, "https://www.69shuba.com/book/55199.htm");
      printf("  (69shuba toc chapters=%zu)\n", t.chapters.size());
      Check(t.ok, "69shuba toc: ok");
      Check(t.chapters.size() > 900, "69shuba toc: chapters > 900");
      Check(!t.chapters.empty() &&
            t.chapters[0].url == "https://www.69shuba.com/txt/55199/35428550",
            "69shuba toc: first chapter url (书签那条没混进来)");
      std::string t930;
      bool bracket = false;
      for (const auto& e : t.chapters) {
        if (e.title.find("第930章") == 0) t930 = e.title;
        if (e.title.find("【") != std::string::npos) bracket = true;
      }
      Check(t930 == "第930章 宗师间的博弈", "69shuba toc: 【51k字】 已从标题里去掉");
      Check(!bracket, "69shuba toc: 标题里不再有 【…】 注释");
      std::string t229;
      for (const auto& e : t.chapters) {
        if (e.title.find("第229章") == 0) t229 = e.title;
      }
      Check(t229 == "第229章 强悍的新模板", "69shuba toc: 站点自己截断的 【… 残尾也去掉");
      Check(t.meta.bookId == "55199", "69shuba toc: bookId");
      Check(t.meta.title.empty(), "69shuba toc: 目录页不留书名，交给详情页");

      BookMeta m;
      Check(SiteParseBookMeta(*s, idx, m), "69shuba meta: 从详情页解析出元信息");
      Check(m.title == "圣域乐园", "69shuba meta: og:novel:book_name");
      Check(m.author == "墨中取炭", "69shuba meta: og:novel:author");
      Check(m.category == "官场职场", "69shuba meta: og:novel:category");
      Check(m.status == "连载", "69shuba meta: og:novel:status");
      Check(m.cover.find("55199s.jpg") != std::string::npos, "69shuba meta: og:image");
    } else {
      printf("  skip 69shuba index/toc samples (testdata not present)\n");
    }

    std::string chap;
    if (s && ReadFileBytes(L"testdata/shuba_chapter_sample.html", chap)) {
      Chapter c = SiteParseChapter(*s, chap);
      printf("  (69shuba chapter title=%s paras=%zu)\n", c.title.c_str(), c.paragraphs.size());
      Check(c.ok, "69shuba chapter: ok");
      Check(c.title == "第930章 宗师间的博弈", "69shuba chapter: 标题已清洗");
      Check(c.paragraphs.size() > 20, "69shuba chapter: paragraphs");
      bool noise = false, heading = false;
      for (const auto& p : c.paragraphs) {
        if (p.find("2025-11-27") != std::string::npos || p.find("作者：") != std::string::npos ||
            p.find("小贴士") != std::string::npos || p.find("上一章") != std::string::npos) {
          noise = true;
        }
        if (p.find("第930章") != std::string::npos) heading = true;
      }
      Check(!noise, "69shuba chapter: 日期/作者/导航/广告没混进正文");
      Check(!heading, "69shuba chapter: 正文开头重复的标题行已丢");
      Check(!c.paragraphs.empty() && c.paragraphs[0].find("渊雷") != std::string::npos,
            "69shuba chapter: 首段就是正文");
    } else {
      printf("  skip 69shuba chapter sample (testdata not present)\n");
    }
  }

  printf("PASS=%d FAIL=%d\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
