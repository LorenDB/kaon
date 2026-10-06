#pragma once

#include <QQmlEngine>

#include "Mod.h"

class UEVRAFW : public Mod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static UEVRAFW *instance();
    static UEVRAFW *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Launchable; }
    QString displayName() const final { return "UEVR AFW"_L1; }
    QString settingsGroup() const final { return "uevr-afw"_L1; }
    QString description() const final { return "UEVR fork with alternate frame warp for smoother DX12 games."_L1; }
    QString info() const final;
    const QLoggingCategory &logger() const final;

    bool hasRepairOption() const override { return false; }
    bool conflictsWithBundledVrPlugins() const override { return true; }

    Game::Engines compatibleEngines() const override { return Game::Engine::Unreal; }
    virtual QList<Mod *> dependencies() const override;
    virtual bool isInstalledForGame(const Game *game) const override;

    virtual QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

    void refreshReleases() override;

public slots:
    void downloadRelease(ModRelease *release) override;
    void deleteRelease(ModRelease *release) override;

protected:
    void launchModImpl(Game *game) override;

private:
    explicit UEVRAFW(QObject *parent = nullptr);
    ~UEVRAFW() = default;

    enum class Paths
    {
        BasePath,
        CurrentInjector,
        CachedReleasesJSON,
    };
    QString path(const Paths p) const;

    virtual QList<ModRelease *> releases() const override { return m_releases; }

    void parseReleaseInfoJson();

    QList<ModRelease *> m_releases;
};
