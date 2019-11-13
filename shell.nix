with import <nixpkgs> {};
pkgs.mkShell {
  buildInputs = [
    cairo xorg.libXdmcp xorg.libpthreadstubs xorg.libxcb pcre xorg.xcbutil
    xorg.xcbutilcursor xorg.xcbutilimage xorg.xcbutilrenderutil xorg.xcbutilwm xcbutilxrm
    (xorg.xcbproto.override { python = python3; })
    alsaLib
    curl
    mpd_clientlib
    libpulseaudio
    wirelesstools
    libnl
  ];
  nativeBuildInputs = [
    cmake
    pkgconfig
    python3
  ];
}
