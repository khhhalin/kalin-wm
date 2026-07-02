# NixOS session

The NixOS session wires kalin-wm into the host as a login option. It lives in the
user's NixOS config repo `~/home-config` (host `KalinBook`, `ly` display
manager). The kalin-wm flake is a flake input; `display.nix` installs the package
and writes `/etc/wayland-sessions/kalin-wm.desktop` so `ly` lists it alongside
niri.

The flake input deliberately does **not** follow the system nixpkgs: the system
nixpkgs ships a different [[wlroots]], so kalin-wm keeps its own pin (wlroots
0.20) via the [[build-system]] flake. Because the input is a local `git+file`,
rebuilds pick up only committed kalin-wm changes.

The session starts the compositor together with the [[quickshell-shell]] and a
`foot --server` terminal via the `kalin-wm-session` wrapper defined in
`~/home-config/display.nix`:

```nix
kalinSession = pkgs.writeShellScriptBin "kalin-wm-session" ''
  export QS_CONFIG_PATH="/home/kalin/environment/quickshell"
  exec ${kalinPkg}/bin/kalin-wm -s 'qs & foot --server'
'';
```

The wrapper sets `QS_CONFIG_PATH` so `qs` loads the shell from
`~/environment/quickshell` and starts Quickshell and the terminal alongside the
compositor.

Activate with:

```bash
sudo nixos-rebuild switch --flake /home/kalin/home-config#KalinBook
```

Then select **kalin-wm** from `ly` at login. As of the latest [[ledger]] entry
the session is wired but has not yet been activated with `nixos-rebuild switch`.
