# Kaon

Kaon is a utility to help you use UEVR on Linux. It is in a pre-alpha state right now
and under heavy development, so expect things to break.

Currently, Kaon does not support Steam installed via Flatpak.

Kaon collects basic anonymous usage information to help inform development; these analytics
can be disabled in the settings page.

## Download

You can download the latest release of Kaon [on the releases page](https://github.com/LorenDB/kaon/releases/latest).

Alternatively, bleeding-edge builds are available via CI. To get them, click on the latest workflow run
[here](https://github.com/LorenDB/kaon/actions) and download the AppImage artifact.

## Features

- Install any UEVR release, including nightly builds
- Auto install the .NET runtime in Proton to make UEVR work

## Building and running

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
