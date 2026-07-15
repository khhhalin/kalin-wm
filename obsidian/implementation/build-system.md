# Build system

- kalin-wm builds with `make`:

```bash
make                 # release build → build/kalin-wm
make debug           # debug build with symbols and assertions
make test-unit       # run C unit tests
make clean           # remove build artifacts
```

- In the Nix devShell:

```bash
cd /home/kalin/environment/kalin-wm
nix develop -c make clean all
nix develop -c make test-unit
```

- The Makefile compiles [[dwl-fork|dwl.c]] plus the independent translation units, and generates Wayland protocol headers with `wayland-scanner`.

- A Nix flake provides a reproducible environment (`nix develop`) and a hermetic package (`nix build`).
- The package bakes the runtime library RPATH into the binary so it finds [[wlroots]] 0.20 when run bare.
- The flake deliberately pins its own nixpkgs (with wlroots 0.20) rather than following any consumer's nixpkgs.

- The flake is consumed as an input by both the [[test-vm]] and the [[nixos-session]].
- Building needs `wlroots-0.20`, `wayland-server`, `xkbcommon`, `libinput`, `wayland-protocols`, and `pkg-config`.
- kalin-wm is Wayland-only — the XWayland/X11 client support dwl carried has been removed (see [[ledger]]).
