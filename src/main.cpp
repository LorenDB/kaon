#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QStandardPaths>

#include "Aptabase.h"
#include "Itch.h"
#include "Heroic.h"

using namespace Qt::Literals;

namespace
{
QtMessageHandler originalHandler = nullptr;
}

// adapted from https://doc.qt.io/qt-6/qtlogging.html#qInstallMessageHandler
void logToFile(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString message = qFormatLogMessage(type, context, msg);
    static QFile logFile{QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/kaon.log"_L1};
    if (!logFile.isOpen())
        logFile.open(QIODevice::WriteOnly);
    logFile.write(message.toLatin1() + '\n');
    logFile.flush();

    if (originalHandler)
        originalHandler(type, context, msg);
}

int main(int argc, char *argv[])
{
    originalHandler = qInstallMessageHandler(logToFile);
    qSetMessagePattern("[%{time}] [%{category}] %{type}: %{message}"_L1);

    QCoreApplication::setApplicationName("Kaon"_L1);
    QCoreApplication::setApplicationVersion("0.1.1"_L1);
    QCoreApplication::setOrganizationName("LorenDB"_L1);
    QCoreApplication::setOrganizationDomain("lorendb.dev"_L1);
    QApplication app(argc, argv);

    qInfo() << "This log only represents the most recent run of Kaon!";

    Aptabase::init("aptabase.lorendb.dev"_L1, "A-SH-5394792661"_L1);
    Aptabase::instance()->track("startup"_L1);

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

    app.setWindowIcon(QIcon::fromTheme("kaon"_L1, QIcon{":/qt/qml/dev/lorendb/kaon/icons/kaon.svg"_L1}));
    return app.exec();
}
