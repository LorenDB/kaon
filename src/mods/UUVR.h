#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

// UUVR comes in a build for each generation of Unity's VR support. Both install the same way, on top of BepInEx, and
// a game gets the one its Unity version can load. The split follows the mod's own installer, Rai Pal:
// https://github.com/Raicuparta/rai-pal-db/blob/main/mod-db/2/mods.json
class UuvrBuild : public GitHubZipExtractorMod
{
    Q_OBJECT

public:
    Mod::Type type() const override { return Mod::Type::Installable; }
    Game::Engines compatibleEngines() const override { return Game::Engine::Unity; }
    QList<Mod *> dependencies() const override;
    bool isInstalledForGame(const Game *game) const override;

    QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

protected:
    explicit UuvrBuild(QObject *parent = nullptr);
    ~UuvrBuild() = default;

    // The file a release has to ship for this build, e.g. "uuvr-mono-modern.zip"
    virtual QString assetName() const = 0;
    // A null version means the game's files didn't say which Unity made them
    virtual bool supportsUnity(const QVersionNumber &version) const = 0;

    void installModImpl(Game *game, const Game::LaunchOption &exe) override;
    QList<Game::LaunchOption> preferredInstallCandidates(const Game *game,
                                                         const QList<Game::LaunchOption> &all) const override;

    QUrl githubUrl() const final { return {"https://api.github.com/repos/Raicuparta/uuvr/releases"_L1}; }
    QString releaseListName() const final { return "uuvr"_L1; }
    bool isThisFileTheActualModDownload(const QString &file) const final;
    QString modInstallDirForGame(const Game *game, const Game::LaunchOption &executable) const final;
};

// For Unity 2018.4 and newer, which load VR through XR plugins
class UUVR : public UuvrBuild
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static UUVR *instance();
    static UUVR *create(QQmlEngine *, QJSEngine *);

    QString displayName() const final { return "UUVR"_L1; }
    QString settingsGroup() const final { return "uuvr"_L1; }
    QString description() const final { return "VR mod for Unity games."_L1; }
    QString info() const final;
    const QLoggingCategory &logger() const final;

protected:
    QString assetName() const final { return "uuvr-mono-modern.zip"_L1; }
    bool supportsUnity(const QVersionNumber &version) const final;

private:
    explicit UUVR(QObject *parent = nullptr);
    ~UUVR() = default;
};

// For older Unity, with VR built into the engine. Its last release is 0.3.1.
class UUVRLegacy : public UuvrBuild
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static UUVRLegacy *instance();
    static UUVRLegacy *create(QQmlEngine *, QJSEngine *);

    QString displayName() const final { return "UUVR Legacy"_L1; }
    QString settingsGroup() const final { return "uuvr-legacy"_L1; }
    QString description() const final { return "VR mod for games made with Unity older than 2018.4."_L1; }
    QString info() const final;
    const QLoggingCategory &logger() const final;

protected:
    QString assetName() const final { return "uuvr-mono-legacy.zip"_L1; }
    bool supportsUnity(const QVersionNumber &version) const final;

private:
    explicit UUVRLegacy(QObject *parent = nullptr);
    ~UUVRLegacy() = default;
};
