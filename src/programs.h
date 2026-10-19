// box's programs: the index a person writes (~/.config/poca/box.json), which
// of its ports are in use, and the programs Poca started. Poca stops only what
// it started, and stops all of it when it exits.
//
// box.json:
//   {"programs": [{"name": "tezcatl", "ports": [4001, 4100],
//                  "run": ["~/my_app/tezcatl/nix/t"], "auth": true,
//                  "cwd": "~/my_app/tezcatl", "env": {"K": "V"},
//                  "log": "/tmp/poca-tezcatl.log"}]}
// "run" is an argument list (no shell); a leading "~" is expanded in run,
// cwd and log. "log" defaults to /tmp/poca-<name>.log.
//
// "auth": true hands the row Poca's secret: POCA_SECRET in its environment
// when Poca starts it, and X-Poca-Secret on its pages' requests (client.cc).
// The secret is made once per run from getrandom and kept in memory only; no
// other row gets POCA_SECRET, even one Poca itself inherited.

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

// The secret for a request to `port`, or "". Only while `port` belongs to an
// "auth" row Poca started and still runs, and only when the request comes
// from the person or a Poca page (`from_port` 0) or from a page of the same
// row (`from_port` one of its ports). Any thread.
std::string SecretFor(int port, int from_port);

// TERM to every program Poca started, KILL after 10 seconds. Blocks.
void StopAll();

}  // namespace poca::programs

#endif  // POCA_PROGRAMS_H_
