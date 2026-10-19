# Notes for agent sessions

Start each work session by reading `README.md`, `PROGRESS.md` and `Poca — 取り決め.md`. The first two describe the
current implementation and the next steps; the agreements record the decisions and are newer than
`Poca — 開発開始のための文書.md` (the first brief, 2026-10-05), where the two disagree.

## Working conventions

- The user speaks Japanese. Reply in Japanese and keep progress updates short and concrete. Code comments and documents
  in English (the agreements are in Japanese).
- Poca knows nothing about what it shows. Pages come from bookmarks, programs from `box.json`; box calls each program's
  own start command and does not copy it.
- Keep the pledge true at every stage: localhost only for the window, no history.
- Build outside any Nix shell: `env -i HOME=$HOME PATH=/usr/bin:/bin cmake --build build --target poca`.
- The CEF SDK and build output are ignored by Git. Preserve upstream CEF license and attribution notices when adapting
  its sources.
- Do not commit or push unless the user explicitly asks.

## Source of truth

The running code is authoritative over prose. When the implementation changes, update `README.md` and `PROGRESS.md` (and
the agreements, when a decision changes) so the next session can resume without the conversation.
