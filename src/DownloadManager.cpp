#include "DownloadManager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

DownloadManager *DownloadManager::s_instance = nullptr;

DownloadManager::DownloadManager(QObject *parent)
    : QObject{parent}
{
    if (s_instance != nullptr)
        throw std::runtime_error{"Attempted to double initialize download manager"};
    else
        s_instance = this;
}

void DownloadManager::download(const QNetworkRequest &request,
                               std::function<void(QByteArray)> successCallback,
                               std::function<void(QNetworkReply::NetworkError, QString)> failureCallback,
                               std::function<void()> finallyCallback)
{
    m_queue.enqueue({request, successCallback, failureCallback, finallyCallback});
    if (m_queue.count() == 1)
        downloadNextInQueue();
}

void DownloadManager::downloadNextInQueue()
{
    const auto download = m_queue.dequeue();

    static QNetworkAccessManager manager;
    manager.setAutoDeleteReplies(true);

    auto reply = manager.get(download.request);
    connect(reply, &QNetworkReply::finished, reply, [=, this] {
        if (reply->error() != QNetworkReply::NoError)
        {
            qDebug() << "Download error:" << reply->errorString();
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
    });
}
