# Kaon

Kaon is a utility for using UEVR and other VR mods on Linux. It is pre-alpha and under heavy development, so
expect breakage.

Kaon collects basic anonymous usage information to inform development. You can disable this in the settings page.

## Features

- Scan games installed via Steam, Heroic, and Itch.
- Install any UEVR release, including nightly builds.
- Install PureDark's UEVR AFW (Alternate Frame Warping). AFW requires DX12 and about 500 MB of extra VRAM.
- Install joeyhodge's UEVR fork.
- Install the .NET runtime in Proton for UEVR.
- Install Left 4 Dead 2, Portal, and Portal 2 VR mods.
- Install UUVR for Unity games, along with the BepInEx setup it requires. Only Mono games are supported for now.
- Enable or disable OpenXR CAS, VR Performance Toolkit, and OptiScaler per game.
- For mods that need launch options, Kaon reads the options a Steam game already has and gives you a single line to
  paste that preserves them.
- Works on the Steam Frame! (Only UEVR has been confirmed working so far)
- Mirrors Kaon into the headset (Settings > Headset view) so you can install mods and change settings without
  removing the headset. Confirmed working with SteamVR and WiVRn.
- Supports Flatpak Steam and Flatpak Heroic.

Kaon does not manage dependency versions. It only checks that a dependency is present, not that its version matches
the mod version you selected. For example, it ensures .NET is installed before running UEVR and BepInEx is installed
before running UUVR, but it does not choose a compatible .NET or BepInEx version for you.

## Requirements

Kaon unpacks zip downloads with `unzip`. If that isn't installed, it uses `bsdtar` or `python3` instead. OptiScaler
ships as `.7z`, which requires `7z` or `7za` (from p7zip). The AppImage bundles `unzip` and `7z`, so it needs
neither installed.

## Screenshot

![Screenshot of Kaon](https://github.com/LorenDB/Kaon/blob/master/screenshot.png)

## Download

Download the latest release from [the releases page](https://github.com/LorenDB/kaon/releases/latest).

Bleeding-edge builds are available from CI. Open the latest workflow run on
[the Actions page](https://github.com/LorenDB/kaon/actions) and download the AppImage artifact.

## Building from source

You need the latest Qt to build Kaon. Older Qt versions may work, but Kaon uses newer Qt features as they are
released, so only the most recent Qt release is officially supported. The OpenXR headers are also required
(`libopenxr-dev` on Debian and Ubuntu, `openxr-devel` on Fedora and openSUSE). The OpenXR loader is needed only at
runtime, and only for the WiVRn/Monado headset view.

The headset view is enabled by default. `-DKAON_OPENVR=OFF` disables the SteamVR dashboard tab. `-DKAON_OPENXR=OFF`
disables the WiVRn/Monado window and removes the OpenXR header requirement.

```bash
git clone https://github.com/LorenDB/kaon.git
cd kaon
mkdir build
cd build
cmake ..
cmake --build .

./src/kaon
```

You can also open `CMakeLists.txt` as a project in Qt Creator and press Ctrl+R to run it.
