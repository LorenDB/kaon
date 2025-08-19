#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

#include "Game.h"

class Mod : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Data only type")

    Q_PROPERTY(QAnyStringView name READ name CONSTANT FINAL)
    Q_PROPERTY(Game::Engines compatibleEngines READ compatibleEngines CONSTANT FINAL)

public:
    virtual QAnyStringView name() = 0;
    virtual Game::Engines compatibleEngines() = 0;

    struct Version
    {

    };
};

class Mods : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Mods *instance();
    static Mods *create(QQmlEngine *qml, QJSEngine *js);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:

private:
    explicit Mods(QObject *parent = nullptr);
    static Mods *s_instance;
};

class ModsFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Game::Engine engineFilter READ engineFilter WRITE setEngineFilter NOTIFY engineFilterChanged FINAL)
    Q_PROPERTY(bool showIncompatible READ showIncompatible WRITE setShowIncompatible NOTIFY showIncompatibleChanged FINAL)

public:
    explicit ModsFilter(QObject *parent = nullptr);

    Game::Engine engineFilter() const { return m_engineFilter; }
    bool showIncompatible() const { return m_showIncompatible; }

    void setEngineFilter(Game::Engine engine);
    void setShowIncompatible(bool state);

signals:
    void engineFilterChanged(Game::Engine engine);
    void showIncompatibleChanged(bool state);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    Game::Engine m_engineFilter{Game::Engine::Unknown};
    bool m_showIncompatible{false};
};
