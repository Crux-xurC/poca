// Poca's one window, laid out with CEF Views:
//
//   | tab × | tab × | ...            |
//   | url              | reload | box |
//   | the active tab's BrowserView    |

#ifndef POCA_WINDOW_H_
#define POCA_WINDOW_H_

#include <string>
#include <vector>

#include "include/cef_request_context.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"

#include "client.h"

namespace poca {

class Window : public CefWindowDelegate,
               public CefBrowserViewDelegate,
               public CefButtonDelegate,
               public CefTextfieldDelegate {
 public:
  Window(const std::string& first_url,
         CefRefPtr<CefRequestContext> context);

  // The live window, or null before it is created and after it is destroyed.
  static Window* Get();

  // Opens |url| in a new tab and shows it. |from_port| is who opens it, for
  // the secret: 0 for the person or a Poca page, else the opening page's port
  // (Client::NoteNewTab).
  void OpenTab(const std::string& url, int from_port = 0);

  void OnTitle(CefRefPtr<CefBrowser> browser, const std::string& title);
  void OnAddress(CefRefPtr<CefBrowser> browser, const std::string& url);

  // CefWindowDelegate
  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  bool CanClose(CefRefPtr<CefWindow> window) override;
  bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  bool GetLinuxWindowProperties(CefRefPtr<CefWindow> window,
                                CefLinuxWindowProperties& properties) override;

  // CefViewDelegate (shared by the window and every view it delegates for)
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;

  // CefBrowserViewDelegate
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  // CefButtonDelegate
  void OnButtonPressed(CefRefPtr<CefButton> button) override;

  // CefTextfieldDelegate
  bool OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                  const CefKeyEvent& event) override;

 private:
  struct Tab {
    CefRefPtr<CefBrowserView> view;
    CefRefPtr<CefPanel> strip_item;
    CefRefPtr<CefLabelButton> title;
    CefRefPtr<CefLabelButton> close;
    std::string url;
  };

  int FindTab(CefRefPtr<CefBrowser> browser) const;
  int FindTabByButton(int button_id) const;
  CefRefPtr<CefBrowser> ActiveBrowser() const;
  void Activate(int index);
  void CloseTab(int index);
  void CloseTabWithButton(int button_id);
  void ShowBox();
  void Navigate(const std::string& text);

  const std::string first_url_;
  CefRefPtr<CefRequestContext> context_;
  CefRefPtr<Client> client_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> strip_;
  CefRefPtr<CefPanel> content_;
  CefRefPtr<CefTextfield> url_;
  std::vector<Tab> tabs_;
  int active_ = -1;
  int next_button_id_;

  IMPLEMENT_REFCOUNTING(Window);
};

}  // namespace poca

#endif  // POCA_WINDOW_H_
