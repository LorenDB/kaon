#include "Wine.h"

#include <QLoggingCategory>
#include <QProcess>

using namespace Qt::Literals;

Q_LOGGING_CATEGORY(WineLog, "wine")

Wine *Wine::s_instance = nullptr;

Wine::Wine(QObject *parent)
    : QObject{parent}
{}

Wine *Wine::instance()
{
    if (!s_instance)
        s_instance = new Wine;
    return s_instance;
}

Wine *Wine::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

void Wine::runInWine(const QString &prettyName,
                     const Game *wineRoot,
                     const QString &command,
                     const QStringList &args,
                     std::function<void()> successCallback,
                     std::function<void()> failureCallback) const
{
    QString commandLog = command;
    if (!args.empty())
        commandLog += args.join(' ');
    qCInfo(WineLog) << "Executing command" << commandLog << "via Wine" << wineRoot->protonBinary();

    auto process = new QProcess;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (wineRoot->protonPrefixExists())
    {
        env.insert("WINEPREFIX"_L1, wineRoot->protonPrefix());
        env.insert("STEAM_COMPAT_DATA_PATH"_L1, wineRoot->protonPrefix());
    }
    env.insert("WINEFSYNC"_L1, "1"_L1);
    process->setProcessEnvironment(env);

    connect(process, &QProcess::finished, this, [=] {
        if (process->exitCode() == 0)
            successCallback();
        else
        {
            // Special case for .NET installer being canceled
            if (command.endsWith("windowsdesktop-runtime-6.0.36-win-x64.exe"_L1) && process->exitCode() == 66)
            {
                successCallback();
                return;
            }

            qCWarning(WineLog) << "Running" << command << "with Wine" << wineRoot->protonBinary() << "failed with exit code"
                               << process->exitCode() << "and error" << process->error();
            failureCallback();
        }
    });
    connect(process, &QProcess::finished, process, &QObject::deleteLater);

    process->start(wineRoot->protonBinary(), QStringList{command} + args);
}
