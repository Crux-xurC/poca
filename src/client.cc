#include "client.h"

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "guard.h"
#include "window.h"

namespace poca {

void Client::MarkTabClosing(int browser_id) {
  CEF_REQUIRE_UI_THREAD();
  closing_tabs_.insert(browser_id);
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
    window->OpenTab(target_url);
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
    window->OpenTab(target_url);
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
  return this;
}

CefResourceRequestHandler::ReturnValue Client::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  return IsAllowedUrl(request->GetURL()) ? RV_CONTINUE : RV_CANCEL;
}

}  // namespace poca
