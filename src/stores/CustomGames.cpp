#include "CustomGames.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#include "Aptabase.h"
#include "Wine.h"

Q_LOGGING_CATEGORY(CustomGameLog, "custom")

CustomGames *CustomGames::s_instance = nullptr;

class CustomGame : public Game
{
    Q_OBJECT

public:
    CustomGame(const QJsonObject &json, QObject *parent)
        : Game{parent}
    {
        m_id = json["id"_L1].toString();
        qCDebug(CustomGameLog) << "Creating game:" << m_id;

        m_name = json["name"_L1].toString();
        m_installDir = json["installDir"_L1].toString();
        m_winePrefix = json["winePrefix"_L1].toString(Wine::instance()->defaultWinePrefix());
        m_wineBinary = json["wineBinary"_L1].toString(Wine::instance()->whichWine());
        m_cardImage = json["cardImage"_L1].toString();
        m_heroImage = json["heroImage"_L1].toString();
        m_logoImage = json["logoImage"_L1].toString();
        m_icon = json["icon"_L1].toString();
        m_logoWidth = json["logoWidth"_L1].toDouble();
        m_logoHeight = json["logoHeight"_L1].toDouble();
        m_logoHPosition = json["logoHPosition"_L1].toVariant().value<LogoPosition>();
        m_logoVPosition = json["logoVPosition"_L1].toVariant().value<LogoPosition>();

        m_type = AppType::Game;
        m_canLaunch = true;

        LaunchOption lo;
        lo.executable = json["executable"_L1].toString();
        if (lo.executable.endsWith(".exe"_L1))
            lo.platform = LaunchOption::Platform::Windows;
        else
            lo.platform = LaunchOption::Platform::Linux;
        m_executables[0] = lo;

        detectGameEngine();
        detectAnticheat();

        m_valid = !m_installDir.isEmpty();
    }

    CustomGame(const QString &name, const QString &executable, QObject *parent)
        : CustomGame{name, executable, Wine::instance()->whichWine(), Wine::instance()->defaultWinePrefix(), parent}
    {}

    CustomGame(
        const QString &name, const QString &executable, const QString &wine, const QString &winePrefix, QObject *parent)
        : Game{parent}
    {
        m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        qCDebug(CustomGameLog) << "Creating game:" << m_id;

        m_name = name;
        QFileInfo fi{executable};
        m_installDir = fi.path();
        m_type = AppType::Game;
        m_canLaunch = true;
        m_wineBinary = wine;
        m_winePrefix = winePrefix;

        LaunchOption lo;
        lo.executable = executable;
        if (lo.executable.endsWith(".exe"_L1))
            lo.platform = LaunchOption::Platform::Windows;
        else
            lo.platform = LaunchOption::Platform::Linux;
        m_executables[0] = lo;

        detectGameEngine();
        detectAnticheat();

        m_valid = !m_installDir.isEmpty();
    }

    Store store() const override { return Store::Custom; }

    void launch() const override
    {
        const auto lo = m_executables.first();
        qCInfo(CustomGameLog) << "Launching" << lo.executable;

        if (lo.platform == LaunchOption::Platform::Windows)
            Wine::instance()->runInWine(m_name, this, lo.executable);
        else if (lo.platform == LaunchOption::Platform::Linux)
        {
            auto process = new QProcess;
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
            connect(process, &QProcess::finished, this, [=, this] {
                if (process->exitCode() != 0)
                    qCWarning(CustomGameLog) << "Running" << lo.executable << "failed with exit code" << process->exitCode()
                                             << "and error" << process->error();
            });
            process->start(lo.executable);
        }
    }

    QString executable() const
    {
        if (m_executables.isEmpty())
            return {};
        return m_executables.first().executable;
    }
};

CustomGames *CustomGames::instance()
{
    if (!s_instance)
        s_instance = new CustomGames;
    return s_instance;
}

CustomGames *CustomGames::create(QQmlEngine *qml, QJSEngine *js)
{
    return instance();
}

bool CustomGames::addGame(const QString &name, const QString &executable, const QString &wine, const QString &winePrefix)
{
    if (QFileInfo fi{executable}; fi.exists() && fi.isFile())
    {
        if (auto g = new CustomGame{name, executable, wine, winePrefix, this}; g->isValid())
        {
            beginInsertRows({}, m_games.size(), m_games.size());
            m_games.push_back(g);
            endInsertRows();
            writeConfig();
            return true;
        }
        else
            g->deleteLater();
    }

    return false;
}

void CustomGames::deleteGame(Game *game)
{
    if (!m_games.contains(game))
        return;

    const auto idx = m_games.indexOf(game);
    beginRemoveRows({}, idx, idx);
    m_games.removeAt(idx);
    endRemoveRows();
}

CustomGames::CustomGames(QObject *parent)
    : Store{parent}
{}

void CustomGames::scanStore()
{
    QFile m_config{QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/custom_games.json"_L1};
    if (m_config.exists())
    {
        if (!m_config.open(QIODevice::ReadOnly))
        {
            qCWarning(CustomGameLog) << "Failed to read custom games config";
            Aptabase::instance()->track("failed-loading-custom-games-bug");
            return;
        }

        qCDebug(CustomGameLog) << "Scanning custom library";
        beginResetModel();

        for (const auto game : std::as_const(m_games))
            game->deleteLater();
        m_games.clear();

        const auto arr = QJsonDocument::fromJson(m_config.readAll()).array();
        for (const auto &game : arr)
        {
            if (auto g = new CustomGame{game.toObject(), this}; g->isValid())
                m_games.push_back(g);
            else
                g->deleteLater();
        }

        endResetModel();
    }
}

void CustomGames::writeConfig()
{
    QFile m_config{QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/custom_games.json"_L1};

    if (!m_config.open(QIODevice::WriteOnly))
    {
        qCWarning(CustomGameLog) << "Failed to write custom games config";
        return;
    }

    QJsonArray arr;
    for (const auto game : std::as_const(m_games))
    {
        QJsonObject json;
        json["id"_L1] = game->id();
        json["name"_L1] = game->name();
        json["installDir"_L1] = game->installDir();
        json["winePrefix"_L1] = game->winePrefix();
        json["wineBinary"_L1] = game->wineBinary();
        json["cardImage"_L1] = game->cardImage();
        json["heroImage"_L1] = game->heroImage();
        json["logoImage"_L1] = game->logoImage();
        json["icon"_L1] = game->icon();
        json["logoWidth"_L1] = game->logoWidth();
        json["logoHeight"_L1] = game->logoHeight();
        json["logoHPosition"_L1] = game->logoHPosition();
        json["logoVPosition"_L1] = game->logoVPosition();
        if (const auto cg = dynamic_cast<CustomGame *>(game); cg)
            json["executable"_L1] = cg->executable();

        arr.push_back(json);
    }

    m_config.write(QJsonDocument{arr}.toJson());
    m_config.close();
}

#include "CustomGames.moc"
