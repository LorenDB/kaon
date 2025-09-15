#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

class Portal2VR : public GitHubZipExtractorMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Portal2VR *instance();
    static Portal2VR *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const override { return "Portal 2 VR"_L1; }
    QString settingsGroup() const override { return "portal2vr"_L1; }
    QString info() const final;
    const QLoggingCategory &logger() const final;

    Game::Engines compatibleEngines() const override { return Game::Engine::Source; }
    virtual bool checkGameCompatibility(const Game *game) const override;
    virtual bool isInstalledForGame(const Game *game) const override;

public slots:
    void installMod(Game *game) override;

protected:
    QUrl githubUrl() const final { return {"https://api.github.com/repos/Gistix/portal2vr/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;
    virtual QString modInstallDirForGame(Game *game) const final;

private:
    explicit Portal2VR(QObject *parent = nullptr);
    ~Portal2VR() {}
};

