#include "scheme.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>

#include "include/cef_parser.h"
#include "include/wrapper/cef_stream_resource_handler.h"

#include "programs.h"

namespace poca {

namespace {

class StringReader : public CefReadHandler {
 public:
  explicit StringReader(std::string data) : data_(std::move(data)) {}

  size_t Read(void* ptr, size_t size, size_t n) override {
    const size_t count = std::min(n, (data_.size() - pos_) / size);
    std::memcpy(ptr, data_.data() + pos_, count * size);
    pos_ += count * size;
    return count;
  }
  int Seek(int64_t offset, int whence) override {
    int64_t target = whence == SEEK_SET   ? offset
                     : whence == SEEK_CUR ? pos_ + offset
                                          : data_.size() + offset;
    if (target < 0 || target > static_cast<int64_t>(data_.size())) {
      return -1;
    }
    pos_ = static_cast<size_t>(target);
    return 0;
  }
  int64_t Tell() override { return pos_; }
  int Eof() override { return pos_ >= data_.size(); }
  bool MayBlock() override { return false; }

 private:
  const std::string data_;
  size_t pos_ = 0;

  IMPLEMENT_REFCOUNTING(StringReader);
};

// The pages Poca has; poca://<page>/ is <page>.html.
bool IsPage(const std::string& host) {
  return host == "home" || host == "box" || host == "blocked";
}

// One plain file name: no directories, no leading dot.
bool IsPlainName(const std::string& name) {
  if (name.empty() || name[0] == '.') {
    return false;
  }
  for (char c : name) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
          c == '-' || c == '_')) {
      return false;
    }
  }
  return true;
}

std::string MimeType(const std::string& name) {
  const std::string ext = std::filesystem::path(name).extension().string();
  if (ext == ".html") return "text/html";
  if (ext == ".css") return "text/css";
  if (ext == ".js") return "text/javascript";
  if (ext == ".json") return "application/json";
  if (ext == ".png") return "image/png";
  if (ext == ".svg") return "image/svg+xml";
  return "application/octet-stream";
}

CefRefPtr<CefResourceHandler> ServeFile(const std::string& path,
                                        const std::string& mime) {
  CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForFile(path);
  if (!stream) {
    return nullptr;
  }
  CefResponse::HeaderMap headers;
  headers.insert({"Cache-Control", "no-store"});
  return new CefStreamResourceHandler(200, "OK", mime, headers, stream);
}

// A JSON answer made on the spot.
CefRefPtr<CefResourceHandler> ServeJson(int status, const std::string& json) {
  CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForHandler(
      new StringReader(json));
  CefResponse::HeaderMap headers;
  headers.insert({"Cache-Control", "no-store"});
  return new CefStreamResourceHandler(status, status == 200 ? "OK" : "Error",
                                      "application/json", headers, stream);
}

// start / stop: a POST from the box page itself, so no other page (a
// localhost page included) can start or stop programs.
CefRefPtr<CefResourceHandler> ServeProgramAction(
    CefRefPtr<CefRequest> request,
    const std::string& action,
    const std::string& query) {
  if (request->GetMethod() != "POST" ||
      request->GetFirstPartyForCookies().ToString() != "poca://box/" ||
      request->GetHeaderByName("Origin").ToString() != "poca://box") {
    return ServeJson(403, "{\"error\":\"only the box page may do this\"}");
  }
  std::string name;
  if (query.rfind("name=", 0) == 0) {
    name = CefURIDecode(query.substr(5), true,
                        static_cast<cef_uri_unescape_rule_t>(
                            UU_SPACES | UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS))
               .ToString();
  }
  const std::string error = action == "start" ? programs::Start(name)
                                              : programs::Stop(name);
  CefRefPtr<CefDictionaryValue> d = CefDictionaryValue::Create();
  d->SetString("error", error);
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(d);
  return ServeJson(error.empty() ? 200 : 409,
                   CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString());
}

}  // namespace

std::string ResourcesDir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) {
    return "poca_resources";
  }
  buf[n] = '\0';
  return (std::filesystem::path(buf).parent_path() / "poca_resources")
      .string();
}

std::string ConfigPath(const std::string& file) {
  const char* config = std::getenv("XDG_CONFIG_HOME");
  std::filesystem::path base;
  if (config && *config) {
    base = config;
  } else {
    const char* home = std::getenv("HOME");
    base = std::filesystem::path(home ? home : "") / ".config";
  }
  return (base / "poca" / file).string();
}

CefRefPtr<CefResourceHandler> SchemeHandlerFactory::Create(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& scheme_name,
    CefRefPtr<CefRequest> request) {
  CefURLParts parts;
  if (!CefParseURL(request->GetURL(), parts)) {
    return nullptr;
  }
  const std::string host = CefString(&parts.host).ToString();
  const std::string path = CefString(&parts.path).ToString();
  if (!IsPage(host)) {
    return nullptr;
  }
  // Anyone may navigate to a Poca page, but only Poca's own pages may load
  // from it: a localhost page cannot read the bookmarks or box's programs.
  // The top-level page decides (first party for cookies): the frame's URL is
  // still the previous page's while a page loads, and poca:// requests carry
  // no referrer. A localhost page embedding poca:// in a frame is refused too.
  const bool navigation = request->GetResourceType() == RT_MAIN_FRAME;
  if (!navigation &&
      request->GetFirstPartyForCookies().ToString().rfind("poca://", 0) != 0) {
    return nullptr;
  }
  if (host == "box" && path == "/bookmarks.json") {
    return ServeFile(ConfigPath("bookmarks.json"), "application/json");
  }
  if (host == "box" && path == "/programs.json") {
    return ServeJson(200, programs::StatusJson());
  }
  if (host == "box" && (path == "/start" || path == "/stop")) {
    return ServeProgramAction(request, path.substr(1),
                              CefString(&parts.query).ToString());
  }
  const std::string name = (path.empty() || path == "/")
                               ? host + ".html"
                               : path.substr(1);
  if (!IsPlainName(name)) {
    return nullptr;
  }
  return ServeFile(ResourcesDir() + "/" + name, MimeType(name));
}

}  // namespace poca
