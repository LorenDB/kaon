#pragma once

#include <QQmlEngine>

#include "Store.h"

class CustomGames : public Store
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static CustomGames *instance();
    static CustomGames *create(QQmlEngine *qml, QJSEngine *js);

    QString storeRoot() const final { return {}; }

    Q_INVOKABLE bool addGame(const QString &name, const QString &executable, const QString &wine, const QString &winePrefix);
    Q_INVOKABLE void deleteGame(Game *game);

private:
    explicit CustomGames(QObject *parent = nullptr);
    ~CustomGames() {}

    void scanStore() final;
    void writeConfig();
};
