#include "programs.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "include/cef_parser.h"

#include "scheme.h"

extern char** environ;

namespace poca::programs {

namespace {

constexpr auto kStopGrace = std::chrono::seconds(10);

struct Entry {
  std::string name;
  std::vector<int> ports;
  std::vector<std::string> run;
  std::string cwd;
  std::map<std::string, std::string> env;
  std::string log;
};

// A program Poca started. Its pid is also its process group.
struct Proc {
  pid_t pid = 0;
  bool reaped = false;
  int status = 0;
  bool stopping = false;
  std::string log;
};

std::mutex g_mutex;
std::map<std::string, Proc> g_procs;
struct rlimit g_core_limit;

std::string ExpandHome(const std::string& path) {
  if (path == "~" || path.rfind("~/", 0) == 0) {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + path.substr(1);
  }
  return path;
}

std::vector<Entry> LoadIndex(std::string* error) {
  std::vector<Entry> entries;
  std::ifstream in(IndexPath());
  if (!in) {
    *error = "no index file";
    return entries;
  }
  std::stringstream text;
  text << in.rdbuf();
  CefString parse_error;
  CefRefPtr<CefValue> root = CefParseJSONAndReturnError(
      text.str(), JSON_PARSER_ALLOW_TRAILING_COMMAS, parse_error);
  if (!root || root->GetType() != VTYPE_DICTIONARY) {
    *error = root ? "the index is not an object" : parse_error.ToString();
    return entries;
  }
  CefRefPtr<CefListValue> list = root->GetDictionary()->GetList("programs");
  if (!list) {
    *error = "the index has no \"programs\" list";
    return entries;
  }
  for (size_t i = 0; i < list->GetSize(); ++i) {
    CefRefPtr<CefDictionaryValue> d = list->GetDictionary(i);
    if (!d || d->GetString("name").empty()) {
      continue;
    }
    Entry e;
    e.name = d->GetString("name");
    if (CefRefPtr<CefListValue> ports = d->GetList("ports")) {
      for (size_t j = 0; j < ports->GetSize(); ++j) {
        e.ports.push_back(ports->GetInt(j));
      }
    }
    if (CefRefPtr<CefListValue> run = d->GetList("run")) {
      for (size_t j = 0; j < run->GetSize(); ++j) {
        e.run.push_back(ExpandHome(run->GetString(j)));
      }
    }
    e.cwd = ExpandHome(d->HasKey("cwd") ? d->GetString("cwd").ToString()
                                        : std::string("~"));
    if (CefRefPtr<CefDictionaryValue> env = d->GetDictionary("env")) {
      CefDictionaryValue::KeyList keys;
      env->GetKeys(keys);
      for (const CefString& key : keys) {
        e.env[key] = env->GetString(key);
      }
    }
    e.log = d->HasKey("log") ? ExpandHome(d->GetString("log"))
                             : "/tmp/poca-" + e.name + ".log";
    entries.push_back(e);
  }
  return entries;
}

bool PortInUse(int port) {
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return false;
  }
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bool in_use = false;
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
    in_use = true;
  } else if (errno == EINPROGRESS) {
    pollfd p = {fd, POLLOUT, 0};
    int err = 0;
    socklen_t len = sizeof(err);
    in_use = poll(&p, 1, 300) == 1 &&
             getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
  }
  close(fd);
  return in_use;
}

// Collects the group leader's exit status once it ends.
void Reap(Proc& proc) {
  if (proc.reaped) {
    return;
  }
  int status = 0;
  pid_t r = waitpid(proc.pid, &status, WNOHANG);
  if (r == proc.pid) {
    proc.reaped = true;
    proc.status = WIFEXITED(status) ? WEXITSTATUS(status)
                                    : 128 + WTERMSIG(status);
  } else if (r < 0 && errno == ECHILD) {
    proc.reaped = true;
    proc.status = -1;
  }
}

// The group outlives its leader when the leader exits before its children.
bool GroupAlive(Proc& proc) {
  Reap(proc);
  return kill(-proc.pid, 0) == 0 || errno == EPERM;
}

void KillLater(pid_t pid) {
  std::thread([pid] {
    std::this_thread::sleep_for(kStopGrace);
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& [name, proc] : g_procs) {
      if (proc.pid == pid && GroupAlive(proc)) {
        kill(-pid, SIGKILL);
      }
    }
  }).detach();
}

}  // namespace

void Init() {
  getrlimit(RLIMIT_CORE, &g_core_limit);
}

std::string IndexPath() {
  return ConfigPath("box.json");
}

std::string StatusJson() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string error;
  const std::vector<Entry> entries = LoadIndex(&error);

  CefRefPtr<CefDictionaryValue> root = CefDictionaryValue::Create();
  root->SetString("index", IndexPath());
  root->SetString("error", error);
  CefRefPtr<CefListValue> list = CefListValue::Create();
  for (const Entry& e : entries) {
    CefRefPtr<CefDictionaryValue> d = CefDictionaryValue::Create();
    d->SetString("name", e.name);
    d->SetString("log", e.log);
    CefRefPtr<CefListValue> ports = CefListValue::Create();
    CefRefPtr<CefListValue> busy = CefListValue::Create();
    for (int port : e.ports) {
      ports->SetInt(ports->GetSize(), port);
      if (PortInUse(port)) {
        busy->SetInt(busy->GetSize(), port);
      }
    }
    // Read before SetList: a list handed to a dictionary is no longer valid.
    std::string state = busy->GetSize() > 0 ? "busy" : "free";
    d->SetList("ports", ports);
    d->SetList("busy", busy);

    auto it = g_procs.find(e.name);
    if (it != g_procs.end()) {
      Proc& proc = it->second;
      if (GroupAlive(proc)) {
        state = proc.stopping ? "stopping" : "running";
      } else if (state == "free") {
        state = "ended";
        d->SetInt("status", proc.status);
        d->SetString("log", proc.log);
      }
    }
    d->SetString("state", state);
    list->SetDictionary(list->GetSize(), d);
  }
  root->SetList("programs", list);

  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(root);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::string Start(const std::string& name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string error;
  const std::vector<Entry> entries = LoadIndex(&error);
  const Entry* entry = nullptr;
  for (const Entry& e : entries) {
    if (e.name == name) {
      entry = &e;
    }
  }
  if (!entry) {
    return "not in the index: " + name;
  }
  if (entry->run.empty()) {
    return name + " has no \"run\"";
  }
  auto it = g_procs.find(name);
  if (it != g_procs.end() && GroupAlive(it->second)) {
    return name + " is already running";
  }
  for (int port : entry->ports) {
    if (PortInUse(port)) {
      return "port " + std::to_string(port) + " is in use";
    }
  }

  // Everything the child needs is built before fork: after it, only
  // async-signal-safe calls.
  std::vector<char*> argv;
  for (const std::string& arg : entry->run) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  std::vector<std::string> env_strings;
  for (char** e = environ; *e; ++e) {
    const std::string kv = *e;
    if (!entry->env.count(kv.substr(0, kv.find('=')))) {
      env_strings.push_back(kv);
    }
  }
  for (const auto& [key, value] : entry->env) {
    env_strings.push_back(key + "=" + value);
  }
  std::vector<char*> envp;
  for (std::string& kv : env_strings) {
    envp.push_back(kv.data());
  }
  envp.push_back(nullptr);
  const char* cwd = entry->cwd.c_str();
  const char* log = entry->log.c_str();
  static const char kNoCwd[] = "poca: cannot enter the working directory\n";
  static const char kNoExec[] = "poca: cannot run the program\n";

  pid_t pid = fork();
  if (pid < 0) {
    return "fork failed";
  }
  if (pid == 0) {
    setsid();
    setrlimit(RLIMIT_CORE, &g_core_limit);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    for (int sig = 1; sig < 32; ++sig) {
      signal(sig, SIG_DFL);
    }
    int out = open(log, O_WRONLY | O_CREAT | O_APPEND, 0600);
    int in = open("/dev/null", O_RDONLY);
    if (out >= 0) {
      dup2(out, 1);
      dup2(out, 2);
    }
    if (in >= 0) {
      dup2(in, 0);
    }
    close_range(3, ~0U, 0);
    if (chdir(cwd) != 0) {
      ssize_t ignored = write(2, kNoCwd, sizeof(kNoCwd) - 1);
      (void)ignored;
      _exit(127);
    }
    execvpe(argv[0], argv.data(), envp.data());
    ssize_t ignored = write(2, kNoExec, sizeof(kNoExec) - 1);
    (void)ignored;
    _exit(127);
  }
  Proc proc;
  proc.pid = pid;
  proc.log = entry->log;
  g_procs[name] = proc;
  return "";
}

std::string Stop(const std::string& name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_procs.find(name);
  if (it == g_procs.end() || !GroupAlive(it->second)) {
    return name + " was not started by Poca, or has ended";
  }
  kill(-it->second.pid, SIGTERM);
  it->second.stopping = true;
  KillLater(it->second.pid);
  return "";
}

void StopAll() {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto& [name, proc] : g_procs) {
    if (GroupAlive(proc)) {
      kill(-proc.pid, SIGTERM);
    }
  }
  const auto deadline = std::chrono::steady_clock::now() + kStopGrace;
  while (std::chrono::steady_clock::now() < deadline) {
    bool alive = false;
    for (auto& [name, proc] : g_procs) {
      alive = alive || GroupAlive(proc);
    }
    if (!alive) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  for (auto& [name, proc] : g_procs) {
    if (GroupAlive(proc)) {
      kill(-proc.pid, SIGKILL);
    }
  }
}

}  // namespace poca::programs
