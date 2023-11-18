{ lib, stdenv, picotool, pico-sdk, gcc-arm-embedded, cmake, python3 }:

stdenv.mkDerivation {
  pname = "uf2-batch-flasher";
  version = "0.0.0";

  src = ./.;

  nativeBuildInputs = [
    picotool pico-sdk cmake python3
    gcc-arm-embedded
  ];
  PICO_SDK_PATH = "${pico-sdk}/lib/pico-sdk";
  PICO_TINYUSB_PATH = "${pico-sdk}/lib/pico-sdk/lib/tinyusb";

  meta = with lib; {
    maintainers = with maintainers; [ nbp ];
  };
}
