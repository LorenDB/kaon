#include <QApplication>
#include <QQmlApplicationEngine>

using namespace Qt::Literals;

int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName("Kaon"_L1);
    QCoreApplication::setApplicationVersion("0.0.1"_L1);
    QCoreApplication::setOrganizationName("LorenDB"_L1);
    QCoreApplication::setOrganizationDomain("lorendb.dev"_L1);
    QApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QObject::connect(
                &engine,
                &QQmlApplicationEngine::objectCreationFailed,
                &app,
                []() { QCoreApplication::exit(-1); },
    Qt::QueuedConnection);
    engine.loadFromModule("dev.lorendb.kaon", "Main");

    return app.exec();
}
