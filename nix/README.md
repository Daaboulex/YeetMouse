# YeetMouse NixOS Flake

YeetMouse may be built using this folder's packaged Nix flake.
As the `flake.nix` file is in the `nix/` folder the flake is accessible under the `github:AndyFilter/YeetMouse?dir=nix` URL.

## NixOS Module Installation

You may install this flake by adding it to your NixOS configuration.
The following example shows the `inputs.yeetmouse` input and the
`yeetmouse.nixosModules.default` NixOS module being added to a system
in your `flake.nix` file.
    
```nix
{
  # Add this to your flake inputs
  # Note that `inputs.nixpkgs` assumes that you have an input called
  # `nixpkgs` and you might need to change it based on your `nixpkgs`
  # input's name.
  inputs.yeetmouse = {
    url = "github:AndyFilter/YeetMouse?dir=nix";
    inputs.nixpkgs.follows = "nixpkgs";
  };
  # <rest of your config> ...

  outputs = { nixpkgs, yeetmouse, ... }: {
    # This is an example of a NixOS system configuration
    nixosConfigurations.HOSTNAME = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        # Add the `yeetmouse` input's NixOS Module to your system's modules:
        yeetmouse.nixosModules.default
      ];
    };
 };
}
```

This will expose a new `hardware.yeetmouse` configuration option in your NixOS system's `config`.
Enabling it installs and loads the `yeetmouse` driver, installs `yeetmousectl` and the GUI, creates
the `yeetmouse` group, and adds `yeetmouse.service`, which seeds and applies the configuration at boot.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    sensitivity = 1.0;
  };
}
```

Then, rebuild and switch into your new system (using `nixos-rebuild`). After a reboot, the
`yeetmouse` driver should be loaded for your connected mouse with the parameters you specified.
The options reach `/etc/yeetmouse/default.conf` only while that file does not exist yet, as described in
[Seeding and the Boot Service](#seeding-and-the-boot-service).

The GUI runs without root. The boot service hands the driver's parameters, `/dev/yeetmouse` and the
seeded files to the `yeetmouse` group, so add your user to that group and log in again for the
membership to apply:

```nix
{
  users.users.<name>.extraGroups = [ "yeetmouse" ];
}
```

After restarting your system, to verify that the driver works start up the yeetmouse GUI:

```sh
yeetmouse
```

## Manual Overlay Installation

Instead of adding the entire NixOS module, you may also manually add `pkgs.yeetmouse` as an
overlay to your system.

The following example also adds the `inputs.yeetmouse` input, but instad of addding the NixOS module
it adds the `yeetmouse.overlays.default` overlay.

```nix
{
  # Add this to your flake inputs
  # Note that `inputs.nixpkgs` assumes that you have an input called
  # `nixpkgs` and you might need to change it based on your `nixpkgs`
  # input's name.
  inputs.yeetmouse = {
    url = "github:AndyFilter/YeetMouse?dir=nix";
    inputs.nixpkgs.follows = "nixpkgs";
  };
  # <rest of your config> ...

  outputs = { nixpkgs, yeetmouse, ... }: {
    # This is an example of a NixOS system configuration
    nixosConfigurations.HOSTNAME = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        # Add the `yeetmouse` input's overlay to your system's overlays:
        { nixpkgs.overlays = [ yeetmouse.overlays.default ]; }
      ];
    };
 };
}
```

This will only add a `pkgs.yeetmouse` package to your NixOS system configuration, and all further setup
needs to be done manually. Check the `module.nix` if you're unsure of how to use the `yeetmouse` package
manually.

```nix
{ pkgs, config, ... }:

let
  yeetmouse = pkgs.yeetmouse.override { inherit (config.boot.kernelPackages) kernel; };
in {
  # This installs the yeetmouse kernel module:
  boot.extraModulePackages = [ yeetmouse ];
  boot.kernelModules = [ "yeetmouse" ];
  # This installs the yeetmouse GUI CLI package:
  environment.systemPackages = [ yeetmouse ];
}
```

The package ships no udev rules and nothing in this setup applies a config at boot. The
`systemd.services.yeetmouse` unit in `module.nix` shows what the module adds. It seeds `/etc`, runs
`yeetmousectl apply /etc/yeetmouse/default.conf` and `yeetmousectl load`, and hands the driver's parameters
and `/dev/yeetmouse` to the `yeetmouse` group.

Since the `pkgs.yeetmouse` package contains a kernel module, it'll be built against a specific version
of the Linux kernel. By default this will be `pkgs.linuxPackages.kernel`, the default version in nixpkgs.
To override this, `.override { inherit (config.boot.kernelPackages) kernel; }` is called in the example
above to use the selected kernel for your NixOS system instead.

## Flake Builds

This flake exposes a `packages.${system}.yeetmouse` output, which allows you to build
and test the `yeetmouse` package locally:

```sh
nix build .#yeetmouse --json --keep-failed
```

## Configuration Options

The typed options below render `/etc/yeetmouse/default.conf` and use the GUI's terminology and structure for
the GUI's main settings. They do not cover every setting the GUI has, such as its Raw Accel features
or the timing switches. For those, write a config file in `yeetmousectl`'s format and pass it as
[`defaultConfig` or a profile](#profiles-and-devices).

### Sensitivity and Anisotropy

Sensitivity may be specified as a single float value.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    sensitivity = 1.0;
  };
}
```

But it may also be separated into a horizontal sensitivity `x` and a vertical to horizontal ratio
`ratioYX` ("Anisotropy").

```nix
{
  hardware.yeetmouse = {
    enable = true;
    sensitivity = {
      x = 1.0;
      ratioYX = 0.8;
    };
  };
}
```

### Global Options

The remaining global options on `hardware.yeetmouse` control the
- `inputCap` (the maximum input pointer speed that's passed to the acceleration function)
- `outputCap` (the maximum sensitivity the acceleration function may produce)
- `offset` (a curve offset applied to the acceleration function's input)
- `preScale` (an input multiplier to adjust for Mouse DPI changes)
All of these options are set to no-op values by default. If they're not altered, they have no effect.
A mouse listed in `devices.conf` takes its `preScale` from its own line instead.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    inputCap = 100.0;
    outputCap = 1.5;
    offset = -5.0;
    preScale = 0.5;
  };
}
```

### Mouse Rotation and Snapping Angles

A rotation angle may be applied to the pointer movement input.
This adjusts the input to account for movement while the mouse is held at an angle.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    rotation.angle = 5.0; # in degrees
  };
}
```

Optionally, a snapping angle may be defined, which is an axis at an angle that the
mouse movement will "snap" to when under a threshold.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    rotation = {
      snappingAngle = 45.0; # in degrees
      snappingThreshold = 2.0; # in degrees
    };
  };
}
```

### Acceleration Modes

The acceleration modes are defined as exclusive sub-options on `hardware.yeetmouse.mode`.

The `mode` option accepts any of the following mode options `linear`, `power`, `classic`, `motivity`,
`synchronous`, `natural`, `jump`, and `lut`.
These options are mutually exclusive and only one must be specified at a time.

#### Linear

Simplest acceleration mode. Accelerates at a constant rate by multiplying acceleration.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    mode.linear = {
      acceleration = 0.2;
    };
  };
}
```

See [RawAccel: Linear](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#linear)

#### Power

Acceleration mode based on an exponent and multiplier as found in Source Engine games.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    mode.power = {
      acceleration = 1.2;
      exponent = 0.2;
    };
  };
}
```

See [RawAccel: Power](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#power)

#### Classic

Acceleration mode based on an exponent and multiplier as found in Quake 3.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    mode.classic = {
      acceleration = 1.2;
      exponent = 2.0;
    };
  };
}
```

See [RawAccel: Classic](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#classic)

#### Motivity

Acceleration mode based on a sigmoid function with a set mid-point.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    mode.motivity = {
      acceleration = 1.2;
      midpoint = 10.0;
    };
  };
}
```

See [RawAccel: Motivity](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#motivity)

#### Synchronous

Acceleration mode with a logarithmic sensitivity curve centered around a synchronous speed `syncspeed`.
`gamma` sets how fast the change occurs, `motivity` how much change occurs, and `smoothness` how the
change tails in and out. `useSmoothing` enables gain and is on by default.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    mode.synchronous = {
      gamma = 0.3;
      motivity = 2.0;
      syncspeed = 2.0;
      smoothness = 1.0;
    };
  };
}
```

See [RawAccel: Synchronous](https://github.com/RawAccelOfficial/rawaccel/blob/master/doc/Guide.md#synchronous)

#### Natural

Acceleration mode with a concave curve which starts at 1 and approaches a maximum sensitivity, set by
the decay rate `acceleration`, the `midpoint` and the `limit`.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    mode.natural = {
      acceleration = 0.15;
      midpoint = 0.0;
      limit = 2.0;
    };
  };
}
```

See [RawAccel: Natural](https://github.com/RawAccelOfficial/rawaccel/blob/d179e22/doc/Guide.md#natural)

#### Jump

Acceleration mode applying gain above a mid-point.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    mode.jump = {
      acceleration = 2.0;
      midpoint = 5.0;
      smoothness = 0.2;
      useSmoothing = true;
    };
  };
}
```

The transition at the midpoint is smoothened by default, which may be disabled by setting `useSmoothing = false;`.
A smoothness is also applied to the whole sigmoid function which is controlled by `smoothness`.

See [RawAccel: Jump](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#jump)

#### Look-up Table

Acceleration mode following a custom curve. The curve is specified using individual `[x, y]` points.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    mode.lut = {
      # A list of points specified as [x y] tuples
      # NOTE: This is just an example and not a valid LUT
      data = [
        [1.1 1.2]
        [5.2 4.8]
        [10.0 10.0]
        [60.0 40.0]
      ];
    };
  };
}
```

See [RawAccel: Lookup Table](https://github.com/RawAccelOfficial/rawaccel/blob/5b39bb6/doc/Guide.md#look-up-table)

### Profiles and Devices

Four options take files instead of typed values. Each file is in `yeetmousectl`'s format, which
`yeetmousectl save <file>` writes from the live parameters.

- `defaultConfig` becomes `/etc/yeetmouse/default.conf`, which `/etc/yeetmouse.conf` links to, the
  settings of every mouse `devices.conf` does not list. When it is set, the typed options above are not used.
- `profiles` names curves, each seeded as `/etc/yeetmouse/profiles/<name>.conf`. A profile leaves out
  `preScale`, `minTime`, `maxTime` and `fixedTime`, which belong to a mouse's line in `devices.conf`.
  A name uses letters, digits, `.`, `_` or `-`, starts with a letter or digit, has at most 31
  characters and is not `disabled`.
- `devices` becomes `/etc/yeetmouse/devices.conf`, one line per mouse named by its vendor and product
  id. The driver gives a listed mouse its line as soon as it connects, with no udev rule and
  no daemon. A line `<vendor:product> disabled` passes that mouse's movement through unchanged.
- `rawAccel` takes a Raw Accel 1.7 `settings.json`, described in [Raw Accel Settings](#raw-accel-settings).

```nix
{
  hardware.yeetmouse = {
    enable = true;
    defaultConfig = ./yeetmouse.conf;
    profiles = {
      power = ./power.conf;
      jump = ./jump.conf;
    };
    devices = ./devices.conf;
  };
}
```

With `devices.conf` holding:

```
046d:c539 power preScale=1 minTime=0 maxTime=100 fixedTime=0
```

The build refuses a profile name given twice and runs `yeetmousectl check` on the result, so a
malformed file or a device line naming a profile that does not exist fails the build.

In the GUI, the profile picker edits the default config or a profile, and the Devices menu gives each
connected mouse a profile, the default config or no acceleration. `yeetmousectl profile` and
`yeetmousectl device` do the same from a shell.

### Raw Accel Settings

`rawAccel` converts a Raw Accel 1.7 `settings.json` at build time with
`yeetmousectl import-rawaccel <file> --into <dir>`. Unless `defaultConfig` is set, its first profile
with its default device settings becomes the default config in place of the typed options. Its
profiles and devices join the ones from `profiles` and `devices`. A setting YeetMouse cannot reproduce
exactly fails the build.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    rawAccel = ./settings.json;
  };
}
```

### Seeding and the Boot Service

`yeetmouse.service` runs at boot. It creates `/etc/yeetmouse` and `/etc/yeetmouse/profiles` for the
`yeetmouse` group and runs `yeetmousectl setup`, which moves an older `/etc/yeetmouse.conf` into
`/etc/yeetmouse/default.conf`, links the old path to it, and copies each file from the options above
into place only when that file does not exist yet, so the GUI owns the files afterwards. It then runs
`yeetmousectl apply /etc/yeetmouse/default.conf`, `yeetmousectl load` and
`yeetmousectl touchpads --record`.

A changed option therefore does not overwrite a file that is already there. To take the new value,
remove the file and restart the service:

```sh
sudo rm /etc/yeetmouse/default.conf
sudo systemctl restart yeetmouse.service
```

### Per-game Profiles

`yeetmousectl run <profile> -- <command>` runs a game on a profile, for example in Steam's launch
options:

```sh
yeetmousectl run power -- %command%
```

While the game runs every mouse that is not disabled uses that profile's curve. The driver drops it
when the game exits or the wrapper is killed, and the saved curves return.

### Saving Without a Password

The GUI writes `/etc/yeetmouse/default.conf`, the profiles and `devices.conf` itself, as a member of
the `yeetmouse` group, so nothing asks for a password and no polkit rule is involved. Touchpad
resolutions are recorded by the boot service.
