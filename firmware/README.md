# UF2 Batch Flasher

This project is useful when you are iterating over multiple revision of the
firmware of a modular system. With the ability to flash 64 boards with a simple
drag and drop action, what used to take tens of minutes can be reduce to a
minute or less.

The UF2 Batch Flasher is a firmware for the Raspberry Pi Pico W, which can be
used to batch a large number of boards using [UF2](https://github.com/microsoft/uf2).

This firmware configures some of the pins of the Raspberry Pi Pico to be used as
a USB host, and then flash one board at a time. Each board is selected we a set
of Select bits, and powered on and off with the Enable Power and Enable Data
pins.

# Building the firmware

## Configuring WIFI (optional)

Currently the Wifi has to be set before compiling, by making a copy of
`wifi.cmake.template`, and set your credentials in this file.

## Building

The build can be achieved using [Nix](https://nixos.org/nix), by typing the following commands in a terminal:

```shell
$ nix build .#uf2-batch-flasher-firmware
```

## Flashing

Flashing can be achieved by copying the UF2 image on the Raspberry Pi Pico,
using the Boot Select button while powwering it.

# Developing the firmware

When developing the firmware, you might want to use [Nix](https://nixos.org/nix)
to setup the environment.

```shell
$ nix develop
```

This command should setup the environment variables for you as well as a few
shell functions which are convenient, such as `build` which abstract over CMake
usage and `flash` to copy the image to `/mnt/pico`.
