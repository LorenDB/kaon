#include "Dotnet.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>

#include "Aptabase.h"
#include "Archive.h"
#include "DownloadManager.h"

Q_LOGGING_CATEGORY(DotNetLog, "dotnet")

namespace
{
    const QUrl runtimeUrl{"https://builds.dotnet.microsoft.com/dotnet/Runtime/6.0.36/dotnet-runtime-6.0.36-win-x64.zip"_L1};
    const QUrl desktopUrl{
        "https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/6.0.36/windowsdesktop-runtime-6.0.36-win-x64.zip"_L1};
    // From Microsoft's release metadata for 6.0.36, the last release of .NET 6:
    // https://builds.dotnet.microsoft.com/dotnet/release-metadata/6.0/releases.json
    const auto runtimeDigest = "sha512:935db5c6cee19f2c016e67168bfae7b491044735de76c673abb3b125dd325fd5e779d7efe12ba80178d4"
                               "6689ae70a25e558a3fa846417d44c5f4ca256e7f4bf2"_L1;
    const auto desktopDigest = "sha512:cee88fef07643dceb3d34ea71b64eeae85b8e39e7042bc4d35bc3a1f1df29373681dc48e31c01c9f593e"
                               "c412699f6762b6fc5817433283c89df35a27ce8885bf"_L1;

    QString dotnetDir(const Game *game)
    {
        const auto dir = game->winePrefix() + "/drive_c/Program Files/dotnet"_L1;
        // An x64 program like UEVR's injector runs emulated under an arm64 Wine, and the .NET host then looks for its
        // runtime in an x64 subfolder, the same as on Windows on Arm. See pal::get_default_installation_dir in the host.
        return game->hasArm64Wine() ? dir + "/x64"_L1 : dir;
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

    const auto saveTo = [this](const QString &path, const ModRelease::Asset &asset) {
        return [this, path, asset](const QByteArray &data) {
            if (QString why; !asset.matches(data, &why))
            {
                failDownload(why);
                return;
            }
            // Written in one step: both files being there is what counts as downloaded
            QSaveFile file{path};
            if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
                failDownload("Kaon couldn't save %1 to %2: %3"_L1.arg(asset.name, path, file.errorString()));
        };
    };

    const auto fail = [](const QNetworkReply::NetworkError, const QString &errorMessage) {
        qCWarning(DotNetLog) << ".NET desktop runtime download failed:" << errorMessage;
    };

    const auto refresh = [this] { releases().constFirst()->setDownloaded(hasDotnetCached()); };

    // The queue runs these one at a time; the second refresh flips downloaded to true.
    const auto assets = releases().constFirst()->assets();
    DownloadManager::instance()->download(
        QNetworkRequest{runtimeUrl}, ".NET Runtime 6.0.36"_L1, true, saveTo(m_runtimeZip, assets.at(0)), fail, refresh);
    DownloadManager::instance()->download(QNetworkRequest{desktopUrl},
                                          ".NET Desktop Runtime 6.0.36"_L1,
                                          true,
                                          saveTo(m_desktopZip, assets.at(1)),
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

QString Dotnet::installHoldReason(const Game *game) const
{
    if (game && !game->hasValidWine())
        return "Can be installed once the prefix exists"_L1;
    return {};
}

void Dotnet::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (!game)
        return;
    if (!hasDotnetCached())
    {
        fail("Download the .NET runtime before installing it."_L1);
        return;
    }
    if (!game->hasValidWine())
    {
        fail("%1 has no Proton prefix yet. Launch it once, rescan, and try again."_L1.arg(game->name()));
        return;
    }

    const auto target = dotnetDir(game);
    if (QString error; !Archive::extract(m_runtimeZip, target, &error) || !Archive::extract(m_desktopZip, target, &error))
    {
        fail(error);
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
                                                   false,
                                                   hasDotnetCached(),
                                                   {ModRelease::Asset{
                                                        .id = 420,
                                                        .name = ".NET Runtime 6.0.36"_L1,
                                                        .url = {runtimeUrl},
                                                        .timestamp = QDateTime{{2024, 11, 12}, {0, 0, 0}},
                                                        .size = 33057872,
                                                        .digest = runtimeDigest,
                                                    },
                                                    ModRelease::Asset{
                                                        .id = 421,
                                                        .name = ".NET Desktop Runtime 6.0.36"_L1,
                                                        .url = {desktopUrl},
                                                        .timestamp = QDateTime{{2024, 11, 12}, {0, 0, 0}},
                                                        .size = 36315065,
                                                        .digest = desktopDigest,
                                                    }},
                                                   // parented to the mod so downloads of it are reported like any other
                                                   // release
                                                   const_cast<Dotnet *>(this)}};
    return l;
}
