#include "Bepinex.h"

#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(BepinexLog, "bepinex")

Bepinex *Bepinex::instance()
{
    static Bepinex b;
    return &b;
}

Bepinex *Bepinex::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

const QLoggingCategory &Bepinex::logger() const
{
    return BepinexLog();
}

bool Bepinex::checkGameCompatibility(const Game *game) const
{
    // TODO: check Mono/IL2CPP/etc.
    return Mod::checkGameCompatibility(game);
}

bool Bepinex::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    return QFileInfo::exists(game->installDir() + "/run_bepinex.sh"_L1);
}

bool Bepinex::isThisFileTheActualModDownload(const QString &file) const
{
    return file.startsWith("BepInEx_linux"_L1);
}

QString Bepinex::modInstallDirForGame(Game *game) const
{
    return game->installDir();
}

Bepinex::Bepinex(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
