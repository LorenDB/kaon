#include "UEVR.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSemaphore>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include "DownloadManager.h"

using namespace Qt::Literals;

UEVR *UEVR::s_instance = nullptr;

UEVR::UEVR(QObject *parent)
    : QAbstractListModel{parent}
{
    if (s_instance != nullptr)
        throw std::runtime_error{"Attempted to double initialize UEVR"};
    else
        s_instance = this;

    QSettings settings;
    settings.beginGroup("uevr"_L1);

    m_currentUevr = settings.value("currentUevr"_L1, {}).toInt();
    m_showNightlies = settings.value("showNightlies"_L1, false).toBool();

    connect(this, &UEVR::showNightliesChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });

    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, [this] {
        parseReleaseInfoJson();
        updateAvailableReleases();
    });
}

UEVR::~UEVR()
{
    QSettings settings;
    settings.beginGroup("uevr"_L1);
    settings.setValue("currentUevr"_L1, m_currentUevr);
    settings.setValue("showNightlies"_L1, m_showNightlies);
}

int UEVR::rowCount(const QModelIndex &parent) const
{
    const auto &source = m_showNightlies ? m_mergedReleases : m_releases;
    return source.count();
}

QVariant UEVR::data(const QModelIndex &index, int role) const
{
    const auto &source = m_showNightlies ? m_mergedReleases : m_releases;
    if (!index.isValid() || index.row() < 0 || index.row() >= source.size())
        return {};
    const auto &item = source.at(index.row());
    switch (role)
    {
    case Roles::Id:
        return item.id;
    case Qt::DisplayRole:
    case Roles::Name:
        return item.name;
    case Roles::Timestamp:
        return item.timestamp;
    case Roles::Installed:
        return item.installed;
    }

    return {};
}

QHash<int, QByteArray> UEVR::roleNames() const
{
    return {{Roles::Id, "id"_ba},
        {Roles::Name, "name"_ba},
        {Roles::Timestamp, "timestamp"_ba},
        {Roles::Installed, "installed"_ba}};
}

const QString UEVR::uevrPath() const
{
    return path(Paths::CurrentUEVR);
}

int UEVR::currentUevr() const
{
    return m_currentUevr;
}

void UEVR::setCurrentUevr(const int id)
{
    auto allVersions = m_releases + m_nightlies;
    auto newVersion =
            std::find_if(allVersions.constBegin(), allVersions.constEnd(), [id](const auto &r) { return r.id == id; });
    if (newVersion == allVersions.constEnd())
    {
        qDebug() << "Attempted to activate nonexistent UEVR";
        return;
    }

    if (!newVersion->installed)
    {
        // TODO: ask if the user wants to install this
    }

    m_currentUevr = id;
    emit currentUevrChanged(id);

    QSettings settings;
    settings.beginGroup("uevr"_L1);
    settings.setValue("currentUevr"_L1, id);
}

void UEVR::launchUEVR(const int steamId)
{
    auto injector = new QProcess;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("WINEPREFIX"_L1,
               QDir::homePath() + "/.local/share/Steam/steamapps/compatdata/" + QString::number(steamId) + "/pfx"_L1);
    env.insert("WINEFSYNC"_L1, "1"_L1);
    injector->setProcessEnvironment(env);
    injector->start(QDir::homePath() + "/.local/share/Steam/steamapps/common/Proton - Experimental/files/bin/wine"_L1,
                    {path(Paths::CurrentUEVRInjector)});
    connect(injector, &QProcess::finished, injector, &QObject::deleteLater);
}

bool UEVR::isInstalled(const int id)
{
    return QFileInfo::exists(path(Paths::UEVRBasePath) + '/' + QString::number(id) + "/UEVRInjector.exe"_L1);
}

void UEVR::downloadUEVR(const int id)
{
    for (const auto &release : m_mergedReleases)
    {
        if (release.id == id && !release.installed)
        {
            downloadRelease(release);
            return;
        }
    }
}

int UEVR::indexFromId(const int id) const
{
    const auto &list = m_showNightlies ? m_mergedReleases : m_releases;
    for (int i = 0; i < list.count(); ++i)
    {
        if (list[i].id == id)
            return i;
    }
    return -1;
}

QString UEVR::path(const Paths path) const
{
    switch (path)
    {
    case Paths::CurrentUEVR:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/uevr/"_L1 +
                QString::number(m_currentUevr);
    case Paths::CurrentUEVRInjector:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/uevr/"_L1 +
                QString::number(m_currentUevr) + "/UEVRInjector.exe"_L1;
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
    static QNetworkAccessManager manager;
    manager.setAutoDeleteReplies(true);

    auto semaphore = new QSemaphore{2};
    semaphore->acquire(2);

    const auto impl = [this, semaphore](QUrl url, const QString cachePath) {
        QNetworkRequest req{url};
        req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

        DownloadManager::instance()->download(
                    req,
                    "UEVR release information"_L1,
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
            qDebug() << "Error while fetching releases:" << errorMessage;
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

    m_releases.clear();
    m_nightlies.clear();

    const auto parse = [this](const QJsonValue &release) -> UEVRRelease {
        UEVRRelease r;
        r.name = release["name"_L1].toString();
        r.id = release["id"_L1].toInt();
        r.timestamp = QDateTime::fromString(release["published_at"_L1].toString(), Qt::ISODate);
        r.installed = QFileInfo::exists(path(Paths::UEVRBasePath) + '/' + QString::number(r.id) + "/UEVRInjector.exe"_L1);
        for (const auto &asset : release["assets"_L1].toArray())
        {
            UEVRRelease::Asset a;
            a.id = asset["id"_L1].toInt();
            a.name = asset["name"_L1].toString();
            a.url = asset["browser_download_url"_L1].toString();
            r.assets.push_back(a);
        }
        return r;
    };

    for (const auto &release : releases.array())
        m_releases.push_back(parse(release));
    for (const auto &nightly : nightlies.array())
        m_nightlies.push_back(parse(nightly));

    m_mergedReleases.clear();
    m_mergedReleases.append(m_releases);
    m_mergedReleases.append(m_nightlies);
    std::sort(m_mergedReleases.begin(), m_mergedReleases.end(), [](const auto &a, const auto &b) {
        return a.timestamp < b.timestamp;
    });

    endResetModel();
    emit currentUevrChanged(m_currentUevr);
}

void UEVR::downloadRelease(const UEVRRelease &release)
{
    QUrl url;
    for (const auto &asset : release.assets)
    {
        if (asset.name.toLower() == "uevr.zip"_L1)
        {
            url = asset.url;
            break;
        }
    }
    if (url.isEmpty())
        return;

    auto tempDir = new QTemporaryDir;
    if (!tempDir->isValid())
    {
        qDebug() << "Failed to create temporary directory";
        return;
    }

    const QString zipPath = tempDir->path() + "/uevr_" + QString::number(release.id) + ".zip";

    DownloadManager::instance()->download(
                QNetworkRequest{url},
                release.name,
                [this, zipPath, id = release.id](const QByteArray &data) {
        QFile file(zipPath);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(data);
            file.close();

            QString targetDir = path(Paths::UEVRBasePath) + '/' + QString::number(id);
            if (!QFileInfo::exists(targetDir))
                QDir().mkpath(targetDir);

            QProcess process;
            process.setWorkingDirectory(targetDir);
            QStringList args;
            args << "-o" << zipPath;
            process.execute("unzip", args);
            process.start("unzip", args);
            process.waitForFinished();

            if (process.exitCode() != 0)
            {
                qDebug() << "Unzip UEVR failed:" << process.errorString();
                qDebug() << process.readAllStandardError();
            }
            else
            {
                // TODO: mark the existing release object as installed

                // This triggers the UI to update. Hacky but worky :)
                emit currentUevrChanged(m_currentUevr);
            }
        }
        else
            qDebug() << "Failed to save UEVR";
    },
    [](const QNetworkReply::NetworkError error, const QString &errorMessage) {
        qDebug() << "Download UEVR failed:" << errorMessage;
    },
    [tempDir] { delete tempDir; });
}
