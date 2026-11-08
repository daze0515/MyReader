#pragma once
#include "Common.h"

// 导出用的一章：标题 + 正文段落
struct ExportChapter {
  std::string title;
  std::vector<std::string> paragraphs;
};

enum class TextEncoding { Utf8Bom, Utf8, Gbk };

const char* TextEncodingId(TextEncoding e);
bool ParseTextEncoding(const std::string& id, TextEncoding& out);

// 合并成整本 UTF-8 文本：每章「标题 + 空行 + 段落」，章与章之间空行分隔
std::string BuildMergedText(const std::vector<ExportChapter>& chapters, bool includeTitles, bool toSimplified);

// UTF-8 -> 目标编码字节（GBK 走 CP936；不可映射字符会退化成 ?）
std::string EncodeText(const std::string& utf8, TextEncoding enc);

bool WriteTextFile(const std::wstring& path, const std::string& bytes, std::string& err);
