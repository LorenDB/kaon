#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

class Game;

// Where UEVR loads native plugins from, and what is in them.
//
// UEVR loads every DLL in the game's own folder (%APPDATA%/UnrealVRMod/<exe-stem>/plugins)
// and in the shared one (%APPDATA%/UEVR/plugins). Both live in the game's Wine prefix, under
// drive_c/users/<user>/AppData/Roaming. Installing a plugin is copying its DLL into the game's
// folder; listing is reading the DLL names back out.
//
// https://docs.uevr.io/plugins/getting_started.html#plugin-installation
namespace UevrPlugins
{
    // Distinct exe stems (file name without its last extension) UEVR may inject, shipping names
    // first. Includes Windows launch options that exist on disk and, for Unreal, the exe in
    // Binaries/Win64 (often Game-Win64-Shipping.exe rather than the launcher). Empty when the
    // game has no Windows build.
    QStringList exeStems(const Game *game);

    // The per-game plugin folders for every Windows executable, e.g.
    // <prefix>/drive_c/users/steamuser/AppData/Roaming/UnrealVRMod/Stray/plugins.
    // With create=true, the Roaming folder and each plugins folder are made first; error says why
    // in words meant for the user when that fails. Without create, folders that don't exist yet are
    // left out.
    QStringList pluginDirsForGame(const Game *game, bool create, QString *error = nullptr);

    // The shared folder UEVR also loads from: <Roaming>/UEVR/plugins. Same create contract.
    QString globalPluginDir(const Game *game, bool create, QString *error = nullptr);

    // DLL file names (e.g. "MyPlugin.dll") found in the game's own folders and in the shared one,
    // sorted, without duplicates. Empty when there is nowhere to look yet.
    QStringList installedPluginNames(const Game *game);

    // Copies the DLL at source into each of the game's plugin folders. Returns an error message in
    // words meant for the user, or empty on success. Overwrites a plugin of the same name.
    QString installPlugin(Game *game, const QUrl &source);

    // Deletes fileName from the game's folders and from the shared one. Returns an error message in
    // words meant for the user, or empty on success.
    QString removePlugin(Game *game, const QString &fileName);

    // Why plugins can't be managed for this game right now, as a sentence for the user. Empty when
    // picking a DLL makes sense: the game has a prefix and a Windows executable.
    QString manageHoldReason(const Game *game);
} // namespace UevrPlugins
