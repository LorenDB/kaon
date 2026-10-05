#pragma once

#include <QDateTime>
#include <QQmlEngine>
#include <QStringList>
#include <QVariant>

#include "Store.h"

class QFileSystemWatcher;
class QTimer;

class Steam : public Store
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool hasSteamVR READ hasSteamVR NOTIFY hasSteamVRChanged FINAL)
    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY librariesChanged FINAL)

public:
    static Steam *instance();
    static Steam *create(QQmlEngine *qml, QJSEngine *js);

    QString storeRoot() const final { return m_steamRoot; }
    QVariantList libraries() const;
    bool hasSteamVR() const { return m_hasSteamVR; }

    Q_INVOKABLE void launchSteamVR();

signals:
    void hasSteamVRChanged(bool state);
    void librariesChanged();

private:
    struct Install
    {
        QString path;
        QString flatpakAppId;
        QString name;
        int count = 0;
        bool hasSteamVR = false;
    };

    explicit Steam(QObject *parent = nullptr);
    ~Steam() = default;

    void prepareScan() final;
    bool readLibrary(QList<Game *> &games) final;
    void finishScan() final;
    void discover(bool report);
    void ensureLibraryWatch();
    void applyLibraryWatch(const QStringList &paths);
    void onLibraryChanged(const QString &path);
    void refreshLibrary();

    QString m_steamRoot;
    QList<Install> m_installs;
    QList<Install> m_scannedInstalls;
    bool m_hasSteamVR = false;
    bool m_scannedSteamVR = false;

    // appmanifest files change while a download runs. The watch debounces that and rescans when the
    // installed set itself changes, so a new game does not wait for Steam to exit.
    QFileSystemWatcher *m_libraryWatcher = nullptr;
    QTimer *m_libraryRefresh = nullptr;
    bool m_libraryDirty = false;
    bool m_waitForAppInfo = false;
    bool m_scannedWaitForAppInfo = false;
    QString m_librarySignature;
    QString m_scannedSignature;
    QDateTime m_appInfoMtime;
    QDateTime m_scannedAppInfoMtime;
    QStringList m_watchPaths;
    QStringList m_scannedWatchPaths;
};
