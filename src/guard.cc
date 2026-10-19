#include "guard.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "include/cef_parser.h"

namespace poca {

namespace {

bool StartsWith(const std::string& s, const char* prefix) {
  return s.rfind(prefix, 0) == 0;
}

bool IsLoopbackHost(const std::string& host) {
  return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

}  // namespace

bool IsAllowedUrl(const std::string& url) {
  std::string lower = url;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (StartsWith(lower, "data:") || StartsWith(lower, "blob:") ||
      lower == "about:blank" || lower == "about:srcdoc") {
    return true;
  }

  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return false;
  }
  const std::string scheme = CefString(&parts.scheme).ToString();
  const std::string host = CefString(&parts.host).ToString();
  if (scheme == "poca") {
    return true;
  }
  if (scheme == "http" || scheme == "https" || scheme == "ws" ||
      scheme == "wss") {
    return IsLoopbackHost(host);
  }
  return false;
}

int LoopbackPort(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return 0;
  }
  const std::string scheme = CefString(&parts.scheme).ToString();
  const bool plain = scheme == "http" || scheme == "ws";
  if ((!plain && scheme != "https" && scheme != "wss") ||
      !IsLoopbackHost(CefString(&parts.host).ToString())) {
    return 0;
  }
  const std::string port = CefString(&parts.port).ToString();
  if (port.empty()) {
    return plain ? 80 : 443;
  }
  return std::atoi(port.c_str());
}

std::string BlockedPageUrl(const std::string& url) {
  return "poca://blocked/?url=" + CefURIEncode(url, false).ToString();
}

}  // namespace poca
