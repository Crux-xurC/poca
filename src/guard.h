// The one place that decides what Poca's window may load.

#ifndef POCA_GUARD_H_
#define POCA_GUARD_H_

#include <string>

namespace poca {

// True for poca:// and for http(s)/ws(s) on 127.0.0.1, localhost or [::1]
// (plus data:, blob:, about:blank and about:srcdoc, which never leave the
// machine). Safe to call on any thread.
bool IsAllowedUrl(const std::string& url);

// The page shown in place of a refused navigation.
std::string BlockedPageUrl(const std::string& url);

}  // namespace poca

#endif  // POCA_GUARD_H_
