#pragma once

#include "Mod.h"
#include <QQmlEngine>

class Bepinex : public Mod
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

    virtual Game::Engines compatibleEngines() const override { return Game::Engine::Unity; }

    virtual bool checkGameCompatibility(const Game *game) const override;
    virtual bool isInstalledForGame(const Game *game) const override;

    enum class Paths
    {
        CurrentRelease,
        BepinexBasePath,
        CachedReleasesJSON,
    };
    QString path(const Paths p) const;

public slots:
    void downloadRelease(ModRelease *release) override;
    void deleteRelease(ModRelease *release) override;
    void installMod(Game *game) override;
    void uninstallMod(Game *game) override;

private:
    explicit Bepinex(QObject *parent = nullptr);
    ~Bepinex() {}

    virtual QList<ModRelease *> releases() const override { return m_releases; }

    void updateAvailableReleases();
    void parseReleaseInfoJson();

    QList<ModRelease *> m_releases;
};

