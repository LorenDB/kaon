#include "Bepinex.h"

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

#include "DownloadManager.h"

Q_LOGGING_CATEGORY(BepinexLog, "bepinex")

Bepinex *Bepinex::instance()
{
    static Bepinex b;
    return &b;
}

Bepinex *Bepinex::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

bool Bepinex::checkGameCompatibility(const Game *game) const
{
    // TODO: check Mono/IL2CPP/etc.
    return Mod::checkGameCompatibility(game);
}

bool Bepinex::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    return QFileInfo::exists(game->installDir() + "/run_bepinex.sh"_L1);
}

QString Bepinex::path(const Paths p) const
{
    switch (p)
    {
    case Paths::CurrentRelease:
        return path(Paths::BepinexBasePath) + "/bepinex_" + QString::number(currentRelease()->id()) + ".zip";
    case Paths::BepinexBasePath:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/bepinex"_L1;
    case Paths::CachedReleasesJSON:
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/bepinex_releases.json"_L1;
    default:
        return {};
    }
}

void Bepinex::downloadRelease(ModRelease *release)
{
    if (release->downloadUrl().isEmpty())
        return;

    const QString zipPath = path(Paths::BepinexBasePath) + "/bepinex_" + QString::number(release->id()) + ".zip";

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
                qCWarning(BepinexLog) << "Failed to save BepInEx";
        },
        [](const QNetworkReply::NetworkError error, const QString &errorMessage) {
            qCWarning(BepinexLog) << "Download BepInEx failed:" << errorMessage;
        });
}

void Bepinex::deleteRelease(ModRelease *release)
{
    if (!release->installed())
        return;

    QFile installed{path(Paths::BepinexBasePath) + "/bepinex_" + QString::number(release->id()) + ".zip"};
    if (installed.remove())
        release->setInstalled(false);
}

void Bepinex::installMod(Game *game)
{
    const QString targetDir = game->installDir() + '/';

    QProcess process;
    process.setWorkingDirectory(targetDir);
    QStringList args;
    args << "-o" << path(Paths::CurrentRelease);
    process.start("unzip", args);
    process.waitForFinished();
    if (process.exitCode() != 0)
    {
        qCWarning(BepinexLog) << "Unzip UUVR failed:" << process.errorString();
        qCWarning(BepinexLog) << process.readAllStandardError();
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

void Bepinex::uninstallMod(Game *game)
{
    const QString targetDir = game->installDir() + '/';

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

Bepinex::Bepinex(QObject *parent)
    : Mod{parent}
{
    QDir{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)}.mkdir("bepinex"_L1);

    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        parseReleaseInfoJson();
        updateAvailableReleases();
    });
}

void Bepinex::updateAvailableReleases()
{
    QNetworkRequest req{{"https://api.github.com/repos/BepInEx/BepInEx/releases"_L1}};
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
            qCInfo(BepinexLog) << "Error while fetching releases:" << errorMessage;
            return;
        });
}

void Bepinex::parseReleaseInfoJson()
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
        const auto installed =
                QFileInfo::exists(path(Paths::BepinexBasePath) + "/bepinex_"_L1 + QString::number(id) + ".zip"_L1);

        QUrl downloadUrl;
        for (const auto &asset : release["assets"_L1].toArray())
        {
            // TODO: differ between x86 and x64 binaries!
            if (const auto assetName = asset["name"_L1].toString(); assetName.startsWith("BepInEx_linux"_L1))
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
