#pragma once

#include <QQmlEngine>
#include <QQuickAsyncImageProvider>

#include "Store.h"

class Itch : public Store
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Itch *instance();
    static Itch *create(QQmlEngine *qml, QJSEngine *js) { return instance(); }

    QString storeRoot() const final { return m_itchRoot; }

private:
    explicit Itch(QObject *parent = nullptr);
    ~Itch() = default;

    void prepareScan() final;
    bool readLibrary(QList<Game *> &games) final;

    QString m_itchRoot;
    QString m_defaultWine;
    QString m_defaultPrefix;
};

class ItchImageCache : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
