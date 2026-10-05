#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QSortFilterProxyModel>

#include "Game.h"

class GameExecutablePickerModel;

class ModRelease : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Model only object")

    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QDateTime timestamp READ timestamp CONSTANT)
    Q_PROPERTY(bool nightly READ nightly CONSTANT)
    Q_PROPERTY(bool downloaded READ downloaded NOTIFY downloadedChanged)
    Q_PROPERTY(qint64 size READ size CONSTANT FINAL)

public:
    struct Asset
    {
        int id = -1;
        QString name;
        QUrl url;
        QDateTime timestamp;
        int size = 0;
    };

    ModRelease(int id,
               QString name,
               QDateTime timestamp,
               bool nightly,
               bool downloaded,
               QList<Asset> assets,
               QObject *parent = nullptr);

    int id() const { return m_id; }
    QString name() const { return m_name; }
    QDateTime timestamp() const { return m_timestamp; }
    bool nightly() const { return m_nightly; }
    bool downloaded() const { return m_downloaded; }
    QList<Asset> assets() const { return m_assets; }
    // Total download size in bytes, or 0 if GitHub didn't say
    qint64 size() const;

    virtual void setDownloaded(bool state);

signals:
    void downloadedChanged(bool state);

private:
    int m_id = 0;
    QString m_name;
    QDateTime m_timestamp;
    bool m_nightly = false;
    bool m_downloaded = false;
    QList<Asset> m_assets;
};

class Mod : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Model only object")

    Q_PROPERTY(QString name READ displayName CONSTANT FINAL)
    Q_PROPERTY(QString settingsGroup READ settingsGroup CONSTANT FINAL)
    Q_PROPERTY(Type type READ type CONSTANT FINAL)
    Q_PROPERTY(ModRelease *currentRelease READ currentRelease NOTIFY currentReleaseChanged FINAL)
    Q_PROPERTY(QString info READ info CONSTANT FINAL)
    Q_PROPERTY(QString description READ description CONSTANT FINAL)
    // Steam (or another launcher) options to paste in. Empty when the mod doesn't need any.
    Q_PROPERTY(QString launchOptions READ launchOptions CONSTANT FINAL)

    Q_PROPERTY(bool hasRepairOption READ hasRepairOption CONSTANT FINAL)
    Q_PROPERTY(bool providesVr READ providesVr CONSTANT FINAL)
    Q_PROPERTY(int launchDelay READ launchDelay WRITE setLaunchDelay NOTIFY launchDelayChanged FINAL)

public:
    Mod(QObject *parent = nullptr);

    virtual QString displayName() const = 0;
    virtual QString settingsGroup() const = 0;
    virtual QString info() const { return {}; }
    // One plain sentence saying what the mod does
    virtual QString description() const { return {}; }
    virtual QString launchOptions() const { return {}; }
    virtual const QLoggingCategory &logger() const = 0;

    virtual bool hasRepairOption() const = 0;

    // False for mods that only exist to support other mods, like the .NET runtime. Those aren't offered as a way to play
    // in VR; they show up as a setup step of the mod that needs them.
    virtual bool providesVr() const { return true; }
    // True if installing the mod runs something inside the game's Wine prefix, which therefore has to exist first.
    virtual bool installsIntoPrefix() const { return false; }
    // Non-empty when the mod fits this game but installation has to wait. Shown instead of the install button.
    virtual QString installHoldReason(const Game *game) const { return {}; }
    // True for mods that bring VR into an Unreal game themselves, so that the VR plugins such a game ships with get in
    // their way. See UnrealVrPlugins.
    virtual bool conflictsWithBundledVrPlugins() const { return false; }

    // Seconds to wait between starting a game and launching this mod into it
    int launchDelay() const;
    void setLaunchDelay(int seconds);

    enum class Type
    {
        Launchable = 1,
        Installable = 1 << 1,
    };
    Q_ENUM(Type)

    virtual Type type() const = 0;
    virtual Game::Engines compatibleEngines() const = 0;
    Q_INVOKABLE bool supportsEngine(Game::Engine engine) const { return compatibleEngines().testFlag(engine); }
    virtual QList<Mod *> dependencies() const { return {}; }

    Q_INVOKABLE virtual bool isInstalledForGame(const Game *game) const = 0;
    Q_INVOKABLE bool dependenciesSatisfied(const Game *game) const;
    Q_INVOKABLE bool isCompatibleWith(const Game *game) const
    {
        return game && !acceptableInstallCandidates(game).isEmpty();
    }
    Q_INVOKABLE bool hasNightlies() const;

    // Set while something asynchronous, like an installer running in Wine, is working on this mod for a game.
    Q_INVOKABLE bool isBusyForGame(const Game *game) const;
    void setBusyForGame(const Game *game, bool busy);

    Q_INVOKABLE QString missingDependencies(const Game *game) const;

    ModRelease *currentRelease() const;
    Q_INVOKABLE ModRelease *releaseFromId(const int id) const;
    Q_INVOKABLE int downloadedCount() const;
    Q_INVOKABLE ModRelease *releaseInstalledForGame(const Game *game);

    // Override this to apply filters to both the entire game and individual executables
    virtual QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    enum Roles
    {
        Id = Qt::UserRole + 1,
        Name,
        Timestamp,
        Downloaded,
    };

public slots:
    virtual void downloadRelease(ModRelease *release) = 0;
    virtual void deleteRelease(ModRelease *release) = 0;

    void launchMod(Game *game);
    void installMod(Game *game);
    virtual void uninstallMod(Game *game);

    void setCurrentRelease(const int id);

signals:
    void currentReleaseChanged(ModRelease *);
    void installedInGameChanged(Game *game);
    void requestChooseLaunchOption(GameExecutablePickerModel *m);
    void releaseDownloadedChanged(ModRelease *release);
    void busyChanged();
    void launchDelayChanged();
    // An install gave up. The message says why, in words meant for the user.
    void installFailed(const QString &message);

protected:
    // Override this to implement the actual installation logic. Your implementation must call this base function at its end!
    virtual void installModImpl(Game *game, const Game::LaunchOption &exe);
    // Override this to implement the actual launch. Only called with a game and a current release.
    virtual void launchModImpl(Game *game) {}

    // Call this when an install can't go on. Logs the message and shows it to the user.
    void fail(const QString &message);

    // Use this if you need to have whatever the settings had at startup, e.g. if you need to download release information
    // before you can build the release list
    int m_lastCurrentReleaseId = 0;

private:
    virtual QList<ModRelease *> releases() const = 0;
    ModRelease *m_currentRelease{nullptr};
    QSet<const Game *> m_busyGames;
};

class ModReleaseFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Mod *mod READ mod WRITE setMod NOTIFY modChanged FINAL)
    Q_PROPERTY(bool showNightlies READ showNightlies WRITE setShowNightlies NOTIFY showNightliesChanged FINAL)

public:
    explicit ModReleaseFilter(QObject *parent = nullptr);

    Mod *mod() const { return m_mod; }
    bool showNightlies() const { return m_showNightlies; }
    Q_INVOKABLE int indexFromRelease(ModRelease *release) const;

    void setMod(Mod *mod);
    void setShowNightlies(bool state);

signals:
    void modChanged(Mod *mod);
    void showNightliesChanged(bool state);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    Mod *m_mod{nullptr};
    bool m_showNightlies{false};
};
