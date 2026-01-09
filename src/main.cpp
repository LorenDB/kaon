#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QStandardPaths>

#include "Aptabase.h"
#include "CustomGames.h"
#include "Dotnet.h"
#include "GamesFilterModel.h"
#include "Heroic.h"
#include "Itch.h"
#include "Portal2VR.h"
#include "Steam.h"
#include "UEVR.h"
#include "UpdateChecker.h"
#include "Wine.h"

#if defined EXPERIMENTAL_UUVR_SUPPORT
#include "BepInExConfigManager.h"
#include "Bepinex.h"
#include "UUVR.h"
#endif

namespace
{
    QtMessageHandler ORIGINAL_HANDLER = nullptr;

#if defined(_DEBUG) || !defined(NDEBUG)
    bool DEBUG_TO_STDOUT = true;
#else
    bool DEBUG_TO_STDOUT = false;
#endif
} // namespace

// adapted from https://doc.qt.io/qt-6/qtlogging.html#qInstallMessageHandler
void logToFile(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString message = qFormatLogMessage(type, context, msg);
    static QFile logFile{QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/kaon.log"_L1};
    if (!logFile.isOpen())
        logFile.open(QIODevice::WriteOnly);
    if (logFile.isOpen())
    {
        logFile.write(message.toLatin1() + '\n');
        logFile.flush();
    }
    else
    {
        static bool hasWarned = false;
        if (!hasWarned)
            ORIGINAL_HANDLER(QtMsgType::QtCriticalMsg, {}, "Failed to open the log file!"_L1);
        hasWarned = true;
    }

    if (ORIGINAL_HANDLER && (DEBUG_TO_STDOUT || type != QtMsgType::QtDebugMsg))
        ORIGINAL_HANDLER(type, context, msg);
}

int main(int argc, char *argv[])
{
    ORIGINAL_HANDLER = qInstallMessageHandler(logToFile);
    qSetMessagePattern("[%{time}] [%{category}] %{type}: %{message}"_L1);

    QCoreApplication::setApplicationName("Kaon"_L1);
    QCoreApplication::setApplicationVersion(KAON_APP_VERSION);
    QCoreApplication::setOrganizationName("LorenDB"_L1);
    QCoreApplication::setOrganizationDomain("lorendb.dev"_L1);
    QApplication app(argc, argv);

    // These needs to exist
    QDir dirMaker;
    dirMaker.mkpath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    dirMaker.mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    dirMaker.mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    QCommandLineParser p;
    p.setApplicationDescription("VR mod manager"_L1);
    p.addHelpOption();
    p.addVersionOption();
    QCommandLineOption shouldLogDebug{"debug"_L1};
    p.addOption(shouldLogDebug);
    p.process(app);
    if (p.isSet(shouldLogDebug))
        DEBUG_TO_STDOUT = true;

    qInfo() << "This log only represents the most recent run of Kaon!";

    Aptabase::init("aptabase.lorendb.dev"_L1, "A-SH-5394792661"_L1);
    Aptabase::instance()->track("startup"_L1);

    QObject::connect(&app, &QApplication::aboutToQuit, &app, [] {
        qInfo() << "Shutting down";
        Aptabase::instance()->track("shutdown"_L1, {}, true);
    });

    // Here we will initialize all the QML singletons, because otherwise we might run into a situation where they don't get
    // initialized immediately. This is quite annoying, e.g. when a mod doesn't show up in the mod list since it hasn't been
    // referred to yet.

    // Steam and Heroic come first. This is because we scan them for potential fallback Wine/Proton binaries, but they need
    // to have loaded that information first. Initializing them first gives them a chance to do that.
    Steam::instance();
    Heroic::instance();

    CustomGames::instance();
    Dotnet::instance();
    GamesFilterModel::instance();
    Itch::instance();
    Portal2VR::instance();
    UEVR::instance();
    UpdateChecker::instance();
    Wine::instance();

#if defined EXPERIMENTAL_UUVR_SUPPORT
    Bepinex::instance();
    BepInExConfigManager::instance();
    UUVR::instance();
#endif

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.addImageProvider("itch-image"_L1, new ItchImageCache);
    engine.addImageProvider("heroic-image"_L1, new HeroicImageCache);
    engine.loadFromModule("dev.lorendb.kaon"_L1, "Main"_L1);

    app.setWindowIcon(QIcon::fromTheme("kaon"_L1, QIcon{":/qt/qml/dev/lorendb/kaon/qml/icons/kaon.svg"_L1}));
    return app.exec();
}
