#include "Portal2VR.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include "Aptabase.h"
#include "DownloadManager.h"

Q_LOGGING_CATEGORY(P2VRLog, "portal2vr")

Portal2VR *Portal2VR::instance()
{
    static Portal2VR p2vr;
    return &p2vr;
}

Portal2VR *Portal2VR::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString Portal2VR::info() const
{
    return "See [GitHub](https://github.com/Gistix/portal2vr?tab=readme-ov-file#how-to-use) for required Portal 2 launch options"_L1;
}

bool Portal2VR::checkGameCompatibility(const Game *game) const
{
    return game->store() == Game::Store::Steam && game->id() == "620"_L1;
}

bool Portal2VR::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    return QFileInfo::exists(game->installDir() + "/bin/openvr_api.dll"_L1);
}

QString Portal2VR::path(const Paths p) const
{
    switch (p)
    {
    case Paths::CurrentPortal2VR:
        return pathForRelease(currentRelease()->id());
    case Paths::Portal2VRBasePath:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Portal2VR"_L1;
    case Paths::CachedReleasesJSON:
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/Portal2VR_releases.json"_L1;
    default:
        return {};
    }
}

QString Portal2VR::pathForRelease(int id) const
{
    return path(Paths::Portal2VRBasePath) + "/Portal2VR_" + QString::number(id) + ".zip";
}

void Portal2VR::downloadRelease(ModRelease *release)
{
    Aptabase::instance()->track("download-portal2vr"_L1, {{"version"_L1, release->name()}});

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
                release->setInstalled(true);
            }
            else
                qCWarning(P2VRLog) << "Failed to save Portal2VR";
        },
        [](const QNetworkReply::NetworkError error, const QString &errorMessage) {
            qCWarning(P2VRLog) << "Download Portal2VR failed:" << errorMessage;
        });
}

void Portal2VR::deleteRelease(ModRelease *release)
{
    if (!release->installed())
        return;

    QFile installed{pathForRelease(release->id())};
    if (installed.remove())
        release->setInstalled(false);
}

void Portal2VR::installMod(Game *game)
{
    QProcess process;
    process.setWorkingDirectory(game->installDir());
    QStringList args;
    args << "-o" << path(Paths::CurrentPortal2VR);
    process.start("unzip", args);
    process.waitForFinished();
    if (process.exitCode() != 0)
    {
        qCWarning(P2VRLog) << "Unzip Portal2VR failed:" << process.errorString();
        qCWarning(P2VRLog) << process.readAllStandardError();
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
}

void Portal2VR::uninstallMod(Game *game)
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
        const auto fullPath = game->installDir() + '/' + file;
        if (fullPath.endsWith('/'))
            dirs << fullPath;
        else
            QFile{fullPath}.remove();
    }

    for (const auto &dir : dirs)
        if (QDir d{dir}; d.isEmpty())
            d.removeRecursively();

    settings.remove("installedFiles"_L1);
}

Portal2VR::Portal2VR(QObject *parent)
    : Mod{parent}
{
    QDir{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)}.mkdir("Portal2VR"_L1);

    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        parseReleaseInfoJson();
        updateAvailableReleases();
    });
}

void Portal2VR::updateAvailableReleases()
{
    QNetworkRequest req{{"https://api.github.com/repos/Gistix/portal2vr/releases"_L1}};
    req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

    DownloadManager::instance()->download(
        req,
        "Portal 2 VR release information"_L1,
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
        [](const QNetworkReply::NetworkError error, const QString &errorMessage) {
            qCInfo(P2VRLog) << "Error while fetching releases:" << errorMessage;
            return;
        });
}

void Portal2VR::parseReleaseInfoJson()
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
        const auto installed = QFileInfo::exists(pathForRelease(id));

        QUrl downloadUrl;
        for (const auto &asset : release["assets"_L1].toArray())
        {
            if (const auto assetName = asset["name"_L1].toString(); assetName.startsWith("Portal2VR"_L1) &&
                    assetName.endsWith(".zip"_L1) &&
                    !assetName.contains("debug"_L1, Qt::CaseInsensitive))
            {
                downloadUrl = asset["browser_download_url"_L1].toString();
                break;
            }
        }

        m_releases.push_back(new ModRelease{id, name, timestamp, false, installed, downloadUrl, this});
    }

    endResetModel();

    setCurrentRelease(currentId == 0 ? m_releases.first()->id() : currentId);
}
