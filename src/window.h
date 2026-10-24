// A Poca window, laid out with CEF Views (one Poca may have several; they
// share one Client and one request context, so a window opened from a page
// keeps that page's place for the secret like a tab does):
//
//   | tab × | tab × | ...            |
//   | url       | hint | reload | box |
//   | the active tab's BrowserView    |   or, split: | left | right |
//
// Two tabs can be shown side by side, 1:1 (a split): Ctrl+Shift+S picks the
// tab to put beside the active one, Ctrl+Shift+X (in a split) picks a tab to
// take the active one's place, or the other side to swap them. Picking is
// done in the tab strip: Left / Right, Enter, Escape (or a click on a tab).
// Ctrl+Shift+PageUp / PageDown move the active tab in the strip.

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
  // |first_from_port| is who opens the first tab (see OpenTab).
  Window(const std::string& first_url,
         CefRefPtr<CefRequestContext> context,
         CefRefPtr<Client> client,
         int first_from_port = 0);

  // A window whose first tab is |popup|, a view CEF made for window.open.
  explicit Window(CefRefPtr<Window> opener);

  // The live window holding |browser|, else the first live window, else null.
  static Window* Owning(CefRefPtr<CefBrowser> browser);

  // Opens |url| in a new tab and shows it. |from_port| is who opens it, for
  // the secret: 0 for the person or a Poca page, else the opening page's port
  // (Client::NoteNewTab).
  void OpenTab(const std::string& url, int from_port = 0);

  // Opens |url| in a new window of this Poca; |from_port| as for OpenTab.
  void OpenWindow(const std::string& url, int from_port = 0);

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
  CefRefPtr<CefBrowserViewDelegate> GetDelegateForPopupBrowserView(
      CefRefPtr<CefBrowserView> browser_view,
      const CefBrowserSettings& settings,
      CefRefPtr<CefClient> client,
      bool is_devtools) override;
  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override;

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
    std::string label;  // the page's title (or url), without the marks
  };

  // Adds |view| (made by OpenTab, or by CEF for a popup) as a tab and shows it.
  void AddTab(CefRefPtr<CefBrowserView> view, const std::string& url);
  int FindTab(CefRefPtr<CefBrowser> browser) const;
  int FindTabByButton(int button_id) const;
  CefRefPtr<CefBrowser> ActiveBrowser() const;
  void Activate(int index);
  void CloseTab(int index);
  void CloseTabWithButton(int button_id);
  void ShowBox();
  void Navigate(const std::string& text);

  // The split (two tabs side by side) and picking a tab in the strip.
  enum Pick { kNoPick, kPickSynthesis, kPickExchange };
  int IndexOf(CefRefPtr<CefBrowserView> view) const;
  bool InSplit(int index) const;
  void ShowViews();
  void RefreshTitle(int index);
  void MoveTab(int from, int to);
  void StartPick(Pick mode);
  void MovePick(int step);
  void EndPick(bool apply);
  void Synthesize(int chosen);
  void Exchange(int chosen);

  const std::string first_url_;
  const int first_from_port_;
  CefRefPtr<CefBrowserView> first_view_;  // a popup's, instead of first_url_
  CefRefPtr<CefRequestContext> context_;
  CefRefPtr<Client> client_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> strip_;
  CefRefPtr<CefPanel> content_;
  CefRefPtr<CefTextfield> url_;
  CefRefPtr<CefLabelButton> hint_;   // what picking asks for; hidden otherwise
  CefRefPtr<CefBrowserView> split_left_;
  CefRefPtr<CefBrowserView> split_right_;
  Pick pick_mode_ = kNoPick;
  int pick_ = -1;
  std::vector<Tab> tabs_;
  int active_ = -1;
  int next_button_id_;

  IMPLEMENT_REFCOUNTING(Window);
};

}  // namespace poca

#endif  // POCA_WINDOW_H_
