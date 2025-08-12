#pragma once

#include <QObject>
#include <QQmlEngine>

class Dotnet : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool hasDotnetCached READ hasDotnetCached NOTIFY hasDotnetCachedChanged FINAL)
    Q_PROPERTY(bool dotnetDownloadInProgress READ dotnetDownloadInProgress NOTIFY dotnetDownloadInProgressChanged FINAL)

public:
    explicit Dotnet(QObject *parent = nullptr);

    bool hasDotnetCached() const;
    bool dotnetDownloadInProgress() const;

public slots:
    void installDotnetDesktopRuntime(int steamId);
    void downloadDotnetDesktopRuntime(int steamId = 0);

    bool isDotnetInstalled(int steamId);

signals:
    void hasDotnetCachedChanged(const bool);
    void dotnetDownloadInProgressChanged(const bool);
    void promptDotnetDownload(const int steamId = 0);
    void dotnetDownloadFailed();

private:
    static Dotnet *s_instance;

    bool m_dotnetDownloadInProgress{false};
};
