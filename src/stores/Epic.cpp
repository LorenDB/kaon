#include "Epic.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QSysInfo>

#include "DownloadManager.h"

Q_LOGGING_CATEGORY(EpicLog, "epic")

namespace
{
    // The launcher keeps its config under the XDG data directory, e.g.
    // ~/.local/share/com.epicgames.eglv2. The AppImage build does not use a sandbox,
    // so there is a single location to look in.
    QString defaultDataDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/com.epicgames.eglv2"_L1;
    }

    // A GE-Proton build unpacks as <something>/<GE-Proton...>/files/bin/wine
    // (and dist/bin/wine for older layouts). The compatdata prefixes hold thousands of
    // files, so they are never descended into while looking for it.
    QString findProtonWine(const QString &dataDir)
    {
        if (dataDir.isEmpty())
            return {};

        QStringList roots;
        const auto compatDir = dataDir + "/compat"_L1;
        if (QFileInfo{compatDir}.isDir())
        {
            for (const auto &entry : QDir{compatDir}.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            {
                // Prefixes live here; everything else may hold the runtime.
                if (entry.fileName().compare("compatdata"_L1, Qt::CaseInsensitive) == 0)
                    continue;
                roots << entry.absoluteFilePath();
            }
        }
        // Fall back to other likely runtime locations if compat holds no runtime yet.
        for (const auto &extra : {"proton"_L1, "runtimes"_L1, "runtime"_L1, "tools"_L1})
        {
            const auto dir = dataDir + '/'_L1 + extra;
            if (QFileInfo{dir}.isDir() && !roots.contains(dir))
                roots << dir;
        }
        if (roots.isEmpty())
            return {};

        const bool arm64 = QSysInfo::currentCpuArchitecture() == "arm64"_L1;
        QString fallback;
        for (const auto &root : std::as_const(roots))
        {
            QDirIterator it{root,
                            {"wine"_L1},
                            QDir::Files | QDir::Executable,
                            QDirIterator::Subdirectories | QDirIterator::FollowSymlinks};
            while (it.hasNext())
            {
                it.next();
                const auto path = it.filePath();
                const auto lower = path.toLower();
                const bool binWine = lower.endsWith("/files/bin/wine"_L1);
                const bool binArm64 = lower.endsWith("/files/bin-arm64/wine"_L1);
                const bool distWine = lower.endsWith("/dist/bin/wine"_L1);
                if (!binWine && !binArm64 && !distWine)
                {
                    if (fallback.isEmpty())
                        fallback = path;
                    continue;
                }
                if (arm64 && binArm64)
                    return path;
                if (!arm64 && binWine)
                    return path;
                if (fallback.isEmpty())
                    fallback = path;
            }
        }
        // Prefer files/bin/wine over dist/bin/wine when both were seen.
        return fallback;
    }

    struct CatalogEntry
    {
        QString title;
        QString cardImage;
        QString heroImage;
    };

    QHash<QString, CatalogEntry> readCatalog(const QString &dataDir)
    {
        QHash<QString, CatalogEntry> catalog;
        QFile file{dataDir + "/cache/prod/catalog-cache.json"_L1};
        if (!file.open(QIODevice::ReadOnly))
        {
            qCDebug(EpicLog) << "No catalog cache at" << file.fileName();
            return catalog;
        }
        const auto doc = QJsonDocument::fromJson(file.readAll());
        const auto items = doc["items"_L1].toObject();
        for (auto it = items.constBegin(); it != items.constEnd(); ++it)
        {
            const auto item = it.value()["item"_L1].toObject();
            if (item.isEmpty())
                continue;
            const auto id = item["id"_L1].toString();
            if (id.isEmpty())
                continue;
            CatalogEntry entry;
            entry.title = item["title"_L1].toString();
            for (const auto &image : item["keyImages"_L1].toArray())
            {
                const auto type = image["type"_L1].toString();
                const auto url = image["url"_L1].toString();
                if (url.isEmpty())
                    continue;
                if (type == "DieselGameBox"_L1 && entry.heroImage.isEmpty())
                    entry.heroImage = url;
                else if (type == "DieselGameBoxTall"_L1 && entry.cardImage.isEmpty())
                    entry.cardImage = url;
            }
            catalog.insert(id, entry);
        }
        qCDebug(EpicLog) << "Catalog cache lists" << catalog.size() << "items";
        return catalog;
    }
} // namespace

class EpicGame : public Game
{
    Q_OBJECT

public:
    EpicGame(const QJsonObject &install,
             const CatalogEntry &catalog,
             const QString &dataDir,
             const QString &protonWine,
             QObject *parent = nullptr)
        : Game{parent}
    {
        const auto itemId = install["itemId"_L1].toString();
        const auto installationId = install["installationId"_L1].toString();
        const auto artifactId = install["artifactId"_L1].toString();
        m_id = !itemId.isEmpty() ? itemId : installationId;
        qCDebug(EpicLog) << "Creating game:" << m_id;

        m_installDir = install["dir"_L1].toString();
        m_type = AppType::Game;

        if (!catalog.title.isEmpty())
            m_name = catalog.title;
        if (m_name.isEmpty())
            m_name = QDir{m_installDir}.dirName();

        if (!catalog.cardImage.isEmpty())
        {
            m_cardImage = "image://epic-image/"_L1 + catalog.cardImage;
            m_icon = m_cardImage;
        }
        if (!catalog.heroImage.isEmpty())
            m_heroImage = "image://epic-image/"_L1 + catalog.heroImage;

        LaunchOption lo;
        const auto platform = install["platform"_L1].toString();
        if (platform.compare("Mac"_L1, Qt::CaseInsensitive) == 0)
            lo.platform = Platform::MacOS;
        else if (platform.compare("Linux"_L1, Qt::CaseInsensitive) == 0)
            lo.platform = Platform::Linux;
        else
            lo.platform = Platform::Windows;
        auto launchExe = install["launchExecutable"_L1].toString();
        if (launchExe.isEmpty())
        {
            // Some manifests leave the executable blank; fall back to the first .exe at the top level.
            const auto exes = QDir{m_installDir}.entryList({"*.exe"_L1}, QDir::Files, QDir::Name);
            if (!exes.isEmpty())
            {
                launchExe = exes.constFirst();
                qCDebug(EpicLog) << "No launch executable for" << m_name << "- using" << launchExe;
            }
        }
        if (const auto launchCmd = install["launchCommand"_L1].toString(); !launchCmd.isEmpty())
            qCDebug(EpicLog) << "Ignoring launch command for" << m_name << ":" << launchCmd;
        lo.executable = resolveWindowsPath(m_installDir, launchExe);
        if (!lo.executable.isEmpty() && QFileInfo::exists(lo.executable))
            m_executables[m_executables.size()] = lo;

        // The prefix path is known before the game has ever run; hasValidWine() stays
        // false until the launcher builds it, and the status page says to launch once.
        if (!artifactId.isEmpty())
            m_winePrefix = dataDir + "/compat/compatdata/"_L1 + artifactId + "/pfx"_L1;
        m_wineBinary = protonWine;
        if (!m_wineBinary.isEmpty())
            qCDebug(EpicLog) << "Using Proton Wine" << m_wineBinary << "for" << m_name;

        detectGameEngine();
        detectArchitectures();
        detectAnticheat();

        m_valid = !m_executables.isEmpty();
    }

    Store store() const override { return Store::Epic; }
    void launch() const override {}
};

QVariantList Epic::libraries() const
{
    if (m_installs.isEmpty())
        return {QVariantMap{{"store"_L1, "Epic"_L1}, {"path"_L1, QString{}}, {"count"_L1, 0}}};

    QVariantList rows;
    for (const auto &install : std::as_const(m_installs))
        rows << QVariantMap{{"store"_L1, install.name}, {"path"_L1, install.path}, {"count"_L1, install.count}};
    return rows;
}

void Epic::discover(bool report)
{
    QList<Install> found;
    const auto path = defaultDataDir();
    if (const QFileInfo info{path}; info.exists() && info.isDir())
    {
        Install install;
        install.path = info.canonicalFilePath();
        install.name = "Epic"_L1;
        found << install;
    }

    auto changed = found.size() != m_installs.size();
    for (int i = 0; !changed && i < found.size(); ++i)
        changed = found.at(i).path != m_installs.at(i).path;
    if (changed)
        m_installs = found;
    if (!m_installs.isEmpty())
        m_epicRoot = m_installs.constFirst().path;
    else
        m_epicRoot.clear();

    if (!report && !changed)
        return;
    if (m_installs.isEmpty())
        qCInfo(EpicLog) << "Epic not found";
    else
        for (const auto &install : std::as_const(m_installs))
            qCInfo(EpicLog) << "Found" << install.name << "at" << install.path;
}

Epic::Epic(QObject *parent)
    : Store{parent}
{
    discover(true);
    countAsLauncher();
}

Epic *Epic::instance()
{
    static auto e = new Epic;
    return e;
}

Epic *Epic::create(QQmlEngine *qml, QJSEngine *js)
{
    return instance();
}

void Epic::prepareScan()
{
    discover(false);
}

bool Epic::readLibrary(QList<Game *> &games)
{
    qCDebug(EpicLog) << "Scanning Epic library";

    const auto dataDir = m_epicRoot;
    if (dataDir.isEmpty())
        return true;

    const auto catalog = readCatalog(dataDir);
    const auto protonWine = findProtonWine(dataDir);
    if (protonWine.isEmpty())
        qCDebug(EpicLog) << "No Proton runtime found under" << dataDir;
    else
        qCInfo(EpicLog) << "Found Proton Wine at" << protonWine;

    // One line per install, plus a header line without an installationId. The same
    // installationId can appear twice (an incomplete entry, then the finished one),
    // so the last line for each id wins.
    QHash<QString, QJsonObject> installs;
    QStringList order;
    if (QFile file{dataDir + "/installations.jsonl"_L1}; file.open(QIODevice::ReadOnly))
    {
        while (!file.atEnd())
        {
            const auto line = file.readLine().trimmed();
            if (line.isEmpty())
                continue;
            const auto obj = QJsonDocument::fromJson(line).object();
            const auto id = obj["installationId"_L1].toString();
            if (id.isEmpty())
                continue;
            if (!installs.contains(id))
                order << id;
            installs.insert(id, obj);
        }
    }
    else
    {
        qCDebug(EpicLog) << "No installations file at" << dataDir + "/installations.jsonl"_L1;
    }

    auto count = 0;
    for (const auto &id : std::as_const(order))
    {
        const auto install = installs.value(id);
        // An interrupted download leaves complete=false behind until it resumes.
        if (install["complete"_L1].toBool(true) == false)
            continue;
        const auto dir = install["dir"_L1].toString();
        if (dir.isEmpty() || !QFileInfo{dir}.isDir())
            continue;

        const auto itemId = install["itemId"_L1].toString();
        if (auto *game = new EpicGame{install, catalog.value(itemId), dataDir, protonWine, nullptr}; game->isValid())
        {
            games.push_back(game);
            ++count;
        }
        else
            delete game;
    }

    Install scanned;
    scanned.path = dataDir;
    scanned.name = "Epic"_L1;
    scanned.count = count;
    m_scannedInstalls = {scanned};
    qCInfo(EpicLog) << "Epic scanned" << count << "installed games";
    return true;
}

void Epic::finishScan()
{
    m_installs = m_scannedInstalls;
    emit librariesChanged();
}

class EpicImageFetcher : public QQuickImageResponse
{
    Q_OBJECT

public:
    EpicImageFetcher(const QString &url, const QSize &)
    {
        QDir cache{QStandardPaths::writableLocation(QStandardPaths::CacheLocation)};
        cache.mkdir("epic_cache"_L1);
        cache.cd("epic_cache"_L1);

        auto file = new QFile{"%1/%2"_L1.arg(cache.path(), url.split('/').last())};
        connect(this, &EpicImageFetcher::finished, file, &QFile::deleteLater);
        if (file->exists() && file->fileTime(QFileDevice::FileModificationTime).daysTo(QDateTime::currentDateTime()) < 30)
        {
            if (file->open(QIODevice::ReadOnly))
                m_image = QImage::fromData(file->readAll());
            else
                m_error = "Could not open cached file";
            emit finished();
        }
        else
        {
            DownloadManager::instance()->download(
                QNetworkRequest{url},
                "Epic image",
                false,
                [this, file](const QByteArray &data) {
                    m_image = QImage::fromData(data);
                    if (file->open(QIODevice::WriteOnly))
                    {
                        file->write(data);
                        file->close();
                    }
                    else
                        qCDebug(EpicLog) << "Could not cache image:" << file->fileName();
                },
                [this, file](const QNetworkReply::NetworkError, const QString &) {
                    // fall back to cache if possible
                    if (file->exists() && file->open(QIODevice::ReadOnly))
                        m_image = QImage::fromData(file->readAll());
                    else
                        m_error = "Could not download or find in cache";
                },
                [this] { emit finished(); });
        }
    }

    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }

private:
    QImage m_image;
    QString m_error;
};

QQuickImageResponse *EpicImageCache::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    QQuickImageResponse *response = new EpicImageFetcher(id, requestedSize);
    return response;
}

#include "Epic.moc"
