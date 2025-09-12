#include "UUVR.h"

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
#include "Bepinex.h"
#include "DownloadManager.h"

Q_LOGGING_CATEGORY(UUVRLog, "uuvr")

UUVR *UUVR::s_instance = nullptr;

UUVR::~UUVR()
{
    QSettings settings;
    settings.beginGroup("uuvr"_L1);
    settings.setValue("currentUuvr"_L1, currentRelease()->id());
}

UUVR *UUVR::instance()
{
    if (!s_instance)
        s_instance = new UUVR;
    return s_instance;
}

UUVR *UUVR::create(QQmlEngine *, QJSEngine *)
{
    return instance();
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

QString UUVR::path(const Paths p) const
{
    switch (p)
    {
    case Paths::CurrentUUVR:
        return path(Paths::UUVRBasePath) + "/uuvr_" + QString::number(currentRelease()->id()) + ".zip";
    case Paths::UUVRBasePath:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/uuvr"_L1;
    case Paths::CachedReleasesJSON:
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/uuvr_releases.json"_L1;
    default:
        return {};
    }
}

void UUVR::downloadRelease(ModRelease *release)
{
    Aptabase::instance()->track("download-uuvr"_L1, {{"version"_L1, release->name()}});

    if (release->downloadUrl().isEmpty())
        return;

    const QString zipPath = path(Paths::UUVRBasePath) + "/uuvr_" + QString::number(release->id()) + ".zip";

    DownloadManager::instance()->download(
        QNetworkRequest{release->downloadUrl()},
        release->name(),
        true,
        [this, zipPath, release](const QByteArray &data) {
            QFile file(zipPath);
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(data);
                file.close();
                release->setInstalled(true);
            }
            else
                qCWarning(UUVRLog) << "Failed to save UUVR";
        },
        [](const QNetworkReply::NetworkError error, const QString &errorMessage) {
            qCWarning(UUVRLog) << "Download UUVR failed:" << errorMessage;
        });
}

void UUVR::deleteRelease(ModRelease *release)
{
    if (!release->installed())
        return;

    QFile installed{path(Paths::UUVRBasePath) + "/uuvr_" + QString::number(release->id()) + ".zip"};
    if (installed.remove())
        release->setInstalled(false);
}

void UUVR::installMod(Game *game)
{
    const QString targetDir = game->installDir() + "/BepInEx"_L1;
    if (!QFileInfo::exists(targetDir))
        QDir().mkpath(targetDir);

    QProcess process;
    process.setWorkingDirectory(targetDir);
    QStringList args;
    args << "-o" << path(Paths::CurrentUUVR);
    process.start("unzip", args);
    process.waitForFinished();
    if (process.exitCode() != 0)
    {
        qCWarning(UUVRLog) << "Unzip UUVR failed:" << process.errorString();
        qCWarning(UUVRLog) << process.readAllStandardError();
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

void UUVR::uninstallMod(Game *game)
{
    const QString targetDir = game->installDir() + "/BepInEx/"_L1;

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->id());
    const auto toRemove = settings.value("installedFiles"_L1).toStringList();

    for (const auto file : toRemove)
    {
        if (file.isEmpty())
            continue;
        const auto fullPath = targetDir + file;
        if (fullPath.endsWith('/'))
        {
            QDir d{fullPath};
            d.removeRecursively();
        }
        else
        {
            QFile f{fullPath};
            f.remove();
        }
    }

    settings.remove("installedFiles"_L1);
}

UUVR::UUVR(QObject *parent)
    : Mod{parent}
{
    QDir{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)}.mkdir("uuvr"_L1);

    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        parseReleaseInfoJson();
        updateAvailableReleases();
    });
}

void UUVR::updateAvailableReleases()
{
    QNetworkRequest req{{"https://api.github.com/repos/Raicuparta/uuvr/releases"_L1}};
    req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

    DownloadManager::instance()->download(
        req,
        "UUVR release information"_L1,
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
            qCInfo(UUVRLog) << "Error while fetching releases:" << errorMessage;
            return;
        });
}

void UUVR::parseReleaseInfoJson()
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
        const auto installed = QFileInfo::exists(path(Paths::UUVRBasePath) + "/uuvr_"_L1 + QString::number(id) + ".zip"_L1);

        QUrl downloadUrl;
        for (const auto &asset : release["assets"_L1].toArray())
        {
            if (const auto assetName = asset["name"_L1].toString();
                    assetName.startsWith("uuvr-mono"_L1) && assetName.endsWith(".zip"_L1))
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
