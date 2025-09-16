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
    const QLoggingCategory &logger() const final;

    Game::Engines compatibleEngines() const override { return Game::Engine::Unity; }
    QList<Mod *> dependencies() const override;
    bool isInstalledForGame(const Game *game) const override;

protected:
    QUrl githubUrl() const final { return {"https://api.github.com/repos/Raicuparta/uuvr/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;
    QString modInstallDirForGame(Game *game) const final;

private:
    explicit UUVR(QObject *parent = nullptr);
    ~UUVR() {}
};
