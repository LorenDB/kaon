#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

#include "Game.h"
#include "Mod.h"

class UEVR : public Mod
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString uevrPath READ uevrPath NOTIFY uevrPathChanged FINAL)
    Q_PROPERTY(ModRelease *currentUevr READ currentUevr NOTIFY currentUevrChanged FINAL)

public:
    ~UEVR();
    static UEVR *instance();
    static UEVR *create(QQmlEngine *, QJSEngine *);

    const QString uevrPath() const;
    ModRelease *currentUevr() const;

    Game::Engines compatibleEngines() const override { return Game::Engine::Unreal; }

    enum class Paths
    {
        CurrentUEVR,
        CurrentUEVRInjector,
        UEVRBasePath,
        CachedReleasesJSON,
        CachedNightliesJSON,
    };
    QString path(const Paths path) const;

public slots:
    void launchUEVR(const Game *game);
    void downloadUEVR(ModRelease *uevr);
    void deleteUEVR(ModRelease *uevr);

    void setCurrentUevr(const int id);

signals:
    void uevrPathChanged(const QString &);
    void currentUevrChanged(ModRelease *);

private:
    explicit UEVR(QObject *parent = nullptr);
    static UEVR *s_instance;

    virtual QList<ModRelease *> releases() const override { return m_releases; }

    void updateAvailableReleases();
    void parseReleaseInfoJson();

    QList<ModRelease *> m_releases;
    ModRelease *m_currentUevr{nullptr};
};

class UEVRFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool showNightlies READ showNightlies WRITE setShowNightlies NOTIFY showNightliesChanged FINAL)

public:
    explicit UEVRFilter(QObject *parent = nullptr);

    bool showNightlies() const { return m_showNightlies; }
    Q_INVOKABLE int indexFromRelease(ModRelease *release) const;

    void setShowNightlies(bool state);

signals:
    void showNightliesChanged(bool state);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool m_showNightlies{false};
};
