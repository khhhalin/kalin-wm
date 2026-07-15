# Agent workflow

- kalin-wm is built with a roadmap-driven workflow for AI/coding agents.
- An agent must, before writing code, read this vault — the [[kalin-wm]] goal note and the relevant object notes — to learn what already exists and what is prioritized (the [[roadmap]] holds the forward backlog).

- The work rules: stability first ([[stability]] outranks all feature work).
- The work rules: minimal, suckless-adjacent changes (no new dependencies or abstractions without need).
- The work rules: defensive C.
- The work rules: no dead code.
- The work rules: follow existing style.
- The work rules: keep the umbrella header `code/include/kalin.h` and [[compile-time-config]] defaults in sync.

- Explicit non-goals: no runtime configuration.
- Explicit non-goals: no text rendering / font libraries / cairo / pango.
- Explicit non-goals: no rewriting the monolith into modules unless asked.

- Deployment guardrail: never auto-activate the real host login session (`sudo nixos-rebuild switch … home-config#KalinBook`) — only when Kalin explicitly asks, and only after VM tests pass.
- Disk guardrail: avoid `nix build`/`nixos-rebuild` churn while iterating (each leaves a new store path); use `nix develop -c make` into local `build/`. See [[dev-restart]] and [[test-vm]] for the iterate loop.

- The vault graph itself is the running record of the project: after completing work, an agent updates the affected object notes (and [[roadmap]] if priorities shifted). There is no separate ledger to append.
- [[ledger]] is a frozen historical archive of past decisions, not updated going forward — the object notes carry current truth.
- When work is run as a supervised fleet of parallel worker agents, [[fleet-workflow]] adds the kalin-wm-specific rules (disjoint scope, `dwl.c` as the one hot shared file, and worktree-only vs keeper-only verification) on top of these.
- This note supersedes the old root `AGENTS.md`.
