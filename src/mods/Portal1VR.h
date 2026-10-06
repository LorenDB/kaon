#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

class Portal1VR : public GitHubZipExtractorMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Portal1VR *instance();
    static Portal1VR *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const override { return "Portal 1 VR"_L1; }
    QString settingsGroup() const override { return "portal1vr"_L1; }
    QString description() const override { return "Full VR conversion for the Windows version of Portal."_L1; }
    QString homepage() const final { return "https://github.com/LorenDB/portal1vr"_L1; }
    QString info() const final;
    QString launchOptions() const final;
    QStringList conflictingLaunchOptions() const final;
    const QLoggingCategory &logger() const final;

    Game::Engines compatibleEngines() const override { return Game::Engine::Source; }
    bool isInstalledForGame(const Game *game) const override;
    QString configFileForGame(const Game *game) const final;
    QString installHoldReason(const Game *game) const override;

    QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

protected:
    void installModImpl(Game *game, const Game::LaunchOption &exe) override;

    QUrl githubUrl() const final { return {"https://api.github.com/repos/LorenDB/portal1vr/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;

private:
    explicit Portal1VR(QObject *parent = nullptr);
    ~Portal1VR() = default;
};
