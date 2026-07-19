# Gate review: focus-ring-amber-fix — BLOCKED

**Verdict: BLOCKED — the task is structurally un-mergeable; merging the branch
delivers none of its objective.**

## The problem

The task's entire objective is to set `focuscolor` in `code/config/config.h`
to `COLOR(0xf0a030ff)` (amber). But `config.h` is **gitignored**:

- `.gitignore:6` → `config.h`
- `git check-ignore code/config/config.h` → matches
- `git ls-files code/config/` tracks only `README.md`, `config.def.h`,
  `config.mk`, `default_binds.h` — **not** `config.h`

Consequently the branch diff (`git diff main...fleet/focus-ring-amber-fix`)
contains **no code change at all** — only two markdown files:
`obsidian/agents/focus-ring-amber-fix/report.md` and
`obsidian/tasks/focus-ring-amber-fix.md`. The worker's actual one-line edit
lived in its worktree's private, gitignored `config.h` and cannot travel
through a git merge.

The primary checkout is unchanged and still teal:
```
code/config/config.h:13:     focuscolor[] = COLOR(0x005577ff)   <- running build, still teal
code/config/config.def.h:13: focuscolor[] = COLOR(0xf0a030ff)   <- template, amber
```

The worker's report (§"Important caveat") documented this honestly and
correctly. This gate confirms it.

## Why not merge

Merging would bring in the markdown and let the task be marked "merged" while
the running compositor still shows no amber ring — a false "shipped" status,
exactly the vault drift the project bans. The git merge is a no-op for the one
thing that mattered.

## Verification

Not run. The branch introduces no compilable change (diff is markdown-only),
so the gate's serial build/VM verification would exercise nothing about this
branch. The worker's own `make clean all` / `make test-unit` greens reflect
its worktree, which is now discarded.

## Resolution (keeper action required — outside git)

Because `config.h` is per-checkout and gitignored, a fleet worker branch can
never deliver this fix. Apply it directly to the primary checkout instead:

- Edit `code/config/config.h:13` `focuscolor` from `COLOR(0x005577ff)` to
  `COLOR(0xf0a030ff)` (one line, matches `config.def.h:13`), **or**
- delete the stale `code/config/config.h` and let `make` regenerate it from
  the already-amber `config.def.h` (`Makefile:117`).

Then the normal live verification (build + restart dance, see `[[dev-restart]]`)
confirms the amber ring.

## Process note

This task should not have been dispatched to the fleet: its sole scoped
deliverable (`code/config/config.h`) is a gitignored, per-checkout file with no
git-based merge path. Future config-value changes of this kind belong to the
keeper as a direct edit to the primary checkout, or must target the tracked
`config.def.h` plus a regenerate step — not a worker branch.
