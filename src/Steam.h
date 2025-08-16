#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

#include "Game.h"

class Steam : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Steam *instance();
    static Steam *create(QQmlEngine *qml, QJSEngine *js);

    enum Roles
    {
        Name = Qt::UserRole + 1,
        SteamID,
        InstallDir,
        CardImage,
        HeroImage,
        LogoImage,
        LastPlayed,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString steamRoot() const { return m_steamRoot; }
    Q_INVOKABLE Game *gameFromId(int steamId) const;

signals:

private:
    explicit Steam(QObject *parent = nullptr);
    static Steam *s_instance;

    void scanSteam();

    QString m_steamRoot;
    QList<Game *> m_games;
};

class SteamFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Game::Engines engineFilter READ engineFilter NOTIFY engineFilterChanged FINAL)
    Q_PROPERTY(Game::AppTypes typeFilter READ typeFilter NOTIFY typeFilterChanged FINAL)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged FINAL)

public:
    explicit SteamFilter(QObject *parent = nullptr);

    Game::Engines engineFilter() const { return m_engineFilter; }
    Game::AppTypes typeFilter() const { return m_typeFilter; }
    QString search() const { return m_search; }

    void setSearch(const QString &search);

    Q_INVOKABLE bool isEngineFilterSet(Game::Engine engine);
    Q_INVOKABLE void setEngineFilter(Game::Engine engine, bool state);
    Q_INVOKABLE bool isTypeFilterSet(Game::AppType type);
    Q_INVOKABLE void setTypeFilter(Game::AppType type, bool state);

signals:
    void engineFilterChanged();
    void typeFilterChanged();
    void searchChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    Game::Engines m_engineFilter;
    Game::AppTypes m_typeFilter;
    QString m_search;
};
