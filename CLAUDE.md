# kalin-wm project preferences

Project-level overrides for `/home/kalin/environment/kalin-wm`. The global
`~/.claude/CLAUDE.md` still applies everywhere else; this file wins on conflict
for work in this repo.

## Communication
- Be concise and direct; minimal justification.
- Surface tradeoffs and risks honestly; say when an idea is bad.
- If an ambiguous choice matters, ask one sharp question instead of guessing.
- Report outcomes faithfully — if tests fail, a VM check was skipped, or the
  vault update below was skipped, say so.
- Default to English; Polish is fine when the user writes in Polish. Code,
  identifiers, and commits stay English.

## Coding
- Match the surrounding code's naming, structure, comment density, and idioms;
  read neighbors first. This codebase is defensive C in the suckless tradition
  (see `obsidian/agent-workflow.md`) — every pointer deref NULL-checked, every
  divisor non-zero, no dead code, no new dependencies without need.
- Reuse existing utilities over new abstractions; YAGNI, no speculative
  generality.
- Comments and docstrings explain *why*, never *what*. Handle errors explicitly.
- Keep changes minimal and focused; don't reformat unrelated code.
- Add/update tests when behavior changes (`make test-unit`); run it and the
  full build (`nix develop -c make clean all`) before calling work done.
- Toolchain is Nix: no imperative global installs. `nix-shell -p <pkg>` for
  ad hoc tools (e.g. `gdb`, not installed by default — needed for
  `coredumpctl` backtraces).

## Vault discipline — read this twice

**The `obsidian/` vault is load-bearing, not optional documentation.** A
stale vault note is a bug, exactly as much as a stale code comment or a
broken test — it doesn't get deferred to "later," it gets fixed in the same
turn as the change that made it stale.

This isn't a hypothetical concern. In one session, the vault was found to
have silently drifted for an extended period: it still described a compositor
architecture (`column-layout` + `anchored-window`) that had been fully
replaced by a connection-graph model, `window-menu.md` claimed a shipped
feature was "not yet implemented," and `keybindings.md` had fallen behind two
separate rebindings of the same key. None of this produced an error or a
warning — it just cost real time to rediscover from scratch, in more than one
session, because the vault said one thing and the code said another.

- **Before** starting any non-trivial change: read `obsidian/kalin-wm.md` (the
  goal note), `obsidian/ledger.md` (recent history), and any object notes that
  touch the area being changed. Don't assume a note is current — check it
  against the actual code if the change depends on it being right.
- **After** any change that alters behavior, architecture, keybinds, IPC
  contract, or the status of a planned/in-progress feature:
  - Add a dated entry to `obsidian/ledger.md` (newest-first, absolute dates)
    describing what changed and why.
  - Update every object note the change touches. If a note now describes
    something removed or renamed, fix it or mark it superseded with a pointer
    to what replaced it — don't leave it describing dead architecture.
  - If a feature moved from planned to shipped (or vice versa), correct its
    status inline. Don't leave "planned"/"not yet implemented" language on
    something that already works.
- When investigating a bug that involved reading vault notes to understand
  "how this is supposed to work," and any of those notes turned out to be
  wrong: fix them as part of the same change, not as a follow-up.

## Git & workflow
- Commit or push only when asked. Focused commits, messages explaining *why*.
- Don't amend/rebase/force-push shared history or add AI co-author trailers
  unless asked.
- PRs: concise summary of what changed and why, plus how it was tested.

## Safety & guardrails
- Confirm before destructive or irreversible actions; look at a target before
  deleting/overwriting it.
- Approval for one action doesn't extend to the next — ask again. Outline a
  plan before wide-reaching changes.
- **Activating the real login session is a hard stop.** Per `AGENTS.md`:
  `sudo nixos-rebuild switch --flake /home/kalin/home-config#KalinBook` (or
  any other change to the host's actual boot/login config) requires explicit
  user approval, every time — never run it as a natural next step after VM
  tests pass, no matter how confident the VM run was.
- Treat anything sent to an external service as published — confirm before
  posting code or data outward.

## Pointers
- `AGENTS.md` — build/run/test/VM commands and expected outputs; read this
  for *how* to do something, not this file.
- `obsidian/kalin-wm.md` — project goal note.
- `obsidian/ledger.md` — recent decisions and progress.
- `obsidian/roadmap.md` — open and planned work.

Note: `obsidian/agent-workflow.md` claims to supersede the old root
`AGENTS.md`, but `AGENTS.md` still exists and is current — an unresolved
inconsistency, not a signal to ignore either file.
