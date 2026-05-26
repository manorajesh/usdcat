{
  description = "Terminal-based USD (Universal Scene Description) viewer";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "usdless";
          version = "1.0.0";
          src = ./.;

          nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config ];

          buildInputs = with pkgs; [
            openusd
            eigen
          ] ++ lib.optionals stdenv.isLinux [ libGL libGLU ]
            ++ lib.optionals stdenv.isDarwin [ darwin.apple_sdk.frameworks.OpenGL ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          meta = with pkgs.lib; {
            description = "Terminal-based USD viewer — renders 3D scenes as Unicode art in the terminal";
            homepage = "https://github.com/manorajesh/usdless";
            license = licenses.mit;
            maintainers = [ ];
            platforms = platforms.linux ++ platforms.darwin;
            mainProgram = "usdless";
          };
        };

        apps.default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/usdless";
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
        };
      });
}
