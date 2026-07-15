# Fleet workflow

- The `fleet` skill runs kalin-wm work as a supervised set of parallel worker agents that cannot clobber each other; this note records the kalin-wm-specific adaptation. It extends [[agent-workflow]], it does not replace it — the stability-first work rules there still bind every worker.
- The keeper (Kalin's live conversation) owns the canon vault and `obsidian/tasks/`; workers run in isolated git worktrees and are the only writers of their own `obsidian/agents/<task-id>/`.

## Partition rule (kalin-wm-specific)
- Each `tasks/<task-id>.md` lists an explicit disjoint `scope` of file paths; no two active tasks may overlap.
- `code/src/dwl.c` is the ~4.1k-line monolith and the hottest shared file — at most one active task may own it. Two workers both editing `dwl.c` is the main conflict risk here.
- Prefer partitions that add a **new module** under `code/src/modules/` over ones that edit `dwl.c`, per the [[roadmap]] rule (e.g. protocol work lands in `code/src/modules/protocols/`, leaving only a one-line `wlr_*_create()` in `setup()`). New-file tasks parallelize cleanly; monolith edits do not.
- Watch the `code/include/kalin.h` vs `dwl.c`-own-`Client` duplication (see [[compile-time-config]]) — a task touching shared structs implicitly touches both and should be a single task.

## Verification is split (do not let workers do end-to-end)
- Workers verify **only** what is isolated to their worktree: `nix develop -c make clean all` (green, exits 0) and `nix develop -c make test-unit` (all pass). This builds into the worktree's local `build/`, not the Nix store — no churn.
- The keeper runs all **shared, serial** verification at the integration gate, never a worker in parallel:
  - VM tests — the [[test-vm]] consumes kalin-wm via a flake input pointing at the working tree, a shared global; only one merged state can be tested at a time.
  - Live-host restart — there is one real running compositor; see [[dev-restart]] for the restart dance.
- Never run `nix build`/`nixos-rebuild` to "verify the package" as routine — disk-space guardrail. Those are keeper-only, on explicit request, after VM tests pass.

## Fold-back
- After merging a worker branch, the keeper folds its `agents/<task-id>/report.md` into canon: update the affected object notes (and [[roadmap]] if priorities shifted) via the snippet workflow (setup-vault skill). The vault graph is the record — there is no [[ledger]] to append (it is a frozen archive, see [[agent-workflow]]). Workers do not touch canon.
