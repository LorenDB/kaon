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

public:
    static Dotnet *instance();
    static Dotnet *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const final { return ".NET Desktop Runtime"_L1; }
    QString settingsGroup() const final { return "dotnet"_L1; }
    const QLoggingCategory &logger() const final;

    bool hasRepairOption() const override { return true; }

    // .NET is only needed for UEVR, so we'll only show it for Unreal games
    virtual Game::Engines compatibleEngines() const override { return Game::Engine::Unreal; }
    virtual bool checkGameCompatibility(const Game *game) const override;
    virtual bool isInstalledForGame(const Game *game) const override;

public slots:
    void downloadRelease(ModRelease *) override;
    void deleteRelease(ModRelease *release) override;

    void installMod(Game *game) override;
    void uninstallMod(Game *game) override;

private:
    explicit Dotnet(QObject *parent = nullptr);
    ~Dotnet() = default;

    virtual QList<ModRelease *> releases() const override;
    bool hasDotnetCached() const;

    QString m_dotnetInstallerCache;
};
