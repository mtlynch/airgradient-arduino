{
  description = "Arduino Nix dev environment";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";

    # 1.2.2
    arduino-cli-nixpkgs.url = "github:NixOS/nixpkgs/bf9fa86a9b1005d932f842edf2c38eeecc98eef3";

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
      airgradientPath = "/dev/ttyACM0";

      # Build pyserial from PyPI
      pyserial = python-nixpkgs.legacyPackages.${system}.python312Packages.buildPythonPackage rec {
        pname = "pyserial";
        version = "3.5";
        src = python-nixpkgs.legacyPackages.${system}.python312Packages.fetchPypi {
          inherit pname version;
          sha256 = "sha256-PHfgFBcN//vYFub/wgXphC77EL6fWOwW0+hnW0klzds=";
        };
        doCheck = false; # Skip tests to avoid port access issues
      };

      # Python with pyserial
      pythonWithSerial = python.withPackages (ps: [ pyserial ]);
    in {
      packages.flash = arduino-cli-nixpkgs.legacyPackages.${system}.writeShellScriptBin "flash" ''
        # ,EraseFlash=all
        AIRGRADIENT_PATH='${airgradientPath}'
        ${arduino-cli}/bin/arduino-cli compile \
            --verbose \
            --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs,DebugLevel=info \
            --library . \
            --port "$AIRGRADIENT_PATH" \
            --verify \
            --upload \
            examples/OneOpenAir/OneOpenAir.ino
      '';

      packages.monitor = arduino-cli-nixpkgs.legacyPackages.${system}.writeShellScriptBin "monitor" ''
        AIRGRADIENT_PATH='${airgradientPath}'
        ${arduino-cli}/bin/arduino-cli monitor --port "$AIRGRADIENT_PATH"
      '';

      packages.default = self.packages.${system}.flash;

      devShells.default = arduino-cli-nixpkgs.legacyPackages.${system}.mkShell {
        packages = [
          arduino-cli
          pythonWithSerial
        ];

        shellHook = ''
          arduino-cli version
          python --version
        '';
      };

      formatter = arduino-cli-nixpkgs.legacyPackages.${system}.alejandra;
    });
}
