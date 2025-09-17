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
    return std::none_of(game->executables().cbegin(),
                        game->executables().cend(),
                        [this, game](const auto &exe) {
        return QFileInfo::exists(modInstallDirForGame(game, exe) + "/GameAssembly.dll"_L1) ||
                QFileInfo::exists(modInstallDirForGame(game, exe) + "/GameAssembly.so"_L1);
    }) &&
            GitHubZipExtractorMod::checkGameCompatibility(game);
}

bool Bepinex::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    // TODO: this only detects Linux/macOS installations of BepInEx
    return std::any_of(game->executables().cbegin(), game->executables().cend(), [this, game](const auto &exe) {
        return QFileInfo::exists(modInstallDirForGame(game, exe) + "/run_bepinex.sh"_L1);
    });
}

void Bepinex::installMod(Game *game)
{
    GitHubZipExtractorMod::installMod(game);

    QSet<QString> dirsInstalledTo;
    for (const auto &exe : game->executables())
    {
        if (!QFileInfo::exists(exe.executable))
            continue;

        const auto installDir = modInstallDirForGame(game, exe);
        // Prevent pointlessly overwriting an already-installed mod
        if (dirsInstalledTo.contains(installDir))
            continue;
        else
            dirsInstalledTo.insert(installDir);

        QProcess process;
        process.setWorkingDirectory(installDir);
        process.start("chmod"_L1, {"+x"_L1, "run_bepinex.sh"_L1});
        process.waitForFinished();

        if (exe.platform == Game::LaunchOption::Platform::Linux)
        {
            QFile file{installDir + "/run_bepinex.sh"_L1};
            if (file.open(QIODevice::ReadWrite))
            {
                auto content = file.readAll();
                file.resize(0);
                file.write(content.replace("executable_name=\"\""_ba,
                                           "executable_name=\""_ba + exe.executable.split('/').last().toLatin1() + '"'));
                file.close();
            }
        }
    }
}

bool Bepinex::isThisFileTheActualModDownload(const QString &file) const
{
    return file.startsWith("BepInEx_linux"_L1);
}

Bepinex::Bepinex(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
