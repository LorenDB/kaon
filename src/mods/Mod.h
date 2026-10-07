#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVariant>
#include <QSortFilterProxyModel>
#include <QUrl>

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
    Q_PROPERTY(bool prerelease READ prerelease CONSTANT)
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
        // "sha256:<hex>" when whoever published the file also published its checksum. Empty otherwise.
        QString digest;

        // Whether data is the file this asset describes: the right size, and the right checksum where there is one.
        // why says what is wrong, in words meant for the user.
        bool matches(const QByteArray &data, QString *why) const;
    };

    ModRelease(int id,
               QString name,
               QDateTime timestamp,
               bool nightly,
               bool prerelease,
               bool downloaded,
               QList<Asset> assets,
               QObject *parent = nullptr);

    int id() const { return m_id; }
    QString name() const { return m_name; }
    QDateTime timestamp() const { return m_timestamp; }
    bool nightly() const { return m_nightly; }
    // Marked as a prerelease by whoever published it, e.g. a preview build
    bool prerelease() const { return m_prerelease; }
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
    bool m_prerelease = false;
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
    // Project homepage, e.g. the mod's GitHub page. Empty when the mod has nowhere to link to.
    Q_PROPERTY(QString homepage READ homepage CONSTANT FINAL)
    // Steam (or another launcher) options to paste in. Empty when the mod doesn't need any.
    Q_PROPERTY(QString launchOptions READ launchOptions CONSTANT FINAL)

    Q_PROPERTY(bool hasRepairOption READ hasRepairOption CONSTANT FINAL)
    Q_PROPERTY(bool providesVr READ providesVr CONSTANT FINAL)
    // Optional tools download like other mods. Each game turns one on itself, and it is not a setup step.
    Q_PROPERTY(bool optional READ optional CONSTANT FINAL)
    // Which VR runtime this tool works with, e.g. "OpenVR" or "OpenXR". Empty when the mod isn't a
    // runtime-specific tool. Shown as "Name · Runtime" on the game's page so players know what each tool supports.
    Q_PROPERTY(QString vrRuntime READ vrRuntime CONSTANT FINAL)
    // True when the mod loads per-game DLL plugins from the prefix, like UEVR does. The game's page then
    // offers to install them.
    Q_PROPERTY(bool supportsPlugins READ supportsPlugins CONSTANT FINAL)
    Q_PROPERTY(int launchDelay READ launchDelay WRITE setLaunchDelay NOTIFY launchDelayChanged FINAL)
    // What the list of releases holds, for the version menu
    Q_PROPERTY(bool hasNightlies READ hasNightlies NOTIFY releasesChanged FINAL)
    Q_PROPERTY(bool hasPrereleases READ hasPrereleases NOTIFY releasesChanged FINAL)
    Q_PROPERTY(int downloadedCount READ downloadedCount NOTIFY releasesChanged FINAL)

public:
    Mod(QObject *parent = nullptr);

    virtual QString displayName() const = 0;
    virtual QString settingsGroup() const = 0;
    virtual QString info() const { return {}; }
    // One plain sentence saying what the mod does
    virtual QString description() const { return {}; }
    virtual QString homepage() const { return {}; }
    virtual QString launchOptions() const { return {}; }
    // Arguments that keep the mod from loading, and so have to come out of a game's launch options
    virtual QStringList conflictingLaunchOptions() const { return {}; }
    // Soft checklist rows Kaon can't verify (in-game toggles, VRAM). Empty for most mods.
    virtual QVariantList softHints(const Game *game) const
    {
        Q_UNUSED(game)
        return {};
    }
    // Handles an action from softHints(). True when this mod handled it.
    virtual bool runHintAction(Game *game, const QString &action)
    {
        Q_UNUSED(game)
        Q_UNUSED(action)
        return false;
    }
    virtual const QLoggingCategory &logger() const = 0;

    virtual bool hasRepairOption() const = 0;

    // False for mods that only exist to support other mods, like the .NET runtime. Those aren't offered as a way to play
    // in VR; they show up as a setup step of the mod that needs them.
    virtual bool providesVr() const { return true; }
    virtual bool optional() const { return false; }
    virtual QString vrRuntime() const { return {}; }
    virtual bool supportsPlugins() const { return false; }
    // For an optional tool: what to tell the player while it is on for a game, e.g. where its settings are
    virtual QString installedNote() const { return {}; }
    // True if installing the mod runs something inside the game's Wine prefix, which therefore has to exist first.
    virtual bool installsIntoPrefix() const { return false; }
    // Non-empty when the mod fits this game but installation has to wait. Shown instead of the install button.
    Q_INVOKABLE virtual QString installHoldReason(const Game *game) const { return {}; }
    // Config file this install wrote, when the mod has one a player edits. Empty when it has none, or it isn't there.
    Q_INVOKABLE virtual QString configFileForGame(const Game *game) const
    {
        Q_UNUSED(game)
        return {};
    }
    // True for mods that bring VR into an Unreal game themselves, so that the VR plugins such a game ships with get in
    // their way. See UnrealVrPlugins.
    virtual bool conflictsWithBundledVrPlugins() const { return false; }
    // Native plugins this mod loads per game, e.g. UEVR's DLLs. Only meaningful when supportsPlugins() is true.
    // installPlugin and removePlugin return an error in words meant for the user, or empty on success.
    Q_INVOKABLE virtual QStringList installedPlugins(Game *game)
    {
        Q_UNUSED(game)
        return {};
    }
    Q_INVOKABLE virtual QString installPlugin(Game *game, const QUrl &source)
    {
        Q_UNUSED(game)
        Q_UNUSED(source)
        return {};
    }
    Q_INVOKABLE virtual QString removePlugin(Game *game, const QString &fileName)
    {
        Q_UNUSED(game)
        Q_UNUSED(fileName)
        return {};
    }
    Q_INVOKABLE virtual QString pluginHoldReason(Game *game)
    {
        Q_UNUSED(game)
        return {};
    }

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
    bool hasNightlies() const;
    bool hasPrereleases() const;

    // Set while something asynchronous, like an installer running in Wine, is working on this mod for a game.
    Q_INVOKABLE bool isBusyForGame(const Game *game) const;
    void setBusyForGame(const Game *game, bool busy);

    Q_INVOKABLE QString missingDependencies(const Game *game) const;

    ModRelease *currentRelease() const;
    Q_INVOKABLE ModRelease *releaseFromId(const int id) const;
    int downloadedCount() const;
    Q_INVOKABLE ModRelease *releaseInstalledForGame(const Game *game);
    // What to call a release where the mod isn't named next to it. "UEVR 1.05" stays as it is; a bare "0.4.0" becomes
    // "UUVR 0.4.0".
    QString releaseTitle(const ModRelease *release) const;
    // The other way round, for right beside the mod's name: "UEVR 1.05" becomes "1.05"
    Q_INVOKABLE QString releaseLabel(const ModRelease *release) const;

    // Override this to apply filters to both the entire game and individual executables
    virtual QMap<int, Game::LaunchOption> acceptableInstallCandidates(const Game *game) const;
    // Asks for the list of releases again. Mods fetch it by themselves on startup; this is for trying again later.
    Q_INVOKABLE virtual void refreshReleases() {}

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
    // The list of releases was rebuilt, or one of them was downloaded or deleted
    void releasesChanged();
    void installedInGameChanged(Game *game);
    void requestChooseLaunchOption(GameExecutablePickerModel *m);
    void releaseDownloadedChanged(ModRelease *release);
    void busyChanged();
    void launchDelayChanged();
    // An install gave up. The message says why, in words meant for the user.
    void installFailed(const QString &message);
    // A download arrived but could not be used, e.g. it was cut short or would not unpack
    void downloadFailed(const QString &message);
    // The mod is still in the game after an attempt to take it out
    void uninstallFailed(const QString &message);

protected:
    // Override this to implement the actual installation logic. Your implementation must call this base function at its end!
    virtual void installModImpl(Game *game, const Game::LaunchOption &exe);
    // Override this to implement the actual launch. Only called with a game and a current release.
    virtual void launchModImpl(Game *game) {}

    // Call this when an install can't go on. Logs the message and shows it to the user.
    void fail(const QString &message);
    // The same for a download that arrived and turned out to be unusable
    void failDownload(const QString &message);
    // And for an uninstall that left the mod where it was
    void failUninstall(const QString &message);

    // Override this to leave out executables the mod could be installed for, but shouldn't be when there is a better one.
    // Only asked when there is more than one. Must not return an empty list.
    virtual QList<Game::LaunchOption> preferredInstallCandidates(const Game *game,
                                                                 const QList<Game::LaunchOption> &all) const
    {
        return all;
    }

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
    Q_PROPERTY(bool showPrereleases READ showPrereleases WRITE setShowPrereleases NOTIFY showPrereleasesChanged FINAL)

public:
    explicit ModReleaseFilter(QObject *parent = nullptr);

    Mod *mod() const { return m_mod; }
    bool showNightlies() const { return m_showNightlies; }
    bool showPrereleases() const { return m_showPrereleases; }
    Q_INVOKABLE int indexFromRelease(ModRelease *release) const;

    void setMod(Mod *mod);
    void setShowNightlies(bool state);
    void setShowPrereleases(bool state);

signals:
    void modChanged(Mod *mod);
    void showNightliesChanged(bool state);
    void showPrereleasesChanged(bool state);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    Mod *m_mod{nullptr};
    bool m_showNightlies{false};
    bool m_showPrereleases{false};
};
