// poca:// serves the pages bundled with Poca, the bookmarks file, and box's
// programs (status, start, stop), nothing else.

#ifndef POCA_SCHEME_H_
#define POCA_SCHEME_H_

#include <string>

#include "include/cef_scheme.h"

namespace poca {

// <directory of the executable>/poca_resources
std::string ResourcesDir();

// $XDG_CONFIG_HOME/poca/<file> (default ~/.config/poca/<file>): the settings a
// person writes (bookmarks.json, box.json). Settings, not history.
std::string ConfigPath(const std::string& file);

class SchemeHandlerFactory : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override;

 private:
  IMPLEMENT_REFCOUNTING(SchemeHandlerFactory);
};

}  // namespace poca

#endif  // POCA_SCHEME_H_
