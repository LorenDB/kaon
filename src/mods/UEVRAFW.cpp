#include "UEVRAFW.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

#include "Aptabase.h"
#include "Archive.h"
#include "Dotnet.h"
#include "DownloadManager.h"
#include "Wine.h"

Q_LOGGING_CATEGORY(UEVRAFWLog, "uevr.afw")

UEVRAFW::UEVRAFW(QObject *parent)
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

UEVRAFW *UEVRAFW::instance()
{
    static auto u = new UEVRAFW;
    return u;
}

UEVRAFW *UEVRAFW::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString UEVRAFW::info() const
{
    return "DX12 only. Enable DLSS, then select AFW in the in-game menu."_L1;
}

const QLoggingCategory &UEVRAFW::logger() const
{
    return UEVRAFWLog();
}

QList<Mod *> UEVRAFW::dependencies() const
{
    return {Dotnet::instance()};
}

bool UEVRAFW::isInstalledForGame(const Game *) const
{
    return currentRelease() && currentRelease()->downloaded();
}

void UEVRAFW::downloadRelease(ModRelease *release)
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
            qCWarning(UEVRAFWLog) << "Download UEVR AFW failed:" << errorMessage;
        });
}

void UEVRAFW::deleteRelease(ModRelease *release)
{
    if (!release || !release->downloaded())
        return;

    QDir installDir{path(Paths::BasePath) + '/' + QString::number(release->id())};
    if (installDir.removeRecursively())
        release->setDownloaded(false);
}

void UEVRAFW::launchModImpl(Game *game)
{
    Wine::instance()->runInWine(currentRelease()->name(), game, path(Paths::CurrentInjector), true);
}

QMap<int, Game::LaunchOption> UEVRAFW::acceptableInstallCandidates(const Game *game) const
{
    auto options = Mod::acceptableInstallCandidates(game);
    options.removeIf(
        [](const std::pair<int, Game::LaunchOption> &exe) { return exe.second.platform != Game::Platform::Windows; });
    return options;
}

QString UEVRAFW::path(const Paths p) const
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

void UEVRAFW::refreshReleases()
{
    QNetworkRequest req{{"https://api.github.com/repos/PureDark/UEVR/releases?per_page=100"_L1}};
    req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

    DownloadManager::instance()->refreshCached(
        req,
        "UEVR AFW release information"_L1,
        path(Paths::CachedReleasesJSON),
        [this](bool changed) {
            if (changed || m_releases.isEmpty())
                parseReleaseInfoJson();
        },
        [](const QString &errorMessage) { qCInfo(UEVRAFWLog) << "Error while fetching releases:" << errorMessage; });
}

void UEVRAFW::parseReleaseInfoJson()
{
    QFile cachedReleases{path(Paths::CachedReleasesJSON)};
    if (!cachedReleases.open(QFile::ReadOnly))
        return;

    const auto releases = QJsonDocument::fromJson(cachedReleases.readAll());
    if (!releases.isArray())
    {
        qCWarning(UEVRAFWLog) << "Cached UEVR AFW release information is invalid";
        return;
    }

    const int currentId = currentRelease() ? currentRelease()->id() : m_lastCurrentReleaseId;

    // Each GitHub release ships two injector builds. Neither is a Kaon nightly: "nightly" here means the
    // build is based on praydog's UEVR nightly, and "joeyhodge" is the UE 5.5-5.8 fork. They are listed as
    // separate versions so a profile can ask for one of them. Asset ids are unique, unlike the shared release id.
    QList<ModRelease *> parsed;
    auto appendVariant = [this, &parsed](const QJsonValue &release, const QString &prefix, const QString &variant) {
        auto releaseName = release["name"_L1].toString();
        if (releaseName.isEmpty())
            releaseName = release["tag_name"_L1].toString();
        const auto published = QDateTime::fromString(release["published_at"_L1].toString(), Qt::ISODate);
        // Identical timestamps sort unstably. Keep the nightly-based build above the joeyhodge build.
        const auto timestamp = variant == "nightly"_L1 ? published : published.addMSecs(-1);

        for (const auto &asset : release["assets"_L1].toArray())
        {
            const auto assetName = asset["name"_L1].toString();
            if (!assetName.startsWith(prefix, Qt::CaseInsensitive) || !assetName.endsWith(".zip"_L1, Qt::CaseInsensitive))
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

            parsed.push_back(new ModRelease{
                id, releaseName + " ("_L1 + variant + ')', timestamp, false, false, downloaded, assets, this});
        }
    };

    for (const auto &release : releases.array())
    {
        appendVariant(release, "UEVR-nightly_AFW_"_L1, "nightly"_L1);
        appendVariant(release, "UEVR-joeyhodge_AFW_"_L1, "joeyhodge"_L1);
    }

    if (parsed.isEmpty())
    {
        qCWarning(UEVRAFWLog) << "No UEVR AFW releases found";
        return;
    }

    beginResetModel();
    for (const auto release : std::as_const(m_releases))
        release->deleteLater();
    m_releases = parsed;
    endResetModel();

    if (currentId != 0 && !releaseFromId(currentId))
        qCWarning(UEVRAFWLog) << "Saved UEVR AFW release" << currentId << "is no longer available";
    setCurrentRelease(releaseFromId(currentId) ? currentId : m_releases.constFirst()->id());
}
