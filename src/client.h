// The CefClient shared by every tab of every window: reports titles and
// addresses to the window, turns popups into tabs or windows, refuses what the guard refuses, and adds
// Poca's secret to the requests of box's "auth" rows.

#ifndef POCA_CLIENT_H_
#define POCA_CLIENT_H_

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#include "include/cef_client.h"

namespace poca {

class Client : public CefClient,
               public CefDisplayHandler,
               public CefDownloadHandler,
               public CefLifeSpanHandler,
               public CefRequestHandler {
 public:
  Client() = default;

  // Opens |url| from |browser|'s window: in a new window for NEW_POPUP and
  // NEW_WINDOW, else in a new tab. |from_port| as for Window::OpenTab.
  void Open(CefRefPtr<CefBrowser> browser,
            const std::string& url,
            cef_window_open_disposition_t disposition,
            int from_port);

  // A popup CEF is creating (window.open, target=_blank) goes to a window of
  // its own: set by OnBeforePopup, taken once by the opener's
  // Window::GetDelegateForPopupBrowserView, which comes next. UI thread.
  bool TakePopupWantsWindow();

  // The next close of this browser is a tab close, not a window close.
  void MarkTabClosing(int browser_id);

  // Who a navigation comes from, for the secret (programs::SecretFor): 0 for
  // the person or a Poca page, a port for a localhost page, -1 otherwise.
  // CEF gives navigations no initiator, so the window says who opens a tab
  // (the first load of a new tab has no previous page) and when the person
  // loads a URL in the url field. UI thread.
  void NoteNewTab(const std::string& url, int from_port);
  void NotePersonLoad(int browser_id, const std::string& url);

  // CEF asks for a new handler after a redirect; the hop to |url| comes from
  // |from_port| (the redirecting page, or -1). IO thread.
  void NoteRedirect(int browser_id, const std::string& url, int from_port);

  // CefClient
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  // CefDisplayHandler
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;

  // CefDownloadHandler
  bool CanDownload(CefRefPtr<CefBrowser> browser,
                   const CefString& url,
                   const CefString& request_method) override;

  // CefLifeSpanHandler
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
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
                     bool* no_javascript_access) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefRequestHandler
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        const CefString& target_url,
                        cef_window_open_disposition_t target_disposition,
                        bool user_gesture) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;

 private:
  // IO thread: who the navigation |request| in |frame| comes from.
  int NavigationFrom(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     CefRefPtr<CefRequest> request);

  // UI thread only. Poca ends when the last browser of the last window closes.
  int browser_count_ = 0;
  bool popup_wants_window_ = false;
  std::set<int> closing_tabs_;

  // Written on the UI thread, taken (once) on the IO thread.
  std::mutex from_mutex_;
  std::map<std::string, int> new_tabs_;    // url -> from
  std::map<int, std::string> person_loads_;  // browser id -> url
  std::map<std::pair<int, std::string>, int> redirects_;  // (id, url) -> from

  IMPLEMENT_REFCOUNTING(Client);
};

}  // namespace poca

#endif  // POCA_CLIENT_H_
