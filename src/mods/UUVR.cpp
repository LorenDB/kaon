#include "UUVR.h"

#include <QFileInfo>
#include <QLoggingCategory>

#include "BepInExConfigManager.h"
#include "Bepinex.h"

Q_LOGGING_CATEGORY(UUVRLog, "uuvr")

namespace
{
    // The first Unity whose XR plugin system the modern build can use
    const QVersionNumber firstModernUnity{2018, 4};
} // namespace

UuvrBuild::UuvrBuild(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}

QList<Mod *> UuvrBuild::dependencies() const
{
    return {Bepinex::instance(), BepInExConfigManager::instance()};
}

bool UuvrBuild::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    const auto exes = acceptableInstallCandidates(game);
    return std::any_of(exes.cbegin(), exes.cend(), [this, game](const auto &exe) {
        return QFileInfo::exists(modInstallDirForGame(game, exe) + "/plugins/Uuvr.dll"_L1);
    });
}

QMap<int, Game::LaunchOption> UuvrBuild::acceptableInstallCandidates(const Game *game) const
{
    if (!game || !supportsUnity(game->engineVersion()))
        return {};

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

QList<Game::LaunchOption> UuvrBuild::preferredInstallCandidates(const Game *game, const QList<Game::LaunchOption> &all) const
{
    // UUVR is a BepInEx plugin, so it belongs next to the executable BepInEx was installed for
    QList<Game::LaunchOption> withBepinex;
    for (const auto &exe : all)
        if (Bepinex::instance()->hasFilesFor(game, exe))
            withBepinex << exe;
    return withBepinex.isEmpty() ? all : withBepinex;
}

bool UuvrBuild::isThisFileTheActualModDownload(const QString &file) const
{
    return file.compare(assetName(), Qt::CaseInsensitive) == 0;
}

QString UuvrBuild::modInstallDirForGame(const Game *game, const Game::LaunchOption &executable) const
{
    return GitHubZipExtractorMod::modInstallDirForGame(game, executable) + "/BepInEx"_L1;
}

void UuvrBuild::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (!unpackInto(game, exe))
        return;

    // UUVR is useless without a working BepInEx. If BepInEx was installed outside Kaon (so Bepinex::installModImpl
    // never ran its own fixups), make sure the doorstop config still works for games with a bundled MonoMod.
    if (exe.platform == Game::Platform::Windows)
        Bepinex::instance()->ensureDoorstopSearchPath(game, exe);

    Mod::installModImpl(game, exe);
}

// ------------------------------------------------------------ modern

UUVR::UUVR(QObject *parent)
    : UuvrBuild{parent}
{}

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
    return "For Unity 2018.4 and newer. Runs on BepInEx, which Kaon installs with it. After installing, start the game "
           "once without VR so UUVR can finish setting up. Press F3 in the game to switch VR on and off, and F5 for "
           "its settings."_L1;
}

bool UUVR::supportsUnity(const QVersionNumber &version) const
{
    // Most Unity games still sold are new enough, so that is the guess when a game doesn't say
    return version.isNull() || version >= firstModernUnity;
}

// ------------------------------------------------------------ legacy

UUVRLegacy::UUVRLegacy(QObject *parent)
    : UuvrBuild{parent}
{}

UUVRLegacy *UUVRLegacy::instance()
{
    static auto u = new UUVRLegacy;
    return u;
}

UUVRLegacy *UUVRLegacy::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

const QLoggingCategory &UUVRLegacy::logger() const
{
    return UUVRLog();
}

QString UUVRLegacy::info() const
{
    return "Uses OpenVR only, so start SteamVR before the game. Runs on BepInEx, which Kaon installs with it. Press F5 "
           "in the game for its settings."_L1;
}

bool UUVRLegacy::supportsUnity(const QVersionNumber &version) const
{
    return !version.isNull() && version < firstModernUnity;
}
