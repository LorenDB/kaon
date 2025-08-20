#include "Itch.h"

#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QTimer>

#include "DownloadManager.h"

using namespace Qt::Literals;

Itch *Itch::s_instance = nullptr;

Itch *Itch::instance()
{
    if (!s_instance)
        s_instance = new Itch;
    return s_instance;
}

int Itch::rowCount(const QModelIndex &parent) const
{
    return m_games.count();
}

QVariant Itch::data(const QModelIndex &index, int role) const
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

QHash<int, QByteArray> Itch::roleNames() const
{
    return {{Roles::GameObject, "game"_ba}};
}

Itch::Itch(QObject *parent)
    : QAbstractListModel{parent}
{
    static const QStringList itchPaths = {
        QDir::homePath() + "/.config/itch"_L1,
        // TODO: sandboxing used in flatpak appears to make UEVR not work, so I'm disabling flatpak detection for now
        // QDir::homePath() + "/.var/app/io.itch.itch/config/itch"_L1,
    };

    for (const auto &path : itchPaths)
    {
        if (QFileInfo fi{path}; fi.exists() && fi.isDir())
        {
            m_itchRoot = path;
            break;
        }
    }

    if (m_itchRoot.isEmpty())
        qDebug() << "Itch not found";

    QTimer::singleShot(0, this, &Itch::scanItch);
}

void Itch::scanItch()
{
    if (m_itchRoot.isEmpty())
        return;

    beginResetModel();

    for (const auto game : std::as_const(m_games))
        game->deleteLater();
    m_games.clear();

    QDirIterator apps{m_itchRoot + "/apps"_L1};
    while (apps.hasNext())
    {
        apps.next();
        if (apps.fileName() == "downloads"_L1 || apps.fileName() == "."_L1 || apps.fileName() == ".."_L1)
            continue;

        if (auto g = Game::fromItch(apps.filePath(), this); g)
            m_games.push_back(g);
    }

    endResetModel();
}

class ItchImageFetcher : public QQuickImageResponse
{
    Q_OBJECT

public:
    ItchImageFetcher(const QString &url, const QSize &)
    {
        static QDir cache{QStandardPaths::writableLocation(QStandardPaths::CacheLocation)};
        if (!cache.exists("itch_cache"_L1))
            cache.mkdir("itch_cache"_L1);
        auto file = new QFile{cache.path() + "/itch_cache/"_L1 + url.split('/').last()};
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
                        "Itch image",
                        false,
                        [this, file](const QByteArray &data) {
                m_image = QImage::fromData(data);

                file->open(QIODevice::WriteOnly);
                file->write(data);
                file->close();
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

        connect(this, &ItchImageFetcher::finished, file, &QFile::deleteLater);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

private:
    QImage m_image;
};

QQuickImageResponse *ItchImageCache::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    QQuickImageResponse *response = new ItchImageFetcher(id, requestedSize);
    return response;
}

#include "Itch.moc"
