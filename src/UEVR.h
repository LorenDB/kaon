#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

#include "Game.h"

class UEVRRelease : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Model only object")

    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QDateTime timestamp READ timestamp CONSTANT)
    Q_PROPERTY(bool nightly READ nightly CONSTANT)
    Q_PROPERTY(bool installed READ installed NOTIFY installedChanged)

public:
    explicit UEVRRelease(const QJsonValue &json, bool nightly, QObject *parent = nullptr);

    struct Asset
    {
        int id;
        QString name;
        QUrl url;
    };

    int id() const { return m_id; }
    QString name() const { return m_name; }
    QDateTime timestamp() const { return m_timestamp; }
    bool nightly() const { return m_nightly; }
    bool installed() const { return m_installed; }
    const QList<Asset> &assets() const;

    void setInstalled(bool state);

signals:
    void installedChanged(bool state);

private:
    int m_id = 0;
    QString m_name;
    QDateTime m_timestamp;
    bool m_nightly = false;
    bool m_installed = false;
    QList<Asset> m_assets;
};

class UEVR : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString uevrPath READ uevrPath NOTIFY uevrPathChanged FINAL)
    Q_PROPERTY(UEVRRelease *currentUevr READ currentUevr NOTIFY currentUevrChanged FINAL)

public:
    ~UEVR();
    static UEVR *instance();
    static UEVR *create(QQmlEngine *, QJSEngine *);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    UEVRRelease *releaseFromId(const int id) const;

    const QString uevrPath() const;
    UEVRRelease *currentUevr() const;

    enum Roles
    {
        Id = Qt::UserRole + 1,
        Name,
        Timestamp,
        Installed,
    };

    enum class Paths
    {
        CurrentUEVR,
        CurrentUEVRInjector,
        UEVRBasePath,
        CachedReleasesJSON,
        CachedNightliesJSON,
    };
    QString path(const Paths path) const;

public slots:
    void launchUEVR(const Game *game);
    void downloadUEVR(UEVRRelease *uevr);
    void deleteUEVR(UEVRRelease *uevr);

    void setCurrentUevr(const int id);

signals:
    void uevrPathChanged(const QString &);
    void currentUevrChanged(UEVRRelease *);

private:
    explicit UEVR(QObject *parent = nullptr);
    static UEVR *s_instance;

    void updateAvailableReleases();
    void parseReleaseInfoJson();

    QList<UEVRRelease *> m_releases;
    UEVRRelease *m_currentUevr{nullptr};
};

class UEVRFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool showNightlies READ showNightlies WRITE setShowNightlies NOTIFY showNightliesChanged FINAL)

public:
    explicit UEVRFilter(QObject *parent = nullptr);

    bool showNightlies() const { return m_showNightlies; }
    Q_INVOKABLE int indexFromRelease(UEVRRelease *release) const;

    void setShowNightlies(bool state);

signals:
    void showNightliesChanged(bool state);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool m_showNightlies{false};
};
