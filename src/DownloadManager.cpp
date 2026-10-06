#include "DownloadManager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>

Q_LOGGING_CATEGORY(DownloadLog, "download")

DownloadManager::DownloadManager(QObject *parent)
    : QObject{parent}
{
    connect(this, &DownloadManager::newDownloadEnqueued, this, [this] {
        if (!m_downloading)
            downloadNextInQueue();
    });
}

DownloadManager *DownloadManager::instance()
{
    static auto dm = new DownloadManager;
    return dm;
}

void DownloadManager::download(const QNetworkRequest &request,
                               const QString &prettyName,
                               bool notifyOnFailure,
                               std::function<void(QByteArray)> successCallback,
                               std::function<void(QNetworkReply::NetworkError, QString)> failureCallback,
                               std::function<void()> finallyCallback)
{
    m_queue.enqueue({request,
                     {},
                     prettyName,
                     notifyOnFailure,
                     [successCallback](QNetworkReply *, const QByteArray &data) { successCallback(data); },
                     failureCallback,
                     finallyCallback});
    emit newDownloadEnqueued();
}

void DownloadManager::refreshCached(const QNetworkRequest &request,
                                    const QString &prettyName,
                                    const QString &cacheFile,
                                    std::function<void(bool)> successCallback,
                                    std::function<void(QString)> failureCallback)
{
    const auto etagFile = cacheFile + ".etag"_L1;

    m_queue.enqueue({request,
                     // Two mods that share a list share its file. The second to ask then gets "not modified".
                     [=](QNetworkRequest &conditional) {
                         // An ETag says nothing once the file it describes is gone
                         if (QFile saved{etagFile}; QFileInfo::exists(cacheFile) && saved.open(QIODevice::ReadOnly))
                         {
                             if (const auto etag = saved.readAll().trimmed(); !etag.isEmpty())
                                 conditional.setRawHeader("If-None-Match"_ba, etag);
                         }
                     },
                     prettyName,
                     false,
                     [=](QNetworkReply *reply, const QByteArray &data) {
                         if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 304)
                         {
                             qCDebug(DownloadLog).noquote() << prettyName << "has not changed";
                             successCallback(false);
                             return;
                         }
                         // A login page from a hotel network is not a list, and must not replace one
                         if (QJsonParseError parsed; QJsonDocument::fromJson(data, &parsed).isNull())
                         {
                             failureCallback("The answer was not JSON: %1"_L1.arg(parsed.errorString()));
                             return;
                         }

                         QSaveFile cache{cacheFile};
                         if (!cache.open(QIODevice::WriteOnly) || cache.write(data) != data.size() || !cache.commit())
                         {
                             failureCallback("Could not write %1: %2"_L1.arg(cacheFile, cache.errorString()));
                             return;
                         }
                         const auto etag = reply->rawHeader("ETag"_ba);
                         if (QFile saved{etagFile}; !etag.isEmpty() && saved.open(QIODevice::WriteOnly))
                             saved.write(etag);
                         else
                             QFile::remove(etagFile);
                         successCallback(true);
                     },
                     [failureCallback](QNetworkReply::NetworkError, const QString &message) { failureCallback(message); },
                     [] {}});
    emit newDownloadEnqueued();
}

void DownloadManager::setProgress(qreal progress)
{
    if (qFuzzyCompare(progress + 2, m_progress + 2))
        return;
    m_progress = progress;
    emit progressChanged();
}

void DownloadManager::downloadNextInQueue()
{
    m_downloading = true;
    emit downloadingChanged();

    const auto download = m_queue.dequeue();
    m_currentDownloadName = download.prettyName;
    // The downloads people start are the ones that report a failure to them
    m_background = !download.notifyOnFailure;
    emit currentDownloadNameChanged();
    setProgress(-1);

    static QNetworkAccessManager manager;
    manager.setAutoDeleteReplies(true);
    // Downloads run one after another. One that stalls must not hold up everything queued behind it.
    manager.setTransferTimeout(30000);

    auto request = download.request;
    if (download.prepare)
        download.prepare(request);
    auto reply = manager.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        // Whole percents are all the status line shows
        setProgress(total > 0 ? qRound(100.0 * received / total) / 100.0 : -1);
    });
    connect(reply, &QNetworkReply::finished, reply, [=, this] {
        if (reply->error() != QNetworkReply::NoError)
        {
            qCDebug(DownloadLog) << "Download error:" << reply->errorString();
            if (download.notifyOnFailure)
                emit downloadFailed(download.prettyName);
            download.failureCallback(reply->error(), reply->errorString());
        }
        else
        {
            const auto data = reply->readAll();
            download.successCallback(reply, data);
        }
        download.finallyCallback();

        if (!m_queue.empty())
            downloadNextInQueue();
        else
        {
            m_downloading = false;
            emit downloadingChanged();
            m_currentDownloadName = {};
            m_background = false;
            emit currentDownloadNameChanged();
            setProgress(-1);
        }
    });
}
