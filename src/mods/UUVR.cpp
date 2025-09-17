#include "UUVR.h"

#include <QFileInfo>
#include <QLoggingCategory>

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

QList<Mod *> UUVR::dependencies() const
{
    return {Bepinex::instance()};
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

UUVR::UUVR(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
