#include "Itch.h"

#include <QDir>
#include <QDirIterator>
#include <QLoggingCategory>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "Aptabase.h"
#include "DownloadManager.h"
#include "Wine.h"

using namespace Qt::Literals;

Q_LOGGING_CATEGORY(ItchLog, "itch")

Itch *Itch::s_instance = nullptr;

class ItchGame : public Game
{
    Q_OBJECT

public:
    ItchGame(const QString &installPath, QObject *parent)
        : Game{parent}
    {
        qCDebug(ItchLog) << "Creating game:" << installPath;

        QTemporaryDir tempDir;
        if (!tempDir.isValid())
        {
            qCWarning(ItchLog) << "Failed to create temporary directory";
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
            qCWarning(ItchLog) << "Unzip Itch info failed:" << unzipJsonProc.errorString();
            qCWarning(ItchLog) << unzipJsonProc.readAllStandardError();
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

        m_wineBinary = Wine::instance()->whichWine();
        m_winePrefix = Wine::instance()->defaultWinePrefix();

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
            entries =
                QDir{entries.first().absoluteFilePath()}.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

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

        m_valid = true;
    }

    Store store() const override { return Store::Itch; }
    void launch() const override {}
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
        qCInfo(ItchLog) << "Itch not found";
    else
        qCInfo(ItchLog) << "Found Itch:" << m_itchRoot;
}

void Itch::scanStore()
{
    if (m_itchRoot.isEmpty())
        return;

    qCDebug(ItchLog) << "Scanning Itch library";
    beginResetModel();

    QStringList installLocations;

    const auto dbPath = m_itchRoot + "/db/butler.db"_L1;
    if (QFileInfo::exists(dbPath))
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE"_L1);
        db.setDatabaseName(dbPath);
        if (db.open())
            if (QSqlQuery q; q.exec("SELECT path FROM install_locations"_L1))
                while (q.next())
                    installLocations.push_back(q.value(0).toString());
    }

    if (installLocations.isEmpty())
    {
        qCDebug(ItchLog) << "Could not open butler.db, falling back to scanning %1/apps"_L1.arg(m_itchRoot);
        installLocations.append(m_itchRoot + "/apps"_L1);
    }

    for (const auto game : std::as_const(m_games))
        game->deleteLater();
    m_games.clear();

    for (const auto &location : installLocations)
    {
        QDirIterator apps{location};
        while (apps.hasNext())
        {
            apps.next();
            if (apps.fileName() == "downloads"_L1 || apps.fileName() == "."_L1 || apps.fileName() == ".."_L1)
                continue;

            if (auto g = new ItchGame{apps.filePath(), this}; g->isValid())
                m_games.push_back(g);
            else
                g->deleteLater();
        }
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
                "Itch image",
                false,
                [this, file](const QByteArray &data) {
                    m_image = QImage::fromData(data);
                    if (file->open(QIODevice::WriteOnly))
                    {
                        file->write(data);
                        file->close();
                    }
                    else
                        qCDebug(ItchLog) << "Could not cache image:" << file->fileName();
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

        connect(this, &ItchImageFetcher::finished, file, &QFile::deleteLater);
    }

    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }

private:
    QImage m_image;
    QString m_error;
};

QQuickImageResponse *ItchImageCache::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    QQuickImageResponse *response = new ItchImageFetcher(id, requestedSize);
    return response;
}

#include "Itch.moc"
