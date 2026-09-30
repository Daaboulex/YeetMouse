{
  lib,
  makeDesktopItem,
  pkgs,
  kernel,
  shortRev ? "dev",
}:

kernel.stdenv.mkDerivation rec {
  pname = "yeetmouse";
  version = shortRev;
  src = lib.fileset.toSource {
    root = ./..;
    fileset = ./..;
  };

  setSourceRoot = "export sourceRoot=$(pwd)/source";
  nativeBuildInputs =
    with pkgs;
    kernel.moduleBuildDependencies
    ++ [
      pkg-config
      makeWrapper
      autoPatchelfHook
      copyDesktopItems
    ];
  buildInputs = [
    kernel.stdenv.cc.cc.lib
    pkgs.glfw3
  ];

  makeFlags =
    let
      kernelBuild = "${kernel.dev}/lib/modules/${kernel.modDirVersion}/build";
    in
    kernel.commonMakeFlags
    ++ [
      "KBUILD_OUTPUT=${kernelBuild}"
      "-C"
      kernelBuild
      "M=$(sourceRoot)/driver"
    ];

  LD_LIBRARY_PATH = "/run/opengl-driver/lib:${lib.makeLibraryPath buildInputs}";

  postBuild = ''
    make "-j$NIX_BUILD_CORES" -C $sourceRoot/tools/yeetmousectl "CXX=$CXX"
    make "-j$NIX_BUILD_CORES" -C $sourceRoot/gui "M=$sourceRoot/gui" \
      "LIBS=-lglfw -lGL" \
      "CXXFLAGS=-Wno-sign-compare -Wno-unused-function -Wno-return-type -isystem $sourceRoot/gui/External"
  '';

  postInstall =
    let
      PATH = [ pkgs.zenity ];
    in
    /* sh */ ''
      install -Dm755 $sourceRoot/tools/yeetmousectl/yeetmousectl $out/bin/yeetmousectl
      install -Dm755 $sourceRoot/gui/YeetMouseGui $out/bin/yeetmouse
      wrapProgram $out/bin/yeetmouse \
        --prefix PATH : ${lib.makeBinPath PATH}
      install -Dm644 $sourceRoot/media/yeetmouse.png \
        $out/share/icons/hicolor/256x256/apps/yeetmouse.png
    '';

  buildFlags = [ "modules" ];
  installFlags = [ "INSTALL_MOD_PATH=${placeholder "out"}" ];
  installTargets = [ "modules_install" ];

  desktopItems = [
    (makeDesktopItem {
      name = pname;
      exec = pname;
      icon = pname;
      type = "Application";
      desktopName = "Yeetmouse GUI";
      comment = "Yeetmouse Configuration Tool";
      categories = [
        "Settings"
        "HardwareSettings"
      ];
    })
  ];

  meta.mainProgram = "yeetmouse";
}
