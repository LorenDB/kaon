#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>

#include "Aptabase.h"
#include "Itch.h"
#include "Heroic.h"

using namespace Qt::Literals;

int main(int argc, char *argv[])
{
    qSetMessagePattern("[%{time}] [%{category}] %{type}: %{message}"_L1);

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
    engine.addImageProvider("itch-image"_L1, new ItchImageCache);
    engine.addImageProvider("heroic-image"_L1, new HeroicImageCache);
    engine.loadFromModule("dev.lorendb.kaon"_L1, "Main"_L1);

    app.setWindowIcon(QIcon::fromTheme("kaon"_L1, QIcon{":/qt/qml/dev/lorendb/kaon/icons/kaon.svg"_L1}));
    return app.exec();
}
