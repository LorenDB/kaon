#pragma once

#include "Mod.h"
#include <QQmlEngine>

class Portal2VR : public Mod
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

    Game::Engines compatibleEngines() const override { return Game::Engine::Source; }
    virtual bool checkGameCompatibility(const Game *game) const override;
    virtual bool isInstalledForGame(const Game *game) const override;

    enum class Paths
    {
        CurrentPortal2VR,
        Portal2VRBasePath,
        CachedReleasesJSON,
    };
    QString path(const Paths p) const;
    QString pathForRelease(int id) const;

public slots:
    void downloadRelease(ModRelease *release) override;
    void deleteRelease(ModRelease *release) override;
    void installMod(Game *game) override;
    void uninstallMod(Game *game) override;

private:
    explicit Portal2VR(QObject *parent = nullptr);
    ~Portal2VR() {}

    virtual QList<ModRelease *> releases() const override { return m_releases; }

    void updateAvailableReleases();
    void parseReleaseInfoJson();

    QList<ModRelease *> m_releases;
};

