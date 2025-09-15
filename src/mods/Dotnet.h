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
    static Dotnet *instance();
    static Dotnet *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const final { return ".NET Desktop Runtime"_L1; }
    QString settingsGroup() const final { return "dotnet"_L1; }
    const QLoggingCategory &logger() const final;

    // Here's a hacky way to set all flags on a QFlags
    virtual Game::Engines compatibleEngines() const override { return Game::Engines::fromInt(INT_MAX); }

    virtual bool checkGameCompatibility(const Game *game) const override;
    virtual bool isInstalledForGame(const Game *game) const override;

    bool hasDotnetCached() const;
    bool dotnetDownloadInProgress() const;

public slots:
    void downloadRelease(ModRelease *) override;
    void deleteRelease(ModRelease *release) override;

    void installMod(Game *game) override;
    void uninstallMod(Game *game) override;
    void downloadDotnetDesktopRuntime(Game *game = nullptr);

signals:
    void hasDotnetCachedChanged(const bool);
    void dotnetDownloadInProgressChanged(const bool);
    void dotnetDownloadFailed();

private:
    explicit Dotnet(QObject *parent = nullptr);
    static Dotnet *s_instance;

    virtual QList<ModRelease *> releases() const override;

    // TODO: remove this flag
    bool m_dotnetDownloadInProgress{false};
    QString m_dotnetInstallerCache;
};
