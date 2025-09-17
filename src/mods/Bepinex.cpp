#include "Bepinex.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>

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

QString Bepinex::info() const
{
    return "Extra setup required; see instructions for "
           "[Linux](https://docs.bepinex.dev/articles/advanced/steam_interop.html#3-configure-steam-to-run-the-script) "
           "and [Windows](https://docs.bepinex.dev/articles/advanced/proton_wine.html) binaries"_L1;
}

const QLoggingCategory &Bepinex::logger() const
{
    return BepinexLog();
}

bool Bepinex::checkGameCompatibility(const Game *game) const
{
    // Filter out IL2CPP builds
    if (QFileInfo::exists(modInstallDirForGame(game) + "/GameAssembly.dll"_L1) ||
            QFileInfo::exists(modInstallDirForGame(game) + "/GameAssembly.so"_L1))
        return false;

    return Mod::checkGameCompatibility(game);
}

bool Bepinex::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    return QFileInfo::exists(modInstallDirForGame(game) + "/run_bepinex.sh"_L1);
}

void Bepinex::installMod(Game *game)
{
    GitHubZipExtractorMod::installMod(game);

    QProcess process;
    process.setWorkingDirectory(modInstallDirForGame(game));
    process.start("chmod"_L1, {"+x"_L1, "run_bepinex.sh"_L1});
    process.waitForFinished();
}

bool Bepinex::isThisFileTheActualModDownload(const QString &file) const
{
    return file.startsWith("BepInEx_linux"_L1);
}

Bepinex::Bepinex(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
