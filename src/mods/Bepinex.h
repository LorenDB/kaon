#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

class Bepinex : public GitHubZipExtractorMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Bepinex *instance();
    static Bepinex *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const final { return "BepInEx"_L1; }
    bool providesVr() const override { return false; }
    QString settingsGroup() const final { return "bepinex"_L1; }
    QString description() const final { return "Plugin loader for Unity games that UUVR runs on."_L1; }
    QString homepage() const final { return "https://github.com/BepInEx/BepInEx"_L1; }
    QString info() const final;
    const QLoggingCategory &logger() const final;

    virtual Game::Engines compatibleEngines() const override { return Game::Engine::Unity; }
    virtual bool isInstalledForGame(const Game *game) const override;
    QString installHoldReason(const Game *game) const override;

    virtual QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

    // Works around games that ship their own MonoMod (e.g. Haste's MonoMod 25 for its workshop modding): the game's
    // copy shadows BepInEx's MonoMod 22 during preloader probing and misses types BepInEx needs, crashing the
    // preloader. Pointing Mono at BepInEx\core first fixes loading without touching game files.
    void ensureDoorstopSearchPath(const Game *game, const Game::LaunchOption &exe) const;

    // Whether BepInEx itself is next to this executable, whatever else it still needs
    bool hasFilesFor(const Game *game, const Game::LaunchOption &exe) const;

public slots:
    void uninstallMod(Game *game) override;

protected:
    void installModImpl(Game *game, const Game::LaunchOption &exe) override;

    QUrl githubUrl() const final { return {"https://api.github.com/repos/BepInEx/BepInEx/releases"_L1}; }
    // BepInEx 6 only exists as prereleases, and UUVR needs BepInEx 5
    bool offersPrereleases() const final { return false; }
    bool isThisFileTheActualModDownload(const QString &file) const final;
    ModRelease::Asset chooseAssetToInstall(const Game *game, const Game::LaunchOption &exe) const final;

private:
    explicit Bepinex(QObject *parent = nullptr);
    ~Bepinex() = default;
};
