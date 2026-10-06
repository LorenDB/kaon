#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

class UUVR : public GitHubZipExtractorMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static UUVR *instance();
    static UUVR *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const override { return "UUVR"_L1; }
    QString settingsGroup() const final { return "uuvr"_L1; }
    QString description() const override { return "VR mod for Unity games."_L1; }
    QString info() const final;
    QString launchOptions() const final;
    const QLoggingCategory &logger() const final;

    Game::Engines compatibleEngines() const override { return Game::Engine::Unity; }
    QList<Mod *> dependencies() const override;
    bool isInstalledForGame(const Game *game) const override;

    virtual QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const override;

protected:
    void installModImpl(Game *game, const Game::LaunchOption &exe) override;

    QUrl githubUrl() const final { return {"https://api.github.com/repos/Raicuparta/uuvr/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;
    QString modInstallDirForGame(const Game *game, const Game::LaunchOption &executable) const final;

private:
    explicit UUVR(QObject *parent = nullptr);
    ~UUVR() = default;
};
