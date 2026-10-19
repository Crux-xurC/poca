#include "app.h"

#include "include/cef_command_line.h"
#include "include/cef_request_context.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

#include "scheme.h"
#include "window.h"

namespace poca {

void App::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  if (!process_type.empty()) {
    return;
  }
  // Chromium's own traffic to the outside (updates, pings, prefetching).
  command_line->AppendSwitch("disable-background-networking");
  command_line->AppendSwitch("disable-component-update");
  command_line->AppendSwitch("no-pings");
  command_line->AppendSwitch("dns-prefetch-disable");
  // The second wall behind the guard: WebSockets skip OnBeforeResourceLoad,
  // so every connection not to 127.0.0.1, localhost or [::1] goes to a proxy
  // on a privileged port nobody listens on. "<-loopback>" drops Chromium's
  // implicit bypass of all of 127.0.0.0/8.
  command_line->AppendSwitchWithValue("proxy-server", "http://127.0.0.1:1");
  command_line->AppendSwitchWithValue("proxy-bypass-list",
                                      "<-loopback>;127.0.0.1;localhost;[::1]");
  // WebRTC may only go through that proxy: no UDP to the outside.
  command_line->AppendSwitchWithValue("force-webrtc-ip-handling-policy",
                                      "disable_non_proxied_udp");
}

void App::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(
      "poca", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                  CEF_SCHEME_OPTION_CORS_ENABLED |
                  CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void App::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  // The global context still keeps a profile (history, cookies) under
  // root_cache_path, so tabs use their own context that lives in memory only.
  CefRequestContextSettings context_settings;
  CefRefPtr<CefRequestContext> context =
      CefRequestContext::CreateContext(context_settings, nullptr);
  context->RegisterSchemeHandlerFactory("poca", "", new SchemeHandlerFactory);

  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();
  std::string url = command_line->GetSwitchValue("url");
  if (url.empty()) {
    url = "poca://home/";
  }
  CefWindow::CreateTopLevelWindow(new Window(url, context));
}

}  // namespace poca
