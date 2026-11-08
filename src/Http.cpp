#include "Http.h"
#include <winhttp.h>

static std::wstring PickProxyEntry(const std::wstring& server, const wchar_t* scheme) {
  if (server.find(L'=') == std::wstring::npos) return server;
  std::wstring want = std::wstring(scheme) + L"=";
  size_t pos = 0;
  while (pos <= server.size()) {
    size_t semi = server.find(L';', pos);
    std::wstring item = (semi == std::wstring::npos) ? server.substr(pos) : server.substr(pos, semi - pos);
    if (item.size() >= want.size() && item.compare(0, want.size(), want) == 0) return item.substr(want.size());
    if (semi == std::wstring::npos) break;
    pos = semi + 1;
  }
  return L"";
}

std::wstring ReadSystemProxy() {
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
        0, KEY_READ, &k) != ERROR_SUCCESS) return L"";
  DWORD enable = 0, cb = sizeof(enable), type = 0;
  if (RegQueryValueExW(k, L"ProxyEnable", nullptr, &type, (LPBYTE)&enable, &cb) != ERROR_SUCCESS) enable = 0;
  std::wstring server;
  if (enable) {
    wchar_t buf[2048] = {};
    DWORD n = (DWORD)(sizeof(buf) - sizeof(wchar_t));
    if (RegQueryValueExW(k, L"ProxyServer", nullptr, &type, (LPBYTE)buf, &n) == ERROR_SUCCESS) server = buf;
  }
  RegCloseKey(k);
  return server;
}

std::wstring ResolveProxy(const ProxyConfig& cfg) {
  if (cfg.mode == ProxyConfig::Mode::None) return L"";
  if (cfg.mode == ProxyConfig::Mode::Manual) return cfg.manual;
  std::wstring s = ReadSystemProxy();
  if (s.empty()) return L"";
  std::wstring http = PickProxyEntry(s, L"http");
  if (http.empty()) http = PickProxyEntry(s, L"https");
  if (http.empty()) http = s;
  if (http.find(L"://") == std::wstring::npos) http = L"http://" + http;
  return http;
}

static bool HostIsSecure(const std::wstring& scheme) {
  return scheme == L"https" || scheme == L"wss";
}

HttpResult HttpGet(const std::wstring& url,
                   const std::wstring& userAgent,
                   const std::wstring& cookieHeader,
                   const std::wstring& proxyUrl,
                   int timeoutMs) {
  HttpResult res;

  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.dwSchemeLength = (DWORD)-1;
  uc.dwHostNameLength = (DWORD)-1;
  uc.dwUrlPathLength = (DWORD)-1;
  uc.dwExtraInfoLength = (DWORD)-1;
  if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) {
    res.error = "bad url";
    return res;
  }
  std::wstring scheme(uc.lpszScheme, uc.dwSchemeLength);
  std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
  std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (uc.dwExtraInfoLength) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  if (path.empty()) path = L"/";

  HINTERNET hSession = WinHttpOpen(userAgent.empty() ? L"Mozilla/5.0" : userAgent.c_str(),
                                   WINHTTP_ACCESS_TYPE_NO_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) { res.error = "WinHttpOpen failed"; return res; }

  if (!proxyUrl.empty()) {
    std::wstring p = proxyUrl;
    if (p.compare(0, 7, L"http://") == 0) p = p.substr(7);
    WINHTTP_PROXY_INFO pi{};
    pi.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    pi.lpszProxy = (LPWSTR)p.c_str();
    WinHttpSetOption(hSession, WINHTTP_OPTION_PROXY, &pi, sizeof(pi));
  }

  int timeout = timeoutMs;
  WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
  WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
  WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
  if (!hConnect) {
    res.error = "connect failed";
    WinHttpCloseHandle(hSession);
    return res;
  }

  DWORD flags = HostIsSecure(scheme) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    res.error = "open request failed";
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }

  DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

  std::wstring headers = L"Accept: text/html,application/xhtml+xml,*/*;q=0.8\r\n";
  headers += L"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n";
  headers += L"Accept-Encoding: identity\r\n";
  if (!cookieHeader.empty()) headers += L"Cookie: " + cookieHeader + L"\r\n";

  BOOL sent = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (!sent) {
    res.error = "send failed";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }
  if (!WinHttpReceiveResponse(hRequest, nullptr)) {
    res.error = "receive failed";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }

  DWORD status = 0, scb = sizeof(status);
  WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &scb, WINHTTP_NO_HEADER_INDEX);
  res.status = (int)status;

  DWORD avail = 0;
  while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
    std::string chunk;
    chunk.resize(avail);
    DWORD read = 0;
    if (!WinHttpReadData(hRequest, chunk.data(), avail, &read) || read == 0) break;
    chunk.resize(read);
    res.body += chunk;
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  res.ok = true;
  return res;
}
