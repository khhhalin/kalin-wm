{
  description = "kalin-wm - A simple Wayland compositor based on dwl";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }: 
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "kalin-wm";
        version = "0.1.0";
        
        src = ./.;
        
        nativeBuildInputs = with pkgs; [
          pkg-config
          wayland-scanner
        ];
        
        buildInputs = with pkgs; [
          wayland
          wayland-protocols
          wlroots
          libxkbcommon
          libinput
          pixman
          libdrm
          mesa
          seatd
        ];
        
        buildPhase = ''
          make
        '';
        
        installPhase = ''
          mkdir -p $out/bin
          cp dwl $out/bin/kalin-wm
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
