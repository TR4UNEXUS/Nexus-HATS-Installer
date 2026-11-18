# Nexus HATS Installer for Switch

## Build

Use devkitPro with devkitA64, devkitARM, Switch portlibs, CMake, Ninja and Python 3.
GCC 15 or newer is needed for the embedded shader resources. From this directory:

```sh
cmake --preset Release
cmake --build --preset Release -j4
```

The NRO is `build/Release/nexus-hats-installer.nro`. CMake also builds the recovery
payload and embeds the matching payload, wallpaper, icons, configuration and CA
bundle into the application. The source preserves the original three-card UI.

For a full installable NSP:

```sh
./build_nsp.sh --keyset /absolute/path/to/your/local/prod.keys
```

The key file stays outside the source and release output. The script passes only
the two keys used for packaging to hacBrewPack. The output is
`out/Nexus-HATS-Installer-1.0.0.nsp`, title ID `01004E5848490000`.
See [BUILD_NSP.md](BUILD_NSP.md) for the packaging details.

`./build_release.sh` makes the NRO ZIP in `out/` without deleting an existing build
or other output files. Launch the NRO with full application memory. The NSP is a
standalone application and prepares its bundled support files on first launch.

## Packs and protection

Pack list: `https://api.github.com/repos/TR4UNEXUS/Nexus-USB-HATS/releases/latest`.
The online menu refetches the current asset list; ZIP downloads use fresh partial
files and are promoted only on completion. Names and versions are not pinned.
Known installer, Manager, Formatter and source attachments are not HATS packs.

The shared app/payload policy always protects the USB routing indexes and Manager
configuration. `assets/romfs/nexus-exclusions.json` supplies the initial preservation
list. A live `hats-exclusions.json` in the release repository can extend or replace
that list without changing the mandatory Nexus protections.

Application updates come from `TR4UNEXUS/Nexus-HATS-Installer/releases/latest`
and must be named `Nexus-HATS-Installer-VERSION.zip` or `.nsp`. Pack downloads
remain linked to `TR4UNEXUS/Nexus-USB-HATS`; they are separate from app updates.

See [host checks and hardware test steps](tests/README.md).
