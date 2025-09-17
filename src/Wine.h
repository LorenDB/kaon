#pragma once

#include <QObject>
#include <QQmlEngine>

#include "Game.h"

class Wine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Wine *instance();
    static Wine *create(QQmlEngine *, QJSEngine *);

    void runInWine(
        const QString &prettyName,
        const Game *wineRoot,
        const QString &command,
        const QStringList &args = {},
        std::function<void()> successCallback = [] {},
        std::function<void()> failureCallback = [] {});

    Q_INVOKABLE QString whichWine() const;
    Q_INVOKABLE QString defaultWinePrefix() const;

signals:
    void processFailed(const QString &prettyName);

private:
    explicit Wine(QObject *parent = nullptr);
    ~Wine() {}
};
