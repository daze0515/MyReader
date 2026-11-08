#include "Html.h"
#include <algorithm>
#include <cstdlib>
#include <cctype>

const std::string* HtmlNode::Attr(const std::string& name) const {
  for (auto& kv : attrs) if (kv.first == name) return &kv.second;
  return nullptr;
}
std::string HtmlNode::Id() const { const std::string* v = Attr("id"); return v ? *v : ""; }
std::string HtmlNode::Class() const { const std::string* v = Attr("class"); return v ? *v : ""; }
bool HtmlNode::HasClass(const std::string& c) const {
  std::string cls = Class();
  size_t i = 0;
  while (i < cls.size()) {
    size_t j = cls.find_first_of(" \t\r\n", i);
    if (j == std::string::npos) j = cls.size();
    if (cls.compare(i, j - i, c) == 0) return true;
    i = j + 1;
  }
  return false;
}

static bool IsVoidTag(const std::string& t) {
  static const char* kVoid[] = { "area","base","br","col","embed","hr","img","input","link","meta","param","source","track","wbr" };
  for (auto v : kVoid) if (t == v) return true;
  return false;
}
static bool IsRawTextTag(const std::string& t) {
  return t == "script" || t == "style" || t == "textarea" || t == "title";
}

namespace {
struct Builder {
  HtmlNode* cur = nullptr;
  void AddText(const std::string& raw) {
    if (raw.empty()) return;
    auto n = std::make_unique<HtmlNode>();
    n->text = raw;
    n->parent = cur;
    cur->children.push_back(std::move(n));
  }
  void Push(HtmlNode* n) {
    n->parent = cur;
    cur->children.push_back(std::unique_ptr<HtmlNode>(n));
    cur = n;
  }
};
}

HtmlDoc HtmlDoc::Parse(const std::string& html) {
  HtmlDoc doc;
  doc.root = std::make_unique<HtmlNode>();
  doc.root->tag = "#document";

  Builder b;
  b.cur = doc.root.get();

  const size_t n = html.size();
  size_t i = 0;
  while (i < n) {
    size_t lt = html.find('<', i);
    if (lt == std::string::npos) { b.AddText(html.substr(i)); break; }
    if (lt > i) b.AddText(html.substr(i, lt - i));
    i = lt;

    if (html.compare(i, 4, "<!--") == 0) {
      size_t e = html.find("-->", i + 4);
      i = (e == std::string::npos) ? n : e + 3;
      continue;
    }
    if (html.compare(i, 2, "<!") == 0 || html.compare(i, 2, "<?") == 0) {
      size_t e = html.find('>', i);
      i = (e == std::string::npos) ? n : e + 1;
      continue;
    }
    if (i + 1 < n && html[i + 1] == '/') {
      size_t e = html.find('>', i);
      if (e == std::string::npos) break;
      std::string name = ToLowerAscii(TrimAscii(html.substr(i + 2, e - i - 2)));
      HtmlNode* p = b.cur;
      while (p && p != doc.root.get() && p->tag != name) p = p->parent;
      if (p && p != doc.root.get()) b.cur = p->parent;
      i = e + 1;
      continue;
    }

    size_t e = i + 1;
    bool inQuote = false; char qc = 0;
    while (e < n) {
      char c = html[e];
      if (inQuote) { if (c == qc) inQuote = false; }
      else if (c == '"' || c == 0x27) { inQuote = true; qc = c; }
      else if (c == '>') break;
      ++e;
    }
    if (e >= n) break;
    std::string inside = html.substr(i + 1, e - i - 1);
    bool selfClose = false;
    if (!inside.empty() && inside.back() == '/') { selfClose = true; inside.pop_back(); }

    size_t k = 0;
    while (k < inside.size() && !isspace((unsigned char)inside[k])) ++k;
    std::string tag = ToLowerAscii(inside.substr(0, k));
    i = e + 1;
    if (tag.empty()) continue;

    auto node = std::make_unique<HtmlNode>();
    node->tag = tag;

    size_t a = k;
    while (a < inside.size()) {
      while (a < inside.size() && isspace((unsigned char)inside[a])) ++a;
      if (a >= inside.size()) break;
      size_t ns = a;
      while (a < inside.size() && inside[a] != '=' && !isspace((unsigned char)inside[a])) ++a;
      std::string an = ToLowerAscii(inside.substr(ns, a - ns));
      while (a < inside.size() && isspace((unsigned char)inside[a])) ++a;
      std::string av;
      if (a < inside.size() && inside[a] == '=') {
        ++a;
        while (a < inside.size() && isspace((unsigned char)inside[a])) ++a;
        if (a < inside.size() && (inside[a] == '"' || inside[a] == 0x27)) {
          char q = inside[a++];
          size_t vs = a;
          while (a < inside.size() && inside[a] != q) ++a;
          av = inside.substr(vs, a - vs);
          if (a < inside.size()) ++a;
        } else {
          size_t vs = a;
          while (a < inside.size() && !isspace((unsigned char)inside[a])) ++a;
          av = inside.substr(vs, a - vs);
        }
      }
      if (!an.empty()) node->attrs.emplace_back(an, av);
    }

    if (IsRawTextTag(tag)) {
      std::string closeTag = "</" + tag;
      size_t found = std::string::npos;
      for (size_t p = i; p + closeTag.size() <= n; ++p) {
        bool match = true;
        for (size_t q = 0; q < closeTag.size(); ++q) {
          if (tolower((unsigned char)html[p + q]) != closeTag[q]) { match = false; break; }
        }
        if (match) { found = p; break; }
      }
      auto raw = std::make_unique<HtmlNode>();
      raw->text = html.substr(i, (found == std::string::npos ? n : found) - i);
      raw->parent = node.get();
      node->children.push_back(std::move(raw));
      HtmlNode* np = node.get();
      b.Push(node.release());
      b.cur = np->parent;
      if (found == std::string::npos) { i = n; }
      else {
        size_t gt = html.find('>', found);
        i = (gt == std::string::npos) ? n : gt + 1;
      }
      continue;
    }

    bool leaf = IsVoidTag(tag) || selfClose;
    HtmlNode* np = node.get();
    b.Push(node.release());
    if (leaf) b.cur = np->parent;
  }

  return doc;
}

static void Walk(HtmlNode* n, const std::function<void(HtmlNode*)>& fn) {
  fn(n);
  for (auto& c : n->children) Walk(c.get(), fn);
}

HtmlNode* HtmlDoc::FindById(const std::string& id) const {
  HtmlNode* found = nullptr;
  Walk(root.get(), [&](HtmlNode* n) { if (!found && n->Id() == id) found = n; });
  return found;
}

void HtmlDoc::FindByClass(const std::string& cls, std::vector<HtmlNode*>& out) const {
  Walk(root.get(), [&](HtmlNode* n) { if (n->HasClass(cls)) out.push_back(n); });
}

HtmlNode* HtmlDoc::FindFirstTag(const std::string& tag) const {
  HtmlNode* found = nullptr;
  Walk(root.get(), [&](HtmlNode* n) { if (!found && n->tag == tag) found = n; });
  return found;
}

namespace {
struct Ent { const char* name; uint32_t cp; };
const Ent kEnts[] = {
  {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",0x27},{"nbsp",0x00A0},
  {"emsp",0x2003},{"ensp",0x2002},{"thinsp",0x2009},{"ldquo",0x201C},{"rdquo",0x201D},
  {"lsquo",0x2018},{"rsquo",0x2019},{"hellip",0x2026},{"mdash",0x2014},{"ndash",0x2013},
  {"middot",0x00B7},{"times",0x00D7},{"laquo",0x00AB},{"raquo",0x00BB},{"copy",0x00A9},
  {"reg",0x00AE},{"trade",0x2122},{"bull",0x2022},{"dagger",0x2020},{"sect",0x00A7},
  {"para",0x00B6},{"deg",0x00B0},{"plusmn",0x00B1},{"frac12",0x00BD},{"iexcl",0x00A1},
  {"iquest",0x00BF},{"cent",0x00A2},{"pound",0x00A3},{"yen",0x00A5},{"euro",0x20AC},
  {"larr",0x2190},{"rarr",0x2192},{"uarr",0x2191},{"darr",0x2193},{"harr",0x2194},
  {"lsaquo",0x2039},{"rsaquo",0x203A},{"sbquo",0x201A},{"bdquo",0x201E},{"permil",0x2030},
  {"prime",0x2032},{"Prime",0x2033},{"oline",0x203E},{"frasl",0x2044},{"weierp",0x2118},
  {"loz",0x25CA},{"hearts",0x2665},{"spades",0x2660},{"clubs",0x2663},{"diams",0x2666},
  {"lang",0x2329},{"rang",0x232A},{"lceil",0x2308},{"rceil",0x2309},{"lfloor",0x230A},{"rfloor",0x230B},
};
}

std::string HtmlDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '&') { out.push_back(s[i++]); continue; }
    size_t sc = s.find(';', i);
    if (sc == std::string::npos || sc - i > 12) { out.push_back(s[i++]); continue; }
    std::string name = s.substr(i + 1, sc - i - 1);
    if (!name.empty() && name[0] == '#') {
      uint32_t cp = 0;
      if (name.size() > 1 && (name[1] == 'x' || name[1] == 'X'))
        cp = (uint32_t)strtoul(name.c_str() + 2, nullptr, 16);
      else
        cp = (uint32_t)strtoul(name.c_str() + 1, nullptr, 10);
      if (cp) { AppendUtf8(out, cp); i = sc + 1; continue; }
    } else {
      bool hit = false;
      for (auto& en : kEnts) {
        if (name == en.name) { AppendUtf8(out, en.cp); hit = true; break; }
      }
      if (hit) { i = sc + 1; continue; }
    }
    out.push_back(s[i++]);
  }
  return out;
}

std::string NodeText(const HtmlNode* n, bool withBreaks) {
  std::string out;
  std::function<void(const HtmlNode*)> rec = [&](const HtmlNode* x) {
    if (x->IsText()) { out += x->text; return; }
    if (x->tag == "script" || x->tag == "style") return;
    if (x->tag == "br") { if (withBreaks) out.push_back('\n'); return; }
    if (withBreaks && (x->tag == "p" || x->tag == "div") && !out.empty() && out.back() != '\n') out.push_back('\n');
    for (auto& c : x->children) rec(c.get());
  };
  rec(n);
  return out;
}
