# Kaon

Kaon is a utility to help you use UEVR on Linux. It is in a pre-alpha state right now
and under heavy development, so expect things to break.

Kaon collects basic anonymous usage information to help inform development; these analytics
can be disabled in the settings page.

## Features

- Scan games installed via Steam, Heroic, and Itch
- Install any UEVR release, including nightly builds
- Install the .NET runtime in Proton to make UEVR work

## Known issues

- When you install or delete a Steam game, you must restart Steam before Kaon will properly register the change.
  Otherwise, Steam does not fully flush its configuration to disk, so Kaon cannot read it.
- Flatpak and Snap installations of stores are not supported at the moment; however, support is planned.
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

You'll need Qt 6.8 installed to build Kaon.

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
