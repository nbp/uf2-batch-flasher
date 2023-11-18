final: prev:

let
  # The pico-sdk git repository is large, and the the use of submodule is not as
  # well optimized in fetchFromGit.
  #
  # Thus to prevent very large downloads, fetching all the history of every
  # submodule, instead we fetch each submodule independently and then add them
  # back where they are supposed to be located.
  staticSubmodules = args@{mappings}:
    let
      linkDir = { path, content }: ''
        # ${final.pkgsBuildHost.xorg.lndir}/bin/lndir -silent ${content} $out/${path}
        mkdir -p $out/${path};
        cp --no-preserve=mode,ownership -r ${content}/. $out/${path};
      '';
    in prev.runCommand "source" { name = "source"; } ''
      mkdir -p $out
      ${prev.lib.concatMapStrings linkDir mappings}
      chmod -R u+rw $out
    '';
in

{
  # This package generate the image of the UF2 Batch Flasher which would be used
  # on the device to flash all connected UF2 devices.
  uf2-batch-flasher-firmware = prev.callPackage ./firmware {};

  # UF2 Batch Flasher board, provide tools such as Kicad to update the schematic
  # and board design.
  uf2-batch-flasher-board = null;

  # trivial builder
  inherit staticSubmodules;

  pico-sdk = prev.pico-sdk.overrideAttrs (old: rec {
    pname = "pico-sdk";
    version = "1.5.0";

    # Usually one would use the option to fetch submodules. However doing it with
    # tinyusb is not recommended as it has a lot of submodules which are not all
    # useful for the RP2040.
    #
    # Relisting the major submodules we care about here is the simplest way to have
    # manageable download sizes.
    src = staticSubmodules {
      mappings = [
        { path = ""; content = prev.fetchFromGitHub {
            owner = "raspberrypi";
            repo = pname;
            rev = version;
            sha256 = "sha256-p69go8KXQR21szPb+R1xuonyFj+ZJDunNeoU7M3zIsE=";
          };
        }
        # TinyUSB provide a way to build a USB client / host.
        { path = "lib/tinyusb"; content = prev.fetchFromGitHub {
            owner = "hathach";
            repo = "tinyusb";
            rev = "c0d79457f61cc4ee27336f430a6f96403bd8b289";
            sha256 = "sha256-tajFhStBTxkBaVB+m7gJDiq8/xGeqy6FcNW+dt3FZxk=";
          };
        }
        # LwIP provides a way to have IP / TCP / HTTP stack for implementing
        # a web server.
        { path = "lib/lwip"; content = prev.fetchFromGitHub {
            owner = "lwip-tcpip";
            repo = "lwip";
            rev = "239918ccc173cb2c2a62f41a40fd893f57faf1d6";
            sha256 = "sha256-oV5YKDDj3dzo/ZR2AQzZKT6jv3n6OpwYu1BqXoK6cVA=";
          };
        }
        # cyw43-driver is used for the Pico W, to support the Wifi module.
        { path = "lib/cyw43-driver"; content = prev.fetchFromGitHub {
            owner = "georgerobotics";
            repo = "cyw43-driver";
            rev = "9bfca61173a94432839cd39210f1d1afdf602c42";
            sha256 = "sha256-iWZDrAAt469yEmH7QXYn35xS9dm7vzL1vSWn6oXneUQ=";
          };
        }
        { path = "lib/tinyusb/hw/mcu/raspberry_pi/Pico-PIO-USB";
          content = final.pico-pio-usb;
        }
      ];
    };
  });

  pico-pio-usb = prev.fetchFromGitHub {
    owner = "sekigon-gonnoc";
    repo = "Pico-PIO-USB";
    rev = "0.5.3";
    sha256 = "sha256-PPC5l6aIGd9G3x1/w0SmxyJDonl6LlQY4iVglO8tgYw=";
  };
}
