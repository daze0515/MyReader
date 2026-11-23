#include "EpubBook.h"
#include "Html.h"
#include "TxtBook.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace {

std::string LocalName(const std::string& tag) {
  const size_t c = tag.rfind(':');
  return c == std::string::npos ? tag : tag.substr(c + 1);
}

void WalkNodes(const HtmlNode* n, const std::function<void(const HtmlNode*)>& fn) {
  fn(n);
  for (const auto& c : n->children) WalkNodes(c.get(), fn);
}

// 按本地名找元素（忽略 dc: / opf: 之类的命名空间前缀）
std::vector<const HtmlNode*> FindAll(const HtmlNode* root, const char* local) {
  std::vector<const HtmlNode*> out;
  if (!root) return out;
  WalkNodes(root, [&](const HtmlNode* n) {
    if (!n->IsText() && LocalName(n->tag) == local) out.push_back(n);
  });
  return out;
}

const HtmlNode* FindFirst(const HtmlNode* root, const char* local) {
  if (!root) return nullptr;
  const HtmlNode* found = nullptr;
  WalkNodes(root, [&](const HtmlNode* n) {
    if (!found && !n->IsText() && LocalName(n->tag) == local) found = n;
  });
  return found;
}

const HtmlNode* FirstChild(const HtmlNode* n, const char* local) {
  if (!n) return nullptr;
  for (const auto& c : n->children) {
    if (!c->IsText() && LocalName(c->tag) == local) return c.get();
  }
  return nullptr;
}

std::string AttrOf(const HtmlNode* n, const char* name) {
  if (!n) return "";
  const std::string* v = n->Attr(name);
  return v ? HtmlDecode(*v) : std::string();
}

bool HasToken(const std::string& list, const std::string& tok) {
  size_t i = 0;
  while (i < list.size()) {
    while (i < list.size() && isspace((unsigned char)list[i])) ++i;
    size_t j = i;
    while (j < list.size() && !isspace((unsigned char)list[j])) ++j;
    if (j > i && list.compare(i, j - i, tok) == 0) return true;
    i = j;
  }
  return false;
}

// 折叠空白：源码里排版用的换行缩进合成一个空格；但换行两边是汉字时直接去掉，
// 否则「阳光\n洒进」会变成「阳光 洒进」。
std::string CollapseWs(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = (unsigned char)s[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
      bool newline = false;
      size_t j = i;
      while (j < s.size()) {
        const unsigned char d = (unsigned char)s[j];
        if (d == '\n' || d == '\r') { newline = true; ++j; continue; }
        if (d == ' ' || d == '\t' || d == '\f') { ++j; continue; }
        break;
      }
      const bool prevWide = !out.empty() && (unsigned char)out.back() >= 0x80;
      const bool nextWide = j < s.size() && (unsigned char)s[j] >= 0x80;
      if (!out.empty() && j < s.size() && !(newline && (prevWide || nextWide))) out.push_back(' ');
      i = j;
      continue;
    }
    out.push_back((char)c);
    ++i;
  }
  return out;
}

std::string CleanXmlText(const std::string& raw) {
  return TrimUtf8(CollapseWs(HtmlDecode(raw)));
}

std::string NodeTextClean(const HtmlNode* n) {
  return n ? CleanXmlText(NodeText(n, false)) : std::string();
}

// 比较标题用：去掉所有空白（含全角空格），「第一章　开始」与「第一章 开始」算同一个
std::string Squash(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    Utf8Decoded d = DecodeUtf8(s, i);
    i += d.bytes ? d.bytes : 1;
    if (d.cp == ' ' || d.cp == '\t' || d.cp == '\n' || d.cp == '\r' || d.cp == 0x3000 || d.cp == 0x00A0) continue;
    AppendUtf8(out, d.cp);
  }
  return out;
}

int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string PercentDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && HexVal(s[i + 1]) >= 0 && HexVal(s[i + 2]) >= 0) {
      out.push_back((char)(HexVal(s[i + 1]) * 16 + HexVal(s[i + 2])));
      i += 2;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::string DirOf(const std::string& path) {
  const size_t p = path.rfind('/');
  return p == std::string::npos ? std::string() : path.substr(0, p + 1);
}

std::string FragmentOf(const std::string& href) {
  const size_t h = href.find('#');
  return h == std::string::npos ? std::string() : PercentDecode(href.substr(h + 1));
}

std::string BaseNameNoExt(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
  return TrimAscii(W2U(name));
}

// HtmlDoc 把 title / script / style 当原始文本标签，遇到 XHTML 里合法的 <title/>
// 会一直找 </title>，把整个正文吞掉。解析前先把它们展开成 <title></title>。
std::string FixSelfClosingRawTags(const std::string& s) {
  static const char* kTags[] = { "title", "script", "style", "textarea" };
  const std::string lower = ToLowerAscii(s);
  std::string out;
  out.reserve(s.size() + 32);
  size_t i = 0;
  while (i < s.size()) {
    const size_t lt = s.find('<', i);
    if (lt == std::string::npos) { out.append(s, i, std::string::npos); break; }
    out.append(s, i, lt - i);
    const char* hit = nullptr;
    for (const char* t : kTags) {
      const size_t n = strlen(t);
      if (lower.compare(lt + 1, n, t) != 0) continue;
      const char c = (lt + 1 + n < s.size()) ? s[lt + 1 + n] : '\0';
      if (c == ' ' || c == '/' || c == '>' || c == '\t' || c == '\n' || c == '\r') { hit = t; break; }
    }
    const size_t gt = hit ? s.find('>', lt) : std::string::npos;
    if (hit && gt != std::string::npos && gt > lt + 1 && s[gt - 1] == '/') {
      out.append(s, lt, gt - 1 - lt);
      out += "></";
      out += hit;
      out += ">";
      i = gt + 1;
    } else {
      out.push_back('<');
      i = lt + 1;
    }
  }
  return out;
}

// EPUB 规定 UTF-8 / UTF-16，但国内不少 EPUB 是 GBK：沿用 TXT 那套编码识别
bool DecodeXml(const std::string& bytes, std::string& out) {
  out.clear();
  if (bytes.empty()) return true;
  const std::string enc = TxtBook::DetectEncoding(bytes);
  if (TxtBook::DecodeToUtf8(bytes, enc, out)) return true;
  out = bytes;   // 解不出来就原样当 UTF-8 用，最多几个乱码，不至于整章打不开
  return true;
}

bool IsBlockTag(const std::string& t) {
  static const std::set<std::string> k = {
    "p", "div", "li", "ul", "ol", "blockquote", "pre", "section", "article", "header", "footer",
    "aside", "main", "nav", "figure", "figcaption", "table", "thead", "tbody", "tr", "td", "th",
    "caption", "dl", "dt", "dd", "address", "center", "hr", "body"
  };
  return k.count(t) > 0;
}

bool IsHeadingTag(const std::string& t) {
  return t.size() == 2 && t[0] == 'h' && t[1] >= '1' && t[1] <= '6';
}

// 整块跳过：头部、脚本样式、注音（<rt> 里是拼音/假名）、公式和多媒体。SVG 单独处理（只取里面的 <image>）
bool IsSkipTag(const std::string& t) {
  static const std::set<std::string> k = {
    "head", "title", "script", "style", "noscript", "math", "rt", "rp",
    "object", "iframe", "audio", "video"
  };
  return k.count(t) > 0;
}

// 段落缓冲里代表「这里有一张图」的占位字符
constexpr char kImgSlot = '\x02';

// alt 只有一两个字（不算空白）时，当成生僻字图片的替代文字
bool IsInlineAlt(const std::string& decoded) {
  const std::string t = TrimUtf8(decoded);
  if (t.empty()) return false;
  int n = 0;
  for (size_t i = 0; i < t.size();) {
    Utf8Decoded d = DecodeUtf8(t, i);
    i += d.bytes ? d.bytes : 1;
    ++n;
  }
  return n <= 2;
}

struct SegBuilder {
  struct Img { std::string path, alt; };   // alt 是原始属性值（未解码）
  std::vector<EpubSegment> segs;
  std::map<std::string, int> anchorIdx;
  std::string baseDir;
  LocalImageResolver resolveImage;
  int cur = 0;
  int inHeading = 0;
  std::string buf;
  std::vector<Img> imgs;   // 当前缓冲里的图片，和 buf 里的 kImgSlot 一一对应

  void AddPara(std::string t, bool heading) {
    if (t.empty()) return;
    EpubSegment& s = segs[(size_t)cur];
    if (heading && s.heading.empty()) s.heading = t;
    s.paragraphs.push_back(std::move(t));
  }

  void Flush(bool heading) {
    if (imgs.empty()) {
      std::string t = CleanXmlText(buf);
      buf.clear();
      AddPara(std::move(t), heading);
      return;
    }
    std::string plain;
    plain.reserve(buf.size());
    for (char c : buf) if (c != kImgSlot) plain.push_back(c);
    if (CleanXmlText(plain).empty()) {
      // 这一块只有图片：每张图单独成一段
      EpubSegment& s = segs[(size_t)cur];
      for (const Img& im : imgs) s.paragraphs.push_back(MakeLocalImagePara(im.path, TrimUtf8(HtmlDecode(im.alt))));
    } else {
      // 图片夹在文字中间：多半是生僻字 / 小图标。alt 是一两个字就嵌回原处，否则丢掉
      std::string merged;
      merged.reserve(buf.size());
      size_t k = 0;
      for (char c : buf) {
        if (c != kImgSlot) { merged.push_back(c); continue; }
        if (k < imgs.size() && IsInlineAlt(HtmlDecode(imgs[k].alt))) merged += imgs[k].alt;
        ++k;
      }
      AddPara(CleanXmlText(merged), heading);
    }
    imgs.clear();
    buf.clear();
  }

  void AddImage(const HtmlNode& node, const std::string* rawHref, const std::string* rawAlt = nullptr) {
    const std::string href = rawHref ? TrimAscii(HtmlDecode(*rawHref)) : std::string();
    if (StartsWith(ToLowerAscii(href), "data:")) return;
    const std::string path = resolveImage ? resolveImage(node, href) : EpubResolvePath(baseDir, href);
    if (path.empty()) return;
    imgs.push_back({ path, rawAlt ? *rawAlt : std::string() });
    buf.push_back(kImgSlot);
  }

  static const std::string* ImageHref(const HtmlNode* n) {
    const std::string* h = n->Attr("xlink:href");
    return h ? h : n->Attr("href");
  }

  void Visit(const HtmlNode* n) {
    if (n->IsText()) { buf += n->text; return; }
    const std::string tag = LocalName(n->tag);
    if (IsSkipTag(tag)) return;

    if (!anchorIdx.empty()) {
      auto it = anchorIdx.find(n->Id());
      if (it == anchorIdx.end() && tag == "a") {
        if (const std::string* nm = n->Attr("name")) it = anchorIdx.find(*nm);
      }
      if (it != anchorIdx.end() && it->second + 1 > cur) {
        Flush(inHeading > 0);
        cur = it->second + 1;
      }
    }

    if (tag == "br") {
      if (inHeading > 0) buf.push_back(' ');   // 标题里的换行当空格，不拆成两段
      else Flush(false);
      return;
    }
    if (tag == "img") {
      AddImage(*n, n->Attr("src"), n->Attr("alt"));
      return;
    }
    if (tag == "svg") {
      // 插图页常见写法：<svg><image xlink:href="..."/></svg>。SVG 里的文字等其它内容不要
      WalkNodes(n, [&](const HtmlNode* x) {
        if (x->IsText() || LocalName(x->tag) != "image") return;
        AddImage(*x, ImageHref(x));
      });
      return;
    }
    if (tag == "image") {
      AddImage(*n, ImageHref(n));
      return;
    }

    const bool heading = IsHeadingTag(tag);
    const bool block = heading || IsBlockTag(tag);
    if (block) Flush(inHeading > 0);
    if (heading) ++inHeading;
    for (const auto& c : n->children) Visit(c.get());
    if (heading) --inHeading;
    if (block) Flush(heading || inHeading > 0);
  }
};

bool IsContentDoc(const std::string& type, const std::string& href) {
  if (type.find("html") != std::string::npos) return true;
  const std::string low = ToLowerAscii(href);
  return EndsWith(low, ".xhtml") || EndsWith(low, ".html") || EndsWith(low, ".htm");
}

}  // namespace

std::string EpubResolvePath(const std::string& baseDir, const std::string& href) {
  std::string h = TrimAscii(href);
  if (h.find("://") != std::string::npos) return "";   // 外链，不在包里
  const size_t cut = h.find_first_of("#?");
  if (cut != std::string::npos) h = h.substr(0, cut);
  h = PercentDecode(h);
  std::replace(h.begin(), h.end(), '\\', '/');
  if (h.empty()) return "";
  const std::string full = (h[0] == '/') ? h.substr(1) : baseDir + h;

  std::vector<std::string> parts;
  size_t i = 0;
  while (i <= full.size()) {
    size_t j = full.find('/', i);
    if (j == std::string::npos) j = full.size();
    const std::string seg = full.substr(i, j - i);
    if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
    else if (!seg.empty() && seg != ".") parts.push_back(seg);
    i = j + 1;
  }
  std::string out;
  for (size_t k = 0; k < parts.size(); ++k) {
    if (k) out.push_back('/');
    out += parts[k];
  }
  return out;
}

std::vector<EpubSegment> EpubExtractSegments(const std::string& xhtml,
                                             const std::vector<std::string>& anchors,
                                             std::string* docTitle,
                                             const std::string& baseDir,
                                             const LocalImageResolver& resolveImage,
                                             size_t maxDepth) {
  HtmlDoc doc = HtmlDoc::Parse(FixSelfClosingRawTags(xhtml), maxDepth);
  SegBuilder b;
  b.baseDir = baseDir;
  b.resolveImage = resolveImage;
  b.segs.resize(anchors.size() + 1);
  for (size_t k = 0; k < anchors.size(); ++k) {
    if (!anchors[k].empty()) b.anchorIdx.emplace(anchors[k], (int)k);
  }
  if (docTitle) *docTitle = NodeTextClean(FindFirst(doc.root.get(), "title"));
  const HtmlNode* body = FindFirst(doc.root.get(), "body");
  b.Visit(body ? body : doc.root.get());
  b.Flush(false);
  return b.segs;
}

bool EpubBook::ReadText(const std::string& name, std::string& out) const {
  std::string raw;
  if (!zip_.Read(name, raw)) return false;
  return DecodeXml(raw, out);
}

bool EpubBook::Load(const std::wstring& path, std::string& err) {
  pathW_ = path;
  title_.clear();
  author_.clear();
  version_.clear();
  cover_.clear();
  coverMime_.clear();
  chapters_.clear();
  anchors_.clear();
  segFile_.clear();
  segCache_.clear();
  sizes_.clear();
  if (!zip_.OpenFile(path, err)) return false;
  if (!Parse(err)) {
    zip_.Close();
    return false;
  }
  return true;
}

bool EpubBook::Parse(std::string& err) {
  std::string text;

  // ---- 1. container.xml -> OPF 路径（找不到就在包里随便找一个 .opf）----
  std::string opfPath;
  if (ReadText("META-INF/container.xml", text)) {
    HtmlDoc c = HtmlDoc::Parse(text);
    for (const HtmlNode* r : FindAll(c.root.get(), "rootfile")) {
      const std::string p = AttrOf(r, "full-path");
      const std::string mt = AttrOf(r, "media-type");
      if (!p.empty() && (mt.empty() || mt == "application/oebps-package+xml")) {
        opfPath = EpubResolvePath("", p);
        break;
      }
    }
  }
  if (opfPath.empty() || !zip_.Has(opfPath)) {
    opfPath.clear();
    for (const std::string& n : zip_.Names()) {
      if (EndsWith(ToLowerAscii(n), ".opf")) { opfPath = n; break; }
    }
  }
  if (opfPath.empty()) { err = "不是有效的 EPUB：找不到 content.opf"; return false; }

  // ---- 2. DRM：正文被加密就没法读（只加密字体的「字体混淆」不算）----
  static const char* kDrm = "这本 EPUB 带 DRM 加密，无法打开";
  if (zip_.IsEncrypted(opfPath)) { err = kDrm; return false; }
  if (ReadText("META-INF/encryption.xml", text)) {
    HtmlDoc e = HtmlDoc::Parse(text);
    for (const HtmlNode* r : FindAll(e.root.get(), "cipherreference")) {
      const std::string uri = ToLowerAscii(AttrOf(r, "uri"));
      if (EndsWith(uri, ".xhtml") || EndsWith(uri, ".html") || EndsWith(uri, ".htm") ||
          EndsWith(uri, ".opf") || EndsWith(uri, ".ncx")) {
        err = kDrm;
        return false;
      }
    }
  }

  // ---- 3. OPF：元信息 + 清单 ----
  if (!ReadText(opfPath, text)) { err = "读取 content.opf 失败"; return false; }
  HtmlDoc opf = HtmlDoc::Parse(text);
  const std::string opfDir = DirOf(opfPath);
  version_ = AttrOf(FindFirst(opf.root.get(), "package"), "version");
  const HtmlNode* metadata = FindFirst(opf.root.get(), "metadata");
  const HtmlNode* mroot = metadata ? metadata : opf.root.get();
  title_ = NodeTextClean(FindFirst(mroot, "title"));
  author_ = NodeTextClean(FindFirst(mroot, "creator"));
  if (title_.empty()) title_ = BaseNameNoExt(pathW_);
  if (title_.empty()) title_ = "未命名";

  struct ManItem { std::string href, type, props; };
  std::map<std::string, ManItem> man;
  std::vector<std::string> manOrder;
  for (const HtmlNode* it : FindAll(opf.root.get(), "item")) {
    const std::string id = AttrOf(it, "id");
    ManItem m;
    m.href = EpubResolvePath(opfDir, AttrOf(it, "href"));
    m.type = ToLowerAscii(AttrOf(it, "media-type"));
    m.props = AttrOf(it, "properties");
    if (id.empty() || m.href.empty()) continue;
    if (man.emplace(id, m).second) manOrder.push_back(id);
  }

  // ---- 4. 目录：EPUB3 nav.xhtml 优先，没有再看 EPUB2 toc.ncx ----
  struct TocRef { std::string file, frag, title; };
  std::vector<TocRef> toc;
  auto addRef = [&](const std::string& baseDir, const std::string& href, const std::string& title) {
    TocRef r;
    r.file = EpubResolvePath(baseDir, href);
    r.frag = FragmentOf(href);
    r.title = title;
    if (!r.file.empty() && !r.title.empty()) toc.push_back(std::move(r));
  };

  std::string navPath;
  for (const std::string& id : manOrder) {
    if (HasToken(man[id].props, "nav")) { navPath = man[id].href; break; }
  }
  if (!navPath.empty() && ReadText(navPath, text)) {
    HtmlDoc nav = HtmlDoc::Parse(FixSelfClosingRawTags(text));
    const HtmlNode* tocNav = nullptr;
    const HtmlNode* firstNav = nullptr;
    for (const HtmlNode* n : FindAll(nav.root.get(), "nav")) {
      if (!firstNav) firstNav = n;
      const std::string kind = AttrOf(n, "epub:type") + " " + AttrOf(n, "role");
      if (HasToken(kind, "toc") || HasToken(kind, "doc-toc")) { tocNav = n; break; }
    }
    if (!tocNav) tocNav = firstNav;
    for (const HtmlNode* a : FindAll(tocNav, "a")) addRef(DirOf(navPath), AttrOf(a, "href"), NodeTextClean(a));
  }
  const HtmlNode* spine = FindFirst(opf.root.get(), "spine");
  if (toc.empty()) {
    std::string ncxPath;
    const std::string tocId = AttrOf(spine, "toc");
    auto t = man.find(tocId);
    if (!tocId.empty() && t != man.end()) ncxPath = t->second.href;
    if (ncxPath.empty()) {
      for (const std::string& id : manOrder) {
        if (man[id].type == "application/x-dtbncx+xml") { ncxPath = man[id].href; break; }
      }
    }
    if (!ncxPath.empty() && ReadText(ncxPath, text)) {
      HtmlDoc ncx = HtmlDoc::Parse(text);
      // 前序遍历：嵌套的子目录项正好排在父项后面
      for (const HtmlNode* np : FindAll(ncx.root.get(), "navpoint")) {
        const HtmlNode* label = FirstChild(np, "navlabel");
        const HtmlNode* content = FirstChild(np, "content");
        addRef(DirOf(ncxPath), AttrOf(content, "src"), NodeTextClean(FindFirst(label, "text")));
      }
    }
  }

  // 按文件归组（路径不区分大小写），同一个「文件#锚点」只留第一条
  std::map<std::string, std::vector<const TocRef*>> byFile;
  std::set<std::string> seenRef;
  for (const TocRef& r : toc) {
    const std::string key = ToLowerAscii(r.file);
    if (!seenRef.insert(key + "#" + r.frag).second) continue;
    byFile[key].push_back(&r);
  }

  // ---- 5. 封面（先找出来，下面跳过封面页要用）：EPUB3 cover-image -> EPUB2 <meta name="cover"> -> 名字里带 cover 的图片 ----
  std::string coverHref;
  for (const std::string& id : manOrder) {
    if (HasToken(man[id].props, "cover-image")) { coverHref = man[id].href; coverMime_ = man[id].type; break; }
  }
  if (coverHref.empty()) {
    for (const HtmlNode* mt : FindAll(mroot, "meta")) {
      if (ToLowerAscii(AttrOf(mt, "name")) != "cover") continue;
      const std::string content = AttrOf(mt, "content");
      auto c = man.find(content);
      if (c != man.end() && StartsWith(c->second.type, "image/")) {
        coverHref = c->second.href;
        coverMime_ = c->second.type;
      } else {
        const std::string p = EpubResolvePath(opfDir, content);
        if (!p.empty() && zip_.Has(p)) coverHref = p;
      }
      break;
    }
  }
  if (coverHref.empty()) {
    for (const std::string& id : manOrder) {
      const ManItem& m = man[id];
      if (!StartsWith(m.type, "image/")) continue;
      if (ToLowerAscii(id).find("cover") != std::string::npos ||
          ToLowerAscii(m.href).find("cover") != std::string::npos) {
        coverHref = m.href;
        coverMime_ = m.type;
        break;
      }
    }
  }
  if (!coverHref.empty()) {
    std::string bytes;
    if (zip_.Read(coverHref, bytes)) cover_ = std::move(bytes);
  }
  const std::string coverKey = ToLowerAscii(coverHref);

  // ---- 6. spine：阅读顺序 ----
  std::set<std::string> done;
  for (const HtmlNode* ref : FindAll(spine, "itemref")) {
    auto it = man.find(AttrOf(ref, "idref"));
    if (it == man.end()) continue;
    const ManItem& m = it->second;
    if (HasToken(m.props, "nav")) continue;   // 目录页本身不当正文
    if (!IsContentDoc(m.type, m.href)) continue;
    const std::string key = ToLowerAscii(m.href);
    if (!done.insert(key).second) continue;
    std::string body;
    if (!ReadText(m.href, body)) continue;

    std::vector<const TocRef*> entries;
    auto f = byFile.find(key);
    if (f != byFile.end()) entries = f->second;

    // 一个文件里有多个目录项、而且带锚点 -> 按锚点切成多章
    std::vector<std::string> anchors;
    if (entries.size() >= 2) {
      for (const TocRef* e : entries) {
        if (!e->frag.empty() && std::find(anchors.begin(), anchors.end(), e->frag) == anchors.end())
          anchors.push_back(e->frag);
      }
    }
    std::string docTitle;
    const std::vector<EpubSegment> segs = EpubExtractSegments(body, anchors, &docTitle, DirOf(m.href));

    if (anchors.empty()) {
      size_t textParas = 0, imgParas = 0;
      bool onlyCover = true;
      std::string heading;
      for (const EpubSegment& s : segs) {
        for (const std::string& p : s.paragraphs) {
          std::string img;
          if (ParseLocalImagePara(p, &img, nullptr)) {
            ++imgParas;
            if (coverKey.empty() || ToLowerAscii(img) != coverKey) onlyCover = false;
          } else {
            ++textParas;
          }
        }
        if (heading.empty()) heading = s.heading;
      }
      if (textParas + imgParas == 0) continue;   // 空页
      // 封面页：只有图，而且就是封面图（或文件名带 cover）。书架上已经有封面了，不当正文
      if (textParas == 0 && (onlyCover || key.find("cover") != std::string::npos)) continue;
      Item item;
      item.file = m.href;
      item.segFrom = 0;
      item.segTo = (int)segs.size() - 1;
      if (!entries.empty()) item.title = entries[0]->title;
      else if (!heading.empty()) item.title = heading;
      else if (!docTitle.empty() && docTitle != title_) item.title = docTitle;
      else if (textParas == 0) item.title = "插图";
      else item.title = "第 " + std::to_string(chapters_.size() + 1) + " 节";
      chapters_.push_back(std::move(item));
      continue;
    }

    anchors_[m.href] = anchors;
    struct Start { int seg; std::string title; };
    std::vector<Start> starts;
    for (const TocRef* e : entries) {
      int s = 0;
      if (!e->frag.empty())
        s = (int)(std::find(anchors.begin(), anchors.end(), e->frag) - anchors.begin()) + 1;
      bool dup = false;
      for (const Start& x : starts) if (x.seg == s) { dup = true; break; }
      if (!dup) starts.push_back({ s, e->title });
    }
    std::stable_sort(starts.begin(), starts.end(), [](const Start& a, const Start& b) { return a.seg < b.seg; });
    starts[0].seg = 0;   // 第一个锚点之前的内容并进第一章
    for (size_t k = 0; k < starts.size(); ++k) {
      Item item;
      item.file = m.href;
      item.segFrom = starts[k].seg;
      item.segTo = (k + 1 < starts.size() ? starts[k + 1].seg : (int)segs.size()) - 1;
      size_t paras = 0;
      for (int s = item.segFrom; s <= item.segTo; ++s) paras += segs[(size_t)s].paragraphs.size();
      if (paras == 0) continue;
      item.title = starts[k].title;
      chapters_.push_back(std::move(item));
    }
  }
  if (chapters_.empty()) {
    err = "没有解析出可阅读的内容";
    return false;
  }

  return true;
}

std::string EpubBook::ChapterTitle(int index) const {
  if (index < 0 || index >= (int)chapters_.size()) return "";
  return chapters_[(size_t)index].title;
}

std::string EpubBook::ChapterFile(int index) const {
  if (index < 0 || index >= (int)chapters_.size()) return "";
  return chapters_[(size_t)index].file;
}

const std::vector<EpubSegment>& EpubBook::Segments(const std::string& file) const {
  if (segFile_ == file && !segCache_.empty()) return segCache_;
  std::string body;
  ReadText(file, body);
  static const std::vector<std::string> kNoAnchors;
  auto a = anchors_.find(file);
  segCache_ = EpubExtractSegments(body, a == anchors_.end() ? kNoAnchors : a->second, nullptr, DirOf(file));
  segFile_ = file;
  return segCache_;
}

bool EpubBook::GetChapter(int index, std::vector<std::string>& paragraphs) const {
  paragraphs.clear();
  if (index < 0 || index >= (int)chapters_.size()) return false;
  const Item& item = chapters_[(size_t)index];
  const std::vector<EpubSegment>& segs = Segments(item.file);
  for (int s = item.segFrom; s <= item.segTo && s < (int)segs.size(); ++s) {
    for (const std::string& p : segs[(size_t)s].paragraphs) paragraphs.push_back(p);
  }
  // 阅读页自己会显示章节名，正文开头重复的那行标题去掉（跳过标题前面的题图）
  for (size_t k = 0; k < paragraphs.size(); ++k) {
    if (IsLocalImagePara(paragraphs[k])) continue;
    if (Squash(paragraphs[k]) == Squash(item.title)) paragraphs.erase(paragraphs.begin() + (std::ptrdiff_t)k);
    break;
  }
  return true;
}

// ---------- 插图 ----------

namespace {
uint32_t U8(const std::string& b, size_t i) { return (uint32_t)(unsigned char)b[i]; }
uint32_t BE16(const std::string& b, size_t i) { return (U8(b, i) << 8) | U8(b, i + 1); }
uint32_t LE16(const std::string& b, size_t i) { return U8(b, i) | (U8(b, i + 1) << 8); }
uint32_t LE24(const std::string& b, size_t i) { return LE16(b, i) | (U8(b, i + 2) << 16); }
uint32_t BE32(const std::string& b, size_t i) { return (BE16(b, i) << 16) | BE16(b, i + 2); }
uint32_t LE32(const std::string& b, size_t i) { return LE16(b, i) | (LE16(b, i + 2) << 16); }

bool IsPng(const std::string& b) { return b.size() >= 8 && b.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0; }
bool IsGif(const std::string& b) { return b.size() >= 6 && (b.compare(0, 6, "GIF87a") == 0 || b.compare(0, 6, "GIF89a") == 0); }
bool IsJpeg(const std::string& b) { return b.size() >= 3 && U8(b, 0) == 0xFF && U8(b, 1) == 0xD8 && U8(b, 2) == 0xFF; }
bool IsWebp(const std::string& b) { return b.size() >= 16 && b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0; }
bool IsBmp(const std::string& b) { return b.size() >= 26 && b.compare(0, 2, "BM") == 0; }
}  // namespace

bool ImageDimensions(const std::string& b, int& w, int& h) {
  w = h = 0;
  const size_t n = b.size();
  long long W = 0, H = 0;
  if (IsPng(b) && n >= 24) {
    W = BE32(b, 16); H = BE32(b, 20);
  } else if (IsGif(b) && n >= 10) {
    W = LE16(b, 6); H = LE16(b, 8);
  } else if (IsBmp(b)) {
    W = (int32_t)LE32(b, 18); H = (int32_t)LE32(b, 22);
    if (H < 0) H = -H;   // 自上而下存储的 BMP 高度是负数
  } else if (IsWebp(b) && n >= 30) {
    if (b.compare(12, 4, "VP8 ") == 0) {
      W = LE16(b, 26) & 0x3FFF; H = LE16(b, 28) & 0x3FFF;
    } else if (b.compare(12, 4, "VP8L") == 0) {
      const uint32_t b0 = U8(b, 21), b1 = U8(b, 22), b2 = U8(b, 23), b3 = U8(b, 24);
      W = 1 + (((b1 & 0x3F) << 8) | b0);
      H = 1 + (((b3 & 0x0F) << 10) | (b2 << 2) | ((b1 & 0xC0) >> 6));
    } else if (b.compare(12, 4, "VP8X") == 0) {
      W = 1 + (long long)LE24(b, 24); H = 1 + (long long)LE24(b, 27);
    }
  } else if (IsJpeg(b)) {
    // 逐个段往后找 SOFn（C0–CF，C4/C8/CC 除外），里面是高、宽
    size_t i = 2;
    while (i + 8 < n) {
      if (U8(b, i) != 0xFF) { ++i; continue; }
      const uint32_t mk = U8(b, i + 1);
      if (mk == 0xFF) { ++i; continue; }
      if (mk == 0x01 || (mk >= 0xD0 && mk <= 0xD8)) { i += 2; continue; }
      if (mk == 0xD9 || mk == 0xDA) break;
      if (mk >= 0xC0 && mk <= 0xCF && mk != 0xC4 && mk != 0xC8 && mk != 0xCC) {
        H = BE16(b, i + 5); W = BE16(b, i + 7);
        break;
      }
      i += 2 + BE16(b, i + 2);
    }
  }
  if (W <= 0 || H <= 0 || W > 100000 || H > 100000) return false;
  w = (int)W;
  h = (int)H;
  return true;
}

std::string ImageMimeType(const std::string& b, const std::string& path) {
  if (IsPng(b)) return "image/png";
  if (IsJpeg(b)) return "image/jpeg";
  if (IsGif(b)) return "image/gif";
  if (IsWebp(b)) return "image/webp";
  if (IsBmp(b)) return "image/bmp";
  const std::string low = ToLowerAscii(path);
  if (EndsWith(low, ".svg")) return "image/svg+xml";
  if (EndsWith(low, ".jpg") || EndsWith(low, ".jpeg")) return "image/jpeg";
  if (EndsWith(low, ".png")) return "image/png";
  if (EndsWith(low, ".gif")) return "image/gif";
  if (EndsWith(low, ".webp")) return "image/webp";
  return "application/octet-stream";
}

std::string EpubUrlEncodePath(const std::string& path) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(path.size() * 3);
  for (unsigned char c : path) {
    const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
    if (keep) { out.push_back((char)c); continue; }
    out.push_back('%');
    out.push_back(kHex[c >> 4]);
    out.push_back(kHex[c & 15]);
  }
  return out;
}

std::string EpubUrlDecode(const std::string& s) { return PercentDecode(s); }

bool EpubBook::ImageSize(const std::string& path, int& w, int& h) const {
  const std::string key = ToLowerAscii(path);
  auto it = sizes_.find(key);
  if (it == sizes_.end()) {
    std::pair<int, int> v(0, 0);
    std::string bytes;
    if (zip_.Read(path, bytes)) ImageDimensions(bytes, v.first, v.second);
    it = sizes_.emplace(key, v).first;
  }
  w = it->second.first;
  h = it->second.second;
  return w > 0 && h > 0;
}

bool EpubBook::ReadResource(const std::string& path, std::string& bytes, std::string& mime) const {
  bytes.clear();
  mime.clear();
  if (path.empty() || !zip_.Read(path, bytes) || bytes.empty()) return false;
  mime = ImageMimeType(bytes, path);
  if (!StartsWith(mime, "image/")) { bytes.clear(); return false; }   // 只给图片，正文 / 样式之类不外露
  return true;
}
