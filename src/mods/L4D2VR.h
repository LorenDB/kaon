#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

class L4D2VR : public GitHubZipExtractorMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static L4D2VR *instance();
    static L4D2VR *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const override { return "Left 4 Dead 2 VR"_L1; }
    QString settingsGroup() const override { return "l4d2vr"_L1; }
    QString description() const override { return "Full VR conversion for Left 4 Dead 2."_L1; }
    QString homepage() const final { return "https://github.com/sd805/l4d2vr"_L1; }
    QString info() const final;
    QString launchOptions() const final;
    QStringList conflictingLaunchOptions() const final;
    const QLoggingCategory &logger() const final;

    Game::Engines compatibleEngines() const override { return Game::Engine::Source; }
    bool isInstalledForGame(const Game *game) const override;
    QString configFileForGame(const Game *game) const final;
    QString shippedConfigMember() const final { return "VR/config.txt"_L1; }
    QString installHoldReason(const Game *game) const override;

    QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

protected:
    void installModImpl(Game *game, const Game::LaunchOption &exe) override;

    QUrl githubUrl() const final { return {"https://api.github.com/repos/sd805/l4d2vr/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;

private:
    explicit L4D2VR(QObject *parent = nullptr);
    ~L4D2VR() = default;
};
