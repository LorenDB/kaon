#include "Heroic.h"

#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>

#include "DownloadManager.h"

Q_LOGGING_CATEGORY(HeroicLog, "heroic")

Heroic *Heroic::s_instance = nullptr;

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

    HeroicGame(SubStore store, const QJsonObject &json, QObject *parent = nullptr)
        : Game{parent}
    {
        if (store == SubStore::Epic)
        {
            m_id = json["app_name"_L1].toString();
            m_name = json["title"_L1].toString();
            m_installDir = json["install_path"_L1].toString();
            m_type = AppType::Game;

            LaunchOption lo;
            lo.executable = m_installDir + '/' + json["executable"_L1].toString();

            // Platform detection code at Heroic:
            // https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher/blob/d2f0ed1c3929c78fc35b58e54bad1ccdfd5d8ed8/src/common/types/legendary.ts#L6
            // (check for updated version if Epic ever adds Linux support, I guess)
            // We are skipping Android and iOS for now, but can add those if it ever becomes a problem
            if (const auto platform = json["platform"].toString(); platform == "Windows"_L1 || platform == "Win32"_L1)
                lo.platform = LaunchOption::Platform::Windows;
            else if (platform == "Mac"_L1)
                lo.platform = LaunchOption::Platform::MacOS;

            m_executables[m_executables.size()] = lo;

            if (QFile metadataFile{Heroic::instance()->storeRoot() +
                                   "/legendaryConfig/legendary/metadata/%1.json"_L1.arg(m_id)};
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
            m_installDir = json["install_path"_L1].toString();

            if (QFile gogGameInfo{"%1/goggame-%2.info"_L1.arg(m_installDir, m_id)}; gogGameInfo.open(QIODevice::ReadOnly))
            {
                const auto info = QJsonDocument::fromJson(gogGameInfo.readAll()).object();
                m_name = info["name"_L1].toString();

                LaunchOption::Platform platform;
                if (const auto p = json["platform"].toString(); p == "windows"_L1)
                    platform = LaunchOption::Platform::Windows;
                else if (p == "osx"_L1)
                    platform = LaunchOption::Platform::MacOS;
                else if (p == "linux"_L1)
                    platform = LaunchOption::Platform::Linux;

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

            if (QFile storeCacheFile{Heroic::instance()->storeRoot() + "/store_cache/gog_api_info.json"};
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
            m_installDir = json["path"_L1].toString();
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
                lo.platform = LaunchOption::Platform::Windows;
                lo.executable = m_installDir + '/' + fuel["Main"_L1]["Command"_L1].toString();
                m_executables[0] = lo;
            }
        }

        // Kinda weird to have this here, but it doesn't work well anywhere else
        qCDebug(HeroicLog) << "Creating game:" << m_id;

        // Common to all substores
        if (QFile gamesConfig{Heroic::instance()->storeRoot() + "/GamesConfig/%1.json"_L1.arg(m_id)};
            gamesConfig.open(QIODevice::ReadOnly))
        {
            const auto installationInfo = QJsonDocument::fromJson(gamesConfig.readAll())[m_id];

            m_winePrefix = installationInfo["winePrefix"_L1].toString();
            m_wineBinary = installationInfo["wineVersion"_L1]["bin"_L1].toString();

            qCDebug(HeroicLog) << "Found Wine prefix for" << m_name << "at" << m_winePrefix;

            // For some reason launching Proton directly doesn't seem to work right, so we'll try to bypass Proton
            // installations and use the underlying Wine directly
            if (m_wineBinary.endsWith("/proton"_L1))
            {
                auto protonBase = m_wineBinary;
                protonBase.remove("/proton"_L1);
                if (QFileInfo files{protonBase + "/files"_L1}; files.exists() && files.isDir())
                    m_wineBinary = protonBase + "/files/bin/wine"_L1;
                else if (QFileInfo dist{protonBase + "/dist"_L1}; dist.exists() && dist.isDir())
                    m_wineBinary = protonBase + "/dist/bin/wine"_L1;
            }
        }

        // Fall back to local icon cache if it exists to avoid loading from the network
        QDirIterator icons{Heroic::instance()->storeRoot() + "/icons"_L1};
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
        detectAnticheat();

        m_valid = true;
    }

    Store store() const override { return Store::Heroic; }
    void launch() const override {}
};

Heroic::Heroic(QObject *parent)
    : Store{parent}
{
    static const QStringList heroicPaths = {
        QDir::homePath() + "/.config/heroic"_L1,
        // TODO: sandboxing used in flatpak appears to make UEVR not work, so I'm disabling flatpak detection for now
        // QDir::homePath() + "/.var/app/com.heroicgameslauncher.hgl/config/heroic"_L1,
    };

    for (const auto &path : heroicPaths)
    {
        if (QFileInfo fi{path}; fi.exists() && fi.isDir())
        {
            m_heroicRoot = path;
            break;
        }
    }

    if (m_heroicRoot.isEmpty())
        qCInfo(HeroicLog) << "Heroic not found";
    else
        qCInfo(HeroicLog) << "Found Heroic:" << m_heroicRoot;
}

Heroic *Heroic::instance()
{
    if (s_instance == nullptr)
        s_instance = new Heroic;
    return s_instance;
}

Heroic *Heroic::create(QQmlEngine *qml, QJSEngine *js)
{
    return instance();
}

void Heroic::scanStore()
{
    if (m_heroicRoot.isEmpty())
        return;

    qCDebug(HeroicLog) << "Scanning Heroic library";
    beginResetModel();

    for (const auto game : std::as_const(m_games))
        game->deleteLater();
    m_games.clear();

    // Here begins a three-part journey.
    // Part the first: Epic
    if (QFile epicInstalled{m_heroicRoot + "/legendaryConfig/legendary/installed.json"_L1};
        epicInstalled.open(QIODevice::ReadOnly))
    {
        qCDebug(HeroicLog) << "Found Epic:" << epicInstalled.fileName();
        const auto epicJson = QJsonDocument::fromJson(epicInstalled.readAll()).object();
        for (const auto &game : epicJson)
        {
            if (auto g = new HeroicGame{HeroicGame::SubStore::Epic, game.toObject(), this}; g->isValid())
                m_games.push_back(g);
            else
                g->deleteLater();
        }
    }

    // Part the second: GOG
    if (QFile gogInstalled{m_heroicRoot + "/gog_store/installed.json"_L1}; gogInstalled.open(QIODevice::ReadOnly))
    {
        qCDebug(HeroicLog) << "Found GOG:" << gogInstalled.fileName();
        const auto gogJson = QJsonDocument::fromJson(gogInstalled.readAll()).object();
        for (const auto &game : gogJson["installed"_L1].toArray())
        {
            if (auto g = new HeroicGame{HeroicGame::SubStore::GOG, game.toObject(), this}; g->isValid())
                m_games.push_back(g);
            else
                g->deleteLater();
        }
    }

    // Part the third: Amazon
    if (QFile amazonLibrary{m_heroicRoot + "/nile_config/nile/library.json"_L1}; amazonLibrary.open(QIODevice::ReadOnly))
    {
        amazonLibraryCache = QJsonDocument::fromJson(amazonLibrary.readAll()).array();
        if (QFile amazonInstalled{m_heroicRoot + "/nile_config/nile/installed.json"_L1};
            amazonInstalled.open(QIODevice::ReadOnly))
        {
            qCDebug(HeroicLog) << "Found Amazon:" << amazonInstalled.fileName();
            const auto amazonJson = QJsonDocument::fromJson(amazonInstalled.readAll()).array();
            for (const auto &game : amazonJson)
            {
                if (auto g = new HeroicGame{HeroicGame::SubStore::Amazon, game.toObject(), this}; g->isValid())
                    m_games.push_back(g);
                else
                    g->deleteLater();
            }
        }
    }

    endResetModel();
}

class HeroicImageFetcher : public QQuickImageResponse
{
    Q_OBJECT

public:
    HeroicImageFetcher(const QString &url, const QSize &)
    {
        QFile heroicCache{Heroic::instance()->storeRoot() + "/images-cache/"_L1 +
                          QCryptographicHash::hash(url.toLatin1(), QCryptographicHash::Sha256).toHex()};
        if (heroicCache.exists() && heroicCache.open(QIODevice::ReadOnly))
        {
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

        // TODO: also figure out how to check Heroic's image cache
        auto file = new QFile{"%1/%2/%3"_L1.arg(cache.path(), storePart, url.split('/').last())};
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

        connect(this, &HeroicImageFetcher::finished, file, &QFile::deleteLater);
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
