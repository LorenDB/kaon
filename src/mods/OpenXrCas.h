#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

class OpenXrCas : public GitHubMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static OpenXrCas *instance();
    static OpenXrCas *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const final { return Mod::Type::Installable; }
    QString displayName() const final { return "OpenXR CAS"_L1; }
    QString settingsGroup() const final { return "openxr-cas"_L1; }
    QString description() const final { return "Sharpens Direct3D 11 OpenXR games."_L1; }
    QString info() const final;
    const QLoggingCategory &logger() const final;

    bool optional() const final { return true; }
    QString vrRuntime() const final { return "OpenXR"_L1; }
    bool providesVr() const final { return false; }
    bool installsIntoPrefix() const final { return true; }
    bool hasRepairOption() const final { return false; }

    Game::Engines compatibleEngines() const final
    {
        return Game::Engine::UnknownEngine | Game::Engine::Unreal | Game::Engine::Unity | Game::Engine::Godot |
               Game::Engine::Source;
    }
    bool isInstalledForGame(const Game *game) const final;
    QString installHoldReason(const Game *game) const final;
    QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const final;

public slots:
    void uninstallMod(Game *game) override;

protected:
    void installModImpl(Game *game, const Game::LaunchOption &exe) override;
    QUrl githubUrl() const final { return {"https://api.github.com/repos/elliotttate/OpenXR-CAS/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;

private:
    explicit OpenXrCas(QObject *parent = nullptr);
    ~OpenXrCas() = default;
};
