#pragma once

#include <QJsonArray>
#include <QQmlEngine>
#include <QQuickAsyncImageProvider>
#include <QStringList>
#include <QVariant>

#include "Store.h"

class Heroic : public Store
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Heroic *instance();
    static Heroic *create(QQmlEngine *qml, QJSEngine *js);

    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY librariesChanged FINAL)

    QString storeRoot() const final { return m_heroicRoot; }
    QVariantList libraries() const;
    // Every config directory, so the image cache can be checked in each install.
    QStringList roots() const;

signals:
    void librariesChanged();

private:
    struct Install
    {
        QString path;
        QString flatpakAppId;
        QString name;
        int count = 0;
    };

    explicit Heroic(QObject *parent = nullptr);
    ~Heroic() = default;

    void scanStore() final;
    void discover(bool report);

    QString m_heroicRoot;
    QList<Install> m_installs;
};

class HeroicImageCache : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
