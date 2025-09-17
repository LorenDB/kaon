#include "DownloadManager.h"

#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>

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
    static DownloadManager dm;
    return &dm;
}

void DownloadManager::download(const QNetworkRequest &request,
                               const QString &prettyName,
                               bool notifyOnFailure,
                               std::function<void(QByteArray)> successCallback,
                               std::function<void(QNetworkReply::NetworkError, QString)> failureCallback,
                               std::function<void()> finallyCallback)
{
    m_queue.enqueue({request, prettyName, notifyOnFailure, successCallback, failureCallback, finallyCallback});
    emit newDownloadEnqueued();
}

void DownloadManager::downloadNextInQueue()
{
    m_downloading = true;
    emit downloadingChanged();

    const auto download = m_queue.dequeue();
    m_currentDownloadName = download.prettyName;
    emit currentDownloadNameChanged();

    static QNetworkAccessManager manager;
    manager.setAutoDeleteReplies(true);

    auto reply = manager.get(download.request);
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
            download.successCallback(data);
        }
        download.finallyCallback();

        if (!m_queue.empty())
            downloadNextInQueue();
        else
        {
            m_downloading = false;
            emit downloadingChanged();
            m_currentDownloadName = {};
            emit currentDownloadNameChanged();
        }
    });
}
