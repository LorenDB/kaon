#pragma once

#include <QQmlEngine>

#include "Mod.h"

class UUVR : public Mod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    ~UUVR();
    static UUVR *instance();
    static UUVR *create(QQmlEngine *, QJSEngine *);

    Mod::Type type() const override { return Mod::Type::Installable; }
    QString displayName() const override { return "UUVR"_L1; }
    QString settingsGroup() const final { return "uuvr"_L1; }

    Game::Engines compatibleEngines() const override { return Game::Engine::Unity; }
    virtual QList<Mod *> dependencies() const override;
    virtual bool isInstalledForGame(const Game *game) const override;

    enum class Paths
    {
        CurrentUUVR,
        UUVRBasePath,
        CachedReleasesJSON,
    };
    QString path(const Paths p) const;

public slots:
    void downloadRelease(ModRelease *release) override;
    void deleteRelease(ModRelease *release) override;
    void installMod(Game *game) override;
    void uninstallMod(Game *game) override;

private:
    explicit UUVR(QObject *parent = nullptr);
    static UUVR *s_instance;

    virtual QList<ModRelease *> releases() const override { return m_releases; }

    void updateAvailableReleases();
    void parseReleaseInfoJson();

    QList<ModRelease *> m_releases;
};

