{
  description = "Arduino Nix dev environment";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";

    # 0.35.1 release
    arduino-cli-nixpkgs.url = "github:NixOS/nixpkgs/5f5210aa20e343b7e35f40c033000db0ef80d7b9";

    # 3.12.0 release
    python-nixpkgs.url = "github:NixOS/nixpkgs/e2b8feae8470705c3f331901ae057da3095cea10";
  };

  outputs = {
    self,
    flake-utils,
    arduino-cli-nixpkgs,
    python-nixpkgs,
  } @ inputs:
    flake-utils.lib.eachDefaultSystem (system: let
      arduino-cli = arduino-cli-nixpkgs.legacyPackages.${system}.arduino-cli;
      python = python-nixpkgs.legacyPackages.${system}.python312;
    in {
      devShells.default = arduino-cli-nixpkgs.legacyPackages.${system}.mkShell {
        packages = [
          arduino-cli
          python
        ];

        shellHook = ''
          arduino-cli version
          python --version
        '';
      };

      formatter = arduino-cli-nixpkgs.legacyPackages.${system}.alejandra;
    });
}
