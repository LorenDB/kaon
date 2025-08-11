#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QAbstractListModel>

class UEVR : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString uevrPath READ uevrPath WRITE setUevrPath NOTIFY uevrPathChanged FINAL)
    Q_PROPERTY(int currentUevr READ currentUevr WRITE setCurrentUevr NOTIFY currentUevrChanged FINAL)
    Q_PROPERTY(bool showNightlies MEMBER m_showNightlies NOTIFY showNightliesChanged FINAL)

public:
    explicit UEVR(QObject *parent = nullptr);
    static UEVR *instance() { return s_instance; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QString &uevrPath() const;
    int currentUevr() const;

    void setUevrPath(const QString &path);
    void setCurrentUevr(const int id);

public slots:
    void installDotnetDesktopRuntime(int steamId);
    void launchUEVR(const int steamId);

signals:
    void uevrPathChanged(const QString &);
    void currentUevrChanged(const int);
    void showNightliesChanged();

    void downloadedDotnetRuntime();
    void dotnetDownloadFailed();

private:
    static UEVR *s_instance;

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
    void downloadRelease(const UEVRRelease &release) const;
    void downloadDotnetDesktopRuntime();

    void rebuild();

    QList<UEVRRelease> m_releases;
    QList<UEVRRelease> m_nightlies;
    QList<UEVRRelease> m_mergedReleases;

    bool m_showNightlies{false};

    QString m_uevrPath;
    int m_currentUevr = 0;
};
