#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

// Optional dxgi.dll proxy that unlocks DLSS inputs (and more) on AMD/Intel, and works on NVIDIA too. AFW needs a DLSS
// path; OptiScaler is the usual one when the GPU has no native DLSS. It is also useful on its own with no UEVR.
class OptiScaler : public GitHubMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static OptiScaler *instance();
    static OptiScaler *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const final { return Mod::Type::Installable; }
    QString displayName() const final { return "OptiScaler"_L1; }
    QString settingsGroup() const final { return "optiscaler"_L1; }
    QString description() const final
    {
        return "Lets games use DLSS inputs and modern upscalers on AMD, Intel, and NVIDIA."_L1;
    }
    QString homepage() const final { return "https://github.com/optiscaler/OptiScaler"_L1; }
    QString info() const final;
    QString launchOptions() const final;
    QString installedNote() const final
    {
        return "On for this game. Overlay with Insert; settings are in OptiScaler.ini."_L1;
    }
    const QLoggingCategory &logger() const final;

    bool optional() const final { return true; }
    bool providesVr() const final { return false; }
    bool hasRepairOption() const final { return false; }

    Game::Engines compatibleEngines() const final
    {
        return Game::Engine::UnknownEngine | Game::Engine::Unreal | Game::Engine::Unity | Game::Engine::Godot |
               Game::Engine::Source;
    }
    bool isInstalledForGame(const Game *game) const final;
    QString configFileForGame(const Game *game) const final;
    QString shippedConfigMember() const final { return "OptiScaler.ini"_L1; }
    QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const final;

public slots:
    void uninstallMod(Game *game) override;

protected:
    void installModImpl(Game *game, const Game::LaunchOption &exe) override;
    QList<Game::LaunchOption> preferredInstallCandidates(const Game *game, const QList<Game::LaunchOption> &all) const final;
    QUrl githubUrl() const final { return {"https://api.github.com/repos/optiscaler/OptiScaler/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;
    bool offersPrereleases() const final { return false; }

private:
    explicit OptiScaler(QObject *parent = nullptr);
    ~OptiScaler() = default;
};
