#include "window.h"

#include <fstream>
#include <iterator>

#include "include/base/cef_callback.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "scheme.h"

namespace poca {

namespace {

Window* g_window = nullptr;

// Windows virtual key codes.
constexpr int kVkeyTab = 0x09;
constexpr int kVkeyReturn = 0x0D;
constexpr int kVkeyB = 0x42;
constexpr int kVkeyR = 0x52;

enum Command {
  kNextTab = 1,
  kPreviousTab,
  kReload,
  kBox,
};

// View ids of the fixed buttons; tab buttons are numbered from kFirstTabId.
enum ButtonId {
  kReloadButton = 1,
  kBoxButton,
  kFirstTabId = 100,
};

constexpr char kBoxUrl[] = "poca://box/";
constexpr char kNormalFont[] = "Sans, 13px";
constexpr char kActiveFont[] = "Sans, Bold 13px";

CefRefPtr<CefBoxLayout> Row(CefRefPtr<CefPanel> panel, int spacing) {
  CefBoxLayoutSettings settings;
  settings.horizontal = true;
  settings.between_child_spacing = spacing;
  settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
  return panel->SetToBoxLayout(settings);
}

CefRefPtr<CefImage> LoadIcon() {
  std::ifstream in(ResourcesDir() + "/poca.png", std::ios::binary);
  std::vector<char> png((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  if (png.empty()) {
    return nullptr;
  }
  CefRefPtr<CefImage> image = CefImage::CreateImage();
  image->AddPNG(1.0f, png.data(), png.size());
  return image;
}

}  // namespace

Window::Window(const std::string& first_url,
               CefRefPtr<CefRequestContext> context)
    : first_url_(first_url),
      context_(context),
      client_(new Client),
      next_button_id_(kFirstTabId) {}

Window* Window::Get() {
  return g_window;
}

void Window::OnWindowCreated(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  g_window = this;
  window_ = window;
  window_->SetTitle("Poca");
  if (CefRefPtr<CefImage> icon = LoadIcon()) {
    window_->SetWindowIcon(icon);
    window_->SetWindowAppIcon(icon);
  }

  CefBoxLayoutSettings column;
  column.horizontal = false;
  CefRefPtr<CefBoxLayout> layout = window_->SetToBoxLayout(column);

  strip_ = CefPanel::CreatePanel(nullptr);
  Row(strip_, 2);
  window_->AddChildView(strip_);

  CefRefPtr<CefPanel> toolbar = CefPanel::CreatePanel(nullptr);
  CefRefPtr<CefBoxLayout> toolbar_layout = Row(toolbar, 4);
  url_ = CefTextfield::CreateTextfield(this);
  toolbar->AddChildView(url_);
  toolbar_layout->SetFlexForView(url_, 1);
  CefRefPtr<CefLabelButton> reload = CefLabelButton::CreateLabelButton(this, "reload");
  reload->SetID(kReloadButton);
  toolbar->AddChildView(reload);
  CefRefPtr<CefLabelButton> box = CefLabelButton::CreateLabelButton(this, "box");
  box->SetID(kBoxButton);
  toolbar->AddChildView(box);
  window_->AddChildView(toolbar);

  content_ = CefPanel::CreatePanel(nullptr);
  content_->SetToFillLayout();
  window_->AddChildView(content_);
  layout->SetFlexForView(content_, 1);

  // High priority: these keys never reach the page.
  window_->SetAccelerator(kNextTab, kVkeyTab, false, true, false, true);
  window_->SetAccelerator(kPreviousTab, kVkeyTab, true, true, false, true);
  window_->SetAccelerator(kReload, kVkeyR, false, true, false, true);
  window_->SetAccelerator(kBox, kVkeyB, true, true, false, true);

  OpenTab(first_url_);
  window_->Show();
}

void Window::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  g_window = nullptr;
  tabs_.clear();
  url_ = nullptr;
  content_ = nullptr;
  strip_ = nullptr;
  window_ = nullptr;
  context_ = nullptr;
}

bool Window::CanClose(CefRefPtr<CefWindow> window) {
  bool can_close = true;
  for (const Tab& tab : tabs_) {
    CefRefPtr<CefBrowser> browser = tab.view->GetBrowser();
    if (browser && !browser->GetHost()->TryCloseBrowser()) {
      can_close = false;
    }
  }
  return can_close;
}

bool Window::OnAccelerator(CefRefPtr<CefWindow> window, int command_id) {
  const int count = static_cast<int>(tabs_.size());
  switch (command_id) {
    case kNextTab:
      Activate((active_ + 1) % count);
      return true;
    case kPreviousTab:
      Activate((active_ + count - 1) % count);
      return true;
    case kReload:
      if (CefRefPtr<CefBrowser> browser = ActiveBrowser()) {
        browser->Reload();
      }
      return true;
    case kBox:
      ShowBox();
      return true;
  }
  return false;
}

// WM_CLASS / app_id "poca": matches StartupWMClass in poca.desktop, so the
// desktop shows Poca's own entry and icon for the window.
bool Window::GetLinuxWindowProperties(CefRefPtr<CefWindow> window,
                                      CefLinuxWindowProperties& properties) {
  CefString(&properties.wayland_app_id) = "poca";
  CefString(&properties.wm_class_class) = "poca";
  CefString(&properties.wm_class_name) = "poca";
  return true;
}

CefSize Window::GetPreferredSize(CefRefPtr<CefView> view) {
  CefRefPtr<CefPanel> panel = view->AsPanel();
  if (panel && panel->AsWindow()) {
    return CefSize(1200, 800);
  }
  return CefSize();
}

void Window::OnButtonPressed(CefRefPtr<CefButton> button) {
  const int id = button->GetID();
  if (id == kReloadButton) {
    OnAccelerator(window_, kReload);
    return;
  }
  if (id == kBoxButton) {
    ShowBox();
    return;
  }
  const int index = FindTabByButton(id);
  if (index < 0) {
    return;
  }
  if (tabs_[index].close->GetID() == id) {
    // The button is destroyed with its tab, and CEF still touches it after
    // this callback returns: close once the callback is over.
    CefPostTask(TID_UI, base::BindOnce(&Window::CloseTabWithButton, this, id));
  } else {
    Activate(index);
  }
}

bool Window::OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                        const CefKeyEvent& event) {
  if (event.type == KEYEVENT_RAWKEYDOWN &&
      event.windows_key_code == kVkeyReturn) {
    Navigate(textfield->GetText());
    return true;
  }
  return false;
}

void Window::OpenTab(const std::string& url, int from_port) {
  CEF_REQUIRE_UI_THREAD();
  if (!window_) {
    return;
  }
  client_->NoteNewTab(url, from_port);
  Tab tab;
  tab.url = url;
  CefBrowserSettings settings;
  tab.view = CefBrowserView::CreateBrowserView(client_, url, settings, nullptr,
                                               context_, this);
  content_->AddChildView(tab.view);

  tab.strip_item = CefPanel::CreatePanel(nullptr);
  Row(tab.strip_item, 0);
  tab.title = CefLabelButton::CreateLabelButton(this, url);
  tab.title->SetID(next_button_id_++);
  tab.title->SetMaximumSize(CefSize(220, 0));
  tab.title->SetFontList(kNormalFont);
  tab.close = CefLabelButton::CreateLabelButton(this, "×");
  tab.close->SetID(next_button_id_++);
  tab.strip_item->AddChildView(tab.title);
  tab.strip_item->AddChildView(tab.close);
  strip_->AddChildView(tab.strip_item);

  tabs_.push_back(tab);
  Activate(static_cast<int>(tabs_.size()) - 1);
}

void Window::OnTitle(CefRefPtr<CefBrowser> browser, const std::string& title) {
  const int index = FindTab(browser);
  if (index < 0) {
    return;
  }
  tabs_[index].title->SetText(title.empty() ? tabs_[index].url : title);
  strip_->Layout();
}

void Window::OnAddress(CefRefPtr<CefBrowser> browser, const std::string& url) {
  const int index = FindTab(browser);
  if (index < 0) {
    return;
  }
  tabs_[index].url = url;
  if (index == active_) {
    url_->SetText(url);
  }
}

int Window::FindTab(CefRefPtr<CefBrowser> browser) const {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    CefRefPtr<CefBrowser> own = tabs_[i].view->GetBrowser();
    if (own && own->IsSame(browser)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int Window::FindTabByButton(int button_id) const {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].title->GetID() == button_id ||
        tabs_[i].close->GetID() == button_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

CefRefPtr<CefBrowser> Window::ActiveBrowser() const {
  if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) {
    return nullptr;
  }
  return tabs_[active_].view->GetBrowser();
}

void Window::Activate(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return;
  }
  active_ = index;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const bool active = static_cast<int>(i) == index;
    tabs_[i].view->SetVisible(active);
    tabs_[i].title->SetFontList(active ? kActiveFont : kNormalFont);
  }
  url_->SetText(tabs_[index].url);
  window_->Layout();
  tabs_[index].view->RequestFocus();
}

void Window::CloseTabWithButton(int button_id) {
  const int index = FindTabByButton(button_id);
  if (index >= 0 && window_) {
    CloseTab(index);
  }
}

void Window::CloseTab(int index) {
  if (tabs_.size() == 1) {
    window_->Close();
    return;
  }
  Tab tab = tabs_[index];
  tabs_.erase(tabs_.begin() + index);
  if (CefRefPtr<CefBrowser> browser = tab.view->GetBrowser()) {
    client_->MarkTabClosing(browser->GetIdentifier());
    browser->GetHost()->CloseBrowser(true);
  }
  // Removing the view tears the browser down (DoClose returned true).
  content_->RemoveChildView(tab.view);
  strip_->RemoveChildView(tab.strip_item);
  if (active_ >= index && active_ > 0) {
    --active_;
  }
  Activate(active_);
}

void Window::ShowBox() {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].url.rfind(kBoxUrl, 0) == 0) {
      Activate(static_cast<int>(i));
      return;
    }
  }
  OpenTab(kBoxUrl);
}

void Window::Navigate(const std::string& text) {
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser || text.empty()) {
    return;
  }
  // "127.0.0.1:4001/x" and "localhost:3000" mean http.
  const std::string url =
      text.find("://") == std::string::npos ? "http://" + text : text;
  client_->NotePersonLoad(browser->GetIdentifier(), url);
  browser->GetMainFrame()->LoadURL(url);
  tabs_[active_].view->RequestFocus();
}

}  // namespace poca
