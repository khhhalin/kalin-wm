# Agent workflow

kalin-wm is built with a roadmap-driven workflow for AI/coding agents. An agent
must, before writing code, read this vault — the [[kalin-wm]] goal note, the
[[ledger]], and the relevant object notes — to learn what already exists and what
is prioritized.

The work rules: stability first ([[stability]] outranks all feature work);
minimal, suckless-adjacent changes (no new dependencies or abstractions without
need); defensive C; no dead code; follow existing style; keep the umbrella header
`code/include/kalin.h` and [[compile-time-config]] defaults in sync.

Explicit non-goals: no runtime configuration, no text rendering / font libraries
/ cairo / pango, no rewriting the monolith into modules unless asked.

After completing work, an agent updates the [[ledger]] and the affected object
notes. This note supersedes the old root `AGENTS.md`.
