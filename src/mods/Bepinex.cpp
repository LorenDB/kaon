#include "Bepinex.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>

Q_LOGGING_CATEGORY(BepinexLog, "bepinex")

Bepinex *Bepinex::instance()
{
    static auto b = new Bepinex;
    return b;
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

bool Bepinex::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    // TODO: this only detects Linux/macOS installations of BepInEx
    const auto exes = acceptableInstallCandidates(game);
    return std::any_of(exes.cbegin(), exes.cend(), [this, game](const auto &exe) {
        return QFileInfo::exists(modInstallDirForGame(game, exe) + "/BepInEx/core/BepInEx.dll"_L1);
    });
}

void Bepinex::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    GitHubZipExtractorMod::installModImpl(game, exe);

    if (!QFileInfo::exists(exe.executable))
        return;

    const auto installDir = modInstallDirForGame(game, exe);

    if (exe.platform == Game::Platform::Linux)
    {
        QProcess process;
        process.setWorkingDirectory(installDir);
        process.start("chmod"_L1, {"+x"_L1, "run_bepinex.sh"_L1});
        process.waitForFinished();

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

QMap<int, Game::LaunchOption> Bepinex::acceptableInstallCandidates(const Game *game) const
{
    auto options = GitHubZipExtractorMod::acceptableInstallCandidates(game);
    options.removeIf([this, game](const std::pair<int, Game::LaunchOption> &exe) {
        // Filter out IL2CPP builds (I seriously doubt that there are any games with both IL2CPP and Mono builds available at
        // the same time, but who knows. People do crazy things sometimes).
        return QFileInfo::exists(modInstallDirForGame(game, exe.second) + "/GameAssembly.dll"_L1) ||
               QFileInfo::exists(modInstallDirForGame(game, exe.second) + "/GameAssembly.so"_L1);
    });
    return options;
}

bool Bepinex::isThisFileTheActualModDownload(const QString &file) const
{
    return file.startsWith("BepInEx"_L1, Qt::CaseInsensitive) &&
            ((file.contains("linux"_L1, Qt::CaseInsensitive) || file.contains("unix", Qt::CaseInsensitive)) ||
             (file.contains("win"_L1, Qt::CaseInsensitive) || file.startsWith("BepInEx_x"_L1, Qt::CaseInsensitive))) &&
            !file.contains("IL2CPP"_L1, Qt::CaseInsensitive) && !file.contains("NET."_L1, Qt::CaseInsensitive);
}

ModRelease::Asset Bepinex::chooseAssetToInstall(const Game *game, const Game::LaunchOption &exe) const
{
    auto assets = currentRelease()->assets();
    assets.removeIf([game, &exe](const ModRelease::Asset &asset) {
        if (exe.arch == Game::Architecture::x64)
            return !asset.name.contains("x64"_L1, Qt::CaseInsensitive);
        else if (exe.arch == Game::Architecture::x86)
            return !asset.name.contains("x86"_L1, Qt::CaseInsensitive);
        return true;
    });

    assets.removeIf([game, &exe](const ModRelease::Asset &asset) {
        if (exe.platform == Game::Platform::Linux)
            return !asset.name.contains("linux"_L1, Qt::CaseInsensitive) &&
                    !asset.name.contains("unix"_L1, Qt::CaseInsensitive);
        else if (exe.platform == Game::Platform::Windows)
            return !asset.name.contains("win"_L1, Qt::CaseInsensitive) &&
                    !asset.name.startsWith("BepInEx_x"_L1, Qt::CaseInsensitive);
        return true;
    });

    if (assets.size() > 0)
        return assets.constFirst();
    else
        return {};
}

Bepinex::Bepinex(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
