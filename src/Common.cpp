#include "Common.h"
#include <shlwapi.h>

std::string W2U(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string out((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring U2W(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring out((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
  return out;
}

std::string TrimAscii(const std::string& s) {
  size_t b = 0, e = s.size();
  auto ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; };
  while (b < e && ws((unsigned char)s[b])) ++b;
  while (e > b && ws((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}

bool StartsWith(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
bool EndsWith(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
std::string ToLowerAscii(const std::string& s) {
  std::string o = s;
  for (auto& c : o) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return o;
}
std::vector<std::string> SplitLines(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '\n') { out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

std::wstring TrimW(const std::wstring& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) ++b;
  while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) --e;
  return s.substr(b, e - b);
}
bool StartsWithW(const std::wstring& s, const std::wstring& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::wstring ModuleDir() {
  wchar_t buf[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring p = buf;
  size_t i = p.find_last_of(L"\\/");
  return i == std::wstring::npos ? L"." : p.substr(0, i);
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  GetFileSizeEx(h, &sz);
  out.resize((size_t)sz.QuadPart);
  DWORD read = 0;
  BOOL ok = out.empty() ? TRUE : ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr);
  CloseHandle(h);
  if (!ok) return false;
  out.resize(read);
  return true;
}

bool WriteFileBytes(const std::wstring& path, const std::string& data) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = data.empty() ? TRUE : WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr);
  CloseHandle(h);
  return ok && written == data.size();
}

bool EnsureDir(const std::wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
  return CreateDirectoryW(path.c_str(), nullptr) != 0;
}

Utf8Decoded DecodeUtf8(const std::string& s, size_t i) {
  unsigned char c = (unsigned char)s[i];
  if (c < 0x80) return { c, 1 };
  int n = 0; uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
  else return { 0xFFFD, 1 };
  if (i + n >= s.size()) return { 0xFFFD, 1 };
  for (int k = 1; k <= n; ++k) {
    unsigned char cc = (unsigned char)s[i + k];
    if ((cc & 0xC0) != 0x80) return { 0xFFFD, 1 };
    cp = (cp << 6) | (cc & 0x3F);
  }
  return { cp, (size_t)(n + 1) };
}

void AppendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) out.push_back((char)cp);
  else if (cp < 0x800) {
    out.push_back((char)(0xC0 | (cp >> 6)));
    out.push_back((char)(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back((char)(0xE0 | (cp >> 12)));
    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back((char)(0x80 | (cp & 0x3F)));
  } else {
    out.push_back((char)(0xF0 | (cp >> 18)));
    out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back((char)(0x80 | (cp & 0x3F)));
  }
}
