#pragma once

#include <QObject>
#include <QQmlEngine>

#include "Game.h"
#include "Mod.h"

class Dotnet : public Mod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool hasDotnetCached READ hasDotnetCached NOTIFY hasDotnetCachedChanged FINAL)
    Q_PROPERTY(bool dotnetDownloadInProgress READ dotnetDownloadInProgress NOTIFY dotnetDownloadInProgressChanged FINAL)

public:
    explicit Dotnet(QObject *parent = nullptr);
    static Dotnet *instance();

    // Here's a hacky way to set all flags on a QFlags
    virtual Game::Engines compatibleEngines() const override { return Game::Engines::fromInt(INT_MAX); }

    virtual bool checkGameCompatibility(Game *game) override;
    virtual bool isInstalledForGame(const Game *game) const override;

    bool hasDotnetCached() const;
    bool dotnetDownloadInProgress() const;

public slots:
    void installDotnetDesktopRuntime(Game *game);
    void downloadDotnetDesktopRuntime(Game *game = nullptr);

signals:
    void hasDotnetCachedChanged(const bool);
    void dotnetDownloadInProgressChanged(const bool);
    void promptDotnetDownload(const Game *game = nullptr);
    void dotnetDownloadFailed();

private:
    static Dotnet *s_instance;

    virtual QList<ModRelease *> releases() const override;

    bool m_dotnetDownloadInProgress{false};
    QString m_dotnetInstallerCache;
};
