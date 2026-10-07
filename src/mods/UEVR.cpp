#include "UEVR.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

#include "Aptabase.h"
#include "Archive.h"
#include "Dotnet.h"
#include "DownloadManager.h"
#include "UevrPlugins.h"
#include "Wine.h"

Q_LOGGING_CATEGORY(UEVRLog, "uevr")

UEVR::UEVR(QObject *parent)
    : Mod{parent}
{
    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        // What was saved last time is there at once; the answer from GitHub replaces it when it differs
        parseReleaseInfoJson();
        refreshReleases();
    });
}

UEVR *UEVR::instance()
{
    static auto u = new UEVR;
    return u;
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

bool UEVR::isInstalledForGame(const Game *game) const
{
    return currentRelease() && currentRelease()->downloaded();
}

void UEVR::downloadRelease(ModRelease *release)
{
    if (!release || release->assets().isEmpty())
        return;

    Aptabase::instance()->track("download-uevr"_L1, {{"version"_L1, release->name()}});

    const auto releaseId = release->id();
    const auto asset = release->assets().constFirst();
    DownloadManager::instance()->download(
        QNetworkRequest{asset.url},
        releaseTitle(release),
        true,
        [this, releaseId, asset](const QByteArray &data) {
            const QString targetDir = path(Paths::UEVRBasePath) + '/' + QString::number(releaseId);
            if (QString error;
                !asset.matches(data, &error) || !Archive::extractFresh(data, targetDir, "UEVRInjector.exe"_L1, &error))
            {
                failDownload(error);
                return;
            }

            // A release refresh may have replaced the ModRelease this download started with.
            if (auto *downloaded = releaseFromId(releaseId))
                downloaded->setDownloaded(true);
        },
        [](const QNetworkReply::NetworkError error, const QString &errorMessage) {
            qCWarning(UEVRLog) << "Download UEVR failed:" << errorMessage;
        });
}

void UEVR::deleteRelease(ModRelease *release)
{
    if (!release || !release->downloaded())
        return;

    QDir installDir{path(Paths::UEVRBasePath) + '/' + QString::number(release->id())};
    if (installDir.removeRecursively())
        release->setDownloaded(false);
}

QStringList UEVR::installedPlugins(Game *game)
{
    return UevrPlugins::installedPluginNames(game);
}

QString UEVR::installPlugin(Game *game, const QUrl &source)
{
    const auto error = UevrPlugins::installPlugin(game, source);
    if (!error.isEmpty())
        return error;
    Aptabase::instance()->track("install-uevr-plugin"_L1, {{"game"_L1, game ? game->name() : QString{}}});
    emit installedInGameChanged(game);
    return {};
}

QString UEVR::removePlugin(Game *game, const QString &fileName)
{
    const auto error = UevrPlugins::removePlugin(game, fileName);
    if (!error.isEmpty())
        return error;
    emit installedInGameChanged(game);
    return {};
}

QString UEVR::pluginHoldReason(Game *game)
{
    return UevrPlugins::manageHoldReason(game);
}

void UEVR::launchModImpl(Game *game)
{
    Wine::instance()->runInWine(currentRelease()->name(), game, path(Paths::CurrentUEVRInjector), true);
}

QMap<int, Game::LaunchOption> UEVR::acceptableInstallCandidates(const Game *game) const
{
    auto options = Mod::acceptableInstallCandidates(game);
    options.removeIf([this, game](const std::pair<int, Game::LaunchOption> &exe) {
        return exe.second.platform != Game::Platform::Windows;
    });
    return options;
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

void UEVR::refreshReleases()
{
    // Releases and nightlies are two repositories. The list is rebuilt once, after both have answered.
    auto settled = std::make_shared<int>(0);
    auto anyChanged = std::make_shared<bool>(false);

    const auto impl = [this, settled, anyChanged](QUrl url, const QString cachePath) {
        QNetworkRequest req{url};
        req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

        const auto done = [this, settled, anyChanged] {
            if (++(*settled) == 2 && (*anyChanged || m_releases.isEmpty()))
                parseReleaseInfoJson();
        };

        DownloadManager::instance()->refreshCached(
            req,
            "UEVR release information"_L1,
            cachePath,
            [anyChanged, done](bool changed) {
                *anyChanged = *anyChanged || changed;
                done();
            },
            [done](const QString &errorMessage) {
                qCInfo(UEVRLog) << "Error while fetching releases:" << errorMessage;
                done();
            });
    };

    impl({"https://api.github.com/repos/praydog/UEVR/releases"_L1}, path(Paths::CachedReleasesJSON));
    impl({"https://api.github.com/repos/praydog/UEVR-nightly/releases"_L1}, path(Paths::CachedNightliesJSON));
}

void UEVR::parseReleaseInfoJson()
{
    const auto read = [](const QString &path) {
        QFile cached{path};
        return cached.open(QFile::ReadOnly) ? QJsonDocument::fromJson(cached.readAll()).array() : QJsonArray{};
    };
    const auto releases = read(path(Paths::CachedReleasesJSON));
    const auto nightlies = read(path(Paths::CachedNightliesJSON));

    const int currentId = currentRelease() ? currentRelease()->id() : m_lastCurrentReleaseId;

    QList<ModRelease *> parsed;
    const auto parseRelease = [this, &parsed](const QJsonValue &json, bool nightly) {
        auto name = json["name"_L1].toString();

        // Shorten the git hash in nightly release names for display purposes
        static const QRegularExpression rx(R"(^UEVR Nightly \d+ \([0-9a-f]{40}\)$)"_L1,
                                           QRegularExpression::CaseInsensitiveOption);

        // We want to end up with a 7-character git hash. Therefore, after finding the " (", we increment 2 to get to the
        // git hash and then 7 more to get to the end of the short hash.
        if (rx.match(name).hasMatch())
            name = name.left(name.lastIndexOf(" ("_L1) + 9) + ')';

        const auto id = json["id"_L1].toInt();
        const auto timestamp = QDateTime::fromString(json["published_at"_L1].toString(), Qt::ISODate);
        const auto downloaded =
            QFileInfo::exists(path(Paths::UEVRBasePath) + '/' + QString::number(id) + "/UEVRInjector.exe"_L1);

        QList<ModRelease::Asset> assets;
        for (const auto &asset : json["assets"_L1].toArray())
        {
            if (asset["name"_L1].toString().toLower() == "uevr.zip"_L1)
            {
                assets.push_back({
                    .id = asset["id"_L1].toInt(),
                    .name = asset["name"_L1].toString(),
                    .url = {asset["browser_download_url"_L1].toString()},
                    .timestamp = QDateTime::fromString(asset["updated_at"_L1].toString(), Qt::ISODate),
                    .size = asset["size"_L1].toInt(),
                    .digest = asset["digest"_L1].toString(),
                });
            }
        }

        // A release without UEVR.zip has nothing to run, and must never end up as the current one
        if (!assets.isEmpty())
            parsed.push_back(new ModRelease{id, name, timestamp, nightly, false, downloaded, assets, this});
    };

    for (const auto &release : releases)
        parseRelease(release, false);
    for (const auto &nightly : nightlies)
        parseRelease(nightly, true);

    // Keep the old list when there is nothing to replace it with, so the current release never points at a deleted one
    if (parsed.isEmpty())
        return;

    beginResetModel();
    for (const auto release : std::as_const(m_releases))
        release->deleteLater();
    m_releases = parsed;
    endResetModel();

    if (currentId != 0 && !releaseFromId(currentId))
        qCWarning(UEVRLog) << "Saved UEVR release" << currentId << "is no longer available";
    setCurrentRelease(releaseFromId(currentId) ? currentId : m_releases.constFirst()->id());
}
