# Agent workflow

- kalin-wm is built with a roadmap-driven workflow for AI/coding agents.
- An agent must, before writing code, read this vault — the [[kalin-wm]] goal note, the [[ledger]], and the relevant object notes — to learn what already exists and what is prioritized.

- The work rules: stability first ([[stability]] outranks all feature work).
- The work rules: minimal, suckless-adjacent changes (no new dependencies or abstractions without need).
- The work rules: defensive C.
- The work rules: no dead code.
- The work rules: follow existing style.
- The work rules: keep the umbrella header `code/include/kalin.h` and [[compile-time-config]] defaults in sync.

- Explicit non-goals: no runtime configuration.
- Explicit non-goals: no text rendering / font libraries / cairo / pango.
- Explicit non-goals: no rewriting the monolith into modules unless asked.

- After completing work, an agent updates the [[ledger]] and the affected object notes.
- This note supersedes the old root `AGENTS.md`.
