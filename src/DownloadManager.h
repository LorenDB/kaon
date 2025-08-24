#pragma once

#include <QNetworkReply>
#include <QObject>
#include <QQmlEngine>
#include <QQueue>

class DownloadManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged FINAL)
    Q_PROPERTY(QString currentDownloadName READ currentDownloadName NOTIFY currentDownloadNameChanged FINAL)

public:
    static DownloadManager *instance();
    static DownloadManager *create(QQmlEngine *qml, QJSEngine *js) { return instance(); }

    void download(
        const QNetworkRequest &request,
        const QString &prettyName,
        bool notifyOnFailure,
        std::function<void(QByteArray)> successCallback,
        std::function<void(QNetworkReply::NetworkError, QString)> failureCallback,
        std::function<void()> finallyCallback = [] {});

    bool downloading() const { return m_downloading; }
    QString currentDownloadName() const { return m_currentDownloadName; }

signals:
    void downloadingChanged();
    void currentDownloadNameChanged();
    void downloadFailed(const QString &whatWasBeingDownloaded);
    void newDownloadEnqueued();

private:
    explicit DownloadManager(QObject *parent = nullptr);
    static DownloadManager *s_instance;

    void downloadNextInQueue();

    struct Download
    {
        QNetworkRequest request;
        QString prettyName;
        bool notifyOnFailure;
        std::function<void(QByteArray)> successCallback;
        std::function<void(QNetworkReply::NetworkError, QString)> failureCallback;
        std::function<void()> finallyCallback;
    };

    QQueue<Download> m_queue;
    bool m_downloading{false};
    QString m_currentDownloadName;
};
