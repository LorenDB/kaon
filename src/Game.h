#pragma once

#include <QObject>
#include <QQmlEngine>

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
    Q_PROPERTY(AppType type READ type CONSTANT)
    Q_PROPERTY(Store store READ store CONSTANT FINAL)
    Q_PROPERTY(bool supportsVr READ supportsVr CONSTANT FINAL)
    Q_PROPERTY(bool vrOnly READ vrOnly CONSTANT FINAL)

    Q_PROPERTY(QString cardImage READ cardImage CONSTANT)
    Q_PROPERTY(QString heroImage READ heroImage CONSTANT)
    Q_PROPERTY(QString logoImage READ logoImage CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
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
        Unreal = 1 << 1,
        Unity = 1 << 2,
        Godot = 1 << 3,
        Source = 1 << 4,
    };
    Q_ENUM(Engine)
    Q_DECLARE_FLAGS(Engines, Engine)

    enum class AppType
    {
        Game = 1,
        App = 1 << 1,
        Tool = 1 << 2,
        Demo = 1 << 3,
        Music = 1 << 4,
    };
    Q_ENUM(AppType)
    Q_DECLARE_FLAGS(AppTypes, AppType)

    enum class Store
    {
        Steam,
    };

    int id() const { return m_id; }
    QString name() const { return m_name; }
    QString installDir() const { return m_installDir; }
    QDateTime lastPlayed() const { return m_lastPlayed; }
    bool protonExists() const { return m_protonExists; }
    QString protonPrefix() const { return m_protonPrefix; }
    QString selectedProtonInstall() const { return m_selectedProtonInstall; }
    Engine engine() const { return m_engine; }
    AppType type() const { return m_type; }
    Store store() const { return m_store; }
    bool supportsVr() const { return m_supportsVr; }
    bool vrOnly() const { return m_vrOnly; }

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
    QString icon() const { return m_icon; }
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
    void detectGameEngine();

    int m_id = 0;
    QString m_name;
    QString m_installDir;
    QDateTime m_lastPlayed;
    bool m_protonExists{false};
    QString m_protonPrefix;
    QString m_selectedProtonInstall;
    Engine m_engine = Engine::Unknown;
    AppType m_type = AppType::Game;
    Store m_store;
    bool m_supportsVr{false};
    bool m_vrOnly{false};

    QString m_cardImage;
    QString m_heroImage;
    QString m_logoImage;
    QString m_icon;
    double m_logoWidth{0};
    double m_logoHeight{0};
    LogoPosition m_logoHPosition;
    LogoPosition m_logoVPosition;

    struct LaunchOption
    {
        QString executable;
        QString type;
    };

    QList<LaunchOption> m_executables;

    // Older Proton installs use a `dist` folder; newer installs use a `files` folder. We need to differentiate them.
    QString m_filesOrDist;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Game::Engines)
Q_DECLARE_OPERATORS_FOR_FLAGS(Game::AppTypes)
