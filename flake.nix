{
  description = "MonoOS — A custom operating system with the Dori kernel, Oki DE, and aswinpkgs";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Cross-compilation toolchain for i686-elf
        crossToolchain = pkgs.pkgsCross.i686-embedded;

        # i686-elf-gcc and binutils from nixpkgs
        crossCC = crossToolchain.buildPackages.gcc;
        crossBinutils = crossToolchain.buildPackages.binutils;

        # Build dependencies
        buildInputs = with pkgs; [
          nasm
          grub2
          xorriso        # needed by grub-mkrescue
          qemu
          gnumake
          coreutils
          
        ];

        # Kernel build
        monoosKernel = pkgs.stdenv.mkDerivation {
          pname = "monoos-kernel";
          version = "0.2.0";
          src = ./.;

          nativeBuildInputs = buildInputs ++ [ crossCC crossBinutils ];

          # Override the cross-compiler prefix for our Makefile
          makeFlags = [
            "CROSS_PREFIX=${crossCC}/bin/i686-elf-"
          ];

          buildPhase = ''
            make libc CROSS_PREFIX=${crossCC}/bin/i686-elf-
            make userland CROSS_PREFIX=${crossCC}/bin/i686-elf-
            make ${pkgs.lib.optionalString true "CROSS_PREFIX=${crossCC}/bin/i686-elf-"} dori.kernel
          '';

          installPhase = ''
            mkdir -p $out/boot $out/lib $out/bin
            cp dori.kernel $out/boot/
            cp libc/libc.a $out/lib/
            cp libc/crt0.o $out/lib/
            cp userland/hello $out/bin/ || true
          '';

          meta = with pkgs.lib; {
            description = "Dori kernel for MonoOS";
            license = licenses.mit;
            platforms = platforms.linux;
          };
        };

        # ISO image
        monoosISO = pkgs.stdenv.mkDerivation {
          pname = "monoos-iso";
          version = "0.2.0";
          src = ./.;

          nativeBuildInputs = buildInputs ++ [ crossCC crossBinutils ];

          buildPhase = ''
            make all CROSS_PREFIX=${crossCC}/bin/i686-elf-
          '';

          installPhase = ''
            mkdir -p $out
            cp monoos.iso $out/
          '';

          meta.description = "MonoOS bootable ISO image";
        };

        # Disk image for testing
        monoosDisk = pkgs.runCommand "monoos-disk" {} ''
          mkdir -p $out
          dd if=/dev/zero of=$out/monoos-disk.img bs=1M count=64 2>/dev/null
        '';

      in {
        packages = {
          kernel = monoosKernel;
          iso = monoosISO;
          disk = monoosDisk;
          default = monoosISO;
        };

        devShells.default = pkgs.mkShell {
          buildInputs = buildInputs ++ [ crossCC crossBinutils ];

          shellHook = ''
            echo "╔═══════════════════════════════════════╗"
            echo "║     MonoOS Development Environment    ║"
            echo "║  Dori kernel · Oki DE · aswinpkgs     ║"
            echo "╚═══════════════════════════════════════╝"
            echo ""
            echo "Cross-compiler: ${crossCC}/bin/i686-elf-gcc"
            echo "NASM:           $(nasm --version)"
            echo ""
            echo "Commands:"
            echo "  make all     - Build kernel + libc + userland + ISO"
            echo "  make run     - Boot in QEMU with disk"
            echo "  make clean   - Clean build artifacts"
            echo ""
            export CROSS_PREFIX="${crossCC}/bin/i686-elf-"
          '';
        };
      }
    );
}
