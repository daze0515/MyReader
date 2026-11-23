#pragma once
#include "Common.h"
#include <cwchar>
#include <cwctype>
#include <string_view>

// 本地书（TXT / EPUB）的统一接口：宿主只关心「有几章、每章叫什么、正文是哪些段落」，
// 阅读页、进度记忆、导出 TXT 都只走这几个函数。
class LocalBook {
 public:
  virtual ~LocalBook() = default;
  virtual const char* Format() const = 0;
  virtual const std::wstring& PathW() const = 0;
  virtual const std::string& Title() const = 0;
  virtual int ChapterCount() const = 0;
  virtual std::string ChapterTitle(int index) const = 0;
  virtual bool GetChapter(int index, std::vector<std::string>& paragraphs) const = 0;
  virtual std::string Category() const { return "本地 TXT"; }
  virtual std::string Author() const { return {}; }
  virtual std::string_view CoverBytes() const { return {}; }
  virtual bool ImageSize(const std::string&, int&, int&) const { return false; }
  virtual bool ReadResource(const std::string&, std::string&, std::string&) const { return false; }
};

// 正文里的插图用一个特殊「段落」表示：\x1E img \x1F <包内路径> \x1F <alt>
// 宿主发给阅读页前换成 {img, alt, w, h} 对象；导出 TXT 时跳过。正常文字里不会出现这两个控制字符。
inline const std::string& LocalImageMark() {
  static const std::string k = std::string(1, '\x1E') + "img" + std::string(1, '\x1F');
  return k;
}
inline std::string MakeLocalImagePara(const std::string& path, const std::string& alt) {
  return LocalImageMark() + path + std::string(1, '\x1F') + alt;
}
inline bool IsLocalImagePara(const std::string& p) {
  return p.compare(0, LocalImageMark().size(), LocalImageMark()) == 0;
}
inline bool ParseLocalImagePara(const std::string& p, std::string* path, std::string* alt) {
  if (!IsLocalImagePara(p)) return false;
  const std::string rest = p.substr(LocalImageMark().size());
  const size_t sep = rest.find('\x1F');
  if (path) *path = rest.substr(0, sep);
  if (alt) *alt = (sep == std::string::npos) ? std::string() : rest.substr(sep + 1);
  return true;
}

inline bool LocalPathHasExt(const std::wstring& path, const wchar_t* ext) {
  const size_t n = wcslen(ext);
  if (path.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (towlower(path[path.size() - n + i]) != towlower(ext[i])) return false;
  }
  return true;
}
inline bool IsEpubPath(const std::wstring& path) { return LocalPathHasExt(path, L".epub"); }
inline bool IsTxtPath(const std::wstring& path) { return LocalPathHasExt(path, L".txt"); }
inline bool IsMobiPath(const std::wstring& path) {
  return LocalPathHasExt(path, L".mobi") || LocalPathHasExt(path, L".azw3") ||
         LocalPathHasExt(path, L".azw");
}
inline bool IsSupportedLocalPath(const std::wstring& path) {
  return IsTxtPath(path) || IsEpubPath(path) || IsMobiPath(path);
}
