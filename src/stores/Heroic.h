#pragma once

#include <QJsonArray>
#include <QQmlEngine>
#include <QQuickAsyncImageProvider>

#include "Store.h"

class Heroic : public Store
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Heroic *instance();
    static Heroic *create(QQmlEngine *qml, QJSEngine *js);

    QString storeRoot() const final { return m_heroicRoot; }

private:
    explicit Heroic(QObject *parent = nullptr);
    static Heroic *s_instance;

    void scanStore() final;

    QString m_heroicRoot;
};

class HeroicImageCache : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
