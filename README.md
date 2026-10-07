# Kaon

Kaon is a utility to help you use UEVR and other VR mods on Linux. It is in a pre-alpha state right now
and under heavy development, so expect things to break.

Kaon collects basic anonymous usage information to help inform development; these analytics
can be disabled in the settings page.

## Features

- Scan games installed via Steam, Heroic, and Itch.
- Install any UEVR release, including nightly builds
- Install PureDark's UEVR AFW (Alternate Frame Warping): Kaon adds `-dx12`, checks for `PDAFWPlugin.dll`, and can write the recommended in-game settings (AFW rendering method, Ghosting Fix, and Bootstrap on joeyhodge builds). You still enable DLSS/DLAA in the game. AFW needs DX12 and about 500 MB extra VRAM; use the nightly build for older UE titles and joeyhodge for UE 5.5–5.8
- Install the .NET runtime in Proton to make UEVR work
- Install Portal and Portal 2 VR mods
- Install UUVR for Unity games, along with the BepInEx setup it needs. Kaon picks the UUVR build for the game's
  Unity version and sets the game's Proton prefix up to load BepInEx, so there are no launch options to add.
- Switch OpenXR CAS, VR Performance Toolkit, and OptiScaler on or off for each game. OptiScaler works alone or as the DLSS path for UEVR AFW on AMD/Intel; it conflicts with VR Performance Toolkit (both use dxgi.dll)
- For mods that need launch options, Kaon reads what a Steam game already has and gives you one line to paste
  that keeps it
- Works on the Steam Frame! (Only UEVR has been confirmed working so far)
- Flatpak Steam and Flatpak Heroic are supported, and a native install of the same launcher is listed separately. The Flatpak launcher has to already be running, and user namespaces have to be enabled so Kaon can enter the game sandbox. Snap stores and the Flatpak itch app are still unsupported.

## Requirements

Kaon unpacks zip downloads with `unzip`. If that isn't installed, it uses `bsdtar` or `python3` instead. OptiScaler ships as `.7z`, which needs `7z` or `7za` (from p7zip).

## Known issues

- Custom games assume you are launching them using your system's wineprefix (i.e. reads the WINEPREFIX environment 
  variable; if not set, falls back to ~/.wine). If you are using custom wineprefixes for your games, UEVR will not
  detect them.

## Screenshot

![Screenshot of Kaon](https://github.com/LorenDB/Kaon/blob/master/screenshot.png)

## Download

You can download the latest release of Kaon [on the releases page](https://github.com/LorenDB/kaon/releases/latest).

Alternatively, bleeding-edge builds are available via CI. To get them, click on the latest workflow run
[here](https://github.com/LorenDB/kaon/actions) and download the AppImage artifact.

## Building from source

You'll need the latest Qt installed to build Kaon. Older Qt versions may work, but I tend to use new Qt
features as they come out, so I only officially support the most recent release of Qt.

``` bash
git clone https://github.com/LorenDB/kaon.git
cd kaon
mkdir build
cd build
cmake ..
cmake --build .

./src/kaon
```

You can also just open CMakeLists.txt as a project in Qt Creator and press Ctrl+R to run the project.
