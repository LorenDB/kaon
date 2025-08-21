#include "Itch.h"

#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include "Aptabase.h"
#include "DownloadManager.h"

using namespace Qt::Literals;

Itch *Itch::s_instance = nullptr;

class ItchGame : public Game
{
    Q_OBJECT

public:
    ItchGame(const QString &installPath, QObject *parent)
        : Game{parent}
    {
        QTemporaryDir tempDir;
        if (!tempDir.isValid())
        {
            qDebug() << "Itch failed to create temporary directory";
            Aptabase::instance()->track("itch-failed-tempdir-bug"_L1);
            return;
        }

        const QString zipPath = installPath + "/.itch/receipt.json.gz"_L1;
        QProcess unzipJsonProc;
        unzipJsonProc.setWorkingDirectory(tempDir.path());
        QStringList args;
        args << "-c"_L1 << zipPath;
        unzipJsonProc.start("gunzip"_L1, args);
        unzipJsonProc.waitForFinished();
        if (unzipJsonProc.exitCode() != 0)
        {
            qDebug() << "Unzip Itch info failed:" << unzipJsonProc.errorString();
            qDebug() << unzipJsonProc.readAllStandardError();
            Aptabase::instance()->track("itch-failed-unzip-bug"_L1);
            return;
        }

        const auto json = QJsonDocument::fromJson(unzipJsonProc.readAllStandardOutput());
        const auto game = json["game"_L1].toObject();

        m_id = QString::number(game["id"_L1].toInt());
        m_name = game["title"_L1].toString();
        m_installDir = installPath;
        m_cardImage = "image://itch-image/"_L1 + game["coverUrl"_L1].toString();
        m_heroImage = m_cardImage;
        m_icon = m_cardImage;

        QProcess whichWineProc;
        whichWineProc.start("which"_L1, {"wine"_L1});
        whichWineProc.waitForFinished();
        if (whichWineProc.exitCode() == 0)
            m_protonBinary = whichWineProc.readAllStandardOutput().trimmed();
        m_protonPrefix = qEnvironmentVariable("WINEPREFIX", QDir::homePath() + "/.wine"_L1);

        if (const auto type = game["classification"_L1]; type == "game"_L1)
            m_type = Game::AppType::Game;
        // Itch refers to generic apps as tools, but we normally use Tools for things like Proton
        else if (type == "tool"_L1)
            m_type = Game::AppType::App;
        else if (type == "soundtrack"_L1)
            m_type = Game::AppType::Music;

        auto entries = QDir{installPath}.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        entries.removeIf([](const QFileInfo &fi) { return fi.baseName() == ".itch"_L1; });
        if (entries.size() == 1)
            entries = QDir{entries.first().absoluteFilePath()}.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

        // TODO: better binary detection algorithm than this!
        for (const auto &entry : std::as_const(entries))
        {
            if (entry.suffix() == "exe"_L1)
            {
                LaunchOption lo;
                lo.executable = entry.absoluteFilePath();
                lo.platform |= LaunchOption::Platform::Windows;
                m_executables[0] = lo;
            }
        }

        detectGameEngine();
    }

    Store store() const override { return Store::Itch; }
};

Itch *Itch::instance()
{
    if (!s_instance)
        s_instance = new Itch;
    return s_instance;
}

Itch::Itch(QObject *parent)
    : Store{parent}
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

    QTimer::singleShot(0, this, &Itch::scanStore);
}

void Itch::scanStore()
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

        if (auto g = new ItchGame{apps.filePath(), this}; !g->id().isEmpty())
            m_games.push_back(g);
        else
            g->deleteLater();
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
