#include "Store.h"

#include <exception>
#include <functional>

#include <QLoggingCategory>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>

#include "GamesFilterModel.h"

Q_LOGGING_CATEGORY(StoreLog, "store")

namespace
{
    int g_launchersLeft = 0;
    bool g_launchersReady = false;
    QList<std::function<void()>> g_launcherWaiters;

    void runAfterLaunchers(const std::function<void()> &fn)
    {
        if (g_launchersReady)
            fn();
        else
            g_launcherWaiters.push_back(fn);
    }
} // namespace

Store::Store(QObject *parent, Start start)
    : QAbstractListModel{parent}
{
    connect(this, &Store::rowsInserted, this, &Store::countChanged);
    connect(this, &Store::rowsRemoved, this, &Store::countChanged);
    connect(this, &Store::modelReset, this, &Store::countChanged);

    QSettings settings;
    m_autoscan = settings.value("autoscan"_L1, true).toBool();
    // Set before the window is built so the library does not flash "No games found" on the first frame.
    if (m_autoscan && start == Start::Now)
        m_scanning = true;

    GamesFilterModel::instance()->registerStore(this);

    // Don't you just love how constructors can't call virtual functions?
    QTimer::singleShot(0, this, [this, start] {
        if (!m_autoscan)
        {
            noteLauncherFinished();
            return;
        }
        if (start == Start::AfterLaunchers)
            runAfterLaunchers([this] { scanStore(); });
        else
            scanStore();
    });
}

int Store::rowCount(const QModelIndex &parent) const
{
    return m_games.count();
}

QVariant Store::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size())
        return {};
    const auto &item = m_games.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
        return item->name();
    case Roles::GameObject:
        return QVariant::fromValue(item);
    }

    return {};
}

QHash<int, QByteArray> Store::roleNames() const
{
    return {{Roles::GameObject, "game"_ba}};
}

int Store::count() const
{
    return m_games.count();
}

void Store::countAsLauncher()
{
    if (m_launcher)
        return;
    m_launcher = true;
    ++g_launchersLeft;
}

void Store::noteLauncherFinished()
{
    if (!m_launcher || m_launcherNoted)
        return;
    m_launcherNoted = true;
    if (--g_launchersLeft > 0)
        return;

    g_launchersReady = true;
    const auto waiting = g_launcherWaiters;
    g_launcherWaiters.clear();
    for (const auto &fn : waiting)
        fn();
}

void Store::scanAgainIfBusy()
{
    if (m_activeScan)
        m_scanAgain = true;
}

void Store::scanStore()
{
    if (m_activeScan)
    {
        m_scanAgain = true;
        return;
    }
    startScan();
}

void Store::startScan()
{
    m_activeScan = true;
    if (!m_scanning)
    {
        m_scanning = true;
        emit scanningChanged();
    }

    prepareScan();

    (void)QtConcurrent::run([this] {
        Q_ASSERT(QThread::currentThread() != thread());

        QList<Game *> games;
        auto publish = true;
        try
        {
            publish = readLibrary(games);
        }
        catch (const std::exception &e)
        {
            qCWarning(StoreLog) << "Library scan failed:" << e.what();
            publish = false;
        }
        catch (...)
        {
            qCWarning(StoreLog) << "Library scan failed";
            publish = false;
        }

        if (!publish)
        {
            for (auto *game : games)
                delete game;
            QMetaObject::invokeMethod(this, [this] { endScan(); }, Qt::QueuedConnection);
            return;
        }

        const auto uiThread = thread();
        for (auto *game : games)
            game->moveToThread(uiThread);
        QMetaObject::invokeMethod(this, [this, games] { publishScan(games); }, Qt::QueuedConnection);
    });
}

void Store::publishScan(const QList<Game *> &games)
{
    Q_ASSERT(QThread::currentThread() == thread());

    for (auto *game : games)
        game->setParent(this);

    beginResetModel();
    const auto old = m_games;
    m_games = games;
    endResetModel();

    for (auto *game : old)
        game->deleteLater();

    finishScan();
    endScan();
}

void Store::endScan()
{
    // Open the gate before clearing our own flag, so a store that was waiting starts in this same turn.
    noteLauncherFinished();
    m_activeScan = false;
    if (m_scanAgain)
    {
        m_scanAgain = false;
        startScan();
        return;
    }
    if (m_scanning)
    {
        m_scanning = false;
        emit scanningChanged();
    }
}
