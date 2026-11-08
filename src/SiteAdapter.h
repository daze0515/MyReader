#pragma once
#include "ExtractTypes.h"

// 站点适配器：把「怎么认这个站、怎么找目录、怎么找正文」写成规则，
// 规则来自 data/sites/*.json，改规则不用重新编译。
//
// 选择器只支持一个很小的子集（够用且好读）：
//   #id          按 id
//   tag          按标签名，如 a / h1
//   .class       按 class
//   tag.class    标签名 + class
//   tag#id       标签名 + id
// 不支持后代组合（"#a b"）、属性选择器（"[x=y]"）。
struct SiteAdapter {
  std::string id;
  std::string name;

  // ---- 认站 ----
  std::vector<std::string> hosts;         // host 相同或为其子域；空 = 不按 host 限制
  std::vector<std::string> pathContains;  // URL 含其中任意一段；空 = 不按路径限制

  // ---- URL 规则 ----
  std::vector<std::string> bookIdMarkers; // 从 URL 取书号：先找标记，再取紧跟的连续数字
  std::string indexUrl;                   // 目录页模板，占位符 {origin} {bookId}
  std::string tocListUrl;                 // 章节列表 AJAX 模板，同上；空 = 不额外请求

  // ---- 目录页 ----
  std::string tocContainer;               // 目录容器选择器；空 = 全文档找
  std::string tocLink;                    // 目录里的链接标签
  std::string tocHrefMustContain;         // 链接规范化后必须包含；空 = 不过滤
  std::vector<std::string> tocWordsLabels;
  std::vector<std::string> tocUpdatedLabels;

  // ---- 章节页 ----
  std::string chapterTitle;
  std::vector<std::string> chapterContent;  // 正文容器选择器，按顺序试
  std::vector<std::string> chapterStrip;    // 要整块丢掉的元素选择器
  std::vector<std::string> chapterBlockTags;
  std::vector<std::string> chapterBreakTags;
  bool dropLeadingHeading = true;           // 正文首段若是章节标题就丢掉

  // ---- 元信息 ----
  std::string lastReadCall;                 // 页面里的脚本调用前缀
  std::vector<std::pair<std::string, int>> lastReadFields;  // 字段名 -> 第几个字符串参数
  // 字段名 -> <meta> 的 property / name（按顺序试，先命中的先用），如 "title" -> "og:title"
  std::vector<std::pair<std::string, std::string>> metaTags;
  bool metaFromTitleTag = true;

  // ---- 标题清洗 ----
  // 成对括号，如 "【】"：把标题里的 【…】 整段删掉（"第930章 引雷【51k字】" -> "第930章 引雷"）
  std::vector<std::string> titleStripBrackets;

  // ---- 人机验证 ----
  std::vector<std::string> challengeBody;
  std::vector<std::string> challengeTitle;
};

class SiteRegistry {
 public:
  // 扫描 dir 下的 *.json。任何单个文件写坏只记进 lastError，不影响其它站点。
  // 内置规则（twkan / 69shuba）始终参与匹配，只是优先级低于磁盘上的文件；
  // 这样把 data/sites/ 清空也照样能打开这两个站。
  void Load(const std::wstring& dir);
  const SiteAdapter* Match(const std::string& url) const;
  const SiteAdapter& Fallback() const { return builtins_[0]; }
  int count() const { return (int)list_.size(); }
  const std::string& lastError() const { return err_; }
  // 把内置规则里磁盘上还没有的那几份落盘，方便直接改
  int EnsureDefaultFiles(const std::wstring& dir) const;

 private:
  std::vector<SiteAdapter> list_;
  std::vector<SiteAdapter> builtins_;   // [0] 即兜底规则
  std::string err_;
};

bool ParseSiteAdapterJson(const std::string& text, SiteAdapter& out, std::string* err);
std::string BuiltinSiteJson();

std::string SiteHost(const std::string& url);
std::string SiteOrigin(const std::string& url);
std::string SiteBookId(const SiteAdapter& a, const std::string& url);
std::string SiteTocListUrl(const SiteAdapter& a, const std::string& indexUrl);
std::string SiteIndexUrl(const SiteAdapter& a, const std::string& url);
bool SiteLooksLikeChallenge(const SiteAdapter& a, const std::string& html);
TocResult SiteParseToc(const SiteAdapter& a, const std::string& html, const std::string& baseUrl);
Chapter SiteParseChapter(const SiteAdapter& a, const std::string& html);
bool SiteParseLastRead(const SiteAdapter& a, const std::string& html, BookMeta& out);
bool SiteParseBookMeta(const SiteAdapter& a, const std::string& html, BookMeta& out);
