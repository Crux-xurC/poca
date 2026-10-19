// Poca's entry point. Adapted from tests/cefsimple/cefsimple_linux.cc in the
// CEF binary distribution (Copyright (c) 2013 The Chromium Embedded Framework
// Authors; BSD-style license in the distribution's LICENSE.txt).

#include <X11/Xlib.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/resource.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "include/cef_app.h"

#include "app.h"
#include "programs.h"

namespace {

int XErrorHandlerImpl(Display* display, XErrorEvent* event) {
  return 0;
}

int XIOErrorHandlerImpl(Display* display) {
  return 0;
}

// CEF needs a root_cache_path per running instance. Poca keeps no profile
// (cache_path stays empty, so browsers are in-memory), and this directory is
// removed on exit. $XDG_RUNTIME_DIR is memory-backed and cleared at logout.
std::string RuntimeBase() {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  return runtime && *runtime ? runtime : "/tmp";
}

// A Poca that was killed leaves its directory behind. CEF's SingletonLock
// there points at "<host>-<pid>"; remove the directory once that pid is gone.
void RemoveStaleRuntimeDirs() {
  std::error_code ec;
  for (const auto& entry :
       std::filesystem::directory_iterator(RuntimeBase(), ec)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("poca-", 0) != 0 || !entry.is_directory(ec)) {
      continue;
    }
    // Another Poca may be starting and not have its lock yet.
    const auto age = std::filesystem::file_time_type::clock::now() -
                     std::filesystem::last_write_time(entry.path(), ec);
    if (ec || age < std::chrono::minutes(1)) {
      continue;
    }
    const std::string lock =
        std::filesystem::read_symlink(entry.path() / "SingletonLock", ec)
            .string();
    const size_t dash = lock.rfind('-');
    if (!ec && dash != std::string::npos &&
        std::filesystem::exists("/proc/" + lock.substr(dash + 1), ec)) {
      continue;
    }
    std::filesystem::remove_all(entry.path(), ec);
  }
}

std::string MakeRuntimeDir() {
  std::string templ = RuntimeBase() + "/poca-XXXXXX";
  if (!mkdtemp(templ.data())) {
    return std::string();
  }
  return templ;
}

}  // namespace

NO_STACK_PROTECTOR
int main(int argc, char* argv[]) {
  // No crash dumps: a dump is a copy of the pages' memory on disk. Every
  // process (browser, renderer, GPU, ...) passes through here.
  poca::programs::Init();
  const struct rlimit no_core = {0, 0};
  setrlimit(RLIMIT_CORE, &no_core);
  prctl(PR_SET_DUMPABLE, 0);

  CefMainArgs main_args(argc, argv);
  CefRefPtr<poca::App> app(new poca::App);

  // Sub-processes (render, GPU, ...) run the same executable and stop here.
  int exit_code = CefExecuteProcess(main_args, app, nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  XSetErrorHandler(XErrorHandlerImpl);
  XSetIOErrorHandler(XIOErrorHandlerImpl);

  RemoveStaleRuntimeDirs();
  const std::string runtime_dir = MakeRuntimeDir();
  if (runtime_dir.empty()) {
    return 1;
  }

  CefSettings settings;
  settings.no_sandbox = true;
  settings.persist_session_cookies = false;
  settings.log_severity = LOGSEVERITY_DISABLE;
  CefString(&settings.root_cache_path) = runtime_dir;

  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    std::filesystem::remove_all(runtime_dir);
    return CefGetExitCode();
  }
  CefRunMessageLoop();
  CefShutdown();
  // What Poca started ends with Poca.
  poca::programs::StopAll();

  std::error_code ignored;
  std::filesystem::remove_all(runtime_dir, ignored);
  return 0;
}
