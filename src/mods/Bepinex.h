#pragma once

#include <QQmlEngine>

#include "GitHubMod.h"

class Bepinex : public GitHubZipExtractorMod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Bepinex *instance();
    static Bepinex *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const final { return "BepInEx"_L1; }
    QString settingsGroup() const final { return "bepinex"_L1; }
    QString info() const final;
    const QLoggingCategory &logger() const final;

    virtual Game::Engines compatibleEngines() const override { return Game::Engine::Unity; }
    virtual bool checkGameCompatibility(const Game *game) const override;
    virtual bool isInstalledForGame(const Game *game) const override;

public slots:
    void installMod(Game *game) override;

protected:
    QUrl githubUrl() const final { return {"https://api.github.com/repos/BepInEx/BepInEx/releases"_L1}; }
    bool isThisFileTheActualModDownload(const QString &file) const final;
    QString modInstallDirForGame(Game *game) const final;

private:
    explicit Bepinex(QObject *parent = nullptr);
    ~Bepinex() {}
};
