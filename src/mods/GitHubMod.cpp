#include "GitHubMod.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>

#include "Aptabase.h"
#include "Archive.h"
#include "DownloadManager.h"

void GitHubMod::downloadRelease(ModRelease *release)
{
    Aptabase::instance()->track("download-"_L1 + settingsGroup(), {{"version"_L1, release->name()}});

    if (release->assets().isEmpty())
        return;

    const auto releaseId = release->id();
    for (const auto asset : release->assets())
    {
        if (asset.url.isEmpty())
            continue;

        DownloadManager::instance()->download(
            QNetworkRequest{asset.url},
            releaseTitle(release),
            true,
            [this, releaseId, asset](const QByteArray &data) {
                // A release refresh may have replaced the ModRelease this download started with.
                const auto release = releaseFromId(releaseId);
                if (!release)
                    return;

                if (QString why; !asset.matches(data, &why))
                {
                    failDownload(why);
                    return;
                }

                // Written in one step: a file that exists is taken to be a finished download
                QSaveFile file{pathForRelease(release, asset)};
                if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
                {
                    failDownload("Kaon couldn't save %1 to %2: %3"_L1.arg(asset.name, file.fileName(), file.errorString()));
                    return;
                }

                // A release can be several files (BepInEx has one per platform). It isn't downloaded until all of them
                // are, or an install waiting on it starts without the file it needs.
                const auto assets = release->assets();
                if (std::all_of(assets.cbegin(), assets.cend(), [this, release](const auto &a) {
                        return QFileInfo::exists(pathForRelease(release, a));
                    }))
                    release->setDownloaded(true);
            },
            [this](const QNetworkReply::NetworkError error, const QString &errorMessage) {
                qCWarning(logger()).noquote() << "Download" << displayName() << "failed:" << errorMessage;
            });
    }
}

void GitHubMod::deleteRelease(ModRelease *release)
{
    if (!release->downloaded())
        return;

    for (const auto &asset : release->assets())
        QFile{pathForRelease(release, asset)}.remove();
    release->setDownloaded(false);
}

QString GitHubMod::path(const Paths p) const
{
    switch (p)
    {
    case Paths::ReleaseBasePath:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + '/' + settingsGroup();
    case Paths::CachedReleasesJSON:
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
               "/%1_releases.json"_L1.arg(releaseListName());
    default:
        return {};
    }
}

QString GitHubMod::pathForRelease(ModRelease *release, const ModRelease::Asset &asset) const
{
    return path(Paths::ReleaseBasePath) +
           "/%1_%2_%3.zip"_L1.arg(settingsGroup(), QString::number(release->id()), QString::number(asset.id));
}

QByteArray GitHubMod::readShippedConfig(const Game *game, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QByteArray{};
    };
    const auto memberSuffix = shippedConfigMember();
    if (!game || memberSuffix.isEmpty())
        return fail("Kaon can't read the original settings for this mod."_L1);

    const auto release = releaseInstalledForGame(game);
    if (!release)
        return fail("Kaon doesn't know which version of %1 is installed."_L1.arg(displayName()));

    bool want64 = false;
    bool want32 = false;
    bool wantWindows = false;
    bool wantLinux = false;
    const auto candidates = acceptableInstallCandidates(game);
    for (auto it = candidates.cbegin(); it != candidates.cend(); ++it)
    {
        if (it->arch == Game::Architecture::x64)
            want64 = true;
        if (it->arch == Game::Architecture::x86)
            want32 = true;
        if (it->platform == Game::Platform::Windows)
            wantWindows = true;
        if (it->platform == Game::Platform::Linux)
            wantLinux = true;
    }

    struct Hit
    {
        ModRelease::Asset asset;
        QString member;
        int score = 0;
    };
    QList<Hit> hits;
    auto sawZip = false;
    for (const auto &asset : release->assets())
    {
        const auto archive = pathForRelease(release, asset);
        if (!QFileInfo::exists(archive))
            continue;
        sawZip = true;
        QString listError;
        QStringList names;
        if (!Archive::list(archive, &names, &listError))
            continue;
        QString member;
        for (const auto &name : names)
        {
            const auto normalized = QString{name}.replace('\\'_L1, '/'_L1);
            if (normalized == memberSuffix || normalized.endsWith('/'_L1 + memberSuffix))
            {
                member = name;
                break;
            }
        }
        if (member.isEmpty())
            continue;

        const auto win =
            asset.name.contains("win"_L1, Qt::CaseInsensitive) || asset.name.startsWith("BepInEx_x"_L1, Qt::CaseInsensitive);
        const auto linuxName =
            asset.name.contains("linux"_L1, Qt::CaseInsensitive) || asset.name.contains("unix"_L1, Qt::CaseInsensitive);
        auto score = 0;
        if (wantWindows && win)
            score += 4;
        if (wantLinux && linuxName)
            score += 4;
        if (want64 && asset.name.contains("x64"_L1, Qt::CaseInsensitive))
            score += 2;
        if (want32 && asset.name.contains("x86"_L1, Qt::CaseInsensitive) &&
            !asset.name.contains("x64"_L1, Qt::CaseInsensitive))
            score += 2;
        hits << Hit{asset, member, score};
    }
    if (hits.isEmpty())
    {
        return fail(sawZip ? "The downloaded %1 doesn't contain its settings file."_L1.arg(displayName()) :
                             "Download this version of %1 again before resetting its settings."_L1.arg(displayName()));
    }

    const auto best = std::max_element(
        hits.cbegin(), hits.cend(), [](const Hit &left, const Hit &right) { return left.score < right.score; });
    QTemporaryDir dir;
    if (!dir.isValid())
        return fail("Couldn't create a temporary folder for the original settings."_L1);
    QString extractError;
    if (!Archive::extract(pathForRelease(release, best->asset), dir.path(), {best->member}, &extractError))
        return fail(extractError);

    const auto normalized = QString{best->member}.replace('\\'_L1, '/'_L1);
    QFile file{dir.filePath(normalized)};
    if (!file.exists())
        file.setFileName(dir.filePath(best->member));
    if (!file.open(QIODevice::ReadOnly))
        return fail("Couldn't read the original settings in the download."_L1);
    return file.readAll();
}

ModRelease::Asset GitHubMod::chooseAssetToInstall(const Game *game, const Game::LaunchOption &exe) const
{
    return currentRelease()->assets().constFirst();
}

GitHubMod::GitHubMod(QObject *parent)
    : Mod{parent}
{
    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        // This gets executed here so we can actually call the virtual settingsGroup(). I love C++! /s
        QDir{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)}.mkdir(settingsGroup());

        // What was saved last time is there at once; the answer from GitHub replaces it when it differs
        parseReleaseInfoJson();
        refreshReleases();
    });
}

void GitHubMod::refreshReleases()
{
    QNetworkRequest req{githubUrl()};
    req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

    DownloadManager::instance()->refreshCached(
        req,
        "%1 release information"_L1.arg(displayName()),
        path(Paths::CachedReleasesJSON),
        [this](bool changed) {
            if (changed || m_releases.isEmpty())
                parseReleaseInfoJson();
        },
        [this](const QString &errorMessage) { qCInfo(logger()) << "Error while fetching releases:" << errorMessage; });
}

void GitHubMod::parseReleaseInfoJson()
{
    QFile cachedReleases{path(Paths::CachedReleasesJSON)};
    if (!cachedReleases.open(QFile::ReadOnly))
        return;

    const auto releases = QJsonDocument::fromJson(cachedReleases.readAll());
    const int currentId = currentRelease() ? currentRelease()->id() : m_lastCurrentReleaseId;

    QList<ModRelease *> parsed;
    for (const auto &release : releases.array())
    {
        if (release["draft"_L1].toBool())
            continue;
        const bool prerelease = release["prerelease"_L1].toBool();
        if (prerelease && !offersPrereleases())
            continue;

        auto name = release["name"_L1].toString();
        if (name.isEmpty())
            name = release["tag_name"_L1].toString();
        const auto id = release["id"_L1].toInt();
        const auto timestamp = QDateTime::fromString(release["published_at"_L1].toString(), Qt::ISODate);

        QList<ModRelease::Asset> assets;
        for (const auto &asset : release["assets"_L1].toArray())
        {
            if (isThisFileTheActualModDownload(asset["name"_L1].toString()))
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

        // Skip releases with nothing Kaon can install (e.g. source-only tags). An empty release must never become
        // the current release: chooseAssetToInstall() would dereference an empty asset list.
        if (assets.isEmpty())
            continue;

        auto m = new ModRelease{id, name, timestamp, false, prerelease, false, assets, this};
        m->setDownloaded(std::all_of(m->assets().cbegin(), m->assets().cend(), [this, m](const auto &a) {
            return QFileInfo::exists(pathForRelease(m, a));
        }));
        parsed.push_back(m);
    }

    // Keep the old list when there is nothing to replace it with, so the current release never points at a deleted one
    if (parsed.isEmpty())
        return;

    beginResetModel();
    for (const auto release : std::as_const(m_releases))
        release->deleteLater();
    m_releases = parsed;
    endResetModel();

    // The release that was current stays current, unless it's gone or the version menu now hides it
    const auto kept = releaseFromId(currentId);
    setCurrentRelease(kept && (!kept->prerelease() || showsPrereleases()) ? currentId : newestOfferedRelease()->id());
}

bool GitHubMod::showsPrereleases() const
{
    // The version menu's switch, which ModReleaseFilter saves
    QSettings settings;
    settings.beginGroup(settingsGroup());
    return settings.value("showPrereleases"_L1, false).toBool();
}

ModRelease *GitHubMod::newestOfferedRelease() const
{
    const bool prereleases = showsPrereleases();
    for (const auto release : m_releases)
        if (prereleases || !release->prerelease())
            return release;
    return m_releases.constFirst();
}

GitHubZipExtractorMod::GitHubZipExtractorMod(QObject *parent)
    : GitHubMod{parent}
{}

QString GitHubZipExtractorMod::modInstallDirForGame(const Game *game, const Game::LaunchOption &executable) const
{
    const auto retval = executable.executable.sliced(0, executable.executable.lastIndexOf('/'));
    if (retval.isEmpty() || !QFileInfo::exists(executable.executable))
        return game->installDir();
    return retval;
}

void GitHubZipExtractorMod::uninstallMod(Game *game)
{
    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    const auto toRemove = settings.value("installedFiles"_L1).toStringList();

    QStringList dirs;
    const auto gameDir = QDir::cleanPath(game->installDir());

    for (const auto &file : toRemove)
    {
        if (file.isEmpty())
            continue;
        if (file.endsWith('/'))
            dirs << file;
        else if (QFile{file}.remove() && !gameDir.isEmpty())
        {
            // Most zips don't list their folders. The ones this mod's files were in go too, once they are empty and
            // as long as they are inside the game.
            for (auto dir = QFileInfo{file}.absolutePath(); dir.startsWith(gameDir + '/') && !dirs.contains(dir);
                 dir = QFileInfo{dir}.absolutePath())
                dirs << dir;
        }
    }

    std::sort(dirs.begin(), dirs.end(), [](const auto &l, const auto &r) { return l.count('/') > r.count('/'); });
    for (const auto &dir : std::as_const(dirs))
        if (QDir d{dir}; d.exists() && d.isEmpty())
            d.removeRecursively();

    settings.remove("installedFiles"_L1);
    Mod::uninstallMod(game);
}

bool GitHubZipExtractorMod::unpackInto(Game *game, const Game::LaunchOption &exe)
{
    const auto release = currentRelease();
    if (!release || release->assets().isEmpty())
    {
        fail("%1 has no version to install yet."_L1.arg(displayName()));
        return false;
    }
    const auto asset = chooseAssetToInstall(game, exe);
    if (asset.id == -1)
    {
        fail("This version of %1 has no download that fits %2."_L1.arg(displayName(), game->name()));
        return false;
    }
    const auto archive = pathForRelease(release, asset);
    if (!QFileInfo::exists(archive))
    {
        fail("Download %1 before installing it."_L1.arg(releaseTitle(release)));
        return false;
    }

    const auto installDir = modInstallDirForGame(game, exe);
    QString error;
    QStringList names;
    if (!Archive::list(archive, &names, &error) || !Archive::extract(archive, installDir, &error))
    {
        fail(error);
        return false;
    }

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    // Files an older version put there are still this mod's to remove
    const auto before = settings.value("installedFiles"_L1).toStringList();
    QSet<QString> installed{before.cbegin(), before.cend()};
    for (const auto &name : std::as_const(names))
    {
        // Uninstalling deletes what is listed here. Nothing a zip says may point outside the folder it went into.
        if (name.startsWith('/'_L1) || name.split('/'_L1).contains(".."_L1))
        {
            qCWarning(logger()) << "Ignoring" << name << "in" << archive;
            continue;
        }
        installed.insert(installDir + '/' + name);
    }
    installed.remove(QString{});
    settings.setValue("installedFiles"_L1, QStringList{installed.cbegin(), installed.cend()});
    return true;
}

void GitHubZipExtractorMod::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (unpackInto(game, exe))
        Mod::installModImpl(game, exe);
}
