#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <cstdint>

// UTF-8 <-> UTF-16
std::string W2U(const std::wstring& w);
std::wstring U2W(const std::string& s);

std::string TrimAscii(const std::string& s);
bool StartsWith(const std::string& s, const std::string& p);
bool EndsWith(const std::string& s, const std::string& p);
std::string ToLowerAscii(const std::string& s);
std::vector<std::string> SplitLines(const std::string& s);

// Wide-string helpers
std::wstring TrimW(const std::wstring& s);
bool StartsWithW(const std::wstring& s, const std::wstring& p);

// Filesystem
std::wstring ModuleDir();
bool ReadFileBytes(const std::wstring& path, std::string& out);
bool WriteFileBytes(const std::wstring& path, const std::string& data);
bool EnsureDir(const std::wstring& path);

// UTF-8 codepoint iteration
struct Utf8Decoded { uint32_t cp; size_t bytes; };
Utf8Decoded DecodeUtf8(const std::string& s, size_t i);
void AppendUtf8(std::string& out, uint32_t cp);
