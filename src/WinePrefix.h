#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

class Game;

// A game's Wine prefix: whether something is running in it, and edits to its registry.
//
// The registry is edited in the files Wine keeps it in. That needs no Wine process and works for a Flatpak game from
// outside its sandbox. A running wineserver holds the registry in memory and writes it back when it exits, which would
// undo an edit, so every edit is refused while the prefix is in use.
namespace WinePrefix
{
    enum class Hive
    {
        // HKEY_CURRENT_USER, kept in user.reg
        User,
        // HKEY_LOCAL_MACHINE, kept in system.reg
        Machine,
    };

    // Whether Wine has set the prefix up far enough to have a registry and a C: drive
    bool isSetUp(const Game *game);
    // Whether a Wine process is running in the prefix
    bool inUse(const Game *game);
    // Every prefix on this machine that has one, to notice a game starting or quitting
    QStringList prefixesInUse();
    // The environment of a Wine process running in the prefix, the wineserver's when there is one. Empty when
    // nothing runs there.
    QHash<QString, QString> runningEnvironment(const Game *game);
    QHash<QString, QString> environmentOfProcess(qint64 pid);

    // Why the registry can't be edited right now, as a sentence for the user. Empty when it can.
    QString editHoldReason(const Game *game);

    // A string value comes back as its text and a DWORD as a decimal number. Null when there is no such value.
    std::optional<QString> value(const Game *game, Hive hive, const QString &key, const QString &name);

    bool setString(
        const Game *game, Hive hive, const QString &key, const QString &name, const QString &text, QString *error);
    bool setDword(const Game *game, Hive hive, const QString &key, const QString &name, quint32 number, QString *error);
    // Succeeds when the value is gone afterwards, whether or not it was there before
    bool remove(const Game *game, Hive hive, const QString &key, const QString &name, QString *error);

    // "native,builtin" for a DLL in HKCU\Software\Wine\DllOverrides makes Wine load the copy in the game folder instead
    // of its own, which is what winecfg's Libraries tab writes
    bool hasNativeDllOverride(const Game *game, const QString &dll);
    bool setNativeDllOverride(const Game *game, const QString &dll, QString *error);
    bool removeDllOverride(const Game *game, const QString &dll, QString *error);

    // The same edits on the text of a registry file, which is all the functions above do once the file is read.
    // key uses single backslashes. raw is a value the way the file spells it, e.g. "\"native\"" or "dword:00000000".
    namespace Text
    {
        bool isRegistry(const QString &text);
        std::optional<QString> rawValue(const QString &text, const QString &key, const QString &name);
        // Both return whether the text changed
        bool set(QString *text, const QString &key, const QString &name, const QString &raw);
        bool remove(QString *text, const QString &key, const QString &name);

        QString quoted(const QString &text);
        // Null for anything that is neither a string nor a DWORD
        std::optional<QString> decoded(const QString &raw);
    } // namespace Text
} // namespace WinePrefix
