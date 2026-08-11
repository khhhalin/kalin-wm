# Worker brief: screenshot-crash-fade

Task substance only — pipeline mechanics (autonomy, signal protocol, report
format, merge/push rules) and the project addendum are appended automatically
by fleet-deck at spawn.

---

**Task:** screenshot-crash-fade — fix the compositor crash when the cursor
hovers the [[screenshot-ui]] info panel, and add the requested fade-on-approach.
This is a **stability crash** (the whole compositor dies) — the crash fix is the
priority; the fade is a secondary UX enhancement that does NOT fix the crash.

**Read first (do NOT edit):** `obsidian/implementation/screenshot-ui.md` — its
"Hover crash + fade-on-approach" section is the full spec and names every touch
site. Also skim how paper mode's `paper_node_rejects_input()` works in `dwl.c`
(the precedent for part 2) and `[[stability]]` for the defensive-C rule.

**Scope — you may edit ONLY these paths:**
- `code/src/dwl.c` — the crash guard + the hover hook (see below). Keep the diff
  tight; this is the single hot file.
- `code/src/modules/screenshot/screenshot_ui.c` — the input-reject callbacks on
  the decorative buffers + the opacity/fade math.
- `obsidian/implementation/screenshot-ui.md` (its impl note — move the section
  from "fix not yet built" to as-built once done)
- `obsidian/agents/screenshot-crash-fade/` (report zone)

**The work, in priority order:**
1. **MANDATORY crash fix** — in `xytonode()` (`dwl.c`), NULL-check the result of
   `wlr_scene_surface_try_from_buffer()` before dereferencing `->surface`. The
   info readout is a plain pixel buffer (not a Wayland surface), so
   `try_from_buffer()` returns NULL and `->surface` segfaults; `motionnotify()`
   runs `xytonode()` on every pointer motion while the UI is active, so hovering
   the bottom-center panel crashes instantly. This guards a whole *class* of bug
   (any non-surface overlay buffer) — it is the defensive-C/[[stability]] rule.
2. **Tidy (belt-and-suspenders)** — give the info panel + frozen-frame
   `wlr_scene_buffer`s a `point_accepts_input` callback returning false, exactly
   like `paper_node_rejects_input()`, so `wlr_scene_node_at()` skips these
   decorative buffers instead of hit-testing them. The callback wiring is where
   the buffers are created in `screenshot_ui.c`.
3. **Fade-on-approach (the requested UX)** — drive
   `wlr_scene_buffer_set_opacity()` on the info node by cursor distance so the
   readout gets out of the way when selecting near bottom-center. Needs a
   per-motion hook: today `screenshotui_draw()` is called only while *dragging*
   (`dwl.c` motionnotify), so add a call that updates opacity on every motion
   while the UI is active. **Use a low alpha floor (~0.15), not full
   transparency** — fading to 0 would hide the live `W×H AT (x,y)` readout right
   when dragging near it. Opacity is unrelated to hit-testing, so this does NOT
   substitute for part 1.

**Verify:** worktree-safe checks only — `nix develop -c make clean all` (exits
0) and `nix develop -c make test-unit` (all pass), green before and after. The
crash itself reproduces only live (hover the panel), which is keeper-only VM/host
verification — do NOT attempt it; confirm the guard compiles and is logically
correct, and describe the reasoning in your report.
