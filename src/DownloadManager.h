#pragma once

#include <QNetworkReply>
#include <QObject>
#include <QQueue>

class DownloadManager : public QObject
{
    Q_OBJECT

public:
    explicit DownloadManager(QObject *parent = nullptr);
    static DownloadManager *instance();

    void download(const QNetworkRequest &request,
                         std::function<void(QByteArray)> successCallback,
                         std::function<void(QNetworkReply::NetworkError, QString)> failureCallback,
                         std::function<void()> finallyCallback = [] {});

private:
    static DownloadManager *s_instance;

    void downloadNextInQueue();

    struct Download
    {
        QNetworkRequest request;
        std::function<void(QByteArray)> successCallback;
        std::function<void(QNetworkReply::NetworkError, QString)> failureCallback;
        std::function<void()> finallyCallback;
    };

    QQueue<Download> m_queue;
};
