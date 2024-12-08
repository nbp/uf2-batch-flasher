{ pkgs ? import <nixpkgs> { overlays = [ ./overlay.nix ]; },
  runCmd ? ""
}:

with pkgs;
mkShell {
  nativeBuildInputs = [
    # For building transmitter-units
    picotool pico-sdk cmake python3 perl
    gcc-arm-embedded

    # Useful tools for debugging
    minicom
    openocd
    gdb
  ];

  PICO_SDK_PATH = "${pico-sdk}/lib/pico-sdk";
  PICO_TINYUSB_PATH = "${pico-sdk}/lib/pico-sdk/lib/tinyusb";
  PICO_PIO_USB_PATH = "${pico-pio-usb}/";
  
  shellHook = ''
    # ...
    clear() {
      rm -rf _build
    }
    build() {
      mkdir _build;
      cmake -S ./firmware -B _build/firmware;
      make -C _build/firmware;
    }
    debug_build() {
      mkdir _build;
      cmake -DCMAKE_BUILD_TYPE=Debug -S ./firmware -B _build/firmware;
      make -C _build/firmware;
    }
    flash() {
      sudo picotool load _build/firmware/uf2-batch-flasher.uf2
      sudo picotool reboot
    }
    start_uart() {
      sudo minicom -D /dev/ttyACM0 -b 115200
    }
    start_openocd() {
      sudo openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -s tcl
    }
    start_gdb_core0() {
      gdb --exec=_build/firmware/uf2-batch-flasher.elf -ex "target extended-remote :3333"
    }
    start_gdb_core1() {
      gdb --exec=_build/firmware/uf2-batch-flasher.elf -ex "target extended-remote :3334"
    }
    start_gdb() {
      sudo gdb --exec=_build/firmware/uf2-batch-flasher.elf \
        -ex 'target extended-remote | openocd -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2040.cfg -c "gdb_port pipe; log_output openocd.log"'
    }
  '' + runCmd;
}
