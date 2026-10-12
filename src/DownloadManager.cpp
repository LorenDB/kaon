#include "DownloadManager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>

#include "GitHub.h"

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
                         // A login page from a hotel network is not a list, and must not replace one. GitHub's
                         // rate-limit answer is JSON too, and object-shaped, so only an array may overwrite the file.
                         QJsonParseError parsed;
                         const auto doc = QJsonDocument::fromJson(data, &parsed);
                         if (parsed.error != QJsonParseError::NoError || !doc.isArray())
                         {
                             const auto message = doc.object().value("message"_L1).toString();
                             if (!message.isEmpty())
                                 failureCallback(message);
                             else if (parsed.error != QJsonParseError::NoError)
                                 failureCallback("The answer was not JSON: %1"_L1.arg(parsed.errorString()));
                             else
                                 failureCallback("The answer was not a list"_L1);
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

void DownloadManager::finishCurrent()
{
    if (!m_queue.empty())
    {
        downloadNextInQueue();
        return;
    }

    m_downloading = false;
    emit downloadingChanged();
    m_currentDownloadName = {};
    m_background = false;
    emit currentDownloadNameChanged();
    setProgress(-1);
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

    auto request = download.request;
    if (download.prepare)
        download.prepare(request);
    GitHub::instance()->prepare(request);

    // A known limit or a rejected token. Asking again would get the same answer and can extend the block.
    if (const auto reason = GitHub::instance()->deferReason(request.url()); !reason.isEmpty())
    {
        qCInfo(DownloadLog).noquote() << "Skipping" << download.prettyName << ":" << reason;
        if (download.notifyOnFailure)
            emit downloadFailed(download.prettyName);
        download.failureCallback(QNetworkReply::ContentAccessDenied, reason);
        download.finallyCallback();
        finishCurrent();
        return;
    }

    static QNetworkAccessManager manager;
    manager.setAutoDeleteReplies(true);
    // Downloads run one after another. One that stalls must not hold up everything queued behind it.
    manager.setTransferTimeout(30000);

    auto reply = manager.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        // Whole percents are all the status line shows
        setProgress(total > 0 ? qRound(100.0 * received / total) / 100.0 : -1);
    });
    connect(reply, &QNetworkReply::finished, reply, [=, this] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Some answers carry an error document with a transport error left unset. That document must not be saved
        // over a release list, and it must not end the process.
        if (reply->error() != QNetworkReply::NoError || status >= 400)
        {
            const auto body = reply->readAll();
            auto message = GitHub::instance()->observe(reply, body);
            if (message.isEmpty())
            {
                message = reply->error() != QNetworkReply::NoError ?
                              reply->errorString() :
                              "The server answered HTTP %1"_L1.arg(QString::number(status));
            }
            qCDebug(DownloadLog) << "Download error:" << message;
            if (download.notifyOnFailure)
                emit downloadFailed(download.prettyName);
            download.failureCallback(
                reply->error() != QNetworkReply::NoError ? reply->error() : QNetworkReply::ContentAccessDenied, message);
        }
        else
        {
            GitHub::instance()->observe(reply, {});
            const auto data = reply->readAll();
            download.successCallback(reply, data);
        }
        download.finallyCallback();
        finishCurrent();
    });
}
