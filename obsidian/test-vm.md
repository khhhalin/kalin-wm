# Test VM

The test VM is a real, non-nested QEMU/KVM NixOS VM that runs kalin-wm as the
actual session compositor on a virtual GPU, with the [[quickshell-shell]]. It
lives in `~/environment/test-vm`. It exercises the DRM backend, real seat/input,
and GL rendering — things headless/nested tests cannot.

It is the current development driver. The loop is: commit kalin-wm, then
`nix build .#vm` and run `./result/bin/run-kalin-test-vm`. It consumes the
[[build-system|kalin-wm flake]] and Quickshell as flake inputs. GL acceleration
comes from `virtio-vga-gl` + virgl with `WLR_NO_HARDWARE_CURSORS=1`.

On boot it autologins and runs `kalin-wm -s <script>`, where the script launches
`qs` (the shell) and a `foot` terminal. Compositor and shell logs are streamed to
host files over virtio-serial ports for debugging; udev rules give the autologin
user ownership of those ports so the session can write them.

Build gotcha: `nix build .#vm --no-link` does not update the `result` symlink, so
relaunching reruns the stale image; build without `--no-link`. Run
`nix flake update kalin-wm` in the VM dir when the kalin-wm tree changes.
