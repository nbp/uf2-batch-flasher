{ pkgs ? import <nixpkgs> { overlays = [ ./overlay.nix ]; } }:

with pkgs;
mkShell {
  nativeBuildInputs = [
    # For building transmitter-units
    picotool pico-sdk cmake python3
    gcc-arm-embedded
  ];

  PICO_SDK_PATH = "${pico-sdk}/lib/pico-sdk";
  PICO_TINYUSB_PATH = "${pico-sdk}/lib/pico-sdk/lib/tinyusb";
  PICO_PIO_USB_PATH = "${pico-pio-usb}/";
  
  shellHook = ''
    # ...
    build() {
      mkdir _build;
      cmake -S ./firmware -B _build/firmware;
      make -C _build/firmware;
    }
    flash() {
      build;
      cp -f _build/firmware/firmware.uf2  /mnt/pico/.
    }
  '';
}
