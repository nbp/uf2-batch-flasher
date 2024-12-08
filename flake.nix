{
  description = "Hardware to flash multiple devices using UF2";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-23.11";
  };

  outputs = { self, nixpkgs, ... }@inputs: let
    pkgs = import nixpkgs {
      overlays = [ self.overlays.default ];
      system = "x86_64-linux";
    };
    syspkgs = {
      x86_64-linux = pkgs;
    };
  in {
    overlays.uf2-batch-flasher = import ./overlay.nix;
    overlays.default = self.overlays.uf2-batch-flasher;

    packages.x86_64-linux = {
      inherit (syspkgs.x86_64-linux) pico-sdk uf2-batch-flasher-firmware uf2bf;
    };

    # nix develop -- Setup an environment with all tools needed By default we
    # consider that people are working on the firmware.
    devShells.x86_64-linux.default = import firmware/shell.nix { inherit pkgs; };
    devShells.x86_64-linux.flash = import firmware/shell.nix {
      inherit pkgs; runCmd = "build; flash;";
    };
  };
}
