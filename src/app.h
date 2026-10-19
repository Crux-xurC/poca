// The CefApp for every Poca process: registers poca:// everywhere and, in the
// browser process, opens the window.

#ifndef POCA_APP_H_
#define POCA_APP_H_

#include "include/cef_app.h"

namespace poca {

class App : public CefApp, public CefBrowserProcessHandler {
 public:
  App() = default;

  // CefApp
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override;
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  // CefBrowserProcessHandler
  void OnContextInitialized() override;

 private:
  IMPLEMENT_REFCOUNTING(App);
};

}  // namespace poca

#endif  // POCA_APP_H_
