#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>
#include <QQuickAsyncImageProvider>

#include "Game.h"

class Heroic : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString heroicRoot READ heroicRoot CONSTANT FINAL)

public:
    static Heroic *instance();
    static Heroic *create(QQmlEngine *qml, QJSEngine *js);

    enum Roles
    {
        GameObject = Qt::UserRole + 1,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString heroicRoot() const { return m_heroicRoot; }

private:
    explicit Heroic(QObject *parent = nullptr);
    static Heroic *s_instance;

    void scanHeroic();

    QString m_heroicRoot;
    QList<Game *> m_games;
};

class HeroicImageCache : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
