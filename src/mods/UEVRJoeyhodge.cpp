#include "UEVRJoeyhodge.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

#include "Aptabase.h"
#include "Archive.h"
#include "Dotnet.h"
#include "DownloadManager.h"
#include "GitHub.h"
#include "UevrPlugins.h"
#include "UevrProfiles.h"
#include "Wine.h"

Q_LOGGING_CATEGORY(UEVRJoeyhodgeLog, "uevr.joeyhodge")

UEVRJoeyhodge::UEVRJoeyhodge(QObject *parent)
    : Mod{parent}
{
    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        // What was saved last time is there at once. GitHub is asked when that copy is old enough to be worth a request.
        parseReleaseInfoJson();
        fetchReleases(GitHub::releaseListMaxAgeSeconds);
    });
}

UEVRJoeyhodge *UEVRJoeyhodge::instance()
{
    static auto u = new UEVRJoeyhodge;
    return u;
}

UEVRJoeyhodge *UEVRJoeyhodge::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString UEVRJoeyhodge::info() const
{
    return "A new release comes out most days, named after what it fixes. Its notes on GitHub often list config.txt "
           "settings to add for a particular game. Kaon lists the releases that ship the whole of UEVR, not the ones "
           "that are only a replacement UEVRBackend.dll. DIBR/Mono builds add two experimental rendering methods: DIBR "
           "rebuilds the right eye from depth (turn Native Stereo Fix off for it), and Mono shows one view to both "
           "eyes."_L1;
}

const QLoggingCategory &UEVRJoeyhodge::logger() const
{
    return UEVRJoeyhodgeLog();
}

QList<Mod *> UEVRJoeyhodge::dependencies() const
{
    return {Dotnet::instance()};
}

bool UEVRJoeyhodge::isInstalledForGame(const Game *) const
{
    return currentRelease() && currentRelease()->downloaded();
}

void UEVRJoeyhodge::downloadRelease(ModRelease *release)
{
    if (!release || release->assets().isEmpty())
        return;

    Aptabase::instance()->track("download-"_L1 + settingsGroup(), {{"version"_L1, release->name()}});

    const auto releaseId = release->id();
    const auto asset = release->assets().constFirst();
    DownloadManager::instance()->download(
        QNetworkRequest{asset.url},
        releaseTitle(release),
        true,
        [this, releaseId, asset](const QByteArray &data) {
            const QString targetDir = path(Paths::BasePath) + '/' + QString::number(releaseId);
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
        [](const QNetworkReply::NetworkError, const QString &errorMessage) {
            qCWarning(UEVRJoeyhodgeLog) << "Download UEVR (joeyhodge) failed:" << errorMessage;
        });
}

void UEVRJoeyhodge::deleteRelease(ModRelease *release)
{
    if (!release || !release->downloaded())
        return;

    QDir installDir{path(Paths::BasePath) + '/' + QString::number(release->id())};
    if (installDir.removeRecursively())
        release->setDownloaded(false);
}

QStringList UEVRJoeyhodge::installedPlugins(Game *game)
{
    return UevrPlugins::installedPluginNames(game);
}

QString UEVRJoeyhodge::installPlugin(Game *game, const QUrl &source)
{
    const auto error = UevrPlugins::installPlugin(game, source);
    if (!error.isEmpty())
        return error;
    Aptabase::instance()->track("install-uevr-joeyhodge-plugin"_L1, {{"game"_L1, game ? game->name() : QString{}}});
    emit installedInGameChanged(game);
    return {};
}

QString UEVRJoeyhodge::removePlugin(Game *game, const QString &fileName)
{
    const auto error = UevrPlugins::removePlugin(game, fileName);
    if (!error.isEmpty())
        return error;
    emit installedInGameChanged(game);
    return {};
}

QString UEVRJoeyhodge::pluginHoldReason(Game *game)
{
    return UevrPlugins::manageHoldReason(game);
}

QString UEVRJoeyhodge::profileName(Game *game)
{
    return UevrProfiles::installedProfileName(game);
}

bool UEVRJoeyhodge::profileHasDll(const QUrl &source)
{
    const auto local = source.isLocalFile() ? source.toLocalFile() : source.toString();
    return UevrProfiles::zipContainsDll(local);
}

QString UEVRJoeyhodge::importProfile(Game *game, const QUrl &source)
{
    const auto error = UevrProfiles::importProfile(game, source);
    if (!error.isEmpty())
        return error;
    Aptabase::instance()->track("import-uevr-joeyhodge-profile"_L1, {{"game"_L1, game ? game->name() : QString{}}});
    emit installedInGameChanged(game);
    return {};
}

QString UEVRJoeyhodge::removeProfile(Game *game)
{
    const auto error = UevrProfiles::removeProfile(game);
    if (!error.isEmpty())
        return error;
    emit installedInGameChanged(game);
    return {};
}

QString UEVRJoeyhodge::profileHoldReason(Game *game)
{
    return UevrProfiles::manageHoldReason(game);
}

void UEVRJoeyhodge::launchModImpl(Game *game)
{
    Wine::instance()->runInWine(currentRelease()->name(), game, path(Paths::CurrentInjector), true);
}

void UEVRJoeyhodge::autoLaunchImpl(Game *game)
{
    // The fork's frontend keeps --attach=<process>, and waits for it when it isn't up yet
    const auto stems = UevrPlugins::exeStems(game);
    const QStringList args = stems.isEmpty() ? QStringList{} : QStringList{"--attach="_L1 + stems.constFirst()};
    Wine::instance()->runInWine(currentRelease()->name(), game, path(Paths::CurrentInjector), args, {}, {}, true);
}

QString UEVRJoeyhodge::path(const Paths p) const
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + '/' + settingsGroup();
    switch (p)
    {
    case Paths::BasePath:
        return base;
    case Paths::CurrentInjector:
        return base + '/' + QString::number(currentRelease()->id()) + "/UEVRInjector.exe"_L1;
    case Paths::CachedReleasesJSON:
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/%1_releases.json"_L1.arg(settingsGroup());
    default:
        return {};
    }
}

void UEVRJoeyhodge::refreshReleases()
{
    fetchReleases(0);
}

void UEVRJoeyhodge::fetchReleases(int maxAgeSeconds)
{
    // About one release a day, so a page of 100 reaches back past the first one that shipped a whole uevr.zip
    QNetworkRequest req{{"https://api.github.com/repos/joeyhodge/UEVR/releases?per_page=100"_L1}};
    req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

    DownloadManager::instance()->refreshCached(
        req,
        "UEVR (joeyhodge) release information"_L1,
        path(Paths::CachedReleasesJSON),
        [this](bool changed) {
            if (changed || m_releases.isEmpty())
                parseReleaseInfoJson();
        },
        [](const QString &errorMessage) { qCInfo(UEVRJoeyhodgeLog) << "Error while fetching releases:" << errorMessage; },
        maxAgeSeconds);
}

void UEVRJoeyhodge::parseReleaseInfoJson()
{
    const auto releases = GitHub::readCachedList(path(Paths::CachedReleasesJSON));
    if (!releases.isArray())
        return;

    const int currentId = currentRelease() ? currentRelease()->id() : m_lastCurrentReleaseId;

    // Most releases are only UEVRBackend.dll, to drop over an existing install, and nothing Kaon can run by itself.
    // Since October 2026 most also ship the whole of UEVR as uevr.zip, and a DIBR/Mono build of it (DIBRUEVR.zip,
    // later DIBRMONOuevr.zip). Each zip is a version of its own, so the release id is the asset's: those are unique.
    QList<ModRelease *> parsed;
    // Where nothing was picked yet: the newest regular build. GitHub lists the DIBR zip first, and the releases by when
    // they were created, not published.
    ModRelease *newest = nullptr;
    for (const auto &release : releases.array())
    {
        if (release["draft"_L1].toBool())
            continue;

        auto releaseName = release["name"_L1].toString().trimmed();
        if (releaseName.isEmpty())
            releaseName = release["tag_name"_L1].toString();
        const auto published = QDateTime::fromString(release["published_at"_L1].toString(), Qt::ISODate);
        const bool prerelease = release["prerelease"_L1].toBool();

        for (const auto &asset : release["assets"_L1].toArray())
        {
            const auto assetName = asset["name"_L1].toString();
            const auto lower = assetName.toLower();
            const bool dibr = lower.startsWith("dibr"_L1) && lower.endsWith("uevr.zip"_L1);
            if (lower != "uevr.zip"_L1 && !dibr)
                continue;

            const auto id = asset["id"_L1].toInt();
            if (id <= 0)
                continue;

            const auto downloaded =
                QFileInfo::exists(path(Paths::BasePath) + '/' + QString::number(id) + "/UEVRInjector.exe"_L1);

            QList<ModRelease::Asset> assets;
            assets.push_back({
                .id = id,
                .name = assetName,
                .url = {asset["browser_download_url"_L1].toString()},
                .timestamp = QDateTime::fromString(asset["updated_at"_L1].toString(), Qt::ISODate),
                .size = asset["size"_L1].toInt(),
                .digest = asset["digest"_L1].toString(),
            });

            // The names are long, and the version menu cuts off their ends, so the variant goes in front.
            // Identical timestamps sort unstably. Keep the regular build above the DIBR/Mono one.
            auto *parsedRelease = new ModRelease{id,
                                                 dibr ? "DIBR/Mono - "_L1 + releaseName : releaseName,
                                                 dibr ? published.addMSecs(-1) : published,
                                                 false,
                                                 prerelease,
                                                 downloaded,
                                                 assets,
                                                 this};
            parsed.push_back(parsedRelease);
            if (!dibr && !prerelease && (!newest || published > newest->timestamp()))
                newest = parsedRelease;
        }
    }

    if (parsed.isEmpty())
    {
        qCWarning(UEVRJoeyhodgeLog) << "No UEVR (joeyhodge) releases with a uevr.zip found";
        return;
    }

    beginResetModel();
    for (const auto release : std::as_const(m_releases))
        release->deleteLater();
    m_releases = parsed;
    endResetModel();

    if (currentId != 0 && !releaseFromId(currentId))
        qCWarning(UEVRJoeyhodgeLog) << "Saved UEVR (joeyhodge) release" << currentId << "is no longer available";
    setCurrentRelease(releaseFromId(currentId) ? currentId : (newest ? newest : m_releases.constFirst())->id());
}
