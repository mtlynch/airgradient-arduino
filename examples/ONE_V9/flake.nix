{
  description = "Arduino Nix dev environment";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";

    # 0.34.2 release
    arduino_cli_dep.url = "github:NixOS/nixpkgs/dd5621df6dcb90122b50da5ec31c411a0de3e538";

    # 3.12.0 release
    python_dep.url = "github:NixOS/nixpkgs/e2b8feae8470705c3f331901ae057da3095cea10";
  };

  outputs = { self, flake-utils, arduino_cli_dep, python_dep }@inputs :
    flake-utils.lib.eachDefaultSystem (system:
    let
      arduino_cli_dep = inputs.arduino_cli_dep.legacyPackages.${system};
      python_dep = inputs.python_dep.legacyPackages.${system};
    in
    {
      devShells.default = arduino_cli_dep.mkShell {
        packages = [
          arduino_cli_dep.arduino-cli
          python_dep.python312
        ];

        shellHook = ''
          arduino-cli version
          python --version
        '';
      };
    });
}
