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

bool UUVR::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    return QFileInfo::exists(game->installDir() + "/BepInEx/plugins/Uuvr.dll"_L1);
}

bool UUVR::isThisFileTheActualModDownload(const QString &file) const
{
    return file.startsWith("uuvr-mono"_L1) && file.endsWith(".zip"_L1);
}

QString UUVR::modInstallDirForGame(Game *game) const
{
    return game->installDir() + "/BepInEx"_L1;
}

UUVR::UUVR(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
