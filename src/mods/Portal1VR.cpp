#include "Portal1VR.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>

#include "Portal1VRInstall.h"

Q_LOGGING_CATEGORY(P1VRLog, "portal1vr")

Portal1VR *Portal1VR::instance()
{
    static auto p = new Portal1VR;
    return p;
}

Portal1VR *Portal1VR::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString Portal1VR::info() const
{
    return "Start SteamVR first. Paste the launch options below into Steam so Proton loads this mod's d3d9.dll. "
           "See [GitHub](https://github.com/LorenDB/portal1vr#installation)."_L1;
}

QString Portal1VR::launchOptions() const
{
    // The shipped d3d9.dll is a patched DXVK, so Proton has to load that file instead of its own.
    return "WINEDLLOVERRIDES=\"d3d9=n,b\" %command% -insecure -fullscreen -novid "
           "+mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0"_L1;
}

const QLoggingCategory &Portal1VR::logger() const
{
    return P1VRLog();
}

bool Portal1VR::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    const auto exes = acceptableInstallCandidates(game);
    return std::any_of(exes.cbegin(), exes.cend(), [](const auto &exe) {
        const auto root = QFileInfo{exe.executable}.absolutePath();
        return QFileInfo::exists(root + "/bin/d3d9.dll"_L1) && QFileInfo::exists(root + "/bin/openvr_api.dll"_L1);
    });
}

QString Portal1VR::installHoldReason(const Game *game) const
{
    const auto exes = acceptableInstallCandidates(game);
    const auto ready =
        std::any_of(exes.cbegin(), exes.cend(), [](const auto &exe) { return QFileInfo::exists(exe.executable); });
    if (exes.isEmpty() || ready)
        return {};
    if (game->runsWindowsBuild())
        return "Needs Portal's Windows build. Wait for Steam to finish installing it, then rescan."_L1;
    return "Needs Portal's Windows build. Force Proton in this game's Steam properties and wait for Steam to finish."_L1;
}

QMap<int, Game::LaunchOption> Portal1VR::acceptableInstallCandidates(const Game *game) const
{
    if (!game || game->store() != Game::Store::Steam || game->id() != "400"_L1 || game->engine() != Game::Engine::Source)
        return {};

    QMap<int, Game::LaunchOption> options;
    for (auto it = game->executables().cbegin(); it != game->executables().cend(); ++it)
    {
        const auto &exe = it.value();
        if (exe.platform != Game::Platform::Windows)
            continue;
        // Portal with RTX is another Windows hl2.exe nested under this app. The mod installs beside the base game.
        const auto leaf = exe.executable.mid(exe.executable.lastIndexOf('/'_L1) + 1);
        if (leaf.compare("hl2.exe"_L1, Qt::CaseInsensitive) != 0)
            continue;
        if (QDir{QFileInfo{exe.executable}.absolutePath()} != QDir{game->installDir()})
            continue;
        // The release is a 32-bit DXVK build. A 64-bit hl2.exe cannot load it.
        if (QFileInfo::exists(exe.executable) && exe.arch == Game::Architecture::x64)
            continue;
        options.insert(it.key(), exe);
    }
    return options;
}

bool Portal1VR::isThisFileTheActualModDownload(const QString &file) const
{
    return (file == "Portal1VR-Windows-x86.zip"_L1 ||
            (file.startsWith("Portal1VR-Windows-x86-"_L1) && file.endsWith(".zip"_L1)));
}

void Portal1VR::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (!QFileInfo::exists(exe.executable))
    {
        fail(
            "Portal's Windows hl2.exe isn't installed yet. Force Proton in Steam properties and wait for Steam to finish."_L1);
        return;
    }

    const auto release = currentRelease();
    if (!release || release->assets().isEmpty())
    {
        fail("Portal 1 VR has no Windows x86 release to install yet."_L1);
        return;
    }
    const auto asset = chooseAssetToInstall(game, exe);
    const auto archive = pathForRelease(release, asset);
    if (!QFileInfo::exists(archive))
    {
        fail("Download Portal 1 VR before installing it."_L1);
        return;
    }

    QTemporaryDir extracted;
    if (!extracted.isValid())
    {
        fail("Couldn't create a temporary folder for Portal 1 VR."_L1);
        return;
    }

    QProcess unzip;
    unzip.start("unzip"_L1, {"-o"_L1, "-qq"_L1, archive, "-d"_L1, extracted.path()});
    if (!unzip.waitForStarted(10000) || !unzip.waitForFinished(180000) || unzip.exitCode() != 0)
    {
        const auto detail = QString::fromLocal8Bit(unzip.readAllStandardError()).trimmed();
        fail(detail.isEmpty() ? "Couldn't extract the Portal 1 VR download."_L1 : detail);
        return;
    }

    QString error;
    QStringList installed;
    if (!installPortal1VRPackage(QFileInfo{exe.executable}.absolutePath(), extracted.path(), installed, error))
    {
        fail(error.isEmpty() ? "Couldn't install Portal 1 VR."_L1 : error);
        return;
    }

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    QSet<QString> tracked{installed.cbegin(), installed.cend()};
    for (const auto &old : settings.value("installedFiles"_L1).toStringList())
    {
        if (!old.isEmpty())
            tracked.insert(old);
    }
    settings.setValue("installedFiles"_L1, QStringList{tracked.cbegin(), tracked.cend()});

    Mod::installModImpl(game, exe);
}

Portal1VR::Portal1VR(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
