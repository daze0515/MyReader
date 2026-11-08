#pragma once
#include "Common.h"

struct ProxyConfig {
  enum class Mode { System, Manual, None };
  Mode mode = Mode::System;
  std::wstring manual;  // e.g. http://127.0.0.1:7890 or socks5://127.0.0.1:1080
};

struct HttpResult {
  bool ok = false;
  int status = 0;
  std::string body;
  std::string error;
};

// Reads WinINET proxy settings (HKCU Internet Settings).
std::wstring ReadSystemProxy();

// Resolves the effective proxy URL for a request, or empty for direct.
std::wstring ResolveProxy(const ProxyConfig& cfg);

HttpResult HttpGet(const std::wstring& url,
                   const std::wstring& userAgent,
                   const std::wstring& cookieHeader,
                   const std::wstring& proxyUrl,
                   int timeoutMs = 20000);
