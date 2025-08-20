#pragma once

#include <QObject>
#include <QQmlEngine>

#include "Game.h"

class Dotnet : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool hasDotnetCached READ hasDotnetCached NOTIFY hasDotnetCachedChanged FINAL)
    Q_PROPERTY(bool dotnetDownloadInProgress READ dotnetDownloadInProgress NOTIFY dotnetDownloadInProgressChanged FINAL)

public:
    explicit Dotnet(QObject *parent = nullptr);
    static Dotnet *instance();

    bool hasDotnetCached() const;
    bool dotnetDownloadInProgress() const;

public slots:
    void installDotnetDesktopRuntime(Game *game);
    void downloadDotnetDesktopRuntime(Game *game = nullptr);

    bool isDotnetInstalled(const Game *game);

signals:
    void hasDotnetCachedChanged(const bool);
    void dotnetDownloadInProgressChanged(const bool);
    void promptDotnetDownload(const Game *game = nullptr);
    void dotnetDownloadFailed();

private:
    static Dotnet *s_instance;

    bool m_dotnetDownloadInProgress{false};
};
