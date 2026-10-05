#include "Dotnet.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>

#include "Aptabase.h"
#include "DownloadManager.h"

Q_LOGGING_CATEGORY(DotNetLog, "dotnet")

namespace
{
    const QUrl runtimeUrl{"https://builds.dotnet.microsoft.com/dotnet/Runtime/6.0.36/dotnet-runtime-6.0.36-win-x64.zip"_L1};
    const QUrl desktopUrl{
        "https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/6.0.36/windowsdesktop-runtime-6.0.36-win-x64.zip"_L1};

    QString dotnetDir(const Game *game)
    {
        const auto dir = game->winePrefix() + "/drive_c/Program Files/dotnet"_L1;
        // An x64 program like UEVR's injector runs emulated under an arm64 Wine, and the .NET host then looks for its
        // runtime in an x64 subfolder, the same as on Windows on Arm. See pal::get_default_installation_dir in the host.
        return game->hasArm64Wine() ? dir + "/x64"_L1 : dir;
    }

    bool extractZip(const QString &zip, const QString &target)
    {
        if (!QFileInfo::exists(target))
            QDir().mkpath(target);

        QProcess unzip;
        unzip.setWorkingDirectory(target);
        unzip.start("unzip"_L1, {"-o"_L1, "-qq"_L1, zip, "-d"_L1, target});
        unzip.waitForFinished(-1);
        if (unzip.exitCode() != 0)
        {
            qCWarning(DotNetLog) << "Unzip" << zip << "failed:" << unzip.errorString();
            qCWarning(DotNetLog) << unzip.readAllStandardError();
            return false;
        }
        return true;
    }

    void removeIfEmpty(const QString &path)
    {
        if (QDir d{path}; d.exists() && d.isEmpty())
            d.removeRecursively();
    }
} // namespace

Dotnet::Dotnet(QObject *parent)
    : Mod{parent},
      m_runtimeZip{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                   "/dotnet-runtime-6.0.36-win-x64.zip"_L1},
      m_desktopZip{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                   "/windowsdesktop-runtime-6.0.36-win-x64.zip"_L1}
{
    // The old .exe installer cannot run under Proton (WiX Burn always initializes
    // its theme manager, even for /quiet, and that fails with 0x80070583). Delete
    // stale caches; the zips below replace them.
    // TODO: migration, remove me once 0.4.0 has shipped with it
    QFile{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
          "/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1}
        .remove();
    QFile{QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1}
        .remove();

    setCurrentRelease(42);
}

Dotnet *Dotnet::instance()
{
    static auto d = new Dotnet;
    return d;
}

Dotnet *Dotnet::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

const QLoggingCategory &Dotnet::logger() const
{
    return DotNetLog();
}

bool Dotnet::isInstalledForGame(const Game *game) const
{
    if (!game || !game->hasValidWine())
        return false;
    const auto basepath = dotnetDir(game);
    return QFileInfo::exists(basepath + "/dotnet.exe"_L1) && QFileInfo::exists(basepath + "/host/fxr/6.0.36"_L1) &&
           QFileInfo::exists(basepath + "/shared/Microsoft.NETCore.App/6.0.36"_L1) &&
           QFileInfo::exists(basepath + "/shared/Microsoft.WindowsDesktop.App/6.0.36"_L1);
}

bool Dotnet::hasDotnetCached() const
{
    return QFileInfo::exists(m_runtimeZip) && QFileInfo::exists(m_desktopZip);
}

void Dotnet::downloadRelease(ModRelease *)
{
    Aptabase::instance()->track("download-"_L1 + settingsGroup(), {{"version"_L1, currentRelease()->name()}});

    const auto saveTo = [](const QString &path, const char *what) {
        return [path, what](const QByteArray &data) {
            QFile file{path};
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(data);
                file.close();
            }
            else
                qCWarning(DotNetLog) << "Failed to save downloaded" << what;
        };
    };

    const auto fail = [](const QNetworkReply::NetworkError, const QString &errorMessage) {
        qCWarning(DotNetLog) << ".NET desktop runtime download failed:" << errorMessage;
    };

    const auto refresh = [this] { releases().constFirst()->setDownloaded(hasDotnetCached()); };

    // The queue runs these one at a time; the second refresh flips downloaded to true.
    DownloadManager::instance()->download(
        QNetworkRequest{runtimeUrl}, ".NET Runtime 6.0.36"_L1, true, saveTo(m_runtimeZip, ".NET runtime"), fail, refresh);
    DownloadManager::instance()->download(QNetworkRequest{desktopUrl},
                                          ".NET Desktop Runtime 6.0.36"_L1,
                                          true,
                                          saveTo(m_desktopZip, ".NET desktop runtime"),
                                          fail,
                                          refresh);
}

void Dotnet::deleteRelease(ModRelease *release)
{
    if (!release->downloaded())
        return;
    if (QFile{m_runtimeZip}.remove() && QFile{m_desktopZip}.remove())
        release->setDownloaded(false);
    else
        release->setDownloaded(hasDotnetCached());
}

void Dotnet::uninstallMod(Game *game)
{
    if (game && game->hasValidWine())
    {
        const auto base = dotnetDir(game);
        QDir{base + "/shared/Microsoft.WindowsDesktop.App/6.0.36"_L1}.removeRecursively();
        QDir{base + "/shared/Microsoft.NETCore.App/6.0.36"_L1}.removeRecursively();
        QDir{base + "/host/fxr/6.0.36"_L1}.removeRecursively();
        removeIfEmpty(base + "/shared/Microsoft.WindowsDesktop.App"_L1);
        removeIfEmpty(base + "/shared/Microsoft.NETCore.App"_L1);
        removeIfEmpty(base + "/shared"_L1);
        removeIfEmpty(base + "/host/fxr"_L1);
        removeIfEmpty(base + "/host"_L1);
        if (!QDir{base + "/shared"_L1}.exists() && !QDir{base + "/host"_L1}.exists())
        {
            // We laid down the only version; take the muxer with us. If something
            // else lives here, leave it alone.
            QFile::remove(base + "/dotnet.exe"_L1);
            QFile::remove(base + "/LICENSE.txt"_L1);
            QFile::remove(base + "/ThirdPartyNotices.txt"_L1);
            removeIfEmpty(base);
        }
    }
    Mod::uninstallMod(game);
}

void Dotnet::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (!hasDotnetCached() || !game || !game->hasValidWine())
        return;

    const auto target = dotnetDir(game);
    if (!extractZip(m_runtimeZip, target) || !extractZip(m_desktopZip, target))
    {
        fail("Kaon couldn't extract the .NET runtime into %1's prefix. Its log has the details: "
             "~/.cache/LorenDB/Kaon/kaon.log"_L1.arg(game->name()));
        return;
    }

    if (!isInstalledForGame(game))
    {
        fail("The .NET runtime was extracted, but it is still missing from %1's prefix."_L1.arg(game->name()));
        return;
    }

    Mod::installModImpl(game, exe);
}

QMap<int, Game::LaunchOption> Dotnet::acceptableInstallCandidates(const Game *game) const
{
    auto options = Mod::acceptableInstallCandidates(game);
    options.removeIf([this, game](const std::pair<int, Game::LaunchOption> &exe) {
        return exe.second.platform != Game::Platform::Windows;
    });
    return options;
}

QList<ModRelease *> Dotnet::releases() const
{
    static QList<ModRelease *> l = {new ModRelease{42,
                                                   ".NET Desktop Runtime 6.0.36"_L1,
                                                   QDateTime{{2024, 11, 12}, {0, 0, 0}},
                                                   false,
                                                   hasDotnetCached(),
                                                   {ModRelease::Asset{
                                                        .id = 420,
                                                        .name = ".NET Runtime 6.0.36"_L1,
                                                        .url = {runtimeUrl},
                                                        .timestamp = QDateTime{{2024, 11, 12}, {0, 0, 0}},
                                                        .size = 33057872,
                                                    },
                                                    ModRelease::Asset{
                                                        .id = 421,
                                                        .name = ".NET Desktop Runtime 6.0.36"_L1,
                                                        .url = {desktopUrl},
                                                        .timestamp = QDateTime{{2024, 11, 12}, {0, 0, 0}},
                                                        .size = 36315065,
                                                    }},
                                                   // parented to the mod so downloads of it are reported like any other
                                                   // release
                                                   const_cast<Dotnet *>(this)}};
    return l;
}
