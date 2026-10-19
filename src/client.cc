#include "client.h"

#include <strings.h>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "guard.h"
#include "programs.h"
#include "window.h"

namespace poca {

namespace {

constexpr char kSecretHeader[] = "X-Poca-Secret";

// Who a page is, for the secret: 0 for Poca's own pages, its port for a
// localhost page, -1 for anything else (an opaque origin, say).
int PagePort(const std::string& url) {
  if (url.rfind("poca://", 0) == 0) {
    return 0;
  }
  const int port = LoopbackPort(url);
  return port > 0 ? port : -1;
}

int BrowserId(CefRefPtr<CefBrowser> browser) {
  return browser ? browser->GetIdentifier() : -1;
}

// One per request (and per redirect hop: CEF asks for a new one after each):
// who it comes from is known only when the handler is asked for.
class RequestGuard : public CefResourceRequestHandler {
 public:
  RequestGuard(CefRefPtr<Client> client, int from_port)
      : client_(client), from_port_(from_port) {}

  // The next hop keeps the secret only when this one carried it, and then
  // comes from the page that redirected: a redirect stays within its row.
  void OnResourceRedirect(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefRequest> request,
                          CefRefPtr<CefResponse> response,
                          CefString& new_url) override {
    client_->NoteRedirect(
        BrowserId(browser), new_url.ToString(),
        carried_ ? PagePort(request->GetURL().ToString()) : -1);
  }

  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override {
    const std::string url = request->GetURL().ToString();
    if (!IsAllowedUrl(url)) {
      return RV_CANCEL;
    }
    // Whatever a page or an earlier hop put there goes; only Poca sets it.
    CefRequest::HeaderMap headers;
    request->GetHeaderMap(headers);
    bool changed = false;
    for (auto it = headers.begin(); it != headers.end();) {
      if (strcasecmp(it->first.ToString().c_str(), kSecretHeader) == 0) {
        it = headers.erase(it);
        changed = true;
      } else {
        ++it;
      }
    }
    const std::string secret =
        programs::SecretFor(LoopbackPort(url), from_port_);
    carried_ = !secret.empty();
    if (carried_) {
      headers.insert(std::make_pair(kSecretHeader, secret));
      changed = true;
    }
    if (changed) {
      request->SetHeaderMap(headers);
    }
    return RV_CONTINUE;
  }

 private:
  CefRefPtr<Client> client_;
  const int from_port_;
  bool carried_ = false;  // IO thread

  IMPLEMENT_REFCOUNTING(RequestGuard);
};

}  // namespace

void Client::MarkTabClosing(int browser_id) {
  CEF_REQUIRE_UI_THREAD();
  closing_tabs_.insert(browser_id);
}

void Client::NoteRedirect(int browser_id,
                          const std::string& url,
                          int from_port) {
  std::lock_guard<std::mutex> lock(from_mutex_);
  redirects_[{browser_id, url}] = from_port;
}

void Client::NoteNewTab(const std::string& url, int from_port) {
  CEF_REQUIRE_UI_THREAD();
  std::lock_guard<std::mutex> lock(from_mutex_);
  // Two tabs opening the same URL from different places: trust neither.
  auto [it, added] = new_tabs_.emplace(url, from_port);
  if (!added && it->second != from_port) {
    it->second = -1;
  }
}

void Client::NotePersonLoad(int browser_id, const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  std::lock_guard<std::mutex> lock(from_mutex_);
  person_loads_[browser_id] = url;
}

int Client::NavigationFrom(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefRequest> request) {
  if (!browser || !frame) {
    return -1;
  }
  const std::string url = request->GetURL().ToString();
  std::lock_guard<std::mutex> lock(from_mutex_);
  if (frame->IsMain()) {
    // The next main-frame load after the url field is the person's, when it
    // goes where they typed. Taken either way.
    auto it = person_loads_.find(browser->GetIdentifier());
    if (it != person_loads_.end()) {
      const bool same = LoopbackPort(it->second) == LoopbackPort(url);
      person_loads_.erase(it);
      if (same) {
        return 0;
      }
    }
  }
  // While a frame navigates, its URL is still the page it leaves. A new
  // frame has none yet: its parent's page is the one that made it.
  std::string previous = frame->GetURL().ToString();
  if (previous.empty() && !frame->IsMain()) {
    if (CefRefPtr<CefFrame> parent = frame->GetParent()) {
      previous = parent->GetURL().ToString();
    }
  }
  if (!previous.empty()) {
    return PagePort(previous);
  }
  // The first load of a new tab: whoever the window says opened it.
  auto it = new_tabs_.find(url);
  if (it == new_tabs_.end()) {
    return -1;
  }
  const int from = it->second;
  new_tabs_.erase(it);
  return from;
}

void Client::OnTitleChange(CefRefPtr<CefBrowser> browser,
                           const CefString& title) {
  if (Window* window = Window::Get()) {
    window->OnTitle(browser, title);
  }
}

void Client::OnAddressChange(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             const CefString& url) {
  if (!frame->IsMain()) {
    return;
  }
  if (Window* window = Window::Get()) {
    window->OnAddress(browser, url);
  }
}

bool Client::CanDownload(CefRefPtr<CefBrowser> browser,
                         const CefString& url,
                         const CefString& request_method) {
  return false;
}

bool Client::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           int popup_id,
                           const CefString& target_url,
                           const CefString& target_frame_name,
                           cef_window_open_disposition_t target_disposition,
                           bool user_gesture,
                           const CefPopupFeatures& popupFeatures,
                           CefWindowInfo& windowInfo,
                           CefRefPtr<CefClient>& client,
                           CefBrowserSettings& settings,
                           CefRefPtr<CefDictionaryValue>& extra_info,
                           bool* no_javascript_access) {
  // Every popup becomes a tab; the guard still decides what it may load.
  if (Window* window = Window::Get()) {
    window->OpenTab(target_url, PagePort(frame->GetURL().ToString()));
  }
  return true;
}

void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  ++browser_count_;
}

bool Client::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  // A closing tab is torn down by removing its view; anything else lets CEF
  // close the window.
  return closing_tabs_.count(browser->GetIdentifier()) > 0;
}

void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  closing_tabs_.erase(browser->GetIdentifier());
  if (--browser_count_ == 0) {
    CefQuitMessageLoop();
  }
}

bool Client::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefRequest> request,
                            bool user_gesture,
                            bool is_redirect) {
  const std::string url = request->GetURL().ToString();
  if (IsAllowedUrl(url)) {
    return false;
  }
  if (frame->IsMain()) {
    CefPostTask(TID_UI, base::BindOnce(
                            [](CefRefPtr<CefFrame> frame, std::string url) {
                              frame->LoadURL(BlockedPageUrl(url));
                            },
                            frame, url));
  }
  return true;
}

bool Client::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              const CefString& target_url,
                              cef_window_open_disposition_t target_disposition,
                              bool user_gesture) {
  if (target_disposition == CEF_WOD_CURRENT_TAB) {
    return false;
  }
  if (Window* window = Window::Get()) {
    window->OpenTab(target_url, PagePort(frame->GetURL().ToString()));
  }
  return true;
}

CefRefPtr<CefResourceRequestHandler> Client::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  {
    std::lock_guard<std::mutex> lock(from_mutex_);
    auto it = redirects_.find(
        {BrowserId(browser), request->GetURL().ToString()});
    if (it != redirects_.end()) {
      const int from = it->second;
      redirects_.erase(it);
      return new RequestGuard(this, from);
    }
  }
  if (is_navigation) {
    return new RequestGuard(this, NavigationFrom(browser, frame, request));
  }
  // Subresources carry their page's origin; an empty one is Chromium's own.
  const std::string initiator = request_initiator.ToString();
  return new RequestGuard(this, initiator.empty() ? 0 : PagePort(initiator));
}

}  // namespace poca
