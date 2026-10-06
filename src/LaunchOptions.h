#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// Launch options the way Steam takes them: `NAME=value wrapper %command% -arguments`. Mods say which ones they need;
// these functions combine them and compare them with what a player already has.
namespace LaunchOptions
{
    struct Parsed
    {
        // In the order they were written
        QStringList envNames;
        QHash<QString, QString> env;
        // Whatever stands between the variables and %command%, e.g. "gamemoderun"
        QString wrapper;
        QStringList arguments;
        bool hasCommand = false;
    };

    Parsed parse(const QString &text);

    // One line with everything every part asks for. DLLs named in several WINEDLLOVERRIDES end up in one.
    QString merge(const QStringList &parts);

    // False for options that use shell syntax beyond variables and a wrapper. Kaon leaves those for the player to edit.
    bool isSimple(const QString &text);

    // Whether current already has everything in required and none of the conflicting arguments
    bool covers(const QString &current, const QString &required, const QStringList &conflicts = {});

    // current with required added and the conflicting arguments taken out. Just required when current is not simple.
    QString combined(const QString &current, const QString &required, const QStringList &conflicts = {});

    // The conflicting arguments that current contains
    QStringList conflictsIn(const QString &current, const QStringList &conflicts);
} // namespace LaunchOptions
