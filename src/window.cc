#include "window.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

#include "include/base/cef_callback.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "scheme.h"

namespace poca {

namespace {

// The live windows, oldest first. UI thread.
std::vector<Window*> g_windows;

// Windows virtual key codes.
constexpr int kVkeyTab = 0x09;
constexpr int kVkeyReturn = 0x0D;
constexpr int kVkeyEscape = 0x1B;
constexpr int kVkeyPrior = 0x21;
constexpr int kVkeyNext = 0x22;
constexpr int kVkeyLeft = 0x25;
constexpr int kVkeyRight = 0x27;
constexpr int kVkeyB = 0x42;
constexpr int kVkeyN = 0x4E;
constexpr int kVkeyR = 0x52;
constexpr int kVkeyS = 0x53;
constexpr int kVkeyX = 0x58;

enum Command {
  kNextTab = 1,
  kPreviousTab,
  kReload,
  kBox,
  kNewWindow,
  kSynthesis,
  kExchange,
  kMoveTabLeft,
  kMoveTabRight,
  // only while picking a tab in the strip
  kPickPrevious,
  kPickNext,
  kPickApply,
  kPickCancel,
};

// View ids of the fixed buttons; tab buttons are numbered from kFirstTabId.
enum ButtonId {
  kReloadButton = 1,
  kBoxButton,
  kFirstTabId = 100,
};

constexpr char kBoxUrl[] = "poca://box/";
constexpr char kHomeUrl[] = "poca://home/";
constexpr char kNormalFont[] = "Sans, 13px";
constexpr char kActiveFont[] = "Sans, Bold 13px";
// The marks before a tab's title: the two sides of the split, and the tab
// being picked.
constexpr char kLeftMark[] = "◧ ";
constexpr char kRightMark[] = "◨ ";
constexpr char kPickMark[] = "▸ ";

CefRefPtr<CefBoxLayout> Row(CefRefPtr<CefPanel> panel, int spacing) {
  CefBoxLayoutSettings settings;
  settings.horizontal = true;
  settings.between_child_spacing = spacing;
  settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
  return panel->SetToBoxLayout(settings);
}

std::vector<char> ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
}

// The window icon: icon/poca-128.png at scale 1, poca-256.png at scale 2.
// Not poca.png itself: on X11 the icon goes to the window as raw pixels
// (_NET_WM_ICON), and 2048x2048 (16 MB) is past what X takes in one
// request -- Chromium then keeps its own logo.
CefRefPtr<CefImage> LoadIcon() {
  CefRefPtr<CefImage> image = CefImage::CreateImage();
  const std::pair<float, const char*> sizes[] = {{1.0f, "poca-128.png"},
                                                 {2.0f, "poca-256.png"}};
  for (const auto& [scale, name] : sizes) {
    std::vector<char> png = ReadFile(ResourcesDir() + "/" + name);
    if (!png.empty()) {
      image->AddPNG(scale, png.data(), png.size());
    }
  }
  return image->IsEmpty() ? nullptr : image;
}

}  // namespace

Window::Window(const std::string& first_url,
               CefRefPtr<CefRequestContext> context,
               CefRefPtr<Client> client,
               int first_from_port)
    : first_url_(first_url),
      first_from_port_(first_from_port),
      context_(context),
      client_(client),
      next_button_id_(kFirstTabId) {}

Window::Window(CefRefPtr<Window> opener)
    : first_from_port_(-1),
      context_(opener->context_),
      client_(opener->client_),
      next_button_id_(kFirstTabId) {}

Window* Window::Owning(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  for (Window* window : g_windows) {
    if (window->FindTab(browser) >= 0) {
      return window;
    }
  }
  return g_windows.empty() ? nullptr : g_windows.front();
}

void Window::OnWindowCreated(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  g_windows.push_back(this);
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
  hint_ = CefLabelButton::CreateLabelButton(this, "");
  hint_->SetEnabled(false);
  hint_->SetVisible(false);
  toolbar->AddChildView(hint_);
  CefRefPtr<CefLabelButton> reload = CefLabelButton::CreateLabelButton(this, "reload");
  reload->SetID(kReloadButton);
  toolbar->AddChildView(reload);
  CefRefPtr<CefLabelButton> box = CefLabelButton::CreateLabelButton(this, "box");
  box->SetID(kBoxButton);
  toolbar->AddChildView(box);
  window_->AddChildView(toolbar);

  // Side by side: a hidden view takes no room, so one visible tab fills it
  // and two (a split) share it 1:1 (each tab's view has flex 1, AddTab).
  content_ = CefPanel::CreatePanel(nullptr);
  CefBoxLayoutSettings side_by_side;
  side_by_side.horizontal = true;
  side_by_side.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  side_by_side.default_flex = 1;
  content_->SetToBoxLayout(side_by_side);
  window_->AddChildView(content_);
  layout->SetFlexForView(content_, 1);

  // High priority: these keys never reach the page.
  window_->SetAccelerator(kNextTab, kVkeyTab, false, true, false, true);
  window_->SetAccelerator(kPreviousTab, kVkeyTab, true, true, false, true);
  window_->SetAccelerator(kReload, kVkeyR, false, true, false, true);
  window_->SetAccelerator(kBox, kVkeyB, true, true, false, true);
  window_->SetAccelerator(kNewWindow, kVkeyN, false, true, false, true);
  window_->SetAccelerator(kSynthesis, kVkeyS, true, true, false, true);
  window_->SetAccelerator(kExchange, kVkeyX, true, true, false, true);
  window_->SetAccelerator(kMoveTabLeft, kVkeyPrior, true, true, false, true);
  window_->SetAccelerator(kMoveTabRight, kVkeyNext, true, true, false, true);

  if (first_view_) {
    AddTab(first_view_, "");
    first_view_ = nullptr;
  } else {
    OpenTab(first_url_, first_from_port_);
  }
  window_->Show();
}

void Window::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), this),
                  g_windows.end());
  tabs_.clear();
  split_left_ = nullptr;
  split_right_ = nullptr;
  hint_ = nullptr;
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
    case kPickPrevious:
      MovePick(-1);
      return true;
    case kPickNext:
      MovePick(1);
      return true;
    case kPickApply:
      EndPick(true);
      return true;
    case kPickCancel:
      EndPick(false);
      return true;
  }
  // any other key ends picking first (the same key again: just that)
  if (pick_mode_ != kNoPick) {
    const bool same = (command_id == kSynthesis && pick_mode_ == kPickSynthesis) ||
                      (command_id == kExchange && pick_mode_ == kPickExchange);
    EndPick(false);
    if (same) {
      return true;
    }
  }
  switch (command_id) {
    case kSynthesis:
      // in a split it is undone: the two are tabs of their own again
      if (InSplit(active_)) {
        split_left_ = nullptr;
        split_right_ = nullptr;
        Activate(active_);
      } else {
        StartPick(kPickSynthesis);
      }
      return true;
    case kExchange:
      StartPick(kPickExchange);
      return true;
    case kMoveTabLeft:
      MoveTab(active_, active_ - 1);
      return true;
    case kMoveTabRight:
      MoveTab(active_, active_ + 1);
      return true;
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
    case kNewWindow:
      OpenWindow(kHomeUrl);
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
  // picking: a click on a tab's title picks it
  if (pick_mode_ != kNoPick && tabs_[index].title->GetID() == id) {
    if (index != active_) {
      pick_ = index;
      EndPick(true);
    }
    return;
  }
  if (pick_mode_ != kNoPick) {
    EndPick(false);
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
  CefBrowserSettings settings;
  AddTab(CefBrowserView::CreateBrowserView(client_, url, settings, nullptr,
                                           context_, this),
         url);
}

CefRefPtr<CefBrowserViewDelegate> Window::GetDelegateForPopupBrowserView(
    CefRefPtr<CefBrowserView> browser_view,
    const CefBrowserSettings& settings,
    CefRefPtr<CefClient> client,
    bool is_devtools) {
  CEF_REQUIRE_UI_THREAD();
  if (client_->TakePopupWantsWindow()) {
    return new Window(this);
  }
  return this;
}

bool Window::OnPopupBrowserViewCreated(
    CefRefPtr<CefBrowserView> browser_view,
    CefRefPtr<CefBrowserView> popup_browser_view,
    bool is_devtools) {
  CEF_REQUIRE_UI_THREAD();
  // The delegate GetDelegateForPopupBrowserView returned (no RTTI: through
  // the CefBrowserViewDelegate base it was stored as).
  Window* host = static_cast<Window*>(static_cast<CefBrowserViewDelegate*>(
      popup_browser_view->GetDelegate().get()));
  if (host == this) {
    AddTab(popup_browser_view, "");
  } else {
    // The new window takes the view as its first tab once it is created.
    host->first_view_ = popup_browser_view;
    CefWindow::CreateTopLevelWindow(host);
  }
  return true;
}

void Window::AddTab(CefRefPtr<CefBrowserView> view, const std::string& url) {
  if (!window_) {
    return;
  }
  if (pick_mode_ != kNoPick) {
    EndPick(false);
  }
  Tab tab;
  tab.url = url;
  tab.label = url.empty() ? "…" : url;
  tab.view = view;
  content_->AddChildView(tab.view);

  tab.strip_item = CefPanel::CreatePanel(nullptr);
  Row(tab.strip_item, 0);
  tab.title = CefLabelButton::CreateLabelButton(this, url.empty() ? "…" : url);
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

void Window::OpenWindow(const std::string& url, int from_port) {
  CEF_REQUIRE_UI_THREAD();
  CefWindow::CreateTopLevelWindow(
      new Window(url, context_, client_, from_port));
}

void Window::OnTitle(CefRefPtr<CefBrowser> browser, const std::string& title) {
  const int index = FindTab(browser);
  if (index < 0) {
    return;
  }
  tabs_[index].label = title.empty() ? tabs_[index].url : title;
  RefreshTitle(index);
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
  ShowViews();
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
  if (pick_mode_ != kNoPick) {
    EndPick(false);
  }
  Tab tab = tabs_[index];
  // a side of the split closes: the other is a tab of its own again
  if (InSplit(index)) {
    split_left_ = nullptr;
    split_right_ = nullptr;
  }
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

int Window::IndexOf(CefRefPtr<CefBrowserView> view) const {
  if (!view) {
    return -1;
  }
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].view->IsSame(view)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool Window::InSplit(int index) const {
  if (!split_left_ || index < 0 || index >= static_cast<int>(tabs_.size())) {
    return false;
  }
  return tabs_[index].view->IsSame(split_left_) ||
         tabs_[index].view->IsSame(split_right_);
}

// The active tab alone, or -- when it is a side of the split -- both sides,
// the left one first.
void Window::ShowViews() {
  const bool split = InSplit(active_);
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const int n = static_cast<int>(i);
    tabs_[i].view->SetVisible(n == active_ || (split && InSplit(n)));
    tabs_[i].title->SetFontList(n == active_ ? kActiveFont : kNormalFont);
    RefreshTitle(n);
  }
  if (split) {
    content_->ReorderChildView(split_left_, 0);
    content_->ReorderChildView(split_right_, 1);
  }
}

void Window::RefreshTitle(int index) {
  Tab& tab = tabs_[index];
  std::string text;
  if (pick_mode_ != kNoPick && index == pick_) {
    text += kPickMark;
  }
  if (split_left_ && tab.view->IsSame(split_left_)) {
    text += kLeftMark;
  } else if (split_right_ && tab.view->IsSame(split_right_)) {
    text += kRightMark;
  }
  tab.title->SetText(text + tab.label);
}

// Moves the tab at |from| to |to| in the strip; the active tab and the one
// being picked stay what they were.
void Window::MoveTab(int from, int to) {
  const int count = static_cast<int>(tabs_.size());
  if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
    return;
  }
  CefRefPtr<CefBrowserView> active = tabs_[active_].view;
  CefRefPtr<CefBrowserView> picked =
      pick_ >= 0 && pick_ < count ? tabs_[pick_].view : nullptr;
  Tab tab = tabs_[from];
  tabs_.erase(tabs_.begin() + from);
  tabs_.insert(tabs_.begin() + to, tab);
  strip_->ReorderChildView(tab.strip_item, to);
  active_ = IndexOf(active);
  pick_ = picked ? IndexOf(picked) : -1;
  strip_->Layout();
  Activate(active_);
}

// Picking: synthesis -- any tab but the active one; exchange (the active tab
// is a side of the split) -- any tab but the active one, the other side too.
void Window::StartPick(Pick mode) {
  const int count = static_cast<int>(tabs_.size());
  if (count < 2 || (mode == kPickExchange && !InSplit(active_))) {
    return;
  }
  pick_mode_ = mode;
  pick_ = (active_ + 1) % count;
  window_->SetAccelerator(kPickPrevious, kVkeyLeft, false, false, false, true);
  window_->SetAccelerator(kPickNext, kVkeyRight, false, false, false, true);
  window_->SetAccelerator(kPickApply, kVkeyReturn, false, false, false, true);
  window_->SetAccelerator(kPickCancel, kVkeyEscape, false, false, false, true);
  hint_->SetText(mode == kPickSynthesis
                     ? "beside it: ← → Enter (Esc: no)"
                     : "in its place, or swap: ← → Enter (Esc: no)");
  hint_->SetVisible(true);
  ShowViews();
  window_->Layout();
}

void Window::MovePick(int step) {
  const int count = static_cast<int>(tabs_.size());
  if (pick_mode_ == kNoPick || count < 2) {
    return;
  }
  do {
    pick_ = (pick_ + step + count) % count;
  } while (pick_ == active_);
  ShowViews();
}

void Window::EndPick(bool apply) {
  if (pick_mode_ == kNoPick) {
    return;
  }
  const Pick mode = pick_mode_;
  const int chosen = pick_;
  pick_mode_ = kNoPick;
  pick_ = -1;
  window_->RemoveAccelerator(kPickPrevious);
  window_->RemoveAccelerator(kPickNext);
  window_->RemoveAccelerator(kPickApply);
  window_->RemoveAccelerator(kPickCancel);
  hint_->SetVisible(false);
  if (apply && chosen >= 0 && chosen < static_cast<int>(tabs_.size()) &&
      chosen != active_) {
    if (mode == kPickSynthesis) {
      Synthesize(chosen);
    } else {
      Exchange(chosen);
    }
    return;
  }
  Activate(active_);
}

// The active tab on the left, |chosen| on the right, next to it in the strip
// (so Ctrl+Tab goes from one side to the other).
void Window::Synthesize(int chosen) {
  CefRefPtr<CefBrowserView> left = tabs_[active_].view;
  CefRefPtr<CefBrowserView> right = tabs_[chosen].view;
  split_left_ = left;
  split_right_ = right;
  const int at = IndexOf(left);
  MoveTab(IndexOf(right), chosen > at ? at + 1 : at);
  Activate(IndexOf(left));
}

// The active tab is a side of the split. |chosen| the other side: the two
// swap sides. Any other tab: it comes forward in the active one's place, and
// the active one goes back where |chosen| was in the strip.
void Window::Exchange(int chosen) {
  CefRefPtr<CefBrowserView> active = tabs_[active_].view;
  CefRefPtr<CefBrowserView> other = tabs_[chosen].view;
  if (InSplit(chosen)) {
    std::swap(split_left_, split_right_);
    MoveTab(IndexOf(other), IndexOf(active));
    Activate(IndexOf(active));
    return;
  }
  if (active->IsSame(split_left_)) {
    split_left_ = other;
  } else {
    split_right_ = other;
  }
  // the two change places in the strip: |other| to the active one's, then the
  // active one to where |other| was
  const int a = IndexOf(active);
  const int b = IndexOf(other);
  MoveTab(b, a);
  MoveTab(IndexOf(active), b);
  Activate(IndexOf(other));
}

}  // namespace poca
