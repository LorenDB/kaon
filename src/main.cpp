#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>

#include "Aptabase.h"
#include "Itch.h"

using namespace Qt::Literals;

int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName("Kaon"_L1);
    QCoreApplication::setApplicationVersion("0.1.1"_L1);
    QCoreApplication::setOrganizationName("LorenDB"_L1);
    QCoreApplication::setOrganizationDomain("lorendb.dev"_L1);
    QApplication app(argc, argv);

    Aptabase::init("aptabase.lorendb.dev"_L1, "A-SH-5394792661"_L1);
    Aptabase::instance()->track("startup"_L1);

    QQmlApplicationEngine engine;
    QObject::connect(
                &engine,
                &QQmlApplicationEngine::objectCreationFailed,
                &app,
                []() { QCoreApplication::exit(-1); },
    Qt::QueuedConnection);
    engine.addImageProvider("itch-image", new ItchImageCache);
    engine.loadFromModule("dev.lorendb.kaon", "Main");

    app.setWindowIcon(QIcon::fromTheme("kaon", QIcon{":/qt/qml/dev/lorendb/kaon/icons/kaon.svg"}));
    return app.exec();
}
