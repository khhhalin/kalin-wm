#!/usr/bin/env bash
# Dev stand-in for the packaged kitty bar command (P4): $1 = app_id.
# The bar's python must be the withPackages ENV interpreter by absolute path:
# a bare `python3` loses twice — kitty's nixpkgs wrapper PATH-prefixes its own
# bundled python for kittens, and kitty's propagated deps put a bare python3
# ahead of the env one even inside this nix-shell. $buildInputs names the env
# store path directly (suffix "-env"). The P4 packaged wrapper interpolates
# the absolute path at build time and dodges all of this.
exec >>/tmp/kalin-bar-wrap.log 2>&1; date
exec nix-shell -p kitty -p "python3.withPackages (ps: [ps.textual ps.textual-image ps.psutil])" --run "
  PY=\$(echo \$buildInputs | tr ' ' '\n' | grep -- '-env\$')/bin/python3
  echo \"PY=\$PY\"
  export PYTHONPATH=/home/kalin/environment/kalin-wm/tools/bar-tuis
  exec kitty --config NONE --class=$1 \
    -o background=#1e1915 -o background_opacity=0.88 -o font_size=11 \
    -o 'font_family=JetBrainsMono Nerd Font' \
    sh -c \"exec \$PY -m kalin_tuis bar\"
"
