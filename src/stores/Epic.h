#pragma once

#include <QQmlEngine>
#include <QQuickAsyncImageProvider>
#include <QVariant>

#include "Store.h"

class Epic : public Store
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Epic *instance();
    static Epic *create(QQmlEngine *qml, QJSEngine *js);

    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY librariesChanged FINAL)

    QString storeRoot() const final { return m_epicRoot; }
    QVariantList libraries() const;

signals:
    void librariesChanged();

private:
    struct Install
    {
        QString path;
        QString name;
        int count = 0;
    };

    explicit Epic(QObject *parent = nullptr);
    ~Epic() = default;

    void prepareScan() final;
    bool readLibrary(QList<Game *> &games) final;
    void finishScan() final;
    void discover(bool report);

    QString m_epicRoot;
    QList<Install> m_installs;
    QList<Install> m_scannedInstalls;
};

class EpicImageCache : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
