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
- Stage 4, first step: the authentication test pattern. Poca makes a secret per run, hands it to `"auth"`
  rows in `POCA_SECRET` and adds `X-Poca-Secret` to requests from the person or from a page of the same row (README,
  "Authentication"). CEF gives navigations no initiator (`null`), so the window records who opens each tab and when the
  person uses the url field; a navigation otherwise comes from the page it leaves; CEF makes a new handler for each
  redirect hop, and a hop keeps the secret only when the previous one carried it. `test/auth-app/server.py` says OK / NG;
  `test/e2e_auth.mjs` checks 25 cases (all ok, run twice).

This machine's `~/.config/poca/box.json` has four rows: tezcatl (4001, 4100), emacs (4000), xipe (4200), all through Nix,
and auth-test (4300, `"auth": true`); `bookmarks.json` has tezcatl and auth-test. The files before auth-test was added
are kept beside them as `*.bak-2026-10-19-auth`.

## Verified

- By the agent, over CDP and XTest: the window, tabs and keys; the guard (outside URLs, subresources, WebSocket, popups,
  downloads); nothing left in the cache directory after exit.
- By crux (2026-10-19): Poca started from the application list → box starts tezcatl, emacs and xipe → the bookmark opens
  tezcatl → closing Poca stops all three; the rows turn back and the pages become unreachable.
- An earlier failure to stop was not Poca's: an old `t -d` had left a relay, Emacs and Xipe Totec running, so box saw their
  ports in use and treated the rows as not started by Poca.

Two core dumps from crashes before the fixes remain in `/var/lib/systemd/coredump` (removing them needs root).

## Next work

1. Done (crux, 2026-10-19): the checks the E2E cannot reach (the window's own views): in Poca, the url field to
   `127.0.0.1:4300` (OK, also while another page is shown), the reload button and Ctrl+R on it (OK); in Brave,
   `http://127.0.0.1:4300/` (NG).
2. Stage 4 with tezcatl: the relay (and through it emacs-vent and Xipe Totec) checks the secret. Open: WebSocket requests
   skip `OnBeforeResourceLoad`, so the header cannot be added to the relay's WebSocket; another carrier is needed (not a
   cookie: cookies ignore the port and would reach every localhost app). Designed together with tezcatl's side.
3. Poca does not stop what it started when it is killed by a signal (TERM, logout); only closing the window does.
4. The Nix-free versions of tezcatl, xipe_totec, emacs-vent and embody come last; box's rows then call them instead of
   `nix develop`.
