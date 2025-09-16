#include "UEVR.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QProcess>
#include <QSemaphore>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include "Aptabase.h"
#include "Dotnet.h"
#include "DownloadManager.h"
#include "Wine.h"

Q_LOGGING_CATEGORY(UEVRLog, "uevr")

UEVR *UEVR::s_instance = nullptr;

UEVR::UEVR(QObject *parent)
    : Mod{parent}
{
    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        parseReleaseInfoJson();
        updateAvailableReleases();
    });
}

UEVR *UEVR::instance()
{
    if (!s_instance)
        s_instance = new UEVR;
    return s_instance;
}

UEVR *UEVR::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

const QLoggingCategory &UEVR::logger() const
{
    return UEVRLog();
}

QList<Mod *> UEVR::dependencies() const
{
    return {Dotnet::instance()};
}

bool UEVR::checkGameCompatibility(const Game *game) const
{
    if (game->noWindowsSupport())
        return false;
    return Mod::checkGameCompatibility(game);
}

bool UEVR::isInstalledForGame(const Game *game) const
{
    return currentRelease()->downloaded();
}

void UEVR::downloadRelease(ModRelease *release)
{
    Aptabase::instance()->track("download-uevr"_L1, {{"version"_L1, release->name()}});

    if (release->downloadUrl().isEmpty())
        return;

    auto tempDir = new QTemporaryDir;
    if (!tempDir->isValid())
    {
        qCWarning(UEVRLog) << "Failed to create temporary directory";
        return;
    }

    const QString zipPath = tempDir->path() + "/uevr_" + QString::number(release->id()) + ".zip";

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

                QString targetDir = path(Paths::UEVRBasePath) + '/' + QString::number(release->id());
                if (!QFileInfo::exists(targetDir))
                    QDir().mkpath(targetDir);

                QProcess process;
                process.setWorkingDirectory(targetDir);
                QStringList args;
                args << "-o" << zipPath;
                process.start("unzip", args);
                process.waitForFinished();

                if (process.exitCode() != 0)
                {
                    qCWarning(UEVRLog) << "Unzip UEVR failed:" << process.errorString();
                    qCWarning(UEVRLog) << process.readAllStandardError();
                }
                else
                    release->setDownloaded(true);
            }
            else
                qCWarning(UEVRLog) << "Failed to save UEVR";
        },
        [](const QNetworkReply::NetworkError error, const QString &errorMessage) {
            qCWarning(UEVRLog) << "Download UEVR failed:" << errorMessage;
        },
        [tempDir] { delete tempDir; });
}

void UEVR::deleteRelease(ModRelease *release)
{
    if (!release->downloaded())
        return;

    QDir installDir{path(Paths::UEVRBasePath) + '/' + QString::number(release->id())};
    if (installDir.removeRecursively())
        release->setDownloaded(false);
}

void UEVR::launchMod(Game *game)
{
    Aptabase::instance()->track("launch-uevr"_L1, {{"version"_L1, currentRelease()->name()}});
    Wine::instance()->runInWine(currentRelease()->name(), game, path(Paths::CurrentUEVRInjector));
}

QString UEVR::path(const Paths path) const
{
    switch (path)
    {
    case Paths::CurrentUEVR:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/uevr/"_L1 +
               QString::number(currentRelease()->id());
    case Paths::CurrentUEVRInjector:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/uevr/"_L1 +
               QString::number(currentRelease()->id()) + "/UEVRInjector.exe"_L1;
    case Paths::UEVRBasePath:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/uevr"_L1;
    case Paths::CachedReleasesJSON:
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/uevr_releases.json"_L1;
    case Paths::CachedNightliesJSON:
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/uevr_nightlies.json"_L1;
    default:
        return {};
    }
}

void UEVR::updateAvailableReleases()
{
    auto semaphore = new QSemaphore{2};
    semaphore->acquire(2);

    const auto impl = [this, semaphore](QUrl url, const QString cachePath) {
        QNetworkRequest req{url};
        req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

        DownloadManager::instance()->download(
            req,
            "UEVR release information"_L1,
            true,
            [this, cachePath, semaphore](const QByteArray &data) {
                QFile cache{cachePath};
                if (cache.open(QFile::WriteOnly))
                {
                    cache.write(data);
                    cache.close();
                }

                semaphore->release();
                if (semaphore->available() == 2)
                {
                    parseReleaseInfoJson();
                    delete semaphore;
                }
            },
            [](const QNetworkReply::NetworkError error, const QString &errorMessage) {
                qCInfo(UEVRLog) << "Error while fetching releases:" << errorMessage;
                return;
            });
    };

    impl({"https://api.github.com/repos/praydog/UEVR/releases"_L1}, path(Paths::CachedReleasesJSON));
    impl({"https://api.github.com/repos/praydog/UEVR-nightly/releases"_L1}, path(Paths::CachedNightliesJSON));
}

void UEVR::parseReleaseInfoJson()
{
    QFile cachedReleases{path(Paths::CachedReleasesJSON)};
    QFile cachedNightlies{path(Paths::CachedNightliesJSON)};
    if (!cachedReleases.open(QFile::ReadOnly) || !cachedNightlies.open(QFile::ReadOnly))
    {
        updateAvailableReleases();
        return;
    }

    beginResetModel();

    const auto releases = QJsonDocument::fromJson(cachedReleases.readAll());
    const auto nightlies = QJsonDocument::fromJson(cachedNightlies.readAll());

    const int currentId = currentRelease() ? currentRelease()->id() : m_lastCurrentReleaseId;

    for (const auto release : std::as_const(m_releases))
        release->deleteLater();
    m_releases.clear();

    auto parseRelease = [this](const QJsonValue &json, bool nightly) {
        auto name = json["name"_L1].toString();

        // Shorten the git hash in nightly release names for display purposes
        static const QRegularExpression rx(R"(^UEVR Nightly \d+ \([0-9a-f]{40}\)$)"_L1,
                                           QRegularExpression::CaseInsensitiveOption);

        if (rx.match(name).hasMatch())
        {
            // We want to end up with a 7-character git hash. Therefore, after finding the " (", we increment 2 to get to the
            // git hash and then 7 more to get to the end of the short hash.
            int parenStart = name.lastIndexOf(" ("_L1) + 9;
            if (parenStart != -1)
                name = name.left(parenStart) + ')';
        }

        const auto id = json["id"_L1].toInt();
        const auto timestamp = QDateTime::fromString(json["published_at"_L1].toString(), Qt::ISODate);
        const auto downloaded = QFileInfo::exists(UEVR::instance()->path(UEVR::Paths::UEVRBasePath) + '/' +
                                                 QString::number(id) + "/UEVRInjector.exe"_L1);

        QUrl downloadUrl;
        for (const auto &asset : json["assets"_L1].toArray())
        {
            if (asset["name"_L1].toString().toLower() == "uevr.zip"_L1)
            {
                downloadUrl = asset["browser_download_url"_L1].toString();
                break;
            }
        }

        return new ModRelease{id, name, timestamp, nightly, downloaded, downloadUrl, this};
    };

    for (const auto &release : releases.array())
        m_releases.push_back(parseRelease(release, false));
    for (const auto &nightly : nightlies.array())
        m_releases.push_back(parseRelease(nightly, true));

    endResetModel();

    setCurrentRelease(currentId == 0 ? m_releases.first()->id() : currentId);
}
