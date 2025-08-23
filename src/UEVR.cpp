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
#include "DownloadManager.h"
#include "Wine.h"

using namespace Qt::Literals;

Q_LOGGING_CATEGORY(UEVRLog, "uevr")

UEVR *UEVR::s_instance = nullptr;

UEVRRelease::UEVRRelease(const QJsonValue &json, bool nightly, QObject *parent)
    : QObject{parent},
      m_nightly{nightly}
{
    m_name = json["name"_L1].toString();

    // Shorten the git hash in nightly release names for display purposes
    static const QRegularExpression rx(R"(^UEVR Nightly \d+ \([0-9a-f]{40}\)$)"_L1,
                                       QRegularExpression::CaseInsensitiveOption);

    if (rx.match(m_name).hasMatch())
    {
        // We want to end up with a 7-character git hash. Therefore, after finding the " (", we increment 2 to get to the
        // git hash and then 7 more to get to the end of the short hash.
        int parenStart = m_name.lastIndexOf(" ("_L1) + 9;
        if (parenStart != -1)
            m_name = m_name.left(parenStart) + ')';
    }

    m_id = json["id"_L1].toInt();
    m_timestamp = QDateTime::fromString(json["published_at"_L1].toString(), Qt::ISODate);
    m_installed = QFileInfo::exists(UEVR::instance()->path(UEVR::Paths::UEVRBasePath) + '/' + QString::number(m_id) +
                                    "/UEVRInjector.exe"_L1);
    for (const auto &asset : json["assets"_L1].toArray())
    {
        UEVRRelease::Asset a;
        a.id = asset["id"_L1].toInt();
        a.name = asset["name"_L1].toString();
        a.url = asset["browser_download_url"_L1].toString();
        m_assets.push_back(a);
    }
}

const QList<UEVRRelease::Asset> &UEVRRelease::assets() const
{
    return m_assets;
}

void UEVRRelease::setInstalled(bool state)
{
    m_installed = state;
    emit installedChanged(state);
}

UEVR::UEVR(QObject *parent)
    : QAbstractListModel{parent}
{
    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        QSettings settings;
        settings.beginGroup("uevr"_L1);
        int id = settings.value("currentUevr"_L1, 0).toInt();

        parseReleaseInfoJson();
        updateAvailableReleases();
        if (id != 0)
            setCurrentUevr(id);
    });
}

UEVR::~UEVR()
{
    QSettings settings;
    settings.beginGroup("uevr"_L1);
    settings.setValue("currentUevr"_L1, m_currentUevr->id());
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

int UEVR::rowCount(const QModelIndex &parent) const
{
    return m_releases.count();
}

QVariant UEVR::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_releases.size())
        return {};
    const auto &item = m_releases.at(index.row());
    switch (role)
    {
    case Roles::Id:
        return item->id();
    case Qt::DisplayRole:
    case Roles::Name:
        return item->name();
    case Roles::Timestamp:
        return item->timestamp();
    case Roles::Installed:
        return item->installed();
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

UEVRRelease *UEVR::releaseFromId(const int id) const
{
    for (const auto release : m_releases)
        if (release->id() == id)
            return release;
    return nullptr;
}

const QString UEVR::uevrPath() const
{
    return path(Paths::CurrentUEVR);
}

UEVRRelease *UEVR::currentUevr() const
{
    return m_currentUevr;
}

void UEVR::setCurrentUevr(const int id)
{
    auto newVersion =
            std::find_if(m_releases.constBegin(), m_releases.constEnd(), [id](const auto &r) { return r->id() == id; });
    if (newVersion == m_releases.constEnd())
    {
        qCWarning(UEVRLog) << "Attempted to activate nonexistent UEVR";
        Aptabase::instance()->track("nonexistent-uevr-activation-bug");
        return;
    }

    m_currentUevr = releaseFromId(id);
    emit currentUevrChanged(m_currentUevr);

    QSettings settings;
    settings.beginGroup("uevr"_L1);
    settings.setValue("currentUevr"_L1, id);
}

void UEVR::launchUEVR(const Game *game)
{
    Aptabase::instance()->track("launch-uevr"_L1, {{"version"_L1, m_currentUevr->name()}});
    Wine::instance()->runInWine(m_currentUevr->name(), game, path(Paths::CurrentUEVRInjector));
}

void UEVR::downloadUEVR(UEVRRelease *release)
{
    Aptabase::instance()->track("download-uevr"_L1, {{"version"_L1, release->name()}});

    QUrl url;
    for (const auto &asset : release->assets())
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
        qCWarning(UEVRLog) << "Failed to create temporary directory";
        return;
    }

    const QString zipPath = tempDir->path() + "/uevr_" + QString::number(release->id()) + ".zip";

    DownloadManager::instance()->download(
                QNetworkRequest{url},
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
                release->setInstalled(true);
        }
        else
            qCWarning(UEVRLog) << "Failed to save UEVR";
    },
    [](const QNetworkReply::NetworkError error, const QString &errorMessage) {
        qCWarning(UEVRLog) << "Download UEVR failed:" << errorMessage;
    },
    [tempDir] { delete tempDir; });
}

void UEVR::deleteUEVR(UEVRRelease *uevr)
{
    if (!uevr->installed())
        return;

    QDir installDir{path(Paths::UEVRBasePath) + '/' + QString::number(uevr->id())};
    if (installDir.removeRecursively())
        uevr->setInstalled(false);
}

QString UEVR::path(const Paths path) const
{
    switch (path)
    {
    case Paths::CurrentUEVR:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/uevr/"_L1 +
                QString::number(m_currentUevr->id());
    case Paths::CurrentUEVRInjector:
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/uevr/"_L1 +
                QString::number(m_currentUevr->id()) + "/UEVRInjector.exe"_L1;
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

    const int currentId = m_currentUevr ? m_currentUevr->id() : 0;

    for (const auto release : std::as_const(m_releases))
        release->deleteLater();
    m_releases.clear();

    for (const auto &release : releases.array())
        m_releases.push_back(new UEVRRelease{release, false, this});
    for (const auto &nightly : nightlies.array())
        m_releases.push_back(new UEVRRelease{nightly, true, this});

    endResetModel();

    setCurrentUevr(currentId == 0 ? m_releases.first()->id() : currentId);
}

UEVRFilter::UEVRFilter(QObject *parent)
{
    setSourceModel(UEVR::instance());

    setSortRole(UEVR::Roles::Timestamp);
    setDynamicSortFilter(true);
    sort(0);

    QSettings settings;
    settings.beginGroup("uevr"_L1);
    m_showNightlies = settings.value("showNightlies"_L1, false).toBool();
}

int UEVRFilter::indexFromRelease(UEVRRelease *release) const
{
    if (!release)
        return -1;

    for (int i = 0; i < sourceModel()->rowCount(); ++i)
    {
        auto sourceIndex = sourceModel()->index(i, 0);
        if (sourceModel()->data(sourceIndex, UEVR::Roles::Id).toInt() == release->id())
        {
            QModelIndex proxyIndex = mapFromSource(sourceIndex);
            if (proxyIndex.isValid())
                return proxyIndex.row();
        }
    }

    return -1;
}

void UEVRFilter::setShowNightlies(bool state)
{
    m_showNightlies = state;
    invalidateRowsFilter();

    emit showNightliesChanged(state);

    QSettings settings;
    settings.beginGroup("uevr"_L1);
    settings.setValue("showNightlies"_L1, m_showNightlies);
}

bool UEVRFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (!m_showNightlies)
    {
        const auto release = UEVR::instance()->releaseFromId(
                    sourceModel()->data(sourceModel()->index(row, 0, parent), UEVR::Roles::Id).toInt());
        if (!release || release->nightly())
            return false;
    }
    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}

bool UEVRFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    return left.data(UEVR::Roles::Timestamp).toDateTime() > right.data(UEVR::Roles::Timestamp).toDateTime();
}
