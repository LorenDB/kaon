#pragma once

#include <QString>
#include <QStringList>

// Host-side helpers for Flatpak Steam and Heroic. Kaon stays outside the sandbox:
// library files under ~/.var are readable directly, but a game's wineserver socket
// lives in that sandbox's private /tmp, so injection has to be started with `flatpak enter`.
namespace Flatpak
{
    inline const QString SteamAppId = QStringLiteral("com.valvesoftware.Steam");
    inline const QString HeroicAppId = QStringLiteral("com.heroicgameslauncher.hgl");

    // Launcher configs store paths as the sandbox sees them. Those strings often also
    // exist on the host (Flatpak Steam rewrites them, and Heroic bind-mounts some
    // directories). When they don't, --persist=. means the sandbox $HOME is
    // ~/.var/app/<app-id> on the host.
    QString hostPath(const QString &appId, const QString &sandboxPath);

    // Paths that might show up in a game process's environment. Proton may use either
    // the host spelling (~/.var/app/...) or the sandbox home spelling.
    QStringList prefixNeedles(const QString &appId, const QString &sandboxPrefix, const QString &hostPrefix);

    // Copy a directory into the launcher's persisted home so the game sandbox can read it.
    // Returns the directory path as the sandbox sees it, or empty on failure.
    // Extra files already there (UEVR profiles) are left in place.
    QString stageTree(const QString &appId, const QString &sourceDir);

    // Pid of a wine/pressure-vessel process inside a Flatpak whose environ or cmdline
    // contains one of the needles. Zero when the game's wineserver is not up yet.
    qint64 findGamePid(const QStringList &needles);
} // namespace Flatpak
