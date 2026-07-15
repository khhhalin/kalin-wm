# wlroots

- wlroots is the Wayland compositor library kalin-wm is built on.
- The project requires **wlroots 0.20** (the Makefile links `pkg-config wlroots-0.20`), compiled with the libinput backend.

- wlroots provides the [[scene-graph]] used for rendering, the DRM/libinput backends, and the implementations behind most supported Wayland protocols.
- The [[build-system]] pins a nixpkgs that ships wlroots 0.20.

- kalin-wm implements 15 essential and 18 of 21 recommended Wayland protocols on top of wlroots; the full matrix is in [[research/protocols/protocol-matrix|the protocol matrix]].
