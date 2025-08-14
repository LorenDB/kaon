#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QDirIterator>

using namespace Qt::Literals;

int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName("Kaon"_L1);
    QCoreApplication::setApplicationVersion("0.0.1"_L1);
    QCoreApplication::setOrganizationName("LorenDB"_L1);
    QCoreApplication::setOrganizationDomain("lorendb.dev"_L1);
    QApplication app(argc, argv);

    QIcon::setThemeSearchPaths({":/icons"_L1});
    QIcon::setThemeName("fallback"_L1);
    // QIcon::setFallbackThemeName("fallback"_L1);

    // QDirIterator d(":/", QDirIterator::Subdirectories);
    // while (d.hasNext())
        // qDebug() << d.next();

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
