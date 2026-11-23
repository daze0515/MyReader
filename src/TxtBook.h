#pragma once
#include "LocalBook.h"

// 本地 TXT 小说：原地引用文件，不复制；按章节标题切分后惰性取正文。
struct TxtChapterRef {
  std::string title;
  size_t bodyStart = 0;   // text_ 中的字节偏移
  size_t bodyEnd = 0;
};

struct TxtSplitRule {
  std::string encoding = "auto";  // auto | UTF-8 | GBK | UTF-16LE | UTF-16BE
  std::string regex;              // 自定义章节正则（按行匹配）；非空则只用它，空则用内置规则
                                  // 有捕获组时取第 1 组作章节名，否则整行作章节名
};

class TxtBook : public LocalBook {
public:
  // 读文件 + 探测编码 + 切分章节。失败时 err 带原因。
  bool Load(const std::wstring& path, const TxtSplitRule& rule, std::string& err);
  // 换规则重新切分（不重新读文件）
  void Resplit(const TxtSplitRule& rule);

  const char* Format() const override { return "txt"; }
  const std::string& Path() const { return path_; }
  const std::wstring& PathW() const override { return pathW_; }
  const std::string& Title() const override { return title_; }
  const std::string& Encoding() const { return enc_; }
  const std::string& Rule() const { return rule_.regex; }
  const std::string& RequestedEncoding() const { return rule_.encoding; }
  const std::vector<TxtChapterRef>& Chapters() const { return chapters_; }
  int ChapterCount() const override { return (int)chapters_.size(); }
  std::string ChapterTitle(int index) const override {
    return (index >= 0 && index < (int)chapters_.size()) ? chapters_[(size_t)index].title : std::string();
  }

  bool GetChapter(int index, std::vector<std::string>& paragraphs) const override;

  static std::string DetectEncoding(const std::string& bytes);
  static bool DecodeToUtf8(const std::string& bytes, const std::string& enc, std::string& out);
  static std::vector<std::string> EncodingChoices();

private:
  void Split();

  std::wstring pathW_;
  std::string path_;
  std::string raw_;
  std::string enc_;
  TxtSplitRule rule_;
  std::string text_;
  std::string title_;
  std::vector<TxtChapterRef> chapters_;
};

// 内置章节标题判定（不含自定义正则），供测试与 UI 提示复用。
bool TxtLineLooksLikeHeading(const std::string& line);
// 去掉首尾空白（半角 + 全角空格 + ）
std::string TrimUtf8(const std::string& s);
