{
  description = "Sleipner toolchain: native, Windows (mingw-w64), Android";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    # Pinned only to source the real SDL2 2.30.x for the Windows
    # cross-compile; nixos-unstable removed the SDL2_classic alias
    # and points SDL2 at sdl2-compat (a shim over SDL3), which
    # regresses OpenGL context creation under Proton / Game Native.
    nixpkgs-sdl2.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, nixpkgs-sdl2, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true;
            android_sdk.accept_license = true;
          };
        };
        pkgsSdl2 = import nixpkgs-sdl2 { inherit system; };

        androidSdk = (pkgs.androidenv.composeAndroidPackages {
          platformVersions = [ "35" ];
          platformToolsVersion = "35.0.2";
          buildToolsVersions = [ "35.0.0" "34.0.0" ];
          includeNDK = true;
          ndkVersions = [ "28.0.13004108" ];
          cmakeVersions = [ "3.22.1" ];
        }).androidsdk;

        commonNative = with pkgs; [
          llvmPackages_22.clang
          llvmPackages_22.clang-tools
          lld_22
          llvm_22
          cmake
          ninja
          pkg-config
          cppcheck
          python3
          python3Packages.pytest
        ];

        desktopGraphicsDeps = with pkgs; [
          xorg.libX11
          xorg.libXcursor
          xorg.libXinerama
          xorg.libXrandr
          xorg.libXi
          xorg.libXext
          xorg.libXrender
          xorg.libXfixes
          libGL
          mesa
          wayland
          libxkbcommon
        ];
        clangStdenv = pkgs.llvmPackages_22.stdenv;
      in {
        devShells.default = (pkgs.mkShell.override { stdenv = clangStdenv; }) {
          packages = commonNative ++ desktopGraphicsDeps;
        };

        devShells.windows = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            pkgsCross.mingwW64.buildPackages.gcc
            cmake
            ninja
          ];
          buildInputs = [
            pkgs.pkgsCross.mingwW64.windows.mingw_w64_pthreads
            pkgsSdl2.pkgsCross.mingwW64.SDL2
          ];
        };

        devShells.android = pkgs.mkShell {
          packages = with pkgs; [
            androidSdk
            openjdk17
            gradle_8
            cmake
            ninja
          ];
          ANDROID_HOME = "${androidSdk}/libexec/android-sdk";
          ANDROID_NDK_HOME = "${androidSdk}/libexec/android-sdk/ndk/28.0.13004108";
        };
      });
}
