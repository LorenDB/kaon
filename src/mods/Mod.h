#pragma once

#include <QAbstractListModel>

#include "Game.h"

class ModRelease : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Model only object")

    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QDateTime timestamp READ timestamp CONSTANT)
    Q_PROPERTY(bool nightly READ nightly CONSTANT)
    Q_PROPERTY(bool installed READ installed NOTIFY installedChanged)
    Q_PROPERTY(QUrl downloadUrl READ downloadUrl CONSTANT FINAL)

public:
    ModRelease(int id,
               QString name,
               QDateTime timestamp,
               bool nightly,
               bool installed,
               QUrl downloadUrl,
               QObject *parent = nullptr);

    int id() const { return m_id; }
    QString name() const { return m_name; }
    QDateTime timestamp() const { return m_timestamp; }
    bool nightly() const { return m_nightly; }
    bool installed() const { return m_installed; }
    QUrl downloadUrl() const { return m_downloadUrl; }

    virtual void setInstalled(bool state);

signals:
    void installedChanged(bool state);

private:
    int m_id = 0;
    QString m_name;
    QDateTime m_timestamp;
    bool m_nightly = false;
    bool m_installed = false;
    QUrl m_downloadUrl;
};

class Mod : public QAbstractListModel
{
    Q_OBJECT

public:
    Mod(QObject *parent = nullptr);

    virtual Game::Engines compatibleEngines() const = 0;
    virtual QList<Mod *> dependencies() const { return {}; }

    // By default, just checks against engine compatibility. You may need to override to check for specific games.
    virtual bool checkGameCompatibility(Game *game);
    virtual bool isInstalledForGame(const Game *game) const = 0;

    Q_INVOKABLE bool dependenciesSatisfied(const Game *game) const;

    ModRelease *releaseFromId(const int id) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    enum Roles
    {
        Id = Qt::UserRole + 1,
        Name,
        Timestamp,
        Installed,
    };

private:
    virtual QList<ModRelease *> releases() const = 0;
};
