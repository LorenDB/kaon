#pragma once

#include <QQmlEngine>

#include "Store.h"

class Steam : public Store
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool hasSteamVR READ hasSteamVR NOTIFY hasSteamVRChanged FINAL)

public:
    static Steam *instance();
    static Steam *create(QQmlEngine *qml, QJSEngine *js);

    QString storeRoot() const final { return m_steamRoot; }
    bool hasSteamVR() const { return m_hasSteamVR; }

    Q_INVOKABLE void launchSteamVR();

signals:
    void hasSteamVRChanged(bool state);

private:
    explicit Steam(QObject *parent = nullptr);
    ~Steam() {}

    void scanStore() final;

    QString m_steamRoot;
    bool m_hasSteamVR = false;
};
