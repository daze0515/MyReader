#include "Export.h"
#include "S2T.h"

#ifndef CP_GBK
#define CP_GBK 936
#endif

const char* TextEncodingId(TextEncoding e) {
  switch (e) {
    case TextEncoding::Utf8Bom: return "utf8bom";
    case TextEncoding::Utf8: return "utf8";
    case TextEncoding::Gbk: return "gbk";
  }
  return "utf8bom";
}

bool ParseTextEncoding(const std::string& id, TextEncoding& out) {
  if (id == "utf8bom" || id == "utf-8-bom" || id == "bom") { out = TextEncoding::Utf8Bom; return true; }
  if (id == "utf8" || id == "utf-8") { out = TextEncoding::Utf8; return true; }
  if (id == "gbk" || id == "gb18030" || id == "cp936") { out = TextEncoding::Gbk; return true; }
  return false;
}

std::string BuildMergedText(const std::vector<ExportChapter>& chapters, bool includeTitles, bool toSimplified) {
  std::string out;
  out.reserve(1u << 20);
  for (size_t i = 0; i < chapters.size(); ++i) {
    const ExportChapter& c = chapters[i];
    if (i) out += "\n";
    if (includeTitles) {
      std::string title = c.title;
      if (title.empty()) title = "第 " + std::to_string(i + 1) + " 章";
      out += title;
      out += "\n\n";
    }
    for (const auto& p : c.paragraphs) {
      out += p;
      out += "\n";
    }
  }
  if (toSimplified) out = ToSimplifiedUtf8(out);
  return out;
}

std::string EncodeText(const std::string& utf8, TextEncoding enc) {
  if (enc == TextEncoding::Utf8 || enc == TextEncoding::Utf8Bom) {
    std::string out;
    if (enc == TextEncoding::Utf8Bom) out.append("\xEF\xBB\xBF", 3);
    out += utf8;
    return out;
  }
  std::wstring w = U2W(utf8);
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_GBK, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out((size_t)n, '\0');
  WideCharToMultiByte(CP_GBK, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
  return out;
}

bool WriteTextFile(const std::wstring& path, const std::string& bytes, std::string& err) {
  if (bytes.empty()) { err = "导出内容为空"; return false; }
  if (!WriteFileBytes(path, bytes)) { err = "写入失败，目标文件可能被占用"; return false; }
  return true;
}
