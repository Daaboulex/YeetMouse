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

This will expose a new `hardware.yeetmouse` configuration option in your NixOS system's `config`,
which you can use to install the `yeetmouse` driver, `yeetmousectl`, the GUI, and a service that
applies your settings whenever the driver loads.

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
Your options are copied to `/etc/yeetmouse/default.conf` only if that file does not exist yet.
See [Changing Options After the First Boot](#changing-options-after-the-first-boot).

The GUI runs without `sudo` for members of the `yeetmouse` group. Add your user to it, then log out
and back in:

```nix
{
  users.users.<name>.extraGroups = [ "yeetmouse" ];
}
```

After restarting your system, to verify that the driver works start up the yeetmouse GUI:

```sh
yeetmouse
```

## Applying Changes

### Changing Options After the First Boot

`yeetmouse.service` runs each time the driver loads. It moves an old `/etc/yeetmouse.conf` into
`/etc/yeetmouse/default.conf`, copies each file from the [options](#configuration-options) only where
none exists yet, records the touchpads' resolutions and applies everything. After that the GUI and
`yeetmousectl` own the files, so a changed option does not replace a file that is already there. To use
the new value, remove the file and restart the service:

```sh
sudo rm /etc/yeetmouse/default.conf
sudo systemctl restart yeetmouse.service
```

If a file is refused, the rest still applies and the service fails. A failure while recording the
touchpads does not fail it.

### A New Driver Without a Reboot

After a rebuild, `modprobe` still loads the driver from the system you booted. If the new tools cannot
read that driver's status, `yeetmousectl status` and the GUI ask you to reboot. If the new system was
built for the kernel you are running, you can load the new driver now instead:

```sh
sudo modprobe -r yeetmouse && sudo env MODULE_DIR=/run/current-system/kernel-modules/lib/modules modprobe yeetmouse
```

The udev rule then starts `yeetmouse.service`, which applies everything again.

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
  # This loads the yeetmouse kernel module at boot:
  boot.kernelModules = [ "yeetmouse" ];
  # This installs the yeetmouse GUI CLI package:
  environment.systemPackages = [ yeetmouse ];
}
```

The package alone applies no settings. The udev rule and `yeetmouse.service` that do are in `module.nix`.

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

The options below set `/etc/yeetmouse/default.conf`. They follow the GUI's terminology and structure
and cover its main settings only. For the rest, such as the Raw Accel features and the timing settings,
use the files described in [Profiles and Devices](#profiles-and-devices).

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

Acceleration mode with a logarithmic curve centered on the speed `syncspeed`. `gamma` sets how fast the
sensitivity changes, `motivity` how much, and `smoothness` how gently it starts and ends. `useSmoothing`
enables gain and is on by default.

```nix
{
  hardware.yeetmouse = {
    enable = true;
    mode.synchronous = {
      gamma = 0.3;
      motivity = 2.0;
      syncspeed = 2.0;
      smoothness = 1.0;
      useSmoothing = true;
    };
  };
}
```

See [RawAccel: Synchronous](https://github.com/RawAccelOfficial/rawaccel/blob/master/doc/Guide.md#synchronous)

#### Natural

Acceleration mode with a concave curve that starts at 1 and approaches `limit`, shaped by `acceleration`
(the decay rate) and `midpoint`. `useSmoothing` enables gain and is off by default.

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

These options take files instead of values. Config files use the format the GUI saves and
`yeetmousectl save <file>` writes.

- `defaultConfig` is copied to `/etc/yeetmouse/default.conf`, the settings for every mouse not listed in
  `devices.conf`. Setting it replaces the options above.
- `profiles` is copied to `/etc/yeetmouse/profiles/<name>.conf`, one file per name. See the
  [main README](../README.org) for what a profile holds.
- `devices` is copied to `/etc/yeetmouse/devices.conf`, one line per mouse, as described in the
  [main README](../README.org).
- `rawAccel` converts a Raw Accel 1.7 `settings.json` when the system is built. Its first profile becomes
  the default config unless `defaultConfig` is set, and its profiles and devices are added to `profiles`
  and `devices`. A setting that cannot be converted exactly fails the build.

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

Or, from Raw Accel:

```nix
{
  hardware.yeetmouse = {
    enable = true;
    rawAccel = ./settings.json;
  };
}
```

The build runs `yeetmousectl check` on these files. A value the driver would refuse, a malformed
`devices.conf` line or a device line naming a profile that does not exist fails the build. An unknown key
in a config file is skipped, so a misspelled key still builds and that setting keeps its default.

To run a game on its own profile, see the [main README](../README.org).
