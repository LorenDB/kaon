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

    Q_INVOKABLE bool addGame(const QString &name, const QString &executable);
    Q_INVOKABLE void deleteGame(Game *game);

private:
    explicit CustomGames(QObject *parent = nullptr);
    static CustomGames *s_instance;

    void scanStore() final;
    void writeConfig();
};
