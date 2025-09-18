#pragma once

#include "GitHubMod.h"

class BepInExConfigManager : public GitHubZipExtractorMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static BepInExConfigManager *instance();
    static BepInExConfigManager *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const final { return "BepInEx Config Manager"_L1; }
    QString settingsGroup() const final { return "bepinex-config-manager"_L1; }
    const QLoggingCategory &logger() const final;

    Game::Engines compatibleEngines() const override { return Game::Engine::Unity; }
    QList<Mod *> dependencies() const override;
    virtual bool isInstalledForGame(const Game *game) const override;

    virtual QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

protected:
    QUrl githubUrl() const final { return {"https://api.github.com/repos/sinai-dev/BepInExConfigManager/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;
    QString modInstallDirForGame(const Game *game, const Game::LaunchOption &executable) const final;
    ModRelease::Asset chooseAssetToInstall(const Game *game, const Game::LaunchOption &exe) const final;

private:
    explicit BepInExConfigManager(QObject *parent = nullptr);
    ~BepInExConfigManager() = default;
};

