#include "Heroic.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#include "DownloadManager.h"
#include "Flatpak.h"

Q_LOGGING_CATEGORY(HeroicLog, "heroic")

namespace
{
    QJsonArray amazonLibraryCache;
}

class HeroicGame : public Game
{
    Q_OBJECT

public:
    enum SubStore
    {
        Epic,
        GOG,
        Amazon,
    };

    HeroicGame(SubStore store,
               const QJsonObject &json,
               const QString &heroicRoot,
               const QString &flatpakAppId,
               QObject *parent = nullptr)
        : Game{parent}
    {
        m_flatpakAppId = flatpakAppId;
        const auto toHost = [flatpakAppId](const QString &path) { return Flatpak::hostPath(flatpakAppId, path); };

        if (store == SubStore::Epic)
        {
            m_id = json["app_name"_L1].toString();
            m_name = json["title"_L1].toString();
            m_installDir = toHost(json["install_path"_L1].toString());
            m_type = AppType::Game;

            LaunchOption lo;
            lo.executable = m_installDir + '/' + json["executable"_L1].toString();

            // Platform detection code at Heroic:
            // https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher/blob/d2f0ed1c3929c78fc35b58e54bad1ccdfd5d8ed8/src/common/types/legendary.ts#L6
            // (check for updated version if Epic ever adds Linux support, I guess)
            // We are skipping Android and iOS for now, but can add those if it ever becomes a problem
            if (const auto platform = json["platform"].toString(); platform == "Windows"_L1 || platform == "Win32"_L1)
                lo.platform = Platform::Windows;
            else if (platform == "Mac"_L1)
                lo.platform = Platform::MacOS;

            m_executables[m_executables.size()] = lo;

            if (QFile metadataFile{heroicRoot + "/legendaryConfig/legendary/metadata/%1.json"_L1.arg(m_id)};
                metadataFile.open(QIODevice::ReadOnly))
            {
                const auto metadata = QJsonDocument::fromJson(metadataFile.readAll());

                for (const auto &image : metadata["metadata"_L1]["keyImages"_L1].toArray())
                {
                    if (image["type"_L1] == "DieselGameBox"_L1)
                        m_heroImage = "image://heroic-image/"_L1 + image["url"_L1].toString();
                    else if (image["type"_L1] == "DieselGameBoxTall"_L1)
                        m_cardImage = "image://heroic-image/"_L1 + image["url"_L1].toString();
                }
            }
        }
        else if (store == SubStore::GOG)
        {
            m_id = json["appName"_L1].toString();
            m_installDir = toHost(json["install_path"_L1].toString());

            if (QFile gogGameInfo{"%1/goggame-%2.info"_L1.arg(m_installDir, m_id)}; gogGameInfo.open(QIODevice::ReadOnly))
            {
                const auto info = QJsonDocument::fromJson(gogGameInfo.readAll()).object();
                m_name = info["name"_L1].toString();

                Platform platform;
                if (const auto p = json["platform"].toString(); p == "windows"_L1)
                    platform = Platform::Windows;
                else if (p == "osx"_L1)
                    platform = Platform::MacOS;
                else if (p == "linux"_L1)
                    platform = Platform::Linux;

                for (const auto &entry : info["playTasks"_L1].toArray())
                {
                    LaunchOption lo;
                    lo.executable = m_installDir + '/' + entry["path"_L1].toString();
                    lo.platform = platform;

                    if (entry["isPrimary"_L1].toBool())
                    {
                        if (const auto typeStr = entry["category"_L1].toString(); typeStr == "game")
                            m_type = AppType::Game;
                    }

                    m_executables[m_executables.size()] = lo;
                }
            }

            if (QFile storeCacheFile{heroicRoot + "/store_cache/gog_api_info.json"};
                storeCacheFile.open(QIODevice::ReadOnly))
            {
                auto storeCache = QJsonDocument::fromJson(storeCacheFile.readAll())["gog_%1"_L1.arg(m_id)];

                m_cardImage = "image://heroic-image/"_L1 + storeCache["game"_L1]["vertical_cover"_L1]["url_format"_L1]
                                                               .toString()
                                                               .replace("{formatter}"_L1, ""_L1)
                                                               .replace("{ext}"_L1, "jpg"_L1);
                m_heroImage = "image://heroic-image/"_L1 + storeCache["game"_L1]["logo"_L1]["url_format"_L1]
                                                               .toString()
                                                               .replace("{formatter}"_L1, ""_L1)
                                                               .replace("{ext}"_L1, "jpg"_L1);
                m_icon = "image://heroic-image/"_L1 + storeCache["game"_L1]["square_icon"_L1]["url_format"_L1]
                                                          .toString()
                                                          .replace("{formatter}"_L1, ""_L1)
                                                          .replace("{ext}"_L1, "jpg"_L1);
            }
        }
        else if (store == SubStore::Amazon)
        {
            m_id = json["id"_L1].toString();
            m_installDir = toHost(json["path"_L1].toString());
            m_type = AppType::Game;

            if (const auto it =
                    std::find_if(amazonLibraryCache.cbegin(),
                                 amazonLibraryCache.cend(),
                                 [this](const QJsonValueConstRef v) { return v["product"_L1]["id"_L1] == m_id; });
                it != amazonLibraryCache.cend())
            {
                const auto info = it->toObject();
                const auto &product = info["product"_L1];

                m_name = product["title"_L1].toString();

                m_cardImage = "image://heroic-image/"_L1 + product["productDetail"_L1]["iconUrl"_L1].toString();
                m_heroImage =
                    "image://heroic-image/"_L1 + product["productDetail"_L1]["details"_L1]["backgroundUrl2"_L1].toString();
            }

            if (QFile fuelJson{m_installDir + "/fuel.json"_L1}; fuelJson.open(QIODevice::ReadOnly))
            {
                const auto fuel = QJsonDocument::fromJson(fuelJson.readAll());

                LaunchOption lo;
                lo.platform = Platform::Windows;
                lo.executable = m_installDir + '/' + fuel["Main"_L1]["Command"_L1].toString();
                m_executables[0] = lo;
            }
        }

        // Kinda weird to have this here, but it doesn't work well anywhere else
        qCDebug(HeroicLog) << "Creating game:" << m_id;

        // Common to all substores
        if (QFile gamesConfig{heroicRoot + "/GamesConfig/%1.json"_L1.arg(m_id)}; gamesConfig.open(QIODevice::ReadOnly))
        {
            const auto installationInfo = QJsonDocument::fromJson(gamesConfig.readAll())[m_id];

            const auto sandboxPrefix = installationInfo["winePrefix"_L1].toString();
            auto sandboxBinary = installationInfo["wineVersion"_L1]["bin"_L1].toString();
            m_winePrefix = Flatpak::hostPath(m_flatpakAppId, sandboxPrefix);
            m_wineBinary = Flatpak::hostPath(m_flatpakAppId, sandboxBinary);

            qCDebug(HeroicLog) << "Found Wine prefix for" << m_name << "at" << m_winePrefix;

            // Launching Proton's wrapper directly does not work, so use the Wine binary it ships.
            // The sandbox path keeps the launcher's original spelling; only the host copy is rewritten.
            if (sandboxBinary.endsWith("/proton"_L1))
            {
                auto protonBase = sandboxBinary;
                protonBase.remove("/proton"_L1);
                const auto hostBase = Flatpak::hostPath(m_flatpakAppId, protonBase);
                QString suffix;
                if (QFileInfo files{hostBase + "/files"_L1}; files.exists() && files.isDir())
                    suffix = "/files/bin/wine"_L1;
                else if (QFileInfo dist{hostBase + "/dist"_L1}; dist.exists() && dist.isDir())
                    suffix = "/dist/bin/wine"_L1;
                if (!suffix.isEmpty())
                {
                    m_wineBinary = hostBase + suffix;
                    sandboxBinary = protonBase + suffix;
                }
            }

            if (!m_flatpakAppId.isEmpty())
            {
                m_sandboxWinePrefix = sandboxPrefix;
                m_sandboxWineBinary = sandboxBinary;
            }
        }

        // Fall back to local icon cache if it exists to avoid loading from the network
        QDirIterator icons{heroicRoot + "/icons"_L1};
        while (icons.hasNext())
        {
            icons.next();
            if (icons.fileName().startsWith(m_id))
            {
                m_icon = "file://"_L1 + icons.filePath();
                break;
            }
        }

        detectGameEngine();
        detectArchitectures();
        detectAnticheat();

        m_valid = m_executables.size() > 0;
    }

    Store store() const override { return Store::Heroic; }
    void launch() const override {}
};

QVariantList Heroic::libraries() const
{
    if (m_installs.isEmpty())
        return {QVariantMap{{"store"_L1, "Heroic"_L1}, {"path"_L1, QString{}}, {"count"_L1, 0}}};

    QVariantList rows;
    for (const auto &install : std::as_const(m_installs))
        rows << QVariantMap{{"store"_L1, install.name}, {"path"_L1, install.path}, {"count"_L1, install.count}};
    return rows;
}

QStringList Heroic::roots() const
{
    QStringList paths;
    for (const auto &install : std::as_const(m_installs))
        paths << install.path;
    return paths;
}

void Heroic::discover(bool report)
{
    struct Candidate
    {
        QString path;
        QString flatpakAppId;
        QString name;
    };
    const QList<Candidate> candidates = {
        {QDir::homePath() + "/.config/heroic"_L1, {}, "Heroic"_L1},
        {QDir::homePath() + "/.var/app/"_L1 + Flatpak::HeroicAppId + "/config/heroic"_L1,
         Flatpak::HeroicAppId,
         "Heroic (Flatpak)"_L1},
    };

    QList<Install> found;
    QStringList seen;
    for (const auto &candidate : candidates)
    {
        const QFileInfo info{candidate.path};
        if (!info.exists() || !info.isDir())
            continue;
        const auto canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical))
            continue;
        seen << canonical;

        Install install;
        install.path = canonical;
        install.flatpakAppId = candidate.flatpakAppId;
        install.name = candidate.name;
        found << install;
    }

    m_heroicRoot.clear();
    for (const auto &install : found)
    {
        if (install.flatpakAppId.isEmpty())
        {
            m_heroicRoot = install.path;
            break;
        }
    }
    if (m_heroicRoot.isEmpty() && !found.isEmpty())
        m_heroicRoot = found.constFirst().path;

    auto changed = found.size() != m_installs.size();
    for (int i = 0; !changed && i < found.size(); ++i)
        changed = found.at(i).path != m_installs.at(i).path || found.at(i).flatpakAppId != m_installs.at(i).flatpakAppId;
    if (changed)
        m_installs = found;

    if (!report && !changed)
        return;
    if (m_installs.isEmpty())
        qCInfo(HeroicLog) << "Heroic not found";
    else
        for (const auto &install : std::as_const(m_installs))
            qCInfo(HeroicLog) << "Found" << install.name << "at" << install.path;
}

Heroic::Heroic(QObject *parent)
    : Store{parent}
{
    discover(true);
    countAsLauncher();
}

Heroic *Heroic::instance()
{
    static auto h = new Heroic;
    return h;
}

Heroic *Heroic::create(QQmlEngine *qml, QJSEngine *js)
{
    return instance();
}

void Heroic::prepareScan()
{
    discover(false);
}

bool Heroic::readLibrary(QList<Game *> &games)
{
    qCDebug(HeroicLog) << "Scanning Heroic library";

    auto installs = m_installs;
    for (auto &install : installs)
    {
        install.count = 0;
        const auto &root = install.path;
        const auto add = [&](HeroicGame *game) {
            if (game->isValid())
            {
                games.push_back(game);
                ++install.count;
            }
            else
                delete game;
        };

        // Epic, then GOG, then Amazon. The Amazon library cache is per install and has to be
        // loaded before that install's games are constructed.
        if (QFile epicInstalled{root + "/legendaryConfig/legendary/installed.json"_L1};
            epicInstalled.open(QIODevice::ReadOnly))
        {
            qCDebug(HeroicLog) << "Found Epic:" << epicInstalled.fileName();
            const auto epicJson = QJsonDocument::fromJson(epicInstalled.readAll()).object();
            for (const auto &game : epicJson)
                add(new HeroicGame{HeroicGame::SubStore::Epic, game.toObject(), root, install.flatpakAppId, nullptr});
        }

        if (QFile gogInstalled{root + "/gog_store/installed.json"_L1}; gogInstalled.open(QIODevice::ReadOnly))
        {
            qCDebug(HeroicLog) << "Found GOG:" << gogInstalled.fileName();
            const auto gogJson = QJsonDocument::fromJson(gogInstalled.readAll()).object();
            for (const auto &game : gogJson["installed"_L1].toArray())
                add(new HeroicGame{HeroicGame::SubStore::GOG, game.toObject(), root, install.flatpakAppId, nullptr});
        }

        if (QFile amazonLibrary{root + "/nile_config/nile/library.json"_L1}; amazonLibrary.open(QIODevice::ReadOnly))
        {
            amazonLibraryCache = QJsonDocument::fromJson(amazonLibrary.readAll()).array();
            if (QFile amazonInstalled{root + "/nile_config/nile/installed.json"_L1};
                amazonInstalled.open(QIODevice::ReadOnly))
            {
                qCDebug(HeroicLog) << "Found Amazon:" << amazonInstalled.fileName();
                const auto amazonJson = QJsonDocument::fromJson(amazonInstalled.readAll()).array();
                for (const auto &game : amazonJson)
                    add(new HeroicGame{HeroicGame::SubStore::Amazon, game.toObject(), root, install.flatpakAppId, nullptr});
            }
        }
    }

    m_scannedInstalls = installs;
    return true;
}

void Heroic::finishScan()
{
    m_installs = m_scannedInstalls;
    emit librariesChanged();
}

class HeroicImageFetcher : public QQuickImageResponse
{
    Q_OBJECT

public:
    HeroicImageFetcher(const QString &url, const QSize &)
    {
        const auto hash = QCryptographicHash::hash(url.toLatin1(), QCryptographicHash::Sha256).toHex();
        for (const auto &root : Heroic::instance()->roots())
        {
            QFile heroicCache{root + "/images-cache/"_L1 + hash};
            if (!heroicCache.exists() || !heroicCache.open(QIODevice::ReadOnly))
                continue;
            m_image = QImage::fromData(heroicCache.readAll());
            emit finished();
            return;
        }

        QDir cache{QStandardPaths::writableLocation(QStandardPaths::CacheLocation)};
        cache.mkdir("heroic_cache"_L1);
        cache.cd("heroic_cache"_L1);
        cache.mkdir("epic"_L1);
        cache.mkdir("gog"_L1);
        cache.mkdir("amazon"_L1);

        QString storePart;
        if (url.contains("epicgames.com"_L1))
            storePart = "epic"_L1;
        else if (url.contains("gog.com"_L1))
            storePart = "gog"_L1;
        else if (url.contains("amazon.com"_L1))
            storePart = "amazon"_L1;
        else
            storePart = '.';

        auto file = new QFile{"%1/%2/%3"_L1.arg(cache.path(), storePart, url.split('/').last())};
        connect(this, &HeroicImageFetcher::finished, file, &QFile::deleteLater);
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
                "Heroic image",
                false,
                [this, file](const QByteArray &data) {
                    m_image = QImage::fromData(data);
                    if (file->open(QIODevice::WriteOnly))
                    {
                        file->write(data);
                        file->close();
                    }
                    else
                        qCDebug(HeroicLog) << "Could not cache image:" << file->fileName();
                },
                [this, file](const QNetworkReply::NetworkError, const QString &) {
                    // fall back to cache if possible
                    if (file->exists())
                    {
                        file->open(QIODevice::ReadOnly);
                        m_image = QImage::fromData(file->readAll());
                    }
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

QQuickImageResponse *HeroicImageCache::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    QQuickImageResponse *response = new HeroicImageFetcher(id, requestedSize);
    return response;
}

#include "Heroic.moc"
