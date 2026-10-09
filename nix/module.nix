shortRev:
{
  pkgs,
  config,
  lib,
  ...
}:

with lib;
let
  cfg = config.hardware.yeetmouse;

  floatRange = lower: upper: types.addCheck types.float (x: x >= lower && x <= upper);

  parameterBasePath = "/sys/module/yeetmouse/parameters";

  rotationType = types.submodule {
    options = {
      angle = mkOption {
        type = floatRange (-180.0) 180.0;
        default = 0.0;
        description = "Rotation adjustment to apply to mouse inputs (in degrees)";
      };

      snappingAngle = mkOption {
        type = floatRange 0.0 179.9;
        default = 0.0;
        description = "Rotation angle to snap to";
      };

      snappingThreshold = mkOption {
        type = floatRange 0.0 179.9;
        default = 0.0;
        description = "Threshold until applying snapping angle";
      };
    };
  };

  modesType = types.attrTag {
    linear = mkOption {
      description = ''
        Simplest acceleration mode. Accelerates at a constant rate by multiplying acceleration.
        See [RawAccel: Linear](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#linear)
      '';
      type = types.submodule {
        options = {
          acceleration = mkOption {
            type = floatRange 0.0 1.0;
            default = 0.15;
            description = "Linear acceleration multiplier";
          };
          useSmoothing = mkOption {
            type = types.bool;
            default = false;
            description = "Enables the ability to use smooth capping in the Linear curve";
            apply = x: if x then "1" else "0";
          };
          smoothCap = mkOption {
            type = floatRange 0.1 10.0;
            default = 6.0;
            apply = toString;
            description = "Only used when useSmoothing is enabled, it a applies a smooth cap to the set value";
          };
        };
      };
      apply = params: [
        {
          value = "1";
          param = "AccelerationMode";
        }
        {
          value = toString params.acceleration;
          param = "Acceleration";
        }
        {
          value = toString params.useSmoothing;
          param = "UseSmoothing";
        }
        {
          value = toString params.smoothCap;
          param = "Midpoint";
        }
      ];
    };

    power = mkOption {
      description = ''
        Acceleration mode based on an exponent and multiplier as found in Source Engine games.
        See [RawAccel: Power](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#power)
      '';
      type = types.submodule {
        options = {
          acceleration = mkOption {
            type = floatRange 0.0005 5.0;
            default = 0.15;
            description = "Power acceleration pre-multiplier";
          };
          exponent = mkOption {
            type = floatRange 0.0005 1.0;
            default = 0.2;
            description = "Power acceleration exponent";
          };
          outputOffset = mkOption {
            type = floatRange 0.0 5.0;
            default = 1.0;
            description = "Speed output offset";
          };
          useSmoothing = mkOption {
            type = types.bool;
            default = false;
            description = "Enables the ability to use smooth capping in the Power curve";
            apply = x: if x then "1" else "0";
          };
          smoothCap = mkOption {
            type = floatRange 0.1 10.0;
            default = 6.0;
            apply = toString;
            description = "Only used when useSmoothing is enabled, it a applies a smooth cap to the set value";
          };
        };
      };
      apply = params: [
        {
          value = "2";
          param = "AccelerationMode";
        }
        {
          value = toString params.acceleration;
          param = "Acceleration";
        }
        {
          value = toString params.exponent;
          param = "Exponent";
        }
        {
          value = toString params.outputOffset;
          param = "Midpoint";
        }
        {
          value = toString params.useSmoothing;
          param = "UseSmoothing";
        }
        {
          value = toString params.smoothCap;
          param = "Motivity";
        }
      ];
    };

    classic = mkOption {
      description = ''
        Acceleration mode based on an exponent and multiplier as found in Quake 3.
        See [RawAccel: Classic](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#classic)
      '';
      type = types.submodule {
        options = {
          acceleration = mkOption {
            type = floatRange 0.0005 5.0;
            default = 0.15;
            apply = toString;
            description = "Classic acceleration pre-multiplier";
          };
          exponent = mkOption {
            type = floatRange 2.0 5.0;
            default = 2.0;
            apply = toString;
            description = "Classic acceleration exponent";
          };
          useSmoothing = mkOption {
            type = types.bool;
            default = false;
            description = "Enables the ability to use smooth capping in the Classic curve";
            apply = x: if x then "1" else "0";
          };
          smoothCap = mkOption {
            type = floatRange 0.1 10.0;
            default = 6.0;
            apply = toString;
            description = "Only used when useSmoothing is enabled, it a applies a smooth cap to the set value";
          };
        };
      };
      apply = params: [
        {
          value = "3";
          param = "AccelerationMode";
        }
        {
          value = toString params.acceleration;
          param = "Acceleration";
        }
        {
          value = toString params.exponent;
          param = "Exponent";
        }
        {
          value = toString params.useSmoothing;
          param = "UseSmoothing";
        }
        {
          value = toString params.smoothCap;
          param = "Midpoint";
        }
      ];
    };

    motivity = mkOption {
      description = ''
        Acceleration mode based on a sigmoid function with a set mid-point.
        See [RawAccel: Motivity](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#motivity)
      '';
      type = types.submodule {
        options = {
          acceleration = mkOption {
            type = floatRange 0.01 10.0;
            default = 0.15;
            apply = toString;
            description = "Motivity acceleration dividend";
          };
          midpoint = mkOption {
            type = floatRange 0.1 50.0;
            default = 10.0;
            apply = toString;
            description = "Motivity acceleration mid-point";
          };
        };
      };
      apply = params: [
        {
          value = "4";
          param = "AccelerationMode";
        }
        {
          value = toString params.acceleration;
          param = "Acceleration";
        }
        {
          value = toString params.midpoint;
          param = "Midpoint";
        }
      ];
    };

    synchronous = mkOption {
      description = ''
        This acceleration type is designed to match how we naturally perceive changes in speed, using a logarithmic sensitivity curve centered around a "synchronous speed." If the synchronous speed is set correctly, the sensitivity change will align with our intuitive estimation of speed differences.
        See [RawAccel: Synchronous](https://github.com/RawAccelOfficial/rawaccel/blob/master/doc/Guide.md#synchronous)
      '';
      type = types.submodule {
        options = {
          gamma = mkOption {
            type = floatRange 0.01 20.0;
            default = 0.3;
            apply = toString;
            description = "Expresses how fast the change occurs";
          };
          smoothness = mkOption {
            type = floatRange 0.1 20.0;
            default = 1.0;
            apply = toString;
            description = "Affects how fast the changes tails in and out";
          };
          motivity = mkOption {
            type = floatRange 1 10.0;
            default = 2.0;
            apply = toString;
            description = "Expresses how much change will occur";
          };
          syncspeed = mkOption {
            type = floatRange 0.01 20.0;
            default = 2.0;
            apply = toString;
            description = "Works a bit like offset";
          };
          useSmoothing = mkOption {
            type = types.bool;
            default = true;
            description = "Enable gain";
            apply = x: if x then "1" else "0";
          };
        };
      };
      apply = params: [
        {
          value = "5";
          param = "AccelerationMode";
        }
        {
          value = toString params.gamma;
          param = "Exponent";
        }
        {
          value = toString params.smoothness;
          param = "Midpoint";
        }
        {
          value = toString params.motivity;
          param = "Motivity";
        }
        {
          value = toString params.syncspeed;
          param = "Acceleration";
        }
        {
          value = params.useSmoothing;
          param = "UseSmoothing";
        }
      ];
    };

    natural = mkOption {
      description = ''
        Acceleration mode Natural features a concave curve which starts at 1 and approaches some maximum sensitivity. The sensitivity version of this curve can be found in the game Diabotical.
        See [RawAccel: Natural](https://github.com/RawAccelOfficial/rawaccel/blob/d179e22/doc/Guide.md#natural)
      '';
      type = types.submodule {
        options = {
          acceleration = mkOption {
            type = floatRange 0.001 5.0;
            default = 0.15;
            description = "Natural decay rate";
          };
          midpoint = mkOption {
            type = floatRange 0 50.0;
            default = 0.0;
            description = "Natural acceleration mid-point";
          };
          limit = mkOption {
            type = floatRange 0.001 8.0;
            default = 2.0;
            description = "Natural acceleration limit (smoothness of the applied output curve)";
          };
          useSmoothing = mkOption {
            type = types.bool;
            default = false;
            description = "Enable Natural curve smoothing (Makes the curve smoother)";
            apply = x: if x then "1" else "0";
          };
        };
      };
      apply = params: [
        {
          value = "6";
          param = "AccelerationMode";
        }
        {
          value = toString params.acceleration;
          param = "Acceleration";
        }
        {
          value = toString params.midpoint;
          param = "Midpoint";
        }
        {
          value = toString params.limit;
          param = "Exponent";
        }
        {
          value = params.useSmoothing;
          param = "UseSmoothing";
        }
      ];
    };

    jump = mkOption {
      description = ''
        Acceleration mode applying gain above a mid-point.
        Optionally, the transition mid-point can be smoothened and a smoothness may be applied to the whole sigmoid function.
        See [RawAccel: Jump](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#jump)
      '';
      type = types.submodule {
        options = {
          acceleration = mkOption {
            type = floatRange 0.01 10.0;
            default = 0.15;
            description = "Jump acceleration dividend";
          };
          midpoint = mkOption {
            type = floatRange 0.1 50.0;
            default = 0.15;
            description = "Jump acceleration mid-point";
          };
          smoothness = mkOption {
            type = floatRange 0.01 1.0;
            default = 0.2;
            description = "Jump curve smoothness (smoothness of the applied output curve)";
          };
          useSmoothing = mkOption {
            type = types.bool;
            default = true;
            description = "Enable Jump smoothing (whether the transition mid-point is smoothed out into the gain curve";
            apply = x: if x then "1" else "0";
          };
        };
      };
      apply = params: [
        {
          value = "7";
          param = "AccelerationMode";
        }
        {
          value = toString params.acceleration;
          param = "Acceleration";
        }
        {
          value = toString params.midpoint;
          param = "Midpoint";
        }
        {
          value = toString params.smoothness;
          param = "Exponent";
        }
        {
          value = params.useSmoothing;
          param = "UseSmoothing";
        }
      ];
    };

    lut =
      let
        tuple =
          ts:
          mkOptionType {
            name = "tuple";
            merge = mergeOneOption;
            check = xs: all id (zipListsWith (t: x: t.check x) ts xs);
            description = "tuple of" + concatMapStrings (t: " (${t.description})") ts;
          };
        lutVec = tuple [
          ((floatRange 0.0 100.0) // { description = "Input speed (x)"; })
          ((floatRange 0.0 100.0) // { description = "Output speed ratio (y)"; })
        ];
      in
      mkOption {
        description = ''
          Acceleration mode following a custom curve.
          The curve is specified using individual `[x, y]` points.
          See [RawAccel: Lookup Table](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#look-up-table)
          The acceleration mode for custom curves is represented as a LUT as well. Use the Yeetmouse GUI to convert bezier curves to a LUT.
        '';
        type = types.submodule {
          options = {
            data = mkOption {
              type = types.listOf lutVec;
              default = [ ];
              apply = ls: map (t: "${toString (elemAt t 0)},${toString (elemAt t 1)}") ls;
              description = "Lookup Table data (a list of `[x, y]` points)";
            };
          };
        };
        apply = params: [
          {
            value = "8";
            param = "AccelerationMode";
          }
          {
            value = length params.data;
            param = "LutSize";
          }
          {
            value = concatStringsSep ";" params.data;
            param = "LutDataBuf";
          }
        ];
      };
  };

  yeetmouse = pkgs.callPackage ./package.nix {
    inherit (config.boot.kernelPackages) kernel;
    inherit shortRev;
  };
in
{
  options.hardware.yeetmouse = {
    enable = mkOption {
      type = types.bool;
      default = false;
      description = "Enable yeetmouse kernel module to add configurable mouse acceleration";
    };

    sensitivity =
      let
        sensitivityValue = floatRange 0.01 10.0;
        anisotropyValue =
          types.submodule {
            options = {
              x = mkOption {
                type = sensitivityValue;
                description = "Horizontal sensitivity";
              };
              ratioYX = mkOption {
                type = sensitivityValue;
                default = 1.0;
                description = "Ratio of vertical to horizontal sensitivity (Y/X)";
              };
            };
          }
          // {
            description = "Anisotropic sensitivity, separating X and Y movement";
          };
      in
      mkOption {
        type = types.either sensitivityValue anisotropyValue;
        default = 1.0;
        description = "Mouse base sensitivity";
        apply =
          sens:
          let
            sensX = if isAttrs sens then sens.x else sens;
            ratio = if isAttrs sens then sens.ratioYX else 1;
          in
          [
            {
              param = "Sensitivity";
              value = toString sensX;
            }
            {
              param = "RatioYX";
              value = toString ratio;
            }
          ];
      };

    inputCap = mkOption {
      type = types.nullOr (floatRange 0.0 200.0);
      default = null;
      description = "Limit the maximum pointer speed before applying acceleration";
      apply = x: {
        value = if x != null then toString x else "0";
        param = "InputCap";
      };
    };

    outputCap = mkOption {
      type = types.nullOr (floatRange 0.0 100.0);
      default = null;
      description = "Cap maximum sensitivity.";
      apply = x: {
        value = if x != null then toString x else "0";
        param = "OutputCap";
      };
    };

    offset = mkOption {
      type = types.nullOr (floatRange (-50.0) 50.0);
      default = 0.0;
      description = "Acceleration curve offset";
      apply = x: {
        value = toString x;
        param = "Offset";
      };
    };

    preScale = mkOption {
      type = floatRange 0.01 10.0;
      default = 1.0;
      description = "Parameter to adjust for DPI";
      apply = x: {
        value = toString x;
        param = "PreScale";
      };
    };

    rotation = mkOption {
      type = rotationType;
      default = { };
      description = "Adjust mouse rotation input and optionally apply a snapping angle";
      apply = x: [
        {
          value = toString x.angle;
          param = "RotationAngle";
        }
        {
          value = toString x.snappingAngle;
          param = "AngleSnap_Angle";
        }
        {
          value = toString x.snappingThreshold;
          param = "AngleSnap_Threshold";
        }
      ];
    };

    mode = mkOption {
      type = modesType;
      default = {
        linear = { };
      };
      description = "Acceleration mode to apply and their parameters";
      apply =
        params:
        [ ]
        ++ (optionals (params ? linear) params.linear)
        ++ (optionals (params ? power) params.power)
        ++ (optionals (params ? classic) params.classic)
        ++ (optionals (params ? motivity) params.motivity)
        ++ (optionals (params ? synchronous) params.synchronous)
        ++ (optionals (params ? natural) params.natural)
        ++ (optionals (params ? jump) params.jump)
        ++ (optionals (params ? lut) params.lut);
    };

    defaultConfig = mkOption {
      type = types.nullOr types.path;
      default = null;
      description = ''
        Config file in yeetmousectl's format, seeded as /etc/yeetmouse/default.conf, which
        /etc/yeetmouse.conf links to: the settings of every
        mouse devices.conf does not list. When null, the options above render it, or rawAccel
        provides it. Seeding copies a file only when it is missing, so the GUI owns it afterwards.
      '';
    };

    profiles = mkOption {
      type = types.attrsOf types.path;
      default = { };
      example = literalExpression "{ power = ./power.conf; }";
      description = ''
        Named profiles seeded as /etc/yeetmouse/profiles/<name>.conf: curve settings in
        yeetmousectl's format, without preScale, minTime, maxTime or fixedTime, which belong to a
        mouse's line in devices.conf. Run one for a game with `yeetmousectl run <name> -- %command%`.
      '';
    };

    devices = mkOption {
      type = types.nullOr types.path;
      default = null;
      description = ''
        devices.conf seeded as /etc/yeetmouse/devices.conf: one line per mouse,
        "vvvv:pppp <profile|disabled> preScale= minTime= maxTime= fixedTime=".
      '';
    };

    rawAccel = mkOption {
      type = types.nullOr types.path;
      default = null;
      description = ''
        Raw Accel 1.7 settings.json converted at build time: its first profile with its default
        device settings becomes the default config, and its profiles and devices join the ones above.
        A setting YeetMouse cannot reproduce exactly fails the build.
      '';
    };

  };

  config = mkIf cfg.enable (
    let
      configKeys = {
        AccelerationMode = "accelMode";
        Acceleration = "accel";
        Exponent = "exponent";
        Midpoint = "midpoint";
        Motivity = "motivity";
        UseSmoothing = "useSmoothing";
        LutSize = "LUT_size";
        LutDataBuf = "LUT_data";
        Sensitivity = "sens";
        RatioYX = "ratioYX";
        InputCap = "inCap";
        OutputCap = "outCap";
        Offset = "offset";
        PreScale = "preScale";
        RotationAngle = "rotation";
        AngleSnap_Angle = "as_angle";
        AngleSnap_Threshold = "as_threshold";
      };
      modeNames = [
        "AccelMode_Current"
        "AccelMode_Linear"
        "AccelMode_Power"
        "AccelMode_Classic"
        "AccelMode_Motivity"
        "AccelMode_Synchronous"
        "AccelMode_Natural"
        "AccelMode_Jump"
        "AccelMode_Lut"
      ];
      configLine =
        entry:
        if entry.param == "AccelerationMode" then
          "accelMode=${elemAt modeNames (toInt entry.value)}\n"
        else
          "${configKeys.${entry.param}}=${toString entry.value}\n";
      renderedDefault = pkgs.writeText "yeetmouse.conf" (
        concatMapStrings configLine (
          [
            cfg.inputCap
            cfg.outputCap
            cfg.offset
            cfg.preScale
          ]
          ++ cfg.sensitivity
          ++ cfg.rotation
          ++ cfg.mode
        )
      );
      rawAccel = pkgs.runCommand "yeetmouse-rawaccel" { } ''
        ${yeetmouse}/bin/yeetmousectl import-rawaccel ${cfg.rawAccel} --into $out
      '';
      defaultConfig =
        if cfg.defaultConfig != null then
          cfg.defaultConfig
        else if cfg.rawAccel != null then
          "${rawAccel}/yeetmouse/default.conf"
        else
          renderedDefault;
      etc = pkgs.runCommand "yeetmouse-etc" { } ''
        mkdir -p $out/yeetmouse/profiles
        install -m 644 ${defaultConfig} $out/yeetmouse/default.conf
        ${optionalString (cfg.rawAccel != null) ''
          install -m 644 ${rawAccel}/yeetmouse/profiles/*.conf $out/yeetmouse/profiles/
          install -m 644 ${rawAccel}/yeetmouse/devices.conf $out/yeetmouse/devices.conf
        ''}
        ${concatStrings (
          mapAttrsToList (name: file: ''
            if [ -e $out/yeetmouse/profiles/${name}.conf ]; then
              echo "hardware.yeetmouse: the profile ${name} is given twice" >&2
              exit 1
            fi
            install -m 644 ${file} $out/yeetmouse/profiles/${name}.conf
          '') cfg.profiles
        )}
        ${optionalString (cfg.devices != null) ''
          cat ${cfg.devices} >> $out/yeetmouse/devices.conf
        ''}
        ${yeetmouse}/bin/yeetmousectl check $out
      '';
      seed = pkgs.writeShellScript "yeetmouse-seed" ''
        set -eu
        PATH=${makeBinPath [ pkgs.coreutils ]}
        install -d -m 2775 -g yeetmouse /etc/yeetmouse /etc/yeetmouse/profiles
        ${yeetmouse}/bin/yeetmousectl setup /etc --seed ${etc}/yeetmouse
        chgrp yeetmouse ${parameterBasePath}/*
      '';
      reload = pkgs.writeShellApplication {
        name = "yeetmouse-reload";
        runtimeInputs = [
          pkgs.coreutils
          pkgs.diffutils
          pkgs.kmod
          config.systemd.package
        ];
        text = ''
          booted=$(readlink -ev /run/booted-system/kernel)
          current=$(readlink -ev /run/current-system/kernel)
          if [ "$booted" != "$current" ]; then
            echo "the kernel changed, so the new yeetmouse driver loads at the next boot"
            exit 0
          fi
          export MODULE_DIR=/run/current-system/kernel-modules/lib/modules
          built_is_loaded() {
            differs=0
            cmp ${yeetmouse}/share/yeetmouse/driver-build-id /sys/module/yeetmouse/notes/.note.gnu.build-id > /dev/null || differs=$?
            [ "$differs" -le 1 ] || exit 1
            [ "$differs" -eq 0 ]
          }
          if [ -d /sys/module/yeetmouse ]; then
            if built_is_loaded; then
              exit 0
            fi
            if ! modprobe -r yeetmouse; then
              echo "could not unload the old yeetmouse driver, so the new one loads at the next boot" >&2
              exit 1
            fi
          fi
          modprobe yeetmouse
          udevadm settle
          if ! built_is_loaded; then
            echo "the booted system's yeetmouse driver loaded first; systemctl restart yeetmouse-reload loads the new one" >&2
            exit 1
          fi
          echo "loaded the rebuilt yeetmouse driver"
        '';
      };
    in
    {
      assertions = [
        {
          assertion = all (
            name: builtins.match "[A-Za-z0-9][A-Za-z0-9._-]{0,30}" name != null && name != "disabled"
          ) (attrNames cfg.profiles);
          message = "hardware.yeetmouse.profiles: a name uses letters, digits, '.', '_' or '-', starts with a letter or digit, has at most 31 characters and is not \"disabled\"";
        }
      ];
      boot.extraModulePackages = [ yeetmouse ];
      boot.kernelModules = [ "yeetmouse" ];
      environment.systemPackages = [ yeetmouse ];
      users.groups.yeetmouse = { };
      services.udev.extraRules = ''
        ACTION=="add", SUBSYSTEM=="misc", KERNEL=="yeetmouse", GROUP="yeetmouse", MODE="0660", TAG+="systemd", ENV{SYSTEMD_WANTS}+="yeetmouse.service"
      '';
      systemd.services.yeetmouse-reload = {
        description = "Load a rebuilt YeetMouse driver while the kernel stays the same";
        wantedBy = [ "multi-user.target" ];
        after = [ "systemd-modules-load.service" ];
        unitConfig.ConditionCapability = "CAP_SYS_MODULE";
        serviceConfig = {
          Type = "oneshot";
          RemainAfterExit = true;
          ExecStart = getExe reload;
        };
      };
      systemd.services.yeetmouse = {
        description = "Apply YeetMouse configuration, profiles and devices";
        bindsTo = [ "dev-yeetmouse.device" ];
        wants = [ "yeetmouse-reload.service" ];
        after = [
          "dev-yeetmouse.device"
          "yeetmouse-reload.service"
        ];
        serviceConfig = {
          Type = "oneshot";
          RemainAfterExit = true;
          ExecStartPre = seed;
          ExecStart = [
            "-${yeetmouse}/bin/yeetmousectl touchpads --record"
            "${yeetmouse}/bin/yeetmousectl load"
          ];
        };
      };
    }
  );
}
