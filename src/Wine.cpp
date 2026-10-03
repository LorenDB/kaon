#include "Wine.h"

#include <QDir>
#include <QLoggingCategory>
#include <QProcess>

#include "Aptabase.h"
#include "Heroic.h"
#include "Steam.h"

Q_LOGGING_CATEGORY(WineLog, "wine")

Wine::Wine(QObject *parent)
    : QObject{parent}
{}

Wine *Wine::instance()
{
    static auto w = new Wine;
    return w;
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
                     std::function<void()> failureCallback)
{
    if (!wineRoot)
    {
        Aptabase::instance()->track("null-wine-game-bug"_L1, {{"command"_L1, command}});
        emit processFailed(prettyName);
        failureCallback();
        return;
    }
    else if (!wineRoot->hasValidWine())
    {
        Aptabase::instance()->track(
            "empty-wine-binary-bug"_L1,
            {{"command"_L1, command},
             {"game"_L1, wineRoot->id()},
             {"store"_L1, QMetaEnum::fromType<Game::Store>().valueToKey(static_cast<quint64>(wineRoot->store()))}});
        emit processFailed(prettyName);
        failureCallback();
        return;
    }

    if (command.isEmpty())
    {
        failureCallback();
        return;
    }

    QString commandLog = command;
    if (!args.empty())
        commandLog += args.join(' ');
    qCInfo(WineLog) << "Executing command" << commandLog << "via Wine" << wineRoot->wineBinary();

    auto process = new QProcess;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("WINEPREFIX"_L1, wineRoot->winePrefix());
    env.insert("STEAM_COMPAT_DATA_PATH"_L1, wineRoot->winePrefix());
    env.insert("WINEFSYNC"_L1, "1"_L1);
    process->setProcessEnvironment(env);

    connect(process, &QProcess::finished, this, [=, this] {
        const auto code = process->exitCode();
        // Wine keeps only the low 8 bits of a Windows exit code. For the .NET installer,
        // 0x42 is 1602 (closed by the user), 0x69 is 1641 and 0xC2 is 3010 (installed, reboot wanted).
        const bool dotnetAccepted = command.endsWith("windowsdesktop-runtime-6.0.36-win-x64.exe"_L1) &&
                                    (code == 0x42 || code == 0x69 || code == 0xC2);
        if (code == 0 || dotnetAccepted)
            successCallback();
        else
        {
            emit processFailed(prettyName);
            qCWarning(WineLog) << "Running" << command << "with Wine" << wineRoot->wineBinary() << "failed with exit code"
                               << code;
            if (process->exitStatus() == QProcess::CrashExit)
                qCWarning(WineLog) << "Wine process crashed:" << process->errorString();
            const auto err = process->readAllStandardError().trimmed();
            if (!err.isEmpty())
                qCWarning(WineLog) << "Wine stderr:" << err;
            failureCallback();
        }
    });
    connect(process, &QProcess::finished, process, &QObject::deleteLater);

    process->start(wineRoot->wineBinary(), QStringList{command} + args);
}

QString Wine::whichWine() const
{
    QString result;

    QProcess whichWineProc;
    whichWineProc.start("which"_L1, {"wine"_L1});
    whichWineProc.waitForFinished();
    if (whichWineProc.exitCode() == 0)
        result = whichWineProc.readAllStandardOutput().trimmed();

    // If we can't find a system Wine, we might be able to piggyback off Proton installs from Steam or Heroic
    if (result.isEmpty())
        for (const auto g : Heroic::instance()->games() + Steam::instance()->games())
            if (!g->wineBinary().isEmpty())
                result = g->wineBinary();

    return result;
}

QString Wine::defaultWinePrefix() const
{
    return qEnvironmentVariable("WINEPREFIX", QDir::homePath() + "/.wine"_L1);
}
