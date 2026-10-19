# Poca

Poca is a thin window for localhost development (tezcatl, a Yesod app, …). It knows nothing about what it shows: the
pages it opens and the programs it can start come from files a person writes.

## The pledge

1. **For localhost only. Poca's window loads nothing from outside.** Only `127.0.0.1`, `localhost`, `[::1]` and Poca's own
   `poca://` pages, for navigation and for everything a page loads (images, fonts, scripts from a CDN). New tabs and
   `window.open` too. Downloads are refused.
2. **No history.** No browsing history, cache, cookies or page storage survive the window: every tab runs in an in-memory
   request context, the cache directory lives under `$XDG_RUNTIME_DIR` and is removed on exit, CEF's log is off, and no core
   dump is written.

The pledge covers **Poca's window only**. Programs started from Poca, or run in a terminal inside a page (`git push`,
`mix deps.get`, …), talk to the network on their own; Poca does not restrict them.

What follows from it: a page that loads Bootstrap or fonts from a CDN breaks in Poca. That is the pledge working.

## Authentication

Poca makes a secret each time it starts (32 random bytes, kept in its memory only) and hands it to the box rows marked
`"auth": true`: in `POCA_SECRET` when Poca starts the program, and as the header `X-Poca-Secret` on the requests of its
pages. The program compares the two and refuses requests without it, so an ordinary browser, `curl`, or another
localhost page cannot use it. Checking is the program's job; Poca only hands the secret over.

Poca adds the header only to a request for a port of an `"auth"` row that Poca started and that still runs, and only
when the request comes from:

- the person: the url field, a bookmark, a tab opened from a Poca page, reload;
- a page of the same row (its links, fetches, redirects within the row).

A page of another port (its fetches, navigations, `window.open`, a redirect it answers with) never gets it, and a header
a page sets itself is dropped. Rows without `"auth"` never see the secret, not even one Poca itself inherited.

Not yet: WebSocket requests skip the handler that adds the header, so a WebSocket to an `"auth"` row goes without it
(tezcatl's relay on `:4100` needs another way; see `PROGRESS.md`).

`test/auth-app/server.py` is a small app that says OK with the secret and NG without; `test/e2e_auth.mjs` drives Poca
over CDP against it (`node test/e2e_auth.mjs`; it uses its own config and ports 4310, 4311, 9333, never
`~/.config/poca`).

## The window

Tabs on top (× closes one), then the url field, bookmark, reload and box buttons, then the page. Keys: `Ctrl+Tab` /
`Ctrl+Shift+Tab` move between tabs, `Ctrl+R` reloads, `Ctrl+Shift+B` opens and closes box. Copy, paste and editing keys are
CEF's defaults; every other key goes to the page.

Poca's own pages: `poca://home` (the logo and the pledge), `poca://box`, `poca://blocked` (shown when something outside was
refused).

## Settings (`~/.config/poca/`)

These are settings a person writes, not history.

`bookmarks.json`:

```json
[
  {"name": "tezcatl", "url": "http://127.0.0.1:4001/main.html"}
]
```

Keep secrets out of URLs; bookmarks are not encrypted.

`box.json` — the programs box can start:

```json
{"programs": [
  {"name": "tezcatl",
   "ports": [4001, 4100],
   "run": ["/nix/var/nix/profiles/default/bin/nix", "develop", "~/my_app/tezcatl",
           "--command", "~/my_app/tezcatl/nix/t"],
   "cwd": "~/my_app/tezcatl",
   "env": {},
   "log": "/tmp/poca-tezcatl.log"},
  {"name": "auth-test",
   "ports": [4300],
   "auth": true,
   "run": ["/usr/bin/python3", "-I", "~/my_app_no_nix/poca/test/auth-app/server.py", "--port", "4300"]}
]}
```

- `run` is an argument list, run without a shell; a leading `~` is expanded. It calls the program's own start command (for
  tezcatl, `t`) instead of copying what that command does. The command must stay in the foreground.
- `cwd` defaults to `~`, `log` to `/tmp/poca-<name>.log` (stdout and stderr).
- `"auth": true` hands the row Poca's secret (see Authentication).
- A row is grey when one of its ports is in use by something Poca did not start; Poca checks again when start is pressed.
- Start puts the program in its own session (`setsid`). Stop sends TERM to the group, then KILL after 10 seconds. Poca stops
  only what it started — and **closing the window stops everything Poca started**.
- A program that ended by itself shows its exit status and its log.
- Only `poca://box` can start or stop (a POST from that origin); a localhost page cannot read `poca://`.

## Build

Download the CEF binary distribution `cef_binary_152.0.14+g8a0b5b9+chromium-152.0.7977.167_linux64` into `vendor/` (not
tracked in Git; it expands to about 3 GB). Build outside any Nix shell, with CMake 3.21+, a C++20 toolchain and the Linux
development libraries CEF needs (X11, GLib, DBus, …):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
env -i HOME=$HOME PATH=/usr/bin:/bin cmake --build build --target poca -j
```

The result is `build/Release/poca` (with `poca_resources/` beside it). Start it with `./run`. `./install-desktop` adds
Poca to the desktop's application list (`~/.local/share/applications/poca.desktop`, `WM_CLASS` `poca`).

The CEF sandbox is off (its SUID helper needs root to install). `CMakeLists.txt` is adapted from the CEF distribution; review
the SDK's `LICENSE.txt` and redistribution terms before publishing builds.

## Development notes

- [What has been decided](Poca%20%E2%80%94%20%E5%8F%96%E3%82%8A%E6%B1%BA%E3%82%81.md) (`Poca — 取り決め.md`, Japanese; newer than the first brief)
- [Progress and next steps](PROGRESS.md)
- [Notes for agent sessions](CODEX.md)
