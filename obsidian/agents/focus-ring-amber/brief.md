# Worker brief: focus-ring-amber

Task substance only — pipeline mechanics (autonomy, signal protocol, report
format, merge/push rules) and the project addendum are appended automatically
by fleet-deck at spawn.

---

**Task:** focus-ring-amber — re-theme the focused-window ring from the inherited
dwl teal-blue to the project's warm amber accent. Config-only change; decided
2026-07-18 and recorded in `obsidian/implementation/focus-ring.md`.

**Read first (do NOT edit):** `obsidian/implementation/focus-ring.md` (the
"Color (decided 2026-07-18)" section is the spec) and `[[compile-time-config]]`
for how the paired `config.def.h`/`config.h` files relate.

**Scope — you may edit ONLY:**
- `code/config/config.def.h`
- `code/config/config.h`
- `obsidian/agents/focus-ring-amber/` (report zone)

Do NOT touch `obsidian/implementation/focus-ring.md` — its Color section already
documents this change as canon; no impl-note update is needed (the note led the
code, not the reverse). If you believe it needs a wording change, note it in
your report instead of editing it — it is outside your scope.

**The change:** set `focuscolor` to `COLOR(0xf0a030ff)` (warm amber, the shell
accent) in place of the current `COLOR(0x005577ff)`, in **both**
`config.def.h` and `config.h` — they are kept in lockstep (see
[[compile-time-config]]); the line is `code/config/*.h:13` today. Change only
`focuscolor`; leave `bordercolor` and `urgentcolor` untouched.

**Out of scope by design:** the separate bar-over-ring *layering* fix noted in
`focus-ring.md` is a `dwl.c` scene-layer change owned by another sequence — do
NOT attempt it here.

**Verify:** worktree-safe checks only — `nix develop -c make clean all` (exits
0) and `nix develop -c make test-unit` (all pass), green before and after. A
pure color constant should not move any test; report honestly if it does.
