# Poca progress and next steps

Updated: 2026-10-19

## Current state

Stages 1 and 2 of `Poca — 取り決め.md` are in place (see `README.md` for what they do):

- Stage 1: the CEF Views (Alloy style) window with tabs, url field, bookmarks, reload and box; the keys; `poca://home`,
  `poca://box`, `poca://blocked`. The localhost guard (OnBeforeBrowse, OnBeforeResourceLoad, popups as tabs, downloads
  refused) with a proxy `127.0.0.1:1` and a `<-loopback>` bypass as a second wall, because WebSocket bypasses the resource
  handler. No history: in-memory request contexts, the cache directory under `$XDG_RUNTIME_DIR` removed on exit (and a
  killed run's leftovers removed on the next start), logging off, `RLIMIT_CORE=0` and `PR_SET_DUMPABLE=0`.
- Stage 2: box starts and stops the programs in `~/.config/poca/box.json` (grey when a port is in use by something else,
  setsid, TERM then KILL after 10 s, closing the window stops what Poca started).
- Stage 3 on the tezcatl side: `nix/t` starts tezcatl alone (pages `:4001`, relay `:4100`) in the foreground and opens no
  browser; emacs-vent and Xipe Totec are separate rows of box. tezcatl keeps its saved skins, blocks, pane sizes, matrix
  colours, bookmarks and kill ring in files (`~/.local/state/tezcatl/work/store/`, its D-store) instead of localStorage,
  which Poca would lose on every close. Both are committed in tezcatl (`4949ca2`).

This machine's `~/.config/poca/box.json` has three rows: tezcatl (4001, 4100), emacs (4000), xipe (4200), all through Nix.

## Verified

- By the agent, over CDP and XTest: the window, tabs and keys; the guard (outside URLs, subresources, WebSocket, popups,
  downloads); nothing left in the cache directory after exit.
- By crux (2026-10-19): Poca started from the application list → box starts tezcatl, emacs and xipe → the bookmark opens
  tezcatl → closing Poca stops all three; the rows turn back and the pages become unreachable.
- An earlier failure to stop was not Poca's: an old `t -d` had left a relay, Emacs and Xipe Totec running, so box saw their
  ports in use and treated the rows as not started by Poca.

Two core dumps from crashes before the fixes remain in `/var/lib/systemd/coredump` (removing them needs root).

## Next work

1. Stage 4: authentication with tezcatl — Poca makes a secret at start, hands it to the programs it starts through the
   environment, and adds it as a header to page requests to the allowed ports only. Designed together with tezcatl's side.
2. The Nix-free versions of tezcatl, xipe_totec, emacs-vent and embody come last; box's rows then call them instead of
   `nix develop`.
