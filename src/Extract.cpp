#include "Extract.h"
#include <algorithm>
#include <cctype>
#include <cstring>

static bool IsCjk(uint32_t cp) {
  return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2FA1F);
}
static bool IsAsciiAlnum(uint32_t cp) {
  return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
}
static bool IsSpaceCp(uint32_t cp) {
  return cp == ' ' || cp == 0x09 || cp == 0x0A || cp == 0x0D || cp == 0x0C ||
         (cp >= 0x2000 && cp <= 0x200A) || cp == 0x00A0 || cp == 0x3000;
}
static bool IsCjkPunct(uint32_t cp) {
  return (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0x2010 && cp <= 0x2027) ||
         (cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) ||
         (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65);
}
static bool IsAllowedCp(uint32_t cp) {
  if (cp < 0x80) return cp >= 0x20 && cp != 0x7F;
  if (IsCjk(cp)) return true;
  if (IsCjkPunct(cp)) return true;
  if (cp >= 0x2030 && cp <= 0x205E) return true;
  if (cp >= 0xFE10 && cp <= 0xFE1F) return true;
  if (cp >= 0x2E80 && cp <= 0x2EFF) return true;
  if (cp >= 0x3100 && cp <= 0x312F) return true;
  if (cp >= 0x31C0 && cp <= 0x31EF) return true;
  if (cp == 0x00B7 || cp == 0x00D7) return true;
  return false;
}

static bool TldOk(const std::string& t) {
  static const char* kTlds[] = { "com","net","org","cc","xyz","top","vip","info","cn","tw","me","io",
                                 "tv","co","la","us","uk","site","online","shop","club","app","fun","live",
                                 "pro","biz","name","wang","ren","pub","art","store","tech","space","website" };
  for (auto t2 : kTlds) if (t == t2) return true;
  return false;
}

static std::string StripDomains(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i];
    if (isalnum(c)) {
      size_t j = i;
      int dots = 0;
      while (j < s.size()) {
        unsigned char d = (unsigned char)s[j];
        if (isalnum(d) || d == '-' || d == '_') { ++j; continue; }
        if (d == '.' && j + 1 < s.size() && isalnum((unsigned char)s[j + 1])) { ++dots; ++j; continue; }
        break;
      }
      std::string tok = s.substr(i, j - i);
      size_t lastDot = tok.rfind('.');
      if (dots > 0 && lastDot != std::string::npos && lastDot + 1 < tok.size()) {
        std::string tld = ToLowerAscii(tok.substr(lastDot + 1));
        if (TldOk(tld)) { i = j; continue; }
      }
      out += tok;
      i = j;
      continue;
    }
    out.push_back(s[i++]);
  }
  return out;
}

static std::string StripKeywords(const std::string& s) {
  static const char* kAscii[] = { "69shux", "shux", "twkan", "www" };
  static const char* kCjk[] = { "69书吧", "69書吧", "台灣小說網", "台湾小说网", "無防盜", "无防盗",
                                "請記住本站", "请记住本站", "最新網址", "最新网址", "無彈窗", "无弹窗",
                                "手機版", "手机版", "內容轉載", "内容转载", "無錯小說", "无错小说" };
  std::string out = s;
  for (auto kw : kAscii) {
    std::string lower = ToLowerAscii(out);
    std::string k = kw;
    size_t pos = 0;
    while ((pos = lower.find(k, pos)) != std::string::npos) {
      out.erase(pos, k.size());
      lower.erase(pos, k.size());
    }
  }
  for (auto kw : kCjk) {
    std::string k = kw;
    size_t pos = 0;
    while ((pos = out.find(k, pos)) != std::string::npos) out.erase(pos, k.size());
  }
  return out;
}

static bool IsAsciiPunctChar(char c) {
  static const char* kP = ".,-_~^|/:;'\"!@#$%&*()[]{}<>?+=";
  return c != 0 && strchr(kP, c) != nullptr;
}

// Drop standalone ASCII punctuation left behind after watermark removal.
static std::string StripOrphanPunct(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    bool boundaryBefore = (i == 0) || s[i - 1] == ' ';
    if (boundaryBefore) {
      size_t k = i;
      while (k < s.size() && IsAsciiPunctChar(s[k])) ++k;
      size_t run = k - i;
      bool boundaryAfter = (k >= s.size()) || s[k] == ' ';
      if (run > 0 && run <= 3 && boundaryAfter) {
        if (!out.empty() && out.back() == ' ') out.pop_back();
        i = k;
        continue;
      }
    }
    out.push_back(s[i++]);
  }
  return out;
}

std::string CleanText(const std::string& in) {
  std::string kept;
  kept.reserve(in.size());
  size_t i = 0;
  while (i < in.size()) {
    Utf8Decoded d = DecodeUtf8(in, i);
    i += d.bytes;
    if (d.cp == 0xFFFD) continue;
    if (IsSpaceCp(d.cp)) { kept.push_back(' '); continue; }
    if (!IsAllowedCp(d.cp)) continue;
    AppendUtf8(kept, d.cp);
  }
  kept = StripOrphanPunct(StripKeywords(StripDomains(kept)));

  std::string out;
  out.reserve(kept.size());
  uint32_t lastCp = 0;
  size_t k = 0;
  while (k < kept.size()) {
    Utf8Decoded d = DecodeUtf8(kept, k);
    k += d.bytes;
    if (d.cp == ' ') {
      if (out.empty() || lastCp == ' ') continue;
      uint32_t nextCp = 0;
      if (k < kept.size()) nextCp = DecodeUtf8(kept, k).cp;
      if (IsCjkPunct(lastCp) || IsCjkPunct(nextCp)) continue;
      out.push_back(' ');
      lastCp = ' ';
      continue;
    }
    AppendUtf8(out, d.cp);
    lastCp = d.cp;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

bool HasContent(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    Utf8Decoded d = DecodeUtf8(s, i);
    i += d.bytes;
    if (IsCjk(d.cp) || IsAsciiAlnum(d.cp)) return true;
  }
  return false;
}

std::string NormalizeUrl(const std::string& base, const std::string& href) {
  std::string h = TrimAscii(href);
  if (h.empty()) return "";
  if (h.compare(0, 7, "http://") == 0 || h.compare(0, 8, "https://") == 0) return h;
  if (h.compare(0, 2, "//") == 0) {
    size_t s = base.find("://");
    if (s == std::string::npos) return "https:" + h;
    return base.substr(0, s) + ":" + h;
  }
  size_t schemeEnd = base.find("://");
  if (schemeEnd == std::string::npos) return h;
  size_t hostEnd = base.find('/', schemeEnd + 3);
  std::string origin = (hostEnd == std::string::npos) ? base : base.substr(0, hostEnd);
  if (h[0] == '/') return origin + h;
  std::string dir = base;
  size_t q = dir.find_first_of("?#");
  if (q != std::string::npos) dir = dir.substr(0, q);
  size_t slash = dir.find_last_of('/');
  dir = (slash == std::string::npos) ? origin + "/" : dir.substr(0, slash + 1);
  return dir + h;
}

// 站方会在正文里随机插一行水印广告，用填充符把关键词拆开，例如
//   「無一錯一首一發一內一容一在一一看！」        ← 「一」当填充符（twkan 实际用法）
//   「无—错—首—发—内—容—在—看！」              ← 破折号当填充符
//   「本書由.全網首發」
//   「本書首發 超便捷，.隨時享 ,提供給你無錯章節，無亂序章節的閱讀體驗」
// 字符本身都是合法汉字，白名单清洗拦不住，只能按关键词 + 行长度识别。
static std::string StripWatermarkNoise(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    Utf8Decoded d = DecodeUtf8(s, i);
    i += d.bytes;
    const uint32_t cp = d.cp;
    if (cp == 0x20 || cp == 0x09 || cp == 0x3000) continue;         // 半角/全角空格
    if (cp >= 0x2010 && cp <= 0x2015) continue;                     // ‐ ‑ ‒ – — ―
    if (cp == 0x2212 || cp == 0x2500 || cp == 0x2501) continue;     // − ─ ━
    if (cp == 0x00B7 || cp == 0x2022 || cp == 0x2027) continue;     // · • ‧
    if (cp == 0x2E || cp == 0x3002 || cp == 0xFF0E) continue;       // . 。 ．
    if (cp == 0x2C || cp == 0x3001 || cp == 0xFF0C) continue;       // , 、 ，
    AppendUtf8(out, cp);
  }
  return out;
}

// 有些水印拿「一」当填充符插在关键词中间（無一錯一首一發…），
// 只有整行「一」足够多时才剥掉，免得误伤正常正文里的「一」。
static std::string StripFillerOne(const std::string& s, int* count) {
  std::string out;
  out.reserve(s.size());
  int n = 0;
  for (size_t i = 0; i < s.size();) {
    Utf8Decoded d = DecodeUtf8(s, i);
    i += d.bytes;
    if (d.cp == 0x4E00) { ++n; continue; }
    AppendUtf8(out, d.cp);
  }
  if (count) *count = n;
  return out;
}

static bool HasWatermarkMark(const std::string& n) {
  static const char* kMarks[] = {
    "无错首发", "無錯首發", "无错小说", "無錯小說", "无错章节", "無錯章節",
    "内容在看", "內容在看", "无乱序", "無亂序", "乱序章节", "亂序章節",
    "本书首发", "本書首發", "首发地址", "首發地址", "首发于", "首發於",
    "全网首发", "全網首發", "本书由", "本書由",
    "本文首发", "本文首發", "请记住本站", "請記住本站", "记住本站", "一秒记住",
    "超便捷", "随时享", "隨時享", "阅读体验", "閱讀體驗", "提供给你", "提供給你",
    "最新章节", "最新章節", "更新最快", "无弹窗", "無彈窗", "免费阅读", "免費閱讀",
    "请收藏", "請收藏", "收藏本站", "手机阅读", "手機閱讀", "手机用户", "手機用戶",
    "台湾小说网", "台灣小說網", "twkan", "69shu", "69书吧", "69書吧",
    "笔趣", "筆趣", "顶点小说", "頂點小說", "域名", "网址是", "網址是",
  };
  for (const char* m : kMarks) {
    if (n.find(m) != std::string::npos) return true;
  }
  return false;
}

// 关键词被拆得太散的广告凑不出完整词组，改用「广告专用词命中数」判定。
static int PromoHits(const std::string& n) {
  static const char* kWeak[] = {
    "首发", "首發", "无错", "無錯", "全网", "全網", "乱序", "亂序", "便捷",
    "随时享", "隨時享", "记住本站", "記住本站", "无弹窗", "無彈窗", "手机阅读", "手機閱讀",
    "收藏本站", "请收藏", "請收藏", "一秒记住", "免费阅读", "免費閱讀",
  };
  int hits = 0;
  for (const char* w : kWeak) {
    if (n.find(w) != std::string::npos) ++hits;
  }
  return hits;
}

bool IsWatermarkLine(const std::string& text) {
  const std::string n = StripWatermarkNoise(text);
  if (n.empty() || n.size() > 96) return false;   // 96 字节 ≈ 32 个汉字，水印行都很短
  if (HasWatermarkMark(n)) return true;
  int ones = 0;
  const std::string stripped = StripFillerOne(n, &ones);
  if (ones >= 3 && HasWatermarkMark(stripped)) return true;
  return PromoHits(stripped) >= 3;
}
