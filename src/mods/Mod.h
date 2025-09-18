#pragma once

#include <QAbstractListModel>
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
    Q_PROPERTY(QUrl downloadUrl READ downloadUrl CONSTANT FINAL)

public:
    ModRelease(int id,
               QString name,
               QDateTime timestamp,
               bool nightly,
               bool downloaded,
               QUrl downloadUrl,
               QObject *parent = nullptr);

    int id() const { return m_id; }
    QString name() const { return m_name; }
    QDateTime timestamp() const { return m_timestamp; }
    bool nightly() const { return m_nightly; }
    bool downloaded() const { return m_downloaded; }
    QUrl downloadUrl() const { return m_downloadUrl; }

    virtual void setDownloaded(bool state);

signals:
    void downloadedChanged(bool state);

private:
    int m_id = 0;
    QString m_name;
    QDateTime m_timestamp;
    bool m_nightly = false;
    bool m_downloaded = false;
    QUrl m_downloadUrl;
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

    Q_PROPERTY(bool hasRepairOption READ hasRepairOption CONSTANT FINAL)

public:
    Mod(QObject *parent = nullptr);

    virtual QString displayName() const = 0;
    virtual QString settingsGroup() const = 0;
    virtual QString info() const { return {}; }
    virtual const QLoggingCategory &logger() const = 0;

    virtual bool hasRepairOption() const = 0;

    enum class Type
    {
        Launchable = 1,
        Installable = 1 << 1,
    };
    Q_ENUM(Type)

    virtual Type type() const = 0;
    virtual Game::Engines compatibleEngines() const = 0;
    virtual QList<Mod *> dependencies() const { return {}; }

    Q_INVOKABLE virtual bool isInstalledForGame(const Game *game) const = 0;
    Q_INVOKABLE bool dependenciesSatisfied(const Game *game) const;

    Q_INVOKABLE QString missingDependencies(const Game *game) const;

    ModRelease *currentRelease() const;
    ModRelease *releaseFromId(const int id) const;
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

    virtual void launchMod(Game *game) {}
    void installMod(Game *game);
    virtual void uninstallMod(Game *game);

    void setCurrentRelease(const int id);

signals:
    void currentReleaseChanged(ModRelease *);
    void installedInGameChanged(Game *game);
    void requestChooseLaunchOption(GameExecutablePickerModel *m);

protected:
    // Override this to implement the actual installation logic. Your implementation must call this base function at its end!
    virtual void installModImpl(Game *game, const Game::LaunchOption &exe);

    // Use this if you need to have whatever the settings had at startup, e.g. if you need to download release information
    // before you can build the release list
    int m_lastCurrentReleaseId = 0;

private:
    virtual QList<ModRelease *> releases() const = 0;
    ModRelease *m_currentRelease{nullptr};
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
    Mod *m_mod;
    bool m_showNightlies{false};
};
