#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

#include "Game.h"
#include "Mod.h"

class UEVR : public Mod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static UEVR *instance();
    static UEVR *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Launchable; }
    QString displayName() const final { return "UEVR"_L1; }
    QString settingsGroup() const final { return "uevr"_L1; }
    QString description() const final { return "Injects into most Unreal Engine 4 and 5 games."_L1; }
    const QLoggingCategory &logger() const final;

    bool hasRepairOption() const override { return false; }
    bool conflictsWithBundledVrPlugins() const override { return true; }

    Game::Engines compatibleEngines() const override { return Game::Engine::Unreal; }
    virtual QList<Mod *> dependencies() const override;
    virtual bool isInstalledForGame(const Game *game) const override;

    virtual QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

    enum class Paths
    {
        CurrentUEVR,
        CurrentUEVRInjector,
        UEVRBasePath,
        CachedReleasesJSON,
        CachedNightliesJSON,
    };
    QString path(const Paths path) const;

    void refreshReleases() override;

public slots:
    void downloadRelease(ModRelease *release) override;
    void deleteRelease(ModRelease *release) override;

protected:
    void launchModImpl(Game *game) override;

private:
    explicit UEVR(QObject *parent = nullptr);
    ~UEVR() = default;

    virtual QList<ModRelease *> releases() const override { return m_releases; }

    void parseReleaseInfoJson();

    QList<ModRelease *> m_releases;
};
