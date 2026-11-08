#include "TxtBook.h"
#include "Extract.h"
#include <algorithm>
#include <regex>

#ifndef CP_GBK
#define CP_GBK 936
#endif

namespace {

std::vector<uint32_t> Cps(const std::string& s) {
  std::vector<uint32_t> v;
  v.reserve(s.size() / 3 + 1);
  for (size_t i = 0; i < s.size();) {
    Utf8Decoded d = DecodeUtf8(s, i);
    i += d.bytes;
    v.push_back(d.cp);
  }
  return v;
}

bool IsDigitCp(uint32_t cp) { return cp >= '0' && cp <= '9'; }

bool InList(const std::vector<uint32_t>& list, uint32_t cp) {
  return std::find(list.begin(), list.end(), cp) != list.end();
}

const std::vector<uint32_t>& CnNumbers() {
  static const std::vector<uint32_t> v = Cps("零一二三四五六七八九十百千万两〇壹贰叁肆伍陆柒捌玖拾佰仟廿卅");
  return v;
}
const std::vector<uint32_t>& ChapterMarks() {
  static const std::vector<uint32_t> v = Cps("章节節回卷集篇话話部幕");
  return v;
}
const std::vector<uint32_t>& BadEnders() {
  static const std::vector<uint32_t> v = Cps("。，！？；：、…—·》】）]}\"'");
  return v;
}

bool StartsWithChapterWord(const std::string& lower) {
  static const char* k = "chapter";
  if (lower.size() < 7 || lower.compare(0, 7, k) != 0) return false;
  if (lower.size() == 7) return true;
  char c = lower[7];
  return c == ' ' || c == '\t' || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

bool HeadingByRule(const std::string& t) {
  if (t.empty()) return false;
  std::vector<uint32_t> c = Cps(t);
  if (c.size() < 2 || c.size() > 40) return false;

  // 【…】 / 《…》 这类标题自带收尾括号，先判，免得被下面的句末标点规则挡掉
  if ((c[0] == 0x3010 && c.back() == 0x3011) || (c[0] == 0x300A && c.back() == 0x300B)) {
    for (uint32_t cp : c) if (InList(ChapterMarks(), cp)) return true;
  }
  if (InList(BadEnders(), c.back())) return false;

  const size_t kMaxPrefix = 8;

  // 第 X 章 / 第 X 回 / 第 X 卷 …
  if (c[0] == 0x7B2C) {
    for (size_t k = 1; k < c.size() && k <= kMaxPrefix; ++k) {
      if (InList(ChapterMarks(), c[k])) return true;
      bool ok = IsDigitCp(c[k]) || InList(CnNumbers(), c[k]) || c[k] == ' ' || c[k] == 0x3000;
      if (!ok) break;
    }
  }

  // Chapter 12 / CHAPTER 12
  if (StartsWithChapterWord(ToLowerAscii(t))) return true;

  // 12. / 12、 / 12 - / 12 空格 开头的短行
  if (IsDigitCp(c[0])) {
    size_t k = 0;
    while (k < c.size() && IsDigitCp(c[k])) ++k;
    if (k <= 4 && k < c.size()) {
      uint32_t sep = c[k];
      if (sep == '.' || sep == 0x3002 || sep == 0xFF0E || sep == 0x3001 || sep == 0xFF0C ||
          sep == ',' || sep == ':' || sep == 0xFF1A || sep == '-' || sep == 0x2014 ||
          sep == 0x2013 || sep == ' ' || sep == 0x3000 || sep == 0x300A) {
        return true;
      }
    }
  }

  // 卷 X / 序章 / 楔子 / 尾声 / 番外 …
  static const std::vector<uint32_t> kPre = Cps("序楔引尾终番前附");
  if (c.size() <= 12 && InList(kPre, c[0])) return true;
  if (c[0] == 0x5377 && c.size() <= 20) return true;

  return false;
}

bool IsValidUtf8(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i];
    size_t n = 0;
    uint32_t cp = 0;
    if (c < 0x80) { ++i; continue; }
    else if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07u; }
    else return false;
    if (i + n >= s.size()) return false;
    for (size_t k = 1; k <= n; ++k) {
      unsigned char cc = (unsigned char)s[i + k];
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3Fu);
    }
    if (n == 1 && cp < 0x80) return false;
    if (n == 2 && cp < 0x800) return false;
    if (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += n + 1;
  }
  return true;
}

std::wstring Utf16ToWide(const std::string& bytes, bool little) {
  std::wstring out;
  size_t i = 0;
  if (bytes.size() >= 2) {
    unsigned char a = (unsigned char)bytes[0], b = (unsigned char)bytes[1];
    if ((little && a == 0xFF && b == 0xFE) || (!little && a == 0xFE && b == 0xFF)) i = 2;
  }
  while (i + 1 < bytes.size()) {
    uint32_t u = little
      ? ((uint32_t)(unsigned char)bytes[i] | ((uint32_t)(unsigned char)bytes[i + 1] << 8))
      : (((uint32_t)(unsigned char)bytes[i] << 8) | (uint32_t)(unsigned char)bytes[i + 1]);
    i += 2;
    if (u >= 0xD800 && u <= 0xDBFF && i + 1 < bytes.size()) {
      uint32_t lo = little
        ? ((uint32_t)(unsigned char)bytes[i] | ((uint32_t)(unsigned char)bytes[i + 1] << 8))
        : (((uint32_t)(unsigned char)bytes[i] << 8) | (uint32_t)(unsigned char)bytes[i + 1]);
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        i += 2;
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
      }
    }
    if (u > 0xFFFF) {
      uint32_t v = u - 0x10000;
      out.push_back((wchar_t)(0xD800 + (v >> 10)));
      out.push_back((wchar_t)(0xDC00 + (v & 0x3FF)));
    } else {
      out.push_back((wchar_t)u);
    }
  }
  return out;
}

std::string NormalizeNewlines(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\r') {
      out.push_back('\n');
      if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::string BaseNameNoExt(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
  return TrimAscii(W2U(name));
}

bool RangeHasRealText(const std::string& text, size_t start, size_t end) {
  int n = 0;
  for (size_t i = start; i < end;) {
    Utf8Decoded d = DecodeUtf8(text, i);
    i += d.bytes;
    if (d.cp == ' ' || d.cp == '\t' || d.cp == 0x3000 || d.cp == '\n' || d.cp == '\r') continue;
    if (++n >= 8) return true;
  }
  return false;
}

}  // namespace

std::string TrimUtf8(const std::string& s) {
  size_t b = 0, e = s.size();
  auto isWs = [](uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == 0x3000 || cp == 0x00A0 || cp == 0xFEFF;
  };
  while (b < e) {
    Utf8Decoded d = DecodeUtf8(s, b);
    if (!isWs(d.cp)) break;
    b += d.bytes;
  }
  while (e > b) {
    size_t k = e - 1;
    while (k > b && ((unsigned char)s[k] & 0xC0) == 0x80) --k;
    Utf8Decoded d = DecodeUtf8(s, k);
    if (d.bytes == 0 || k + d.bytes != e || !isWs(d.cp)) break;
    e = k;
  }
  return s.substr(b, e - b);
}

bool TxtLineLooksLikeHeading(const std::string& line) {
  return HeadingByRule(TrimUtf8(line));
}

std::vector<std::string> TxtBook::EncodingChoices() {
  return { "auto", "UTF-8", "GBK", "UTF-16LE", "UTF-16BE" };
}

std::string TxtBook::DetectEncoding(const std::string& bytes) {
  if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
      (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) return "UTF-8";
  if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE) return "UTF-16LE";
  if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFE && (unsigned char)bytes[1] == 0xFF) return "UTF-16BE";
  // 无 BOM：先按严格 UTF-8 校验，过不了就当 GBK（GB18030 兼容）
  std::string probe = bytes.substr(0, std::min<size_t>(bytes.size(), 1u << 20));
  if (IsValidUtf8(probe)) return "UTF-8";
  return "GBK";
}

bool TxtBook::DecodeToUtf8(const std::string& bytes, const std::string& enc, std::string& out) {
  std::string utf8;
  if (enc == "UTF-8") {
    size_t skip = (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
                   (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) ? 3 : 0;
    utf8 = bytes.substr(skip);
    if (!IsValidUtf8(utf8.substr(0, std::min<size_t>(utf8.size(), 1u << 20)))) return false;
  } else if (enc == "UTF-16LE" || enc == "UTF-16BE") {
    std::wstring w = Utf16ToWide(bytes, enc == "UTF-16LE");
    if (w.empty()) return false;
    utf8 = W2U(w);
  } else if (enc == "GBK" || enc == "GB18030") {
    int n = MultiByteToWideChar(CP_GBK, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (n <= 0) return false;
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_GBK, 0, bytes.data(), (int)bytes.size(), w.data(), n);
    utf8 = W2U(w);
  } else {
    return false;
  }
  if (utf8.empty()) return false;
  out = NormalizeNewlines(utf8);
  return true;
}

bool TxtBook::Load(const std::wstring& path, const TxtSplitRule& rule, std::string& err) {
  pathW_ = path;
  path_ = W2U(path);
  raw_.clear();
  if (!ReadFileBytes(path, raw_)) { err = "无法读取文件（可能已被移动或删除）"; return false; }
  if (raw_.empty()) { err = "文件是空的"; return false; }

  rule_ = rule;
  std::string enc = rule.encoding;
  if (enc.empty() || enc == "auto") enc = DetectEncoding(raw_);
  std::string utf8;
  if (!DecodeToUtf8(raw_, enc, utf8)) {
    err = "文本解码失败，请在「工具 → 章节识别」里手动指定编码";
    return false;
  }
  enc_ = enc;
  text_ = std::move(utf8);
  title_ = BaseNameNoExt(path);
  if (title_.empty()) title_ = "未命名";
  Split();
  if (chapters_.empty()) { err = "没有解析出可阅读的内容"; return false; }
  return true;
}

void TxtBook::Resplit(const TxtSplitRule& rule) {
  rule_ = rule;
  if (rule_.encoding.empty() || rule_.encoding == "auto") {
    enc_ = DetectEncoding(raw_);
  } else if (rule_.encoding != enc_) {
    std::string utf8;
    if (DecodeToUtf8(raw_, rule_.encoding, utf8)) {
      enc_ = rule_.encoding;
      text_ = std::move(utf8);
    }
  }
  Split();
}

void TxtBook::Split() {
  chapters_.clear();

  // 用宽字符正则：UTF-8 字节串下 '.' 只匹配 1 个字节，中文标题会匹配不上
  std::wregex custom;
  const std::wregex* customPtr = nullptr;
  if (!rule_.regex.empty()) {
    try {
      custom = std::wregex(U2W(rule_.regex), std::regex::ECMAScript);
      customPtr = &custom;
    } catch (...) {
      customPtr = nullptr;
    }
  }

  struct Head { size_t start; size_t end; std::string title; };
  std::vector<Head> heads;
  size_t lineStart = 0;
  while (lineStart <= text_.size()) {
    size_t nl = text_.find('\n', lineStart);
    size_t lineEnd = (nl == std::string::npos) ? text_.size() : nl;
    std::string line = text_.substr(lineStart, lineEnd - lineStart);
    std::string trimmed = TrimUtf8(line);
    // 用户填了正则就只用用户的规则（留空才用内置规则）
    bool hit = false;
    std::string title = trimmed;
    if (customPtr) {
      try {
        const std::wstring wline = U2W(trimmed);
        std::wsmatch m;
        if (std::regex_search(wline, m, *customPtr)) {
          hit = true;
          if (m.size() > 1 && m[1].matched) {
            std::string cap = TrimUtf8(W2U(m[1].str()));
            if (!cap.empty()) title = cap;
          }
        }
      } catch (...) { hit = false; }
    } else {
      hit = HeadingByRule(trimmed);
    }
    if (hit) {
      Head h;
      h.start = lineStart;
      h.end = (nl == std::string::npos) ? text_.size() : nl + 1;
      h.title = title;
      heads.push_back(std::move(h));
    }
    if (nl == std::string::npos) break;
    lineStart = nl + 1;
  }

  if (heads.empty()) {
    TxtChapterRef r;
    r.title = title_.empty() ? std::string("全文") : title_;
    r.bodyStart = 0;
    r.bodyEnd = text_.size();
    chapters_.push_back(std::move(r));
    return;
  }

  if (heads[0].start > 0 && RangeHasRealText(text_, 0, heads[0].start)) {
    TxtChapterRef r;
    r.title = "前言";
    r.bodyStart = 0;
    r.bodyEnd = heads[0].start;
    chapters_.push_back(std::move(r));
  }

  for (size_t i = 0; i < heads.size(); ++i) {
    TxtChapterRef r;
    r.title = heads[i].title;
    r.bodyStart = heads[i].end;
    r.bodyEnd = (i + 1 < heads.size()) ? heads[i + 1].start : text_.size();
    chapters_.push_back(std::move(r));
  }
}

bool TxtBook::GetChapter(int index, std::vector<std::string>& paragraphs) const {
  paragraphs.clear();
  if (index < 0 || index >= (int)chapters_.size()) return false;
  const TxtChapterRef& ref = chapters_[index];
  size_t pos = ref.bodyStart;
  while (pos < ref.bodyEnd) {
    size_t nl = text_.find('\n', pos);
    size_t lineEnd = (nl == std::string::npos || nl > ref.bodyEnd) ? ref.bodyEnd : nl;
    std::string line = TrimUtf8(text_.substr(pos, lineEnd - pos));
    if (!line.empty() && !IsWatermarkLine(line)) paragraphs.push_back(std::move(line));
    if (nl == std::string::npos || nl >= ref.bodyEnd) break;
    pos = nl + 1;
  }
  return true;
}
