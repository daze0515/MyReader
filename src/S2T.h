#pragma once
#include "Common.h"

// 简繁转换（表数据见 S2T.cpp，由 web/s2t.js 生成，和前端保持同一套映射）
std::string ToSimplifiedUtf8(const std::string& s);
std::string ToTraditionalUtf8(const std::string& s);
