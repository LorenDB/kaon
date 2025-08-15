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
    Q_PROPERTY(QDateTime lastPlayed READ lastPlayed CONSTANT)
    Q_PROPERTY(bool protonExists READ protonExists NOTIFY protonExistsChanged FINAL)
    Q_PROPERTY(QString protonPrefix READ protonPrefix CONSTANT)
    Q_PROPERTY(QString selectedProtonInstall READ selectedProtonInstall NOTIFY selectedProtonInstallChanged FINAL)

    Q_PROPERTY(QString cardImage READ cardImage CONSTANT)
    Q_PROPERTY(QString heroImage READ heroImage CONSTANT)
    Q_PROPERTY(QString logoImage READ logoImage CONSTANT)
    Q_PROPERTY(double logoWidth READ logoWidth CONSTANT)
    Q_PROPERTY(double logoHeight READ logoHeight CONSTANT)
    Q_PROPERTY(LogoPosition logoHPosition READ logoHPosition CONSTANT)
    Q_PROPERTY(LogoPosition logoVPosition READ logoVPosition CONSTANT)

    Q_PROPERTY(bool dotnetInstalled READ dotnetInstalled NOTIFY dotnetInstalledChanged FINAL)

public:
    explicit Game(int steamId, QObject *parent = nullptr);

    enum Engine
    {
        Unknown = 1,
        Runtime = 1 << 1, // Also encapsulates Proton

        Unreal = 1 << 2,
        Unity = 1 << 3,
        Godot = 1 << 4,
        Source = 1 << 5,
    };
    Q_ENUM(Engine)
    Q_DECLARE_FLAGS(Engines, Engine)

    int id() const { return m_id; }
    QString name() const { return m_name; }
    QString installDir() const { return m_installDir; }
    QDateTime lastPlayed() const { return m_lastPlayed; }
    bool protonExists() const { return m_protonExists; }
    QString protonPrefix() const { return m_protonPrefix; }
    QString selectedProtonInstall() const { return m_selectedProtonInstall; }
    Engine engine() const { return m_engine; }

    enum LogoPosition
    {
        Center,
        Top,
        Bottom,
        Left,
        Right,
    };
    Q_ENUM(LogoPosition)

    QString cardImage() const { return m_cardImage; }
    QString heroImage() const { return m_heroImage; }
    QString logoImage() const { return m_logoImage; }
    double logoWidth() const { return m_logoWidth; }
    double logoHeight() const { return m_logoHeight; }
    LogoPosition logoHPosition() const { return m_logoHPosition; }
    LogoPosition logoVPosition() const { return m_logoVPosition; }

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
    QDateTime m_lastPlayed;
    bool m_protonExists{false};
    QString m_protonPrefix;
    QString m_selectedProtonInstall;
    Engine m_engine = Engine::Unknown;

    QString m_cardImage;
    QString m_heroImage;
    QString m_logoImage;
    double m_logoWidth{0};
    double m_logoHeight{0};
    LogoPosition m_logoHPosition;
    LogoPosition m_logoVPosition;

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

    QString steamRoot() const { return m_steamRoot; }

public slots:
    Game *gameFromId(int steamId) const;

signals:

private:
    explicit Steam(QObject *parent = nullptr);
    static Steam *s_instance;

    void scanSteam();

    QString m_steamRoot;
    QList<Game *> m_games;
};

class SteamFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Game::Engines engineFilter READ engineFilter NOTIFY engineFilterChanged FINAL)

public:
    explicit SteamFilter(QObject *parent = nullptr);

    Game::Engines engineFilter() const { return m_engineFilter; }

    void setshowTools(bool state);

    Q_INVOKABLE bool isEngineFilterSet(Game::Engine engine);
    Q_INVOKABLE void setEngineFilter(Game::Engine engine, bool state);

signals:
    void showToolsChanged(bool state);
    void engineFilterChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    Game::Engines m_engineFilter;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Game::Engines)
