#pragma once

#include <QQmlEngine>

#include "Mod.h"

class UEVRJoeyhodge : public Mod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static UEVRJoeyhodge *instance();
    static UEVRJoeyhodge *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Launchable; }
    QString displayName() const final { return "UEVR (joeyhodge)"_L1; }
    QString settingsGroup() const final { return "uevr-joeyhodge"_L1; }
    QString description() const final { return "UEVR fork with frequent fixes for UE 5.5-5.8 and particular games."_L1; }
    QString homepage() const final { return "https://github.com/joeyhodge/UEVR"_L1; }
    QString info() const final;
    const QLoggingCategory &logger() const final;

    bool hasRepairOption() const override { return false; }
    bool conflictsWithBundledVrPlugins() const override { return true; }
    bool supportsPlugins() const override { return true; }
    bool supportsProfiles() const override { return true; }

    Game::Engines compatibleEngines() const override { return Game::Engine::Unreal; }
    virtual QList<Mod *> dependencies() const override;
    virtual bool isInstalledForGame(const Game *game) const override;

    void refreshReleases() override;

public slots:
    void downloadRelease(ModRelease *release) override;
    void deleteRelease(ModRelease *release) override;

    // Same native plugins as UEVR: the fork loads DLLs from the same per-game and shared folders.
    // Returns an error in words meant for the user, or empty on success.
    QStringList installedPlugins(Game *game) override;
    QString installPlugin(Game *game, const QUrl &source) override;
    QString removePlugin(Game *game, const QString &fileName) override;
    QString pluginHoldReason(Game *game) override;

    // Same settings profiles as UEVR: both read the per-game folders importProfile() writes.
    // The import and remove functions return an error in words meant for the user, or empty on success.
    QString profileName(Game *game) override;
    bool profileHasDll(const QUrl &source) override;
    QString importProfile(Game *game, const QUrl &source) override;
    QString removeProfile(Game *game) override;
    QString profileHoldReason(Game *game) override;

protected:
    void launchModImpl(Game *game) override;
    void autoLaunchImpl(Game *game) override;

private:
    explicit UEVRJoeyhodge(QObject *parent = nullptr);
    ~UEVRJoeyhodge() = default;

    enum class Paths
    {
        BasePath,
        CurrentInjector,
        CachedReleasesJSON,
    };
    QString path(const Paths p) const;

    virtual QList<ModRelease *> releases() const override { return m_releases; }

    void fetchReleases(int maxAgeSeconds);
    void parseReleaseInfoJson();

    QList<ModRelease *> m_releases;
};
