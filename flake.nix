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
      packages.build = arduino-cli-nixpkgs.legacyPackages.${system}.writeShellScriptBin "build" ''
        # Create build directory
        BUILD_DIR="$(pwd)/build"
        mkdir -p "$BUILD_DIR"

        # Compile the Arduino sketch
        ${arduino-cli}/bin/arduino-cli compile \
            --verbose \
            --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs,DebugLevel=info \
            --library . \
            --output-dir "$BUILD_DIR" \
            --verify \
            examples/OneOpenAir/OneOpenAir.ino

        echo "Build completed. Binary files are in: $BUILD_DIR"
        ls -la "$BUILD_DIR"
      '';

      packages.flash = arduino-cli-nixpkgs.legacyPackages.${system}.writeShellScriptBin "flash" ''
        # Flash pre-compiled binary to ESP32
        AIRGRADIENT_PATH='${airgradientPath}'
        BUILD_DIR="$(pwd)/build"

        # Check if build directory exists
        if [ ! -d "$BUILD_DIR" ]; then
          echo "Error: Build directory not found at $BUILD_DIR"
          echo "Please run 'nix run .#build' first to compile the firmware."
          exit 1
        fi

        # Check if binary files exist
        if [ ! -f "$BUILD_DIR/OneOpenAir.ino.bin" ]; then
          echo "Error: Compiled binary not found in $BUILD_DIR"
          echo "Please run 'nix run .#build' first to compile the firmware."
          exit 1
        fi

        echo "Flashing firmware from: $BUILD_DIR"
        ${arduino-cli}/bin/arduino-cli upload \
            --verbose \
            --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs,DebugLevel=info \
            --port "$AIRGRADIENT_PATH" \
            --input-dir "$BUILD_DIR"
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
