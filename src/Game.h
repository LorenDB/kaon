#pragma once

#include <QObject>
#include <QQmlEngine>

class Game : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Model only object")

    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString installDir READ installDir CONSTANT)
    Q_PROPERTY(QDateTime lastPlayed READ lastPlayed CONSTANT)
    Q_PROPERTY(QString winePrefix READ winePrefix CONSTANT)
    Q_PROPERTY(QString wineBinary READ wineBinary NOTIFY wineBinaryChanged FINAL)
    Q_PROPERTY(AppType type READ type CONSTANT)
    Q_PROPERTY(Store store READ store CONSTANT FINAL)
    Q_PROPERTY(bool supportsVr READ supportsVr CONSTANT FINAL)
    Q_PROPERTY(bool vrOnly READ vrOnly CONSTANT FINAL)
    Q_PROPERTY(bool hasMultiplePlatforms READ hasMultiplePlatforms CONSTANT FINAL)
    Q_PROPERTY(bool noWindowsSupport READ noWindowsSupport CONSTANT FINAL)
    Q_PROPERTY(bool hasAnticheat READ hasAnticheat CONSTANT FINAL)

    Q_PROPERTY(QString cardImage READ cardImage CONSTANT)
    Q_PROPERTY(QString heroImage READ heroImage CONSTANT)
    Q_PROPERTY(QString logoImage READ logoImage CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
    Q_PROPERTY(double logoWidth READ logoWidth CONSTANT)
    Q_PROPERTY(double logoHeight READ logoHeight CONSTANT)
    Q_PROPERTY(LogoPosition logoHPosition READ logoHPosition CONSTANT)
    Q_PROPERTY(LogoPosition logoVPosition READ logoVPosition CONSTANT)

    // We can't always perform actions depending on what store the games are from.
    Q_PROPERTY(bool canLaunch MEMBER m_canLaunch CONSTANT FINAL)
    Q_PROPERTY(bool canOpenSettings MEMBER m_canOpenSettings CONSTANT FINAL)

public:
    enum Engine
    {
        UnknownEngine = 1,
        Unreal = 1 << 1,
        Unity = 1 << 2,
        Godot = 1 << 3,
        Source = 1 << 4,
    };
    Q_ENUM(Engine)
    Q_DECLARE_FLAGS(Engines, Engine)

    enum class Architecture
    {
        x86,
        x64,
        UnknownArch,
    };
    Q_ENUM(Architecture)

    enum class AppType
    {
        Game = 1,
        App = 1 << 1,
        Tool = 1 << 2,
        Demo = 1 << 3,
        Music = 1 << 4,
        Other = 1 << 5,
    };
    Q_ENUM(AppType)
    Q_DECLARE_FLAGS(AppTypes, AppType)

    enum class Store
    {
        Steam = 1,
        Itch = 1 << 1,
        Heroic = 1 << 2,
        Custom = 1 << 3,
    };
    Q_ENUM(Store)
    Q_DECLARE_FLAGS(Stores, Store)

    enum class Feature
    {
        Flatscreen = 1,
        VR = 1 << 1,
        // Anticheat is more of an antifeature for VR modding but it fits here, I guess
        Anticheat = 1 << 2,
    };
    Q_ENUM(Feature)
    Q_DECLARE_FLAGS(Features, Feature)

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString installDir() const { return m_installDir; }
    QDateTime lastPlayed() const { return m_lastPlayed; }
    QString winePrefix() const { return m_winePrefix; }
    QString wineBinary() const { return m_wineBinary; }
    Engine engine() const { return m_engine; }
    AppType type() const { return m_type; }
    Features features() const { return m_features; }
    bool supportsVr() const { return m_features.testFlag(Feature::VR); }
    bool vrOnly() const { return supportsVr() && !m_features.testFlag(Feature::Flatscreen); }
    bool hasMultiplePlatforms() const;
    bool noWindowsSupport() const;
    bool hasAnticheat() const { return m_features.testFlag(Feature::Anticheat); }

    virtual Store store() const = 0;

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

    // This is used to detect if a game has fully loaded or if there were errors parsing it.
    bool isValid() const { return m_valid; }

    Q_INVOKABLE bool hasValidWine() const;

    Q_INVOKABLE virtual void launch() const = 0;

signals:
    void winePrefixExistsChanged(bool state);
    void wineBinaryChanged(QString path);

protected:
    explicit Game(QObject *parent = nullptr);

    void detectGameEngine();
    void detectArchitectures();
    void detectAnticheat();

    QString m_id;
    QString m_name;
    QString m_installDir;
    QDateTime m_lastPlayed;
    QString m_winePrefix;
    QString m_wineBinary;
    AppType m_type = AppType::Other;
    Features m_features = Feature::Flatscreen;

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
        enum class Platform
        {
            Windows,
            Linux,
            MacOS,
        };

        Platform platform;
        Architecture arch = Architecture::UnknownArch;
        QString executable;
    };

    QMap<int, LaunchOption> m_executables;

    bool m_canLaunch{false};
    bool m_canOpenSettings{false};

    // Set this to true at the very end of your Game constructor
    bool m_valid = false;

private:
    Engine m_engine = Engine::UnknownEngine;
};
Q_DECLARE_METATYPE(Game)

Q_DECLARE_OPERATORS_FOR_FLAGS(Game::Engines)
Q_DECLARE_OPERATORS_FOR_FLAGS(Game::AppTypes)
