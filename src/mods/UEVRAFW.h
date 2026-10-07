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
    QString homepage() const final { return "https://github.com/PureDark/UEVR"_L1; }
    QString info() const final;
    QString launchOptions() const final;
    QStringList conflictingLaunchOptions() const final;
    QVariantList softHints(const Game *game) const final;
    const QLoggingCategory &logger() const final;

    bool hasRepairOption() const override { return false; }
    bool conflictsWithBundledVrPlugins() const override { return true; }

    Game::Engines compatibleEngines() const override { return Game::Engine::Unreal; }
    virtual QList<Mod *> dependencies() const override;
    virtual bool isInstalledForGame(const Game *game) const override;

    virtual QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

    // True when this release name is the joeyhodge-based AFW build (UE 5.5–5.8).
    bool isJoeyhodgeRelease(const ModRelease *release) const;
    // Per-game UEVR config under the Wine prefix, if the prefix exists.
    QString configFileForGame(const Game *game) const final;
    // Writes VR_RenderingMethod=3, VR_GhostingFix, and Bootstrap on joeyhodge builds.
    bool applyRecommendedConfig(Game *game, QString *error = nullptr);
    bool recommendedConfigApplied(const Game *game) const;

    void refreshReleases() override;
    bool runHintAction(Game *game, const QString &action) override;

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
