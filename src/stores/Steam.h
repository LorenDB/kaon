#pragma once

#include <QQmlEngine>

#include "Store.h"

class Steam : public Store
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Steam *instance();
    static Steam *create(QQmlEngine *qml, QJSEngine *js);

    QString storeRoot() const final { return m_steamRoot; }

private:
    explicit Steam(QObject *parent = nullptr);
    static Steam *s_instance;

    void scanStore() final;

    QString m_steamRoot;
};
