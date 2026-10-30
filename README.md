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
- a page of the same row (its links, fetches, redirects within the row, and the tabs and windows it opens).

A page of another port (its fetches, navigations, `window.open`, a redirect it answers with) never gets it, and a header
a page sets itself is dropped. Rows without `"auth"` never see the secret, not even one Poca itself inherited.

WebSocket requests skip the handler that adds the header, so a WebSocket goes through a **ticket**: the page first
fetches a ticket over HTTP (that request gets the header), then opens the WebSocket with the ticket in its URL. The
program gives a ticket only to a request with the secret, accepts it for one handshake within a few seconds, and
refuses a WebSocket without a good one. The secret itself never goes into a URL. Poca needs nothing more for this; it
is the program's part.

`test/auth-app/server.py` is a small app that does both: OK / NG for the page, and `/ticket` with `/ws` (shown on its
page as WS OK / WS NG). `test/e2e_auth.mjs` drives Poca over CDP against it (`node test/e2e_auth.mjs`; it uses its own
config and ports 4310, 4311, 9333, never `~/.config/poca`).
`test/e2e_window.mjs` checks the windows the same way (ports 4320, 4321, 9334; it counts the windows with `wmctrl` and
sends `Ctrl+N` through XTest, so the display needs a window manager: on a display of its own, `Xvfb :99` and `openbox`).
`test/e2e_split.mjs` checks the split and the strip's keys (port 4330): xdotool sends the keys and a screenshot says
which tab's colour is at the left and the right of the page. Run it on a display of its own, without a window manager
(`Xvfb :99`, `DISPLAY=:99 node test/e2e_split.mjs`; `POCA=` and `XDOTOOL=` say where they are), so that neither the keys
nor the window reach the desktop. All three start Poca with `--no-first-run` (in a fresh profile Chromium's first-run
dialog, its terms of service, would stop it) and take `POCA=` for another build (e.g. `build-dev/Release/poca`).

## The window

Tabs on top (× closes one), then the url field, bookmark, reload and box buttons, then the page. Keys: `Ctrl+Tab` /
`Ctrl+Shift+Tab` move between tabs, `Ctrl+R` reloads, `Ctrl+Shift+B` opens and closes box, `Ctrl+N` opens a new window
(with `poca://home`), `Ctrl+Shift+PageUp` / `Ctrl+Shift+PageDown` move the active tab left / right in the strip. Copy,
paste and editing keys are CEF's defaults; every other key goes to the page.

**Two tabs side by side (a split).** `Ctrl+Shift+S` picks a tab to show beside the active one: the strip marks the tab
picked with `▸`, `←` / `→` move the mark, `Enter` takes it (a click on a tab too), `Escape` leaves it. The active tab goes on
the left, the one picked on the right, half the width each; the one picked moves next to it in the strip, so `Ctrl+Tab` goes
from one side to the other and the split stays. The sides are marked `◧` (left) and `◨` (right); going to any other tab
shows that one alone, and coming back to either side shows the two again. With a side of the split active:
`Ctrl+Shift+X` picks a tab to take the active one's place (the active one goes back where the picked one was in the strip),
or picks the other side to swap the two; `Ctrl+Shift+S` undoes the split. Closing a side undoes it too. While picking, the
toolbar says what is asked. These keys were chosen to leave the pages their own `Ctrl+S` (save in many editors) and
`Ctrl+X` (Emacs' prefix).

One Poca can have several windows. A page's `window.open` with features (`width=…`, `popup`) and a shift-click open a new
window; `window.open` without features, `target=_blank` and a middle click open a tab in the page's own window. CEF makes
the popup and Poca places it, so `window.open` returns the new window as in a browser, and Chromium's pop-up blocker still
applies: without a key press or click shortly before, `window.open` gets `null` and nothing opens. The size
the features ask for is not followed (every window starts 1200×800). A window or tab a page opens keeps who opened it, so
a page of an `"auth"` row keeps the secret in the windows it opens (see Authentication); the windows share the in-memory
request context, so they also share what pages keep while Poca runs. A second Poca started on its own is a separate
run with its own secret and has started none of the first one's programs: their pages get nothing from it.

Closing a window closes its tabs; Poca ends with its last window.

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
  only what it started — and **closing Poca's last window stops everything Poca started**.
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

The window icon is `icon/poca-128.png` and `icon/poca-256.png`, made from `poca.png` (which stays the launcher's icon and the
home page's logo). A 2048 px icon is too large to put on an X11 window, and Chromium's logo is shown instead. After changing
`poca.png`, make them again: `for s in 128 256; do magick poca.png -resize ${s}x${s} -strip PNG32:icon/poca-$s.png; done`.

The CEF sandbox is off (its SUID helper needs root to install). `CMakeLists.txt` is adapted from the CEF distribution; review
the SDK's `LICENSE.txt` and redistribution terms before publishing builds.

## Development notes

- [Progress and next steps](PROGRESS.md)

## License

MIT ([LICENSE](LICENSE)). `CMakeLists.txt` and `src/main.cc` are adapted from the CEF binary distribution; the parts taken
from it remain under CEF's BSD-style license (see the notes at the top of those files).
