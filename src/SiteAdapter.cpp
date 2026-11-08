#include "SiteAdapter.h"
#include "Extract.h"
#include "Html.h"
#include "Json.h"
#include <algorithm>
#include <cctype>
#include <functional>

// ---------------------------------------------------------------------------
// 内置默认规则：编译进 exe。data/sites/*.json 缺失或写坏时用它兜底，
// 保证「规则文件改烂了」也不会把书打不开。
// ---------------------------------------------------------------------------
static const char* kBuiltinJson = R"SITEJSON(
{
  "id": "twkan",
  "name": "台灣小說網",
  "match": {
    "hosts": ["twkan.com"],
    "pathContains": ["/book/", "/txt/"]
  },
  "urls": {
    "bookIdMarkers": ["/book/", "/txt/"],
    "indexUrl": "{origin}/book/{bookId}/index.html",
    "tocListUrl": "{origin}/ajax_novels/chapterlist/{bookId}.html"
  },
  "toc": {
    "container": "#allchapter",
    "link": "a",
    "hrefMustContain": "/txt/",
    "wordsLabels": ["字數", "字数"],
    "updatedLabels": ["更新時間", "更新时间"]
  },
  "chapter": {
    "title": "h1",
    "content": ["#txtcontent0", "#chaptercontent", "#content", "#booktxt", "#nr1", "#text", "#chapterContent"],
    "strip": [".txtad", ".ad", ".ads"],
    "blockTags": ["p", "div"],
    "breakTags": ["br"],
    "dropLeadingHeading": true
  },
  "meta": {
    "lastReadCall": "lastread.set(",
    "lastReadFields": { "bookId": 0, "title": 2, "author": 4, "category": 5, "cover": 6, "status": 7 },
    "metaFromTitleTag": true
  },
  "challenge": {
    "bodyMarkers": ["_cf_chl_opt"],
    "titleMarkers": ["just a moment", "attention required", "请稍候", "正在验证", "verifying"]
  }
}
)SITEJSON";

// ---------------------------------------------------------------------------
// 内置规则之二：69shuba.com（69书吧）。目录在 /book/{id}/ 的 #catalog 里，
// 正文在 .txtnav 里（段落用 <br><br> 分隔），书名作者在详情页的 og:novel:* 上。
// ---------------------------------------------------------------------------
static const char* kBuiltin69Json = R"SITEJSON(
{
  "id": "69shuba",
  "name": "69书吧",
  "match": {
    "hosts": ["69shuba.com"],
    "pathContains": ["/book/", "/txt/"]
  },
  "urls": {
    "bookIdMarkers": ["/book/", "/txt/"],
    "indexUrl": "{origin}/book/{bookId}.htm",
    "tocListUrl": "{origin}/book/{bookId}/"
  },
  "toc": {
    "container": "#catalog",
    "link": "a",
    "hrefMustContain": "/txt/"
  },
  "chapter": {
    "title": "h1",
    "content": [".txtnav"],
    "strip": ["h1", ".txtinfo", "#txtright", ".contentadv", ".bottom-ad", ".bottom-ad2",
              "#ad-first-slot-pc", "#ad-second-slot-pc", "#ad-3-2-container", ".page1",
              ".baocuo", "#baocuo", "#tuijian", ".yuedutuijian"],
    "blockTags": ["p", "div"],
    "breakTags": ["br"],
    "dropLeadingHeading": true
  },
  "meta": {
    "metaTags": {
      "title": ["og:novel:book_name", "og:title"],
      "author": ["og:novel:author"],
      "category": ["og:novel:category"],
      "status": ["og:novel:status"],
      "cover": ["og:image"]
    },
    "metaFromTitleTag": false
  },
  "titleStripBrackets": ["【】"],
  "challenge": {
    "bodyMarkers": ["_cf_chl_opt"],
    "titleMarkers": ["just a moment", "attention required", "请稍候", "正在验证", "verifying"]
  }
}
)SITEJSON";

std::string BuiltinSiteJson() { return std::string(kBuiltinJson); }

// 内置规则表：文件名 -> 规则内容。首启动落盘，也是删文件后的兜底。
struct BuiltinRule {
  const char* file;
  const char* json;
};
static const BuiltinRule kBuiltins[] = {
  { "twkan.json", kBuiltinJson },
  { "69shuba.json", kBuiltin69Json },
};
static const int kBuiltinCount = (int)(sizeof(kBuiltins) / sizeof(kBuiltins[0]));

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
static bool InList(const std::vector<std::string>& v, const std::string& s) {
  for (const auto& x : v) {
    if (x == s) return true;
  }
  return false;
}

static bool ContainsAny(const std::vector<std::string>& v, const std::string& s) {
  for (const auto& x : v) {
    if (!x.empty() && s.find(x) != std::string::npos) return true;
  }
  return false;
}

static void ReadStrArray(const Json& o, const char* key, std::vector<std::string>& out) {
  const Json* v = o.Get(key);
  if (!v || !v->IsArr()) return;
  for (const auto& e : v->arr) {
    if (e.t == Json::T::Str && !e.str.empty()) out.push_back(e.str);
  }
}

static std::string ReadStr(const Json& o, const char* key, const std::string& def) {
  const Json* v = o.Get(key);
  return (v && v->t == Json::T::Str) ? v->str : def;
}

static bool ReadBool(const Json& o, const char* key, bool def) {
  const Json* v = o.Get(key);
  return (v && v->t == Json::T::Bool) ? v->b : def;
}

// ---------------------------------------------------------------------------
// 规则解析
// ---------------------------------------------------------------------------
bool ParseSiteAdapterJson(const std::string& text, SiteAdapter& out, std::string* err) {
  Json j;
  if (!Json::Parse(text, j) || !j.IsObj()) {
    if (err) *err = "JSON 语法错误";
    return false;
  }

  SiteAdapter a;
  a.id = ReadStr(j, "id", "");
  if (a.id.empty()) {
    if (err) *err = "缺少 id";
    return false;
  }
  a.name = ReadStr(j, "name", a.id);

  if (const Json* m = j.Get("match"); m && m->IsObj()) {
    ReadStrArray(*m, "hosts", a.hosts);
    ReadStrArray(*m, "pathContains", a.pathContains);
  }
  if (a.hosts.empty() && a.pathContains.empty()) {
    if (err) *err = "match 里至少要写 hosts 或 pathContains，否则没法判断该不该用这份规则";
    return false;
  }

  if (const Json* u = j.Get("urls"); u && u->IsObj()) {
    ReadStrArray(*u, "bookIdMarkers", a.bookIdMarkers);
    a.indexUrl = ReadStr(*u, "indexUrl", "");
    a.tocListUrl = ReadStr(*u, "tocListUrl", "");
  }
  if (a.bookIdMarkers.empty()) a.bookIdMarkers.push_back("/book/");

  if (const Json* t = j.Get("toc"); t && t->IsObj()) {
    a.tocContainer = ReadStr(*t, "container", "");
    a.tocLink = ReadStr(*t, "link", "a");
    a.tocHrefMustContain = ReadStr(*t, "hrefMustContain", "");
    ReadStrArray(*t, "wordsLabels", a.tocWordsLabels);
    ReadStrArray(*t, "updatedLabels", a.tocUpdatedLabels);
  }
  if (a.tocLink.empty()) a.tocLink = "a";

  if (const Json* c = j.Get("chapter"); c && c->IsObj()) {
    a.chapterTitle = ReadStr(*c, "title", "");
    ReadStrArray(*c, "content", a.chapterContent);
    ReadStrArray(*c, "strip", a.chapterStrip);
    ReadStrArray(*c, "blockTags", a.chapterBlockTags);
    ReadStrArray(*c, "breakTags", a.chapterBreakTags);
    a.dropLeadingHeading = ReadBool(*c, "dropLeadingHeading", true);
  }
  if (a.chapterContent.empty()) {
    if (err) *err = "chapter.content 为空，找不到正文容器";
    return false;
  }
  if (a.chapterBlockTags.empty()) {
    a.chapterBlockTags.push_back("p");
    a.chapterBlockTags.push_back("div");
  }
  if (a.chapterBreakTags.empty()) a.chapterBreakTags.push_back("br");

  if (const Json* m = j.Get("meta"); m && m->IsObj()) {
    a.lastReadCall = ReadStr(*m, "lastReadCall", "");
    a.metaFromTitleTag = ReadBool(*m, "metaFromTitleTag", true);
    if (const Json* f = m->Get("lastReadFields"); f && f->IsObj()) {
      for (const auto& kv : f->obj) {
        if (kv.second.t != Json::T::Num) continue;
        a.lastReadFields.emplace_back(kv.first, (int)kv.second.num);
      }
    }
    // metaTags: { "title": ["og:novel:book_name", "og:title"], ... }
    if (const Json* mt = m->Get("metaTags"); mt && mt->IsObj()) {
      for (const auto& kv : mt->obj) {
        if (!kv.second.IsArr()) continue;
        for (const auto& e : kv.second.arr) {
          if (e.t == Json::T::Str && !e.str.empty()) a.metaTags.emplace_back(kv.first, e.str);
        }
      }
    }
  }
  if (a.lastReadFields.empty()) {
    a.lastReadFields = { {"bookId", 0}, {"title", 2}, {"author", 4},
                         {"category", 5}, {"cover", 6}, {"status", 7} };
  }

  if (const Json* c = j.Get("challenge"); c && c->IsObj()) {
    ReadStrArray(*c, "bodyMarkers", a.challengeBody);
    ReadStrArray(*c, "titleMarkers", a.challengeTitle);
  }
  if (a.challengeBody.empty()) a.challengeBody.push_back("_cf_chl_opt");
  if (a.challengeTitle.empty()) {
    a.challengeTitle = { "just a moment", "attention required", "请稍候", "正在验证", "verifying" };
  }

  ReadStrArray(j, "titleStripBrackets", a.titleStripBrackets);

  out = std::move(a);
  return true;
}

// ---------------------------------------------------------------------------
// 选择器子集
// ---------------------------------------------------------------------------
struct Sel {
  std::string tag, id, cls;
  bool ok = false;
};

static Sel ParseSel(const std::string& raw) {
  Sel s;
  const std::string str = TrimAscii(raw);
  if (str.empty()) return s;
  size_t p = 0;
  while (p < str.size() && str[p] != '#' && str[p] != '.') ++p;
  s.tag = ToLowerAscii(str.substr(0, p));
  while (p < str.size()) {
    const char kind = str[p++];
    size_t e = p;
    while (e < str.size() && str[e] != '#' && str[e] != '.') ++e;
    const std::string v = str.substr(p, e - p);
    if (v.empty()) return s;   // 形如 "#" / "a." 的写法当无效
    if (kind == '#') s.id = v;
    else s.cls = v;
    p = e;
  }
  if (s.tag.empty() && s.id.empty() && s.cls.empty()) return s;
  s.ok = true;
  return s;
}

static bool NodeMatches(const HtmlNode* n, const Sel& s) {
  if (!n || n->IsText()) return false;
  if (!s.tag.empty() && n->tag != s.tag) return false;
  if (!s.id.empty() && n->Id() != s.id) return false;
  if (!s.cls.empty() && !n->HasClass(s.cls)) return false;
  return true;
}

static void CollectTag(const HtmlNode* n, const std::string& tag, std::vector<const HtmlNode*>& out) {
  if (n->tag == tag) out.push_back(n);
  for (const auto& c : n->children) CollectTag(c.get(), tag, out);
}

static void CollectElems(const HtmlNode* n, std::vector<const HtmlNode*>& out) {
  if (!n->IsText()) out.push_back(n);
  for (const auto& c : n->children) CollectElems(c.get(), out);
}

static const HtmlNode* FindFirstSel(const HtmlDoc& doc, const std::string& sel) {
  const Sel s = ParseSel(sel);
  if (!s.ok || !doc.root) return nullptr;
  std::vector<const HtmlNode*> all;
  if (!s.tag.empty()) CollectTag(doc.root.get(), s.tag, all);
  else CollectElems(doc.root.get(), all);
  for (const HtmlNode* n : all) {
    if (NodeMatches(n, s)) return n;
  }
  return nullptr;
}

static bool MatchAnySel(const std::vector<std::string>& sels, const HtmlNode* n) {
  for (const auto& raw : sels) {
    const Sel s = ParseSel(raw);
    if (s.ok && NodeMatches(n, s)) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// URL
// ---------------------------------------------------------------------------
std::string SiteHost(const std::string& url) {
  const size_t p = url.find("://");
  const std::string rest = (p == std::string::npos) ? url : url.substr(p + 3);
  const size_t e = rest.find_first_of("/?#");
  std::string host = (e == std::string::npos) ? rest : rest.substr(0, e);
  const size_t at = host.rfind('@');
  if (at != std::string::npos) host = host.substr(at + 1);
  const size_t colon = host.find(':');
  if (colon != std::string::npos) host = host.substr(0, colon);
  return ToLowerAscii(host);
}

std::string SiteOrigin(const std::string& url) {
  const size_t s = url.find("://");
  if (s == std::string::npos) return "";
  const size_t h = url.find('/', s + 3);
  return (h == std::string::npos) ? url : url.substr(0, h);
}

std::string SiteBookId(const SiteAdapter& a, const std::string& url) {
  for (const auto& marker : a.bookIdMarkers) {
    if (marker.empty()) continue;
    const size_t pos = url.find(marker);
    if (pos == std::string::npos) continue;
    size_t s = pos + marker.size();
    size_t e = s;
    while (e < url.size() && isdigit((unsigned char)url[e])) ++e;
    if (e > s) return url.substr(s, e - s);
  }
  return "";
}

static std::string FillTemplate(const std::string& tpl, const std::string& origin,
                                const std::string& bookId) {
  std::string out;
  size_t i = 0;
  while (i < tpl.size()) {
    if (tpl[i] == '{') {
      const size_t e = tpl.find('}', i);
      if (e != std::string::npos) {
        const std::string key = tpl.substr(i + 1, e - i - 1);
        if (key == "origin") out += origin;
        else if (key == "bookId") out += bookId;
        else out += tpl.substr(i, e - i + 1);
        i = e + 1;
        continue;
      }
    }
    out.push_back(tpl[i++]);
  }
  return out;
}

std::string SiteTocListUrl(const SiteAdapter& a, const std::string& indexUrl) {
  if (a.tocListUrl.empty()) return "";
  const std::string origin = SiteOrigin(indexUrl);
  const std::string id = SiteBookId(a, indexUrl);
  if (origin.empty() || id.empty()) return "";
  return FillTemplate(a.tocListUrl, origin, id);
}

std::string SiteIndexUrl(const SiteAdapter& a, const std::string& url) {
  if (a.indexUrl.empty()) return "";
  const std::string origin = SiteOrigin(url);
  const std::string id = SiteBookId(a, url);
  if (origin.empty() || id.empty()) return "";
  return FillTemplate(a.indexUrl, origin, id);
}

// ---------------------------------------------------------------------------
// 注册表
// ---------------------------------------------------------------------------
void SiteRegistry::Load(const std::wstring& dir) {
  list_.clear();
  err_.clear();

  builtins_.clear();
  for (int i = 0; i < kBuiltinCount; ++i) {
    SiteAdapter a;
    std::string be;
    if (ParseSiteAdapterJson(kBuiltins[i].json, a, &be)) builtins_.push_back(std::move(a));
    else err_ += std::string(kBuiltins[i].file) + "：内置规则写坏了（程序 bug）：" + be + "\n";
  }
  if (builtins_.empty()) {
    // 内置 JSON 全写错属于程序 bug，给一份最小可用规则，别让整本书打不开
    SiteAdapter b;
    b.id = "twkan";
    b.name = "台灣小說網";
    b.hosts.push_back("twkan.com");
    b.pathContains.push_back("/book/");
    b.pathContains.push_back("/txt/");
    b.bookIdMarkers.push_back("/book/");
    b.bookIdMarkers.push_back("/txt/");
    b.indexUrl = "{origin}/book/{bookId}/index.html";
    b.tocListUrl = "{origin}/ajax_novels/chapterlist/{bookId}.html";
    b.tocContainer = "#allchapter";
    b.tocLink = "a";
    b.tocHrefMustContain = "/txt/";
    b.chapterTitle = "h1";
    b.chapterContent.push_back("#txtcontent0");
    b.chapterBlockTags.push_back("p");
    b.chapterBlockTags.push_back("div");
    b.chapterBreakTags.push_back("br");
    b.lastReadCall = "lastread.set(";
    b.lastReadFields = { {"bookId", 0}, {"title", 2}, {"author", 4},
                         {"category", 5}, {"cover", 6}, {"status", 7} };
    b.challengeBody.push_back("_cf_chl_opt");
    builtins_.push_back(std::move(b));
  }

  if (dir.empty()) return;

  const std::wstring pat = dir + L"/*.json";
  WIN32_FIND_DATAW fd = {};
  HANDLE h = FindFirstFileW(pat.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    std::string text;
    if (!ReadFileBytes(dir + L"/" + fd.cFileName, text)) continue;
    SiteAdapter a;
    std::string e;
    if (ParseSiteAdapterJson(text, a, &e)) list_.push_back(std::move(a));
    else err_ += W2U(fd.cFileName) + "：" + e + "\n";
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

int SiteRegistry::EnsureDefaultFiles(const std::wstring& dir) const {
  if (dir.empty()) return 0;
  EnsureDir(dir);
  int written = 0;
  for (int i = 0; i < kBuiltinCount; ++i) {
    const std::wstring path = dir + L"/" + U2W(kBuiltins[i].file);
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
    if (WriteFileBytes(path, std::string(kBuiltins[i].json) + "\n")) ++written;
  }
  return written;
}

static bool HostMatches(const std::vector<std::string>& hosts, const std::string& host) {
  if (hosts.empty()) return true;
  if (host.empty()) return false;
  for (const auto& raw : hosts) {
    std::string h = ToLowerAscii(TrimAscii(raw));
    if (h.empty()) continue;
    if (h[0] == '.') h = h.substr(1);
    if (host == h) return true;
    if (host.size() > h.size() &&
        host.compare(host.size() - h.size(), h.size(), h) == 0 &&
        host[host.size() - h.size() - 1] == '.') return true;
  }
  return false;
}

static bool AdapterMatchesUrl(const SiteAdapter& a, const std::string& url,
                              const std::string& host) {
  if (!HostMatches(a.hosts, host)) return false;
  if (!a.pathContains.empty() && !ContainsAny(a.pathContains, url)) return false;
  return true;
}

const SiteAdapter* SiteRegistry::Match(const std::string& url) const {
  const std::string host = SiteHost(url);
  // 磁盘上的规则优先，其次才是内置规则：用户想覆盖某个站，写同名 host 的文件就行
  for (const auto& a : list_) {
    if (AdapterMatchesUrl(a, url, host)) return &a;
  }
  for (const auto& a : builtins_) {
    if (AdapterMatchesUrl(a, url, host)) return &a;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// 解析
// ---------------------------------------------------------------------------
bool SiteLooksLikeChallenge(const SiteAdapter& a, const std::string& html) {
  if (ContainsAny(a.challengeBody, html)) return true;
  const size_t t = html.find("<title>");
  if (t != std::string::npos) {
    const size_t e = html.find("</title>", t);
    if (e != std::string::npos) {
      const std::string title = ToLowerAscii(HtmlDecode(html.substr(t + 7, e - t - 7)));
      for (const auto& m : a.challengeTitle) {
        if (!m.empty() && title.find(ToLowerAscii(m)) != std::string::npos) return true;
      }
    }
  }
  return false;
}

// 从 title 属性里取「字數: 1234」「更新時間: 2024-01-01」这类字段
static std::string AttrTitleField(const std::string& titleAttr,
                                  const std::vector<std::string>& labels, bool toEnd) {
  const std::string t = HtmlDecode(titleAttr);
  for (const auto& label : labels) {
    if (label.empty()) continue;
    size_t p = t.find(label);
    if (p == std::string::npos) continue;
    p += label.size();
    while (p < t.size() && (t[p] == ':' || t[p] == ' ' || (unsigned char)t[p] >= 0x80)) ++p;
    size_t e = p;
    if (toEnd) {
      e = t.size();
    } else {
      while (e < t.size() && t[e] != ' ' && t[e] != '\t') ++e;
    }
    std::string v = TrimAscii(t.substr(p, e - p));
    if (toEnd && v.size() > 16) v = TrimAscii(v.substr(0, 16));
    if (!v.empty()) return v;
  }
  return "";
}

// 删掉标题里的括号注释，如 "【】" 表示把 【…】 整段去掉。
// 只作用于标题，不动正文（正文里的【提示：…】要原样留着）。
static std::string StripBracketNotes(const std::string& s,
                                     const std::vector<std::string>& pairs) {
  if (s.empty() || pairs.empty()) return s;
  std::string out = s;
  for (const auto& p : pairs) {
    if (p.empty()) continue;
    size_t olen = 1;
    const unsigned char c0 = (unsigned char)p[0];
    if ((c0 & 0xE0) == 0xC0) olen = 2;
    else if ((c0 & 0xF0) == 0xE0) olen = 3;
    else if ((c0 & 0xF8) == 0xF0) olen = 4;
    if (p.size() < olen) continue;
    size_t last = p.size() - 1;
    while (last > 0 && ((unsigned char)p[last] & 0xC0) == 0x80) --last;
    if (last < olen) continue;
    const std::string open = p.substr(0, olen);
    const std::string close = p.substr(last);
    size_t pos = 0;
    while ((pos = out.find(open, pos)) != std::string::npos) {
      const size_t end = out.find(close, pos + open.size());
      if (end == std::string::npos) {
        out.erase(pos);   // 站点自己把标题截断成没闭合的 【…，残尾一并去掉
        break;
      }
      out.erase(pos, end + close.size() - pos);
    }
  }
  const std::string r = TrimAscii(out);
  return r.empty() ? s : r;   // 整条标题就是一条注释时别删没了
}

// 从一段标签文本里取属性值，如 AttrValue("<meta property=\"og:title\" content=\"x\">", "content")
static std::string AttrValue(const std::string& tag, const std::string& name) {
  const std::string low = ToLowerAscii(tag);
  const std::string key = name + "=";
  size_t p = 0;
  while ((p = low.find(key, p)) != std::string::npos) {
    const bool boundary = (p == 0) || isspace((unsigned char)low[p - 1]);
    const size_t v = p + key.size();
    if (!boundary || v >= tag.size()) { p += key.size(); continue; }
    const char q = tag[v];
    if (q == '"' || q == '\'') {
      const size_t e = tag.find(q, v + 1);
      if (e == std::string::npos) return "";
      return tag.substr(v + 1, e - v - 1);
    }
    size_t e = v;
    while (e < tag.size() && !isspace((unsigned char)tag[e]) && tag[e] != '>') ++e;
    return tag.substr(v, e - v);
  }
  return "";
}

// 取 <meta property="key" content="…"> 或 <meta name="key" content="…"> 的 content
static std::string MetaTagContent(const std::string& html, const std::string& key) {
  const std::string want = ToLowerAscii(key);
  size_t pos = 0;
  while ((pos = html.find("<meta", pos)) != std::string::npos) {
    const size_t end = html.find('>', pos);
    if (end == std::string::npos) break;
    const std::string tag = html.substr(pos, end - pos);
    pos = end + 1;
    if (ToLowerAscii(AttrValue(tag, "property")) != want &&
        ToLowerAscii(AttrValue(tag, "name")) != want) continue;
    const std::string v = TrimAscii(HtmlDecode(AttrValue(tag, "content")));
    if (!v.empty()) return v;
  }
  return "";
}

// 元信息：先按规则里的 metaTags 取 <meta>，再退回 <title>。只填空字段，不覆盖已有值。
static void FillMetaFromHtml(const SiteAdapter& a, const std::string& html, BookMeta& out) {
  for (const auto& kv : a.metaTags) {
    const std::string& field = kv.first;
    std::string* dst = nullptr;
    if (field == "title") dst = &out.title;
    else if (field == "author") dst = &out.author;
    else if (field == "cover") dst = &out.cover;
    else if (field == "category") dst = &out.category;
    else if (field == "status") dst = &out.status;
    else if (field == "bookId") dst = &out.bookId;
    else continue;
    if (!dst->empty()) continue;
    const std::string v = MetaTagContent(html, kv.second);
    if (v.empty()) continue;
    *dst = (field == "cover") ? v : CleanText(v);
  }

  if (!a.metaFromTitleTag || !out.title.empty()) return;
  const size_t t = html.find("<title>");
  if (t == std::string::npos) return;
  const size_t e = html.find("</title>", t);
  if (e == std::string::npos) return;
  std::string title = TrimAscii(HtmlDecode(html.substr(t + 7, e - t - 7)));
  const size_t comma = title.find_first_of(",，");
  if (comma != std::string::npos) title = title.substr(0, comma);
  title = CleanText(title);
  if (!title.empty()) out.title = title;
}

TocResult SiteParseToc(const SiteAdapter& a, const std::string& html, const std::string& baseUrl) {
  TocResult r;
  HtmlDoc doc = HtmlDoc::Parse(html);

  const HtmlNode* scope = a.tocContainer.empty() ? nullptr : FindFirstSel(doc, a.tocContainer);
  std::vector<const HtmlNode*> anchors;
  CollectTag(scope ? scope : doc.root.get(), a.tocLink, anchors);

  std::vector<std::string> seen;
  for (const HtmlNode* n : anchors) {
    const std::string* href = n->Attr("href");
    if (!href) continue;
    const std::string url = NormalizeUrl(baseUrl, *href);
    if (url.empty()) continue;
    if (!a.tocHrefMustContain.empty() && url.find(a.tocHrefMustContain) == std::string::npos) continue;
    if (std::find(seen.begin(), seen.end(), url) != seen.end()) continue;
    const std::string title =
      StripBracketNotes(CleanText(HtmlDecode(NodeText(n, false))), a.titleStripBrackets);
    if (title.empty()) continue;
    seen.push_back(url);

    TocEntry e;
    e.title = title;
    e.url = url;
    if (const std::string* ta = n->Attr("title")) {
      e.words = AttrTitleField(*ta, a.tocWordsLabels, false);
      e.updated = AttrTitleField(*ta, a.tocUpdatedLabels, true);
    }
    r.chapters.push_back(std::move(e));
  }

  if (r.chapters.empty()) {
    r.error = "no chapters found";
    return r;
  }

  r.meta.bookId = SiteBookId(a, baseUrl);
  FillMetaFromHtml(a, html, r.meta);

  r.ok = true;
  return r;
}

static std::vector<std::string> QuotedStrings(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '"') {
      const size_t e = s.find('"', i + 1);
      if (e == std::string::npos) break;
      out.push_back(s.substr(i + 1, e - i - 1));
      i = e + 1;
      continue;
    }
    ++i;
  }
  return out;
}

bool SiteParseLastRead(const SiteAdapter& a, const std::string& html, BookMeta& out) {
  if (a.lastReadCall.empty()) return false;
  const size_t p = html.find(a.lastReadCall);
  if (p == std::string::npos) return false;
  const size_t e = html.find(')', p);
  if (e == std::string::npos) return false;
  const std::vector<std::string> vals = QuotedStrings(html.substr(p, e - p));

  bool any = false;
  for (const auto& f : a.lastReadFields) {
    if (f.second < 0 || f.second >= (int)vals.size()) continue;
    const std::string& raw = vals[(size_t)f.second];
    if (f.first == "cover") out.cover = TrimAscii(raw);
    else if (f.first == "bookId") out.bookId = CleanText(raw);
    else if (f.first == "title") out.title = CleanText(raw);
    else if (f.first == "author") out.author = CleanText(raw);
    else if (f.first == "category") out.category = CleanText(raw);
    else if (f.first == "status") out.status = CleanText(raw);
    else continue;
    any = true;
  }
  return any;
}

bool SiteParseBookMeta(const SiteAdapter& a, const std::string& html, BookMeta& out) {
  bool any = false;
  BookMeta lr;
  if (SiteParseLastRead(a, html, lr)) {
    if (out.title.empty() && !lr.title.empty()) { out.title = lr.title; any = true; }
    if (out.author.empty() && !lr.author.empty()) { out.author = lr.author; any = true; }
    if (out.cover.empty() && !lr.cover.empty()) { out.cover = lr.cover; any = true; }
    if (out.category.empty() && !lr.category.empty()) { out.category = lr.category; any = true; }
    if (out.status.empty() && !lr.status.empty()) { out.status = lr.status; any = true; }
  }
  const BookMeta before = out;
  FillMetaFromHtml(a, html, out);
  if (out.title != before.title || out.author != before.author || out.cover != before.cover ||
      out.category != before.category || out.status != before.status) any = true;
  return any;
}

Chapter SiteParseChapter(const SiteAdapter& a, const std::string& html) {
  Chapter ch;
  HtmlDoc doc = HtmlDoc::Parse(html);

  if (!a.chapterTitle.empty()) {
    if (const HtmlNode* h = FindFirstSel(doc, a.chapterTitle)) {
      ch.title =
        StripBracketNotes(CleanText(HtmlDecode(NodeText(h, false))), a.titleStripBrackets);
    }
  }

  const HtmlNode* content = nullptr;
  for (const auto& sel : a.chapterContent) {
    content = FindFirstSel(doc, sel);
    if (content) break;
  }
  if (!content) {
    ch.error = "content container not found";
    return ch;
  }

  std::string raw;
  std::function<void(const HtmlNode*)> rec = [&](const HtmlNode* n) {
    if (n->IsText()) { raw += n->text; return; }
    if (n->tag == "script" || n->tag == "style") return;
    if (MatchAnySel(a.chapterStrip, n)) return;
    if (InList(a.chapterBreakTags, n->tag)) { raw.push_back('\n'); return; }
    const bool block = InList(a.chapterBlockTags, n->tag);
    if (block && !raw.empty() && raw.back() != '\n') raw.push_back('\n');
    for (const auto& c : n->children) rec(c.get());
    if (block && !raw.empty() && raw.back() != '\n') raw.push_back('\n');
  };
  for (const auto& c : content->children) rec(c.get());

  const std::string text = HtmlDecode(raw);
  for (auto& line : SplitLines(text)) {
    std::string cleaned = CleanText(line);
    if (cleaned.empty() || !HasContent(cleaned) || IsWatermarkLine(cleaned)) continue;
    ch.paragraphs.push_back(std::move(cleaned));
  }

  if (a.dropLeadingHeading && !ch.paragraphs.empty()) {
    const std::string& first = ch.paragraphs.front();
    const bool looksHeading = first.find("章") != std::string::npos && first.size() < 80 &&
                              (first.find("第") == 0 || first.find("第") == 1);
    if (looksHeading) ch.paragraphs.erase(ch.paragraphs.begin());
  }

  if (ch.paragraphs.empty()) {
    ch.error = "empty content";
    return ch;
  }
  ch.ok = true;
  return ch;
}
