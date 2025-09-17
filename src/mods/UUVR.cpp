#include "UUVR.h"

#include <QFileInfo>
#include <QLoggingCategory>

#include "Bepinex.h"

Q_LOGGING_CATEGORY(UUVRLog, "uuvr")

UUVR *UUVR::instance()
{
    static UUVR uuvr;
    return &uuvr;
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

bool UUVR::checkGameCompatibility(const Game *game) const
{
    // Filter out IL2CPP builds
    return std::none_of(game->executables().cbegin(),
                        game->executables().cend(),
                        [this, game](const auto &exe) {
        return QFileInfo::exists(GitHubZipExtractorMod::modInstallDirForGame(game, exe) +
                                 "/GameAssembly.dll"_L1) ||
                QFileInfo::exists(GitHubZipExtractorMod::modInstallDirForGame(game, exe) +
                                  "/GameAssembly.so"_L1);
    }) &&
            GitHubZipExtractorMod::checkGameCompatibility(game);
}

bool UUVR::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    return std::any_of(game->executables().cbegin(), game->executables().cend(), [this, game](const auto &exe) {
        return QFileInfo::exists(modInstallDirForGame(game, exe) + "/plugins/Uuvr.dll"_L1);
    });
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
