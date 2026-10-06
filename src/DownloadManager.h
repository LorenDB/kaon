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
    // How much of the current download has arrived, from 0 to 1. Negative while the server hasn't said how big it is.
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged FINAL)
    // True for fetches nobody asked for, like cover art and release lists. They aren't worth a line in the status strip.
    Q_PROPERTY(bool background READ background NOTIFY currentDownloadNameChanged FINAL)

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

    // Keeps a copy of what a URL returns in cacheFile, for lists that are read again on every start. The saved ETag is
    // sent along, and a server that answers "not modified" leaves the file as it is. GitHub doesn't count those answers
    // against its hourly limit. changed says whether the file was rewritten. Failures are logged, never shown: there
    // is usually an older copy to carry on with.
    void refreshCached(
        const QNetworkRequest &request,
        const QString &prettyName,
        const QString &cacheFile,
        std::function<void(bool changed)> successCallback,
        std::function<void(QString)> failureCallback = [](const QString &) {});

    bool downloading() const { return m_downloading; }
    QString currentDownloadName() const { return m_currentDownloadName; }
    qreal progress() const { return m_progress; }
    bool background() const { return m_background; }

signals:
    void downloadingChanged();
    void currentDownloadNameChanged();
    void progressChanged();
    void downloadFailed(const QString &whatWasBeingDownloaded);
    void newDownloadEnqueued();

private:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() = default;

    void downloadNextInQueue();
    void setProgress(qreal progress);

    struct Download
    {
        QNetworkRequest request;
        // Last changes to the request, made when its turn comes. Whatever was queued ahead of it has finished by then.
        std::function<void(QNetworkRequest &)> prepare;
        QString prettyName;
        bool notifyOnFailure;
        std::function<void(QNetworkReply *, QByteArray)> successCallback;
        std::function<void(QNetworkReply::NetworkError, QString)> failureCallback;
        std::function<void()> finallyCallback;
    };

    QQueue<Download> m_queue;
    bool m_downloading{false};
    QString m_currentDownloadName;
    bool m_background{false};
    qreal m_progress{-1};
};
