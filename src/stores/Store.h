#pragma once

#include <QAbstractListModel>
#include <QList>

#include "Game.h"

class Store : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString storeRoot READ storeRoot CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    // True from the moment a scan is requested until its games have been published on the UI thread.
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged FINAL)

public:
    enum Roles
    {
        GameObject = Qt::UserRole + 1,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const final;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const final;
    QHash<int, QByteArray> roleNames() const final;

    QList<Game *> games() const { return m_games; }

    virtual QString storeRoot() const = 0;
    // Starts a scan and returns immediately. A scan that is already running is followed by one more.
    Q_INVOKABLE void scanStore();
    int count() const;
    bool scanning() const { return m_scanning; }

signals:
    void countChanged();
    void scanningChanged();

protected:
    enum class Start
    {
        // Scan on the next turn of the event loop.
        Now,
        // Wait until Steam and Heroic have each finished a scan, so Proton binaries exist to fall back on.
        AfterLaunchers,
    };

    explicit Store(QObject *parent = nullptr, Start start = Start::Now);

    // Steam and Heroic call this so AfterLaunchers stores know when both libraries have been published.
    void countAsLauncher();

    // UI thread, before the worker starts. Snapshot anything the worker needs from other UI-thread objects.
    virtual void prepareScan() {}
    // Worker thread. Append parentless Game objects. Do not touch the model, m_games, or objects that live on the UI thread.
    // Return false to leave the current games in place, for example when that store is not installed.
    virtual bool readLibrary(QList<Game *> &games) = 0;
    // UI thread, after the new games are in the model and before scanning goes back to false.
    virtual void finishScan() {}

    // The in-flight scan read the library before this change. Run another scan when it publishes.
    void scanAgainIfBusy();

    QList<Game *> m_games;

private:
    void startScan();
    void publishScan(const QList<Game *> &games);
    void endScan();
    void noteLauncherFinished();

    bool m_autoscan = true;
    bool m_scanning = false;
    bool m_activeScan = false;
    bool m_scanAgain = false;
    bool m_launcher = false;
    bool m_launcherNoted = false;
};
