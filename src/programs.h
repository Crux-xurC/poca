// box's programs: the index a person writes (~/.config/poca/box.json), which
// of its ports are in use, and the programs Poca started. Poca stops only what
// it started, and stops all of it when it exits.
//
// box.json:
//   {"programs": [{"name": "tezcatl", "ports": [4001, 4100],
//                  "run": ["~/my_app/tezcatl/nix/t", "--no-browser"],
//                  "cwd": "~/my_app/tezcatl", "env": {"K": "V"},
//                  "log": "/tmp/poca-tezcatl.log"}]}
// "run" is an argument list (no shell); a leading "~" is expanded in run,
// cwd and log. "log" defaults to /tmp/poca-<name>.log.

#ifndef POCA_PROGRAMS_H_
#define POCA_PROGRAMS_H_

#include <string>

namespace poca::programs {

// Remembers the resource limits programs get back. Call first in main.
void Init();

// $XDG_CONFIG_HOME/poca/box.json (default ~/.config/poca/...).
std::string IndexPath();

// {"index": path, "error": "...", "programs": [{name, ports, busy: [port],
//  state: "free" | "busy" | "running" | "stopping" | "ended", status, log}]}
std::string StatusJson();

// Both return an error message, or "" on success. Any thread.
std::string Start(const std::string& name);
std::string Stop(const std::string& name);

// TERM to every program Poca started, KILL after 10 seconds. Blocks.
void StopAll();

}  // namespace poca::programs

#endif  // POCA_PROGRAMS_H_
