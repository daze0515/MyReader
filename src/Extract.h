#pragma once
#include "Common.h"
#include "ExtractTypes.h"
#include "Html.h"

// 正文水印广告行（站方随机插入，字符合法但整行是广告）
bool IsWatermarkLine(const std::string& text);
std::string CleanText(const std::string& in);
bool HasContent(const std::string& s);
std::string NormalizeUrl(const std::string& base, const std::string& href);
