{
  description = "kalin-wm - A simple Wayland compositor based on dwl";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      # Runtime libraries the compositor dlopen/links against.
      deps = with pkgs; [
        wayland
        wayland-protocols
        wlroots
        libxkbcommon
        libinput
        pixman
        libdrm
        mesa
        seatd
        systemd # sd-bus, for calling logind's SetBrightness (see ipc.c's
                # brightness command / kalin.h's backlight_* prototypes)
      ];
    in {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "kalin-wm";
        version = "0.1.0";

        src = ./.;

        nativeBuildInputs = with pkgs; [
          pkg-config
          wayland-scanner
          patchelf
        ];

        buildInputs = deps;

        buildPhase = ''
          make
        '';

        installPhase = ''
          mkdir -p $out/bin
          cp build/kalin-wm $out/bin/kalin-wm
        '';

        # Bake the runtime library path into the binary so it finds
        # libwlroots-0.20.so etc. when run bare (outside `nix develop`), e.g. as
        # the session compositor in a VM or on a real install.
        postFixup = ''
          patchelf --set-rpath "${pkgs.lib.makeLibraryPath deps}" $out/bin/kalin-wm
        '';
      };

      devShells.${system}.default = pkgs.mkShell {
        buildInputs = with pkgs; [
          pkg-config
          wayland-scanner
          wayland
          wayland-protocols
          wlroots
          libxkbcommon
          libinput
          pixman
          libdrm
          mesa
          seatd
          systemd
          gdb
          valgrind
        ];
        
        shellHook = ''
          echo "kalin-wm Development Environment"
          echo "================================"
          echo ""
          echo "Build: make"
          echo "Run:   ./scripts/run-tty"
          echo ""
        '';
      };
    };
}
