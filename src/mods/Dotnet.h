#pragma once

#include <QObject>
#include <QQmlEngine>

#include "Game.h"
#include "Mod.h"

class Dotnet : public Mod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Dotnet *instance();
    static Dotnet *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const final { return ".NET Desktop Runtime"_L1; }
    QString settingsGroup() const final { return "dotnet"_L1; }
    QString description() const final { return "Windows runtime that UEVR needs inside each game's Proton prefix."_L1; }
    const QLoggingCategory &logger() const final;

    bool hasRepairOption() const override { return false; }
    bool providesVr() const override { return false; }
    bool installsIntoPrefix() const override { return true; }

    // .NET is only needed for UEVR, so we'll only show it for Unreal games
    virtual Game::Engines compatibleEngines() const override { return Game::Engine::Unreal; }
    virtual bool isInstalledForGame(const Game *game) const override;

    virtual QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

public slots:
    void downloadRelease(ModRelease *) override;
    void deleteRelease(ModRelease *release) override;

    void uninstallMod(Game *game) override;

protected:
    void installModImpl(Game *game, const Game::LaunchOption &exe) override;

private:
    explicit Dotnet(QObject *parent = nullptr);
    ~Dotnet() = default;

    virtual QList<ModRelease *> releases() const override;
    bool hasDotnetCached() const;
    // The installer's own window fails under Proton (WiX theme manager, exit 0x583). Quiet mode skips that UI.
    void runInstaller(Game *game, const Game::LaunchOption &exe, const QStringList &args, bool install);

    QString m_dotnetInstallerCache;
};
