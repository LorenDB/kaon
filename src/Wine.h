#pragma once

#include <QObject>
#include <QProcess>
#include <QQmlEngine>

#include "Game.h"

class Wine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Wine *instance();
    static Wine *create(QQmlEngine *, QJSEngine *);

    void runInWine(
        const QString &prettyName,
        const Game *wineRoot,
        const QString &command,
        const QStringList &args = {},
        std::function<void()> successCallback = [] {},
        std::function<void()> failureCallback = [] {},
        bool inLauncherSandbox = false);

    // Injectors pass true so a Flatpak game is started with `flatpak enter`. Host games ignore the flag.
    void runInWine(const QString &prettyName, const Game *wineRoot, const QString &command, bool inLauncherSandbox)
    {
        runInWine(prettyName, wineRoot, command, {}, [] {}, [] {}, inLauncherSandbox);
    }

    Q_INVOKABLE QString whichWine() const;
    Q_INVOKABLE QString defaultWinePrefix() const;

signals:
    void processFailed(const QString &prettyName);

private:
    explicit Wine(QObject *parent = nullptr);
    ~Wine() = default;

    void startWineProcess(const QString &program,
                          const QStringList &arguments,
                          const QProcessEnvironment &environment,
                          const QString &prettyName,
                          const QString &command,
                          const QString &wineBinary,
                          const std::function<void()> &successCallback,
                          const std::function<void()> &failureCallback);
    void enterGameSandbox(qint64 pid,
                          const QString &appId,
                          const QString &sandboxPrefix,
                          const QString &sandboxWine,
                          const QString &prettyName,
                          const QString &command,
                          const QStringList &args,
                          const std::function<void()> &successCallback,
                          const std::function<void()> &failureCallback);
};
