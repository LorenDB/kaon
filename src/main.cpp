#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

#include "Aptabase.h"
#include "Heroic.h"
#include "Itch.h"
#include "UpdateChecker.h"

using namespace Qt::Literals;

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
    QCoreApplication::setApplicationVersion("0.2.2"_L1);
    QCoreApplication::setOrganizationName("LorenDB"_L1);
    QCoreApplication::setOrganizationDomain("lorendb.dev"_L1);
    QApplication app(argc, argv);

    // These needs to exist
    QDir dirMaker;
    dirMaker.mkpath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    dirMaker.mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));

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

    QObject::connect(&app, &QApplication::aboutToQuit, &app, [] {
        qInfo() << "Shutting down";
        Aptabase::instance()->track("shutdown"_L1, {}, true);
    });

    Aptabase::init("aptabase.lorendb.dev"_L1, "A-SH-5394792661"_L1);
    Aptabase::instance()->track("startup"_L1);

    UpdateChecker::instance();

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
