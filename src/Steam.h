#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

class Game : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Model only object")

    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString installDir READ installDir CONSTANT)
    Q_PROPERTY(QString cardImage READ cardImage CONSTANT)
    Q_PROPERTY(QString heroImage READ heroImage CONSTANT)
    Q_PROPERTY(QString logoImage READ logoImage CONSTANT)
    Q_PROPERTY(QDateTime lastPlayed READ lastPlayed CONSTANT)
    Q_PROPERTY(bool protonExists READ protonExists NOTIFY protonExistsChanged FINAL)
    Q_PROPERTY(QString protonPrefix READ protonPrefix CONSTANT)
    Q_PROPERTY(QString selectedProtonInstall READ selectedProtonInstall NOTIFY selectedProtonInstallChanged FINAL)

    Q_PROPERTY(bool dotnetInstalled READ dotnetInstalled NOTIFY dotnetInstalledChanged FINAL)

public:
    explicit Game(int steamId, QObject *parent = nullptr);

    int id() const { return m_id; }
    QString name() const { return m_name; }
    QString installDir() const { return m_installDir; }
    QString cardImage() const { return m_cardImage; }
    QString heroImage() const { return m_heroImage; }
    QString logoImage() const { return m_logoImage; }
    QDateTime lastPlayed() const { return m_lastPlayed; }
    bool protonExists() const { return m_protonExists; }
    QString protonPrefix() const { return m_protonPrefix; }
    QString selectedProtonInstall() const { return m_selectedProtonInstall; }

    QString protonBinary() const;

    bool dotnetInstalled() const;

signals:
    void protonExistsChanged(bool state);
    void selectedProtonInstallChanged(QString path);

    void dotnetInstalledChanged();

private:
    int m_id = 0;
    QString m_name;
    QString m_installDir;
    QString m_cardImage;
    QString m_heroImage;
    QString m_logoImage;
    QDateTime m_lastPlayed;
    bool m_protonExists{false};
    QString m_protonPrefix;
    QString m_selectedProtonInstall;

    // Older Proton installs use a `dist` folder; newer installs use a `files` folder. We need to differentiate them.
    QString m_filesOrDist;
};

class Steam : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Steam *instance();
    static Steam *create(QQmlEngine *qml, QJSEngine *js);

    enum Roles
    {
        Name = Qt::UserRole + 1,
        SteamID,
        InstallDir,
        CardImage,
        HeroImage,
        LogoImage,
        LastPlayed,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    Game *gameFromId(int steamId) const;

signals:

private:
    explicit Steam(QObject *parent = nullptr);
    static Steam *s_instance;

    void scanSteam();

    QList<Game *> m_games;
};

class SteamFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool showAll READ showAll WRITE setShowAll NOTIFY showAllChanged FINAL)

public:
    explicit SteamFilter(QObject *parent = nullptr);

    bool showAll() const { return m_showAll; }

    void setShowAll(bool state);

signals:
    void showAllChanged(bool state);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    bool m_showAll{false};
};
