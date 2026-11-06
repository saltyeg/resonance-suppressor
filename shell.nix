{ pkgs ? import <nixpkgs> {} }:

let
  inherit (pkgs) lib stdenv;

  # Libraries JUCE dlopen()s at runtime on Linux; they must be on LD_LIBRARY_PATH
  # for rsui / rshost to start from inside the shell.
  linuxRuntimeLibs = with pkgs; [
    alsa-lib
    freetype
    fontconfig
    libGL
    xorg.libX11
    xorg.libXext
    xorg.libXrender
    xorg.libXrandr
    xorg.libXinerama
    xorg.libXcursor
    xorg.libXcomposite
  ];
in
pkgs.mkShell {
  packages = with pkgs; [
    cmake
    ninja
    pkg-config # JUCE's CMake finds its Linux deps through pkg-config
    git
    cacert
  ]
  # apple-sdk only exists in nixpkgs >= 24.11; older channels use the stdenv SDK.
  ++ lib.optionals (stdenv.hostPlatform.isDarwin && pkgs ? apple-sdk) [ pkgs.apple-sdk ]
  ++ lib.optionals stdenv.hostPlatform.isLinux linuxRuntimeLibs;

  env = {
    SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-certificates.crt";
  } // lib.optionalAttrs stdenv.hostPlatform.isLinux {
    LD_LIBRARY_PATH = lib.makeLibraryPath linuxRuntimeLibs;
  };
}
