# Test VM

- The test VM is a real, non-nested QEMU/KVM NixOS VM that runs kalin-wm as the actual session compositor on a virtual GPU, with the [[quickshell-shell]].
- It lives in `~/environment/test-vm`.
- It exercises the DRM backend, real seat/input, and GL rendering — things headless/nested tests cannot.

- It is the current development driver.
- The loop is: commit kalin-wm, then `nix build .#vm` and run `./result/bin/run-kalin-test-vm`.
- It consumes the [[build-system|kalin-wm flake]] and Quickshell as flake inputs.
- GL acceleration comes from `virtio-vga-gl` + virgl with `WLR_NO_HARDWARE_CURSORS=1`.

- On boot it autologins and runs `kalin-wm -s <script>`, where the script launches `qs` (the shell) and a `foot` terminal.
- Compositor and shell logs are streamed to host files over virtio-serial ports for debugging.
- udev rules give the autologin user ownership of those ports so the session can write them.

- Build gotcha: `nix build .#vm --no-link` does not update the `result` symlink, so relaunching reruns the stale image; build without `--no-link`.
- Run `nix flake update kalin-wm` in the VM dir when the kalin-wm tree changes.
- **Sharp gotcha: a brand-new, never-`git add`-ed source file is invisible to the VM's
  build**, even though a local `nix develop -c make` sees it fine. The flake's `path:`
  input evaluates the git-tracked view of the tree — modifications to already-tracked
  files show up, but a wholly untracked new file (e.g. a new module) silently drops out,
  and the resulting build just fails with a confusing `No rule to make target
  .../newfile.o` (or, worse, silently links a stale binary missing the new code with no
  error at all, if the old `.o` happens to already exist from a previous successful
  build). Fix: `git add` the new file (staging is enough, no need to commit) before
  `nix build .#vm`.
- **Another sharp gotcha: the VM's disk persists `~/.config/kalin-wm/binds.conf` across
  `vmctl down`/`up` cycles** (only the QEMU process restarts, not the disk image).
  `binds_init()` only writes the compiled-in `DEFAULT_BINDS` to that path if it doesn't
  already exist — so after changing `default_binds.h`, a keybind that already had an
  entry on a previous boot keeps its *old* behavior indefinitely until that file is
  deleted (from a terminal inside the VM) and the VM rebooted, or the file is edited
  directly (binds.conf hot-reloads live via inotify, so an edit alone, no reboot, is
  enough if you'd rather not delete it).
- `scripts/vmctl.py` also has a `click X Y [BUTTON]` command (QMP `input-send-event`,
  assumes the VM's 1280x800 default) for testing mouse interaction, not just keyboard.
- **tty2 runs a second real compositor (niri), added 2026-07-09, untested**: added
  to reproduce a DRM-master-handoff scenario (kalin-wm on one VT, a competing
  compositor on another — matching the real host setup) rather than a plain VT
  switch, which was confirmed *not* to crash kalin-wm on its own. `vm.nix` pulls
  `pkgs.niri` (nixpkgs, not a new flake input) and autologins it on tty2 via a
  `getty@tty2` `ExecStart` override (`autologinUser` only wires up tty1). This
  build stalled for 35+ minutes on a from-source niri compile and was killed
  before finishing — the wiring is believed correct but **has never actually
  booted**. Revisit if reproducing a DRM-master-contention bug again; consider
  prefetching/pinning a cached niri build first so the rebuild isn't from source.
