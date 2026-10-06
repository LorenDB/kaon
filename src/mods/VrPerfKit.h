#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

class VrPerfKit : public GitHubMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static VrPerfKit *instance();
    static VrPerfKit *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const final { return Mod::Type::Installable; }
    QString displayName() const final { return "VR Performance Toolkit"_L1; }
    QString settingsGroup() const final { return "vrperfkit"_L1; }
    QString description() const final
    {
        return "Upscales and sharpens Direct3D 11 games, and can render the edges cheaper."_L1;
    }
    QString info() const final;
    QString launchOptions() const final;
    QString installedNote() const final { return "Edit vrperfkit.yml next to the game to tune it."_L1; }
    const QLoggingCategory &logger() const final;

    bool optional() const final { return true; }
    QString vrRuntime() const final { return "OpenVR"_L1; }
    bool providesVr() const final { return false; }
    bool hasRepairOption() const final { return false; }

    Game::Engines compatibleEngines() const final
    {
        return Game::Engine::UnknownEngine | Game::Engine::Unreal | Game::Engine::Unity | Game::Engine::Godot |
               Game::Engine::Source;
    }
    bool isInstalledForGame(const Game *game) const final;
    QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const final;

public slots:
    void uninstallMod(Game *game) override;

protected:
    void installModImpl(Game *game, const Game::LaunchOption &exe) override;
    QList<Game::LaunchOption> preferredInstallCandidates(const Game *game, const QList<Game::LaunchOption> &all) const final;
    QUrl githubUrl() const final { return {"https://api.github.com/repos/fholger/vrperfkit/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;

private:
    explicit VrPerfKit(QObject *parent = nullptr);
    ~VrPerfKit() = default;
};
