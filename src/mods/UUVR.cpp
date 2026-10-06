#include "UUVR.h"

#include <QFileInfo>
#include <QLoggingCategory>

#include "BepInExConfigManager.h"
#include "Bepinex.h"

Q_LOGGING_CATEGORY(UUVRLog, "uuvr")

UUVR *UUVR::instance()
{
    static auto u = new UUVR;
    return u;
}

UUVR *UUVR::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

const QLoggingCategory &UUVR::logger() const
{
    return UUVRLog();
}

QString UUVR::info() const
{
    return "UUVR runs on BepInEx, which Kaon installs automatically. After installing, paste the launch options "
           "below into Steam so Proton loads BepInEx, then start the game once without VR so UUVR can finish "
           "setting up. Press F3 in-game to toggle VR."_L1;
}

QString UUVR::launchOptions() const
{
    // BepInEx hooks Windows games through doorstop's winhttp.dll, which Proton only loads with this override.
    // Same requirement as BepInEx itself; duplicated here because this is the card most users will read.
    return Bepinex::instance()->launchOptions();
}

QList<Mod *> UUVR::dependencies() const
{
    return {Bepinex::instance(), BepInExConfigManager::instance()};
}

bool UUVR::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    const auto exes = acceptableInstallCandidates(game);
    return std::any_of(exes.cbegin(), exes.cend(), [this, game](const auto &exe) {
        return QFileInfo::exists(modInstallDirForGame(game, exe) + "/plugins/Uuvr.dll"_L1);
    });
}

QMap<int, Game::LaunchOption> UUVR::acceptableInstallCandidates(const Game *game) const
{
    auto options = GitHubZipExtractorMod::acceptableInstallCandidates(game);
    options.removeIf([this, game](const std::pair<int, Game::LaunchOption> &exe) {
        // UUVR only supports Windows games (run via Proton/Wine on Linux). Its patcher ships Windows-only XR
        // loader DLLs, so offering it for native Linux builds would produce a broken install.
        if (exe.second.platform != Game::Platform::Windows)
            return true;
        // Filter out IL2CPP builds (I seriously doubt that there are any games with both IL2CPP and Mono builds available at
        // the same time, but who knows. People do crazy things sometimes.
        return QFileInfo::exists(GitHubZipExtractorMod::modInstallDirForGame(game, exe.second) + "/GameAssembly.dll"_L1) ||
               QFileInfo::exists(GitHubZipExtractorMod::modInstallDirForGame(game, exe.second) + "/GameAssembly.so"_L1);
    });
    return options;
}

bool UUVR::isThisFileTheActualModDownload(const QString &file) const
{
    return file.toLower() == "uuvr-mono-modern.zip"_L1;
}

QString UUVR::modInstallDirForGame(const Game *game, const Game::LaunchOption &executable) const
{
    return GitHubZipExtractorMod::modInstallDirForGame(game, executable) + "/BepInEx"_L1;
}

void UUVR::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    GitHubZipExtractorMod::installModImpl(game, exe);

    // UUVR is useless without a working BepInEx. If BepInEx was installed outside Kaon (so Bepinex::installModImpl
    // never ran its own fixups), make sure the doorstop config still works for games with a bundled MonoMod.
    if (exe.platform == Game::Platform::Windows)
        Bepinex::instance()->ensureDoorstopSearchPath(game, exe);
}

UUVR::UUVR(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
