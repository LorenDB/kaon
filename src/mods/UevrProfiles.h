#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

class Game;

// Per-game UEVR settings ("profiles"): the contents of %APPDATA%/UnrealVRMod/<exe-stem> in the game's Wine
// prefix (config.txt, bindings, scripts...). Authors share one as a zip named after the exe, either with the
// files at its root or under one top-level folder of that name. Importing one is extracting it into each of
// the game's profile folders, the way UEVR's own Import config button does.
//
// https://github.com/praydog/uevr-frontend/blob/main/UEVR/GameConfig.cs
namespace UevrProfiles
{
    // The profile folders for every Windows executable, e.g.
    // <prefix>/drive_c/users/steamuser/AppData/Roaming/UnrealVRMod/Stray.
    // With create=true the Roaming folder and each profile folder are made first; error says why
    // in words meant for the user when that fails. Without create, folders that don't exist yet are
    // left out.
    QStringList profileDirsForGame(const Game *game, bool create, QString *error = nullptr);

    // Name of the profile folder holding a config.txt, e.g. "Stray". Empty when no profile is installed.
    QString installedProfileName(const Game *game);

    // True when the zip at localPath holds a DLL anywhere inside it. Exported profiles never do; UEVR's own
    // import warns about them because they run as code. False when the zip can't be read at all.
    bool zipContainsDll(const QString &localPath);

    // Copies the profile zip at source into each of the game's profile folders. Returns an error message in
    // words meant for the user, or empty on success. Overwrites the profile files already there.
    QString importProfile(Game *game, const QUrl &source);

    // Deletes the profile files from each of the game's profile folders, leaving plugins/ and sdkdump/ alone.
    // Returns an error message in words meant for the user, or empty on success.
    QString removeProfile(Game *game);

    // Why profiles can't be managed for this game right now, as a sentence for the user. Empty when
    // importing a profile makes sense: the game has a prefix and a Windows executable.
    QString manageHoldReason(const Game *game);
} // namespace UevrProfiles
