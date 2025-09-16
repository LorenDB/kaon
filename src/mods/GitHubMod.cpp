#include "GitHubMod.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include "Aptabase.h"
#include "DownloadManager.h"

void GitHubMod::downloadRelease(ModRelease *release)
{
    Aptabase::instance()->track("download-"_L1 + settingsGroup(), {{"version"_L1, release->name()}});

    if (release->downloadUrl().isEmpty())
        return;

    DownloadManager::instance()->download(
        QNetworkRequest{release->downloadUrl()},
        release->name(),
        true,
        [this, release](const QByteArray &data) {
            QFile file{pathForRelease(release->id())};
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(data);
                file.close();
                release->setDownloaded(true);
            }
            else
                qCWarning(logger()).noquote() << "Failed to save" << displayName();
        },
        [this](const QNetworkReply::NetworkError error, const QString &errorMessage) {
            qCWarning(logger()).noquote() << "Download" << displayName() << "failed:" << errorMessage;
        });
}

void GitHubMod::deleteRelease(ModRelease *release)
{
    if (!release->downloaded())
        return;

    QFile downloaded{pathForRelease(release->id())};
    if (downloaded.remove())
        release->setDownloaded(false);
}

QString GitHubMod::path(const Paths p) const
{
    switch (p)
    {
    case Paths::CurrentRelease:
        return pathForRelease(currentRelease()->id());
    case Paths::ReleaseBasePath:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + '/' + settingsGroup();
    case Paths::CachedReleasesJSON:
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/%1_releases.json"_L1.arg(settingsGroup());
    default:
        return {};
    }
}

QString GitHubMod::pathForRelease(int id) const
{
    return path(Paths::ReleaseBasePath) + "/%1_%2.zip"_L1.arg(settingsGroup(), QString::number(id));
}

GitHubMod::GitHubMod(QObject *parent)
    : Mod{parent}
{
    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        // This gets executed here so we can actually call the virtual settingsGroup(). I love C++! /s
        QDir{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)}.mkdir(settingsGroup());

        parseReleaseInfoJson();
        updateAvailableReleases();
    });
}

void GitHubMod::updateAvailableReleases()
{
    QNetworkRequest req{githubUrl()};
    req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

    DownloadManager::instance()->download(
        req,
        "%1 release information"_L1.arg(displayName()),
        true,
        [this](const QByteArray &data) {
            QFile cache{path(Paths::CachedReleasesJSON)};
            if (cache.open(QFile::WriteOnly))
            {
                cache.write(data);
                cache.close();
            }
            parseReleaseInfoJson();
        },
        [this](const QNetworkReply::NetworkError error, const QString &errorMessage) {
            qCInfo(logger()) << "Error while fetching releases:" << errorMessage;
            return;
        });
}

void GitHubMod::parseReleaseInfoJson()
{
    QFile cachedReleases{path(Paths::CachedReleasesJSON)};
    if (!cachedReleases.open(QFile::ReadOnly))
    {
        updateAvailableReleases();
        return;
    }

    beginResetModel();

    const auto releases = QJsonDocument::fromJson(cachedReleases.readAll());
    const int currentId = currentRelease() ? currentRelease()->id() : 0;

    for (const auto release : std::as_const(m_releases))
        release->deleteLater();
    m_releases.clear();

    for (const auto &release : releases.array())
    {
        const auto name = release["name"_L1].toString();
        const auto id = release["id"_L1].toInt();
        const auto timestamp = QDateTime::fromString(release["published_at"_L1].toString(), Qt::ISODate);
        const auto downloaded = QFileInfo::exists(pathForRelease(id));

        QUrl downloadUrl;
        for (const auto &asset : release["assets"_L1].toArray())
        {
            if (isThisFileTheActualModDownload(asset["name"_L1].toString()))
            {
                downloadUrl = asset["browser_download_url"_L1].toString();
                break;
            }
        }

        m_releases.push_back(new ModRelease{id, name, timestamp, false, downloaded, downloadUrl, this});
    }

    endResetModel();

    setCurrentRelease(currentId == 0 ? m_releases.first()->id() : currentId);
}

GitHubZipExtractorMod::GitHubZipExtractorMod(QObject *parent)
    : GitHubMod{parent}
{}

void GitHubZipExtractorMod::installMod(Game *game)
{
    if (!QFileInfo::exists(modInstallDirForGame(game)))
        QDir().mkpath(modInstallDirForGame(game));

    QProcess process;
    process.setWorkingDirectory(modInstallDirForGame(game));
    QStringList args;
    args << "-o" << path(Paths::CurrentRelease);
    process.start("unzip", args);
    process.waitForFinished();
    if (process.exitCode() != 0)
    {
        qCWarning(logger()).noquote() << "Unzip" << displayName() << "failed:" << process.errorString();
        qCWarning(logger()) << process.readAllStandardError();
        return;
    }

    args[0] = "-Z1"_L1;
    process.start("unzip", args);
    process.waitForFinished();
    if (process.exitCode() == 0)
    {
        QSettings settings;
        settings.beginGroup(settingsGroup());
        settings.beginGroup(game->id());

        const auto listing = QString{process.readAllStandardOutput()}.split('\n');
        settings.setValue("installedFiles"_L1, listing);
    }

    Mod::installMod(game);
}

void GitHubZipExtractorMod::uninstallMod(Game *game)
{
    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->id());
    const auto toRemove = settings.value("installedFiles"_L1).toStringList();

    QStringList dirs;

    for (const auto &file : toRemove)
    {
        if (file.isEmpty())
            continue;
        const auto fullPath = modInstallDirForGame(game) + '/' + file;
        if (fullPath.endsWith('/'))
            dirs << fullPath;
        else
            QFile{fullPath}.remove();
    }

    std::sort(dirs.begin(), dirs.end(), [](const auto &l, const auto &r) { return l.count('/') > r.count('/'); });
    for (const auto &dir : std::as_const(dirs))
        if (QDir d{dir}; d.isEmpty())
            d.removeRecursively();

    settings.remove("installedFiles"_L1);
    Mod::uninstallMod(game);
}
