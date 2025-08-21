#include "Heroic.h"

#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include "Aptabase.h"
#include "DownloadManager.h"

using namespace Qt::Literals;

Heroic *Heroic::s_instance = nullptr;

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
        m_store = Game::Store::Heroic;
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

        QFile gamesConfig{Heroic::instance()->heroicRoot() + "/GamesConfig/%1.json"_L1.arg(m_id)};
        gamesConfig.open(QIODevice::ReadOnly);
        const auto installationInfo = QJsonDocument::fromJson(gamesConfig.readAll())[m_id];

        m_protonPrefix = installationInfo["winePrefix"_L1].toString();
        m_protonBinary = installationInfo["wineVersion"_L1]["bin"_L1].toString();

        // For some reason launching proton directly doesn't seem to work right, so we'll try to bypass Proton installations
        // and use the underlying wine directly
        if (m_protonBinary.endsWith("/proton"_L1))
        {
            auto protonBase = m_protonBinary;
            protonBase.remove("/proton"_L1);
            if (QFileInfo files{protonBase + "/files"_L1}; files.exists() && files.isDir())
                m_protonBinary = protonBase + "/files/bin/wine"_L1;
            else if (QFileInfo dist{protonBase + "/dist"_L1}; dist.exists() && dist.isDir())
                m_protonBinary = protonBase + "/dist/bin/wine"_L1;
        }

        QFile metadataFile{Heroic::instance()->heroicRoot() + "/legendaryConfig/legendary/metadata/%1.json"_L1.arg(m_id)};
        metadataFile.open(QIODevice::ReadOnly);
        const auto metadata = QJsonDocument::fromJson(metadataFile.readAll());

        for (const auto &image : metadata["metadata"_L1]["keyImages"_L1].toArray())
        {
            if (image["type"_L1] == "DieselGameBox"_L1)
                m_heroImage = "image://heroic-image/"_L1 + image["url"_L1].toString();
            else if (image["type"_L1] == "DieselGameBoxTall"_L1)
                m_cardImage = "image://heroic-image/"_L1 + image["url"_L1].toString();
        }

        detectGameEngine();
    }
};

Heroic::Heroic(QObject *parent)
    : QAbstractListModel{parent}
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
        qDebug() << "Heroic not found";

    QTimer::singleShot(0, this, &Heroic::scanHeroic);
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

int Heroic::rowCount(const QModelIndex &parent) const
{
    return m_games.count();
}

QVariant Heroic::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size())
        return {};
    const auto &item = m_games.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
        return item->name();
    case Roles::GameObject:
        return QVariant::fromValue(item);
    }

    return {};
}

QHash<int, QByteArray> Heroic::roleNames() const
{
    return {{Roles::GameObject, "game"_ba}};
}

void Heroic::scanHeroic()
{
    if (m_heroicRoot.isEmpty())
        return;

    beginResetModel();

    for (const auto game : std::as_const(m_games))
        game->deleteLater();
    m_games.clear();

    // Here begins a three-part journey. Part one is Epic, part two is GOG, and part three is Amazon.

    // Part the first: Epic
    QFile epicInstalled{m_heroicRoot + "/legendaryConfig/legendary/installed.json"_L1};
    epicInstalled.open(QIODevice::ReadOnly);
    const auto epicJson = QJsonDocument::fromJson(epicInstalled.readAll()).object();

    for (const auto &game : epicJson)
        m_games.push_back(new HeroicGame{HeroicGame::SubStore::Epic, game.toObject(), this});

    // Part the second: GOG
    // TODO

    // Part the third: Amazon
    // TODO

    endResetModel();
}

class HeroicImageFetcher : public QQuickImageResponse
{
    Q_OBJECT

public:
    HeroicImageFetcher(const QString &url, const QSize &)
    {
        QDir cache{QStandardPaths::writableLocation(QStandardPaths::CacheLocation)};
        cache.mkdir("heroic_cache"_L1);
        cache.cd("heroic_cache"_L1);
        cache.mkdir("epic"_L1);
        cache.mkdir("gog"_L1);
        cache.mkdir("amazon"_L1);

        QString storePart;
        if (url.contains("epicgames.com"_L1))
            storePart = "epic"_L1;

        // TODO: also figure out how to check Heroic's image cache
        auto file = new QFile{"%1/%2/%3"_L1.arg(cache.path(), storePart, url.split('/').last())};
        if (file->exists() && file->fileTime(QFileDevice::FileModificationTime).daysTo(QDateTime::currentDateTime()) < 30)
        {
            file->open(QIODevice::ReadOnly);
            m_image = QImage::fromData(file->readAll());
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
                    qDebug() << file->fileName();
            },
            [this, file](const QNetworkReply::NetworkError, const QString &) {
                // fall back to cache if possible
                if (file->exists())
                {
                    file->open(QIODevice::ReadOnly);
                    m_image = QImage::fromData(file->readAll());
                }
            },
            [this] { emit finished(); });
        }

        connect(this, &HeroicImageFetcher::finished, file, &QFile::deleteLater);
    }

    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }

private:
    QImage m_image;
};

QQuickImageResponse *HeroicImageCache::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    QQuickImageResponse *response = new HeroicImageFetcher(id, requestedSize);
    return response;
}

#include "Heroic.moc"
