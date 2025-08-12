#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QAbstractListModel>

class UEVR : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString uevrPath READ uevrPath NOTIFY uevrPathChanged FINAL)
    Q_PROPERTY(int currentUevr READ currentUevr WRITE setCurrentUevr NOTIFY currentUevrChanged FINAL)
    Q_PROPERTY(bool showNightlies MEMBER m_showNightlies NOTIFY showNightliesChanged FINAL)

public:
    explicit UEVR(QObject *parent = nullptr);
    ~UEVR();
    static UEVR *instance() { return s_instance; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QString uevrPath() const;
    int currentUevr() const;

    void setCurrentUevr(const int id);

public slots:
    void launchUEVR(const int steamId);
    bool isInstalled(const int id);
    void downloadUEVR(const int id);

    int indexFromId(const int id) const;

signals:
    void uevrPathChanged(const QString &);
    void currentUevrChanged(const int);
    void showNightliesChanged();

private:
    static UEVR *s_instance;

    enum class Paths
    {
        CurrentUEVR,
        CurrentUEVRInjector,
        UEVRBasePath,
        CachedReleasesJSON,
        CachedNightliesJSON,
    };
    QString path(const Paths path) const;

    enum Roles
    {
        Id = Qt::UserRole + 1,
        Name,
        Timestamp,
        Installed,
    };

    struct UEVRRelease
    {
        int id = 0;
        QString name;
        QDateTime timestamp;

        struct Asset
        {
            int id;
            QString name;
            QUrl url;
        };
        QList<Asset> assets;

        bool installed = false;
    };

    void updateAvailableReleases();
    void parseReleaseInfoJson();
    void downloadRelease(const UEVRRelease &release);

    QList<UEVRRelease> m_releases;
    QList<UEVRRelease> m_nightlies;
    QList<UEVRRelease> m_mergedReleases;

    bool m_showNightlies{false};

    int m_currentUevr = 0;
};
