#include "UEVR.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QStandardPaths>

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

    updateAvailableReleases();

    m_uevrPath = settings.value("path"_L1, {}).toString();
    m_currentUevr = settings.value("currentUevr"_L1, {}).toInt();

    connect(this, &UEVR::showNightliesChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
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
    case Qt::DisplayRole:
        return item.name;
    }

    return {};
}

QHash<int, QByteArray> UEVR::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "name";
    return roles;
}

const QString &UEVR::uevrPath() const
{
    return m_uevrPath;
}

int UEVR::currentUevr() const
{
    return m_currentUevr;
}

void UEVR::setUevrPath(const QString &path)
{
    auto normalized = path;
    if (normalized.startsWith("file://"_L1))
        normalized.remove("file://"_L1);
    normalized = QFileInfo{normalized}.canonicalFilePath();
    if (!QFileInfo::exists(normalized))
    {
        qDebug() << "Invalid UEVR path";
        return;
    }

    QSettings settings;
    settings.beginGroup("uevr"_L1);
    settings.setValue("path"_L1, normalized);

    m_uevrPath = normalized;
    emit uevrPathChanged(normalized);
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
        return;
    }

    m_currentUevr = id;
    emit currentUevrChanged(id);

    QSettings settings;
    settings.beginGroup("uevr"_L1);
    settings.setValue("currentUevr"_L1, id);
}

void UEVR::updateAvailableReleases()
{
    static QNetworkAccessManager manager;
    manager.setAutoDeleteReplies(true);

    auto impl = [this](QUrl url, QList<UEVRRelease> &dest) {
        QNetworkRequest req{url};
        req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);
        auto reply = manager.get(QNetworkRequest{url});
        connect(reply, &QNetworkReply::finished, this, [this, reply, &dest] {
            if (reply->error() != QNetworkReply::NoError)
            {
                qDebug() << "Error while fetching releases:" << reply->errorString();
                return;
            }

            dest.clear();
            auto json = QJsonDocument::fromJson(reply->readAll());
            for (const auto &release : json.array())
            {
                UEVRRelease r;
                r.name = release["name"_L1].toString();
                r.id = release["id"_L1].toInt();
                r.timestamp = QDateTime::fromString(release["published_at"_L1].toString(), Qt::ISODate);
                for (const auto &asset : release["assets"_L1].toArray())
                {
                    UEVRRelease::Asset a;
                    a.id = asset["id"_L1].toInt();
                    a.name = asset["name"_L1].toString();
                    a.url = asset["browser_download_url"_L1].toString();
                    r.assets.push_back(a);
                }
                dest.push_back(r);
            }
            rebuild();
        });
    };

    impl({"https://api.github.com/repos/praydog/UEVR/releases"_L1}, m_releases);
    impl({"https://api.github.com/repos/praydog/UEVR-nightly/releases"_L1}, m_nightlies);
}

void UEVR::downloadRelease(const UEVRRelease &release) const
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

    QTemporaryDir tempDir;
    if (!tempDir.isValid())
    {
        qDebug() << "Failed to create temporary directory";
        return;
    }

    const QString zipPath = tempDir.path() + "/uevr_" + QString::number(release.id) + ".zip";

    static QNetworkAccessManager manager;
    manager.setAutoDeleteReplies(true);

    auto reply = manager.get(QNetworkRequest{url});
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, zipPath, id = release.id]() {
        if (reply->error() != QNetworkReply::NoError)
        {
            qDebug() << "Download error:" << reply->errorString();
            return;
        }

        QFile file(zipPath);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(reply->readAll());
            file.close();

            QString targetDir =
                    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/uevr/" + QString::number(id);
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
                qDebug() << "Unzip failed:" << process.errorString();
                qDebug() << process.readAllStandardError();
            }
            else
            {
                qDebug() << "Download and unzip completed successfully";
            }
        }
        else
        {
            qDebug() << "Failed to save downloaded file";
        }
    });
}

void UEVR::launchUEVR(const int steamId)
{
    QProcess injector;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("WINEPREFIX"_L1,
               QDir::homePath() + "/.local/share/Steam/steamapps/compatdata/" + QString::number(steamId) + "/pfx"_L1);
    env.insert("WINEFSYNC"_L1, "1"_L1);
    injector.setProcessEnvironment(env);
    injector.start(QDir::homePath() + "/.local/share/Steam/steamapps/common/Proton - Experimental/files/bin/wine"_L1,
                   {QDir::homePath() + "/.uevr/UEVRInjector.exe"_L1});
}

void UEVR::rebuild()
{
    beginResetModel();
    m_mergedReleases.clear();
    m_mergedReleases.append(m_releases);
    m_mergedReleases.append(m_nightlies);
    std::sort(m_mergedReleases.begin(), m_mergedReleases.end(), [](const auto &a, const auto &b) {
        return a.timestamp < b.timestamp;
    });
    endResetModel();
}
