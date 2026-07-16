# App launcher (kalin-launch)

- **Warm-amber `fzf` launcher, replaces fuzzel** on `Super+P` and `tap Super`
  (2026-07-16). Fuzzel was slow/unreliable (icon-theme loading + `.desktop`
  fuzzy-match quirks — "selected but nothing launched"); launching by exact
  command was always instant, which is what this leans into.
- **Not a Textual TUI on purpose.** A launcher is opened/dismissed constantly,
  so Python+Textual cold-start (~200-400ms) would feel like fuzzel. `fzf` in a
  foot window is C-fast, keyboard-native, and themes to the bar's warm palette —
  it *is* a TUI, just not the [[bar-tuis]] Textual chrome.
- **Presentation: a plain foot toplevel**, same accepted pattern as
  `kalin-clip-picker` (`Super+V`) — no compositor floating/overlay work. Bind:
  `foot --app-id=kalin-launcher -e kalin-launch`. `tap Super`'s
  `toggle-launcher` tracks it as the `kalin-apps` tmux window "launcher" (see
  [[spawn]]), killed to toggle off — works for any GUI command, foot included.
- **Script:** `tools/launcher/kalin-launch` (working-tree file, editable without
  a rebuild — same precedent as the `kalinwm` dev launcher). The `desktop.nix`
  `kalinLaunch` wrapper only PATH-pins fzf/foot/tmux/util-linux/python3 and execs
  the working-tree path (so it is NOT pinned/reproducible — acceptable for a
  personal launcher, matches how `kalinwm` hardcodes the repo path).
- **Three sources, one merged list** (TSV `TYPE⇥DISPLAY⇥PAYLOAD[⇥term]`, fzf
  `--with-nth=2` shows only DISPLAY, full line returned for dispatch):
  - `app` — `.desktop` entries (python parses Name/Exec, strips `%f/%U/...`
    field codes, honours `Terminal=true`→`foot -e`, skips NoDisplay/Hidden,
    dedups by name). Launched detached (`setsid -f`), foot closes.
  - `cmd` — every `$PATH` executable (dmenu_run style). Run in the launcher's
    own foot via `exec`. GUI tools are better picked from the app list.
  - `tmux` — live `tmux list-sessions` (attach) + a "new terminal" entry
    (`kalin-term`), folding in what `kalin-term-pick` did.
- **Cache:** apps+commands cached to `$XDG_CACHE_HOME/kalin-launch/entries.tsv`,
  rebuilt only when an app dir is newer than the cache (catches installs) — so
  it pops instantly (~42 apps + ~1350 cmds here). tmux is live each open.
- **Theme:** `fzf --color` mapped to [[quickshell-shell|Theme.qml]] tokens
  (bg `#1e1915`, fg `#f0ddc0`, accent/prompt/hl `#f0a030`, current-line
  `#3d2c1c`, border `#4a3625`), `--reverse --border=rounded`, prompt `❯`.
- **Fuzzel** is retheme'd to the warm palette (`~/.config/fuzzel/fuzzel.ini`)
  and still installed — `kalin-term-pick` uses `fuzzel --dmenu`. **Follow-ups:**
  migrate `kalin-term-pick` fully into `kalin-launch`'s tmux source, then retire
  fuzzel; optional background cache refresh; optional dedicated foot sizing.
