#include "WinePrefix.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>
#include <functional>

#include "Game.h"

Q_LOGGING_CATEGORY(WinePrefixLog, "wineprefix")

namespace
{
    const auto dllOverridesKey = "Software\\Wine\\DllOverrides"_L1;
    const auto busy = "Quit the game before changing this. Its Proton prefix is in use."_L1;

    QString hivePath(const Game *game, WinePrefix::Hive hive)
    {
        return QDir{game->winePrefix()}.filePath(hive == WinePrefix::Hive::User ? "user.reg"_L1 : "system.reg"_L1);
    }

    // ------------------------------------------------------------ registry text

    // A registry file spells a key with its backslashes doubled
    QString fileKey(QString key)
    {
        return key.replace('\\'_L1, "\\\\"_L1);
    }

    QString endOfLine(const QString &text)
    {
        return text.contains("\r\n"_L1) ? "\r\n"_L1 : "\n"_L1;
    }

    // Splits `"name"=rest`. False for a line that isn't a named value, such as a comment or the default value.
    bool parseNamed(QStringView line, QString *name, QString *rest)
    {
        if (!line.startsWith(u'"'))
            return false;
        QString decoded;
        for (qsizetype i = 1; i < line.size(); ++i)
        {
            const auto c = line.at(i);
            if (c == u'\\' && i + 1 < line.size())
            {
                decoded += line.at(++i);
                continue;
            }
            if (c == u'"')
            {
                if (i + 1 >= line.size() || line.at(i + 1) != u'=')
                    return false;
                *name = decoded;
                *rest = line.mid(i + 2).toString();
                return true;
            }
            decoded += c;
        }
        return false;
    }

    struct Section
    {
        // Start of the "[key] time" line, of the line after it, and of the next section. All -1 when there is no section.
        qsizetype header = -1;
        qsizetype body = -1;
        qsizetype end = -1;
    };

    // Values never start a line with '[', so "\n[" only ever begins a section
    Section findSection(const QString &text, const QString &key)
    {
        const QString needle = '['_L1 + fileKey(key) + ']'_L1;
        for (qsizetype from = 0;;)
        {
            const auto at = text.indexOf(needle, from, Qt::CaseInsensitive);
            if (at < 0)
                return {};
            if (at == 0 || text.at(at - 1) == u'\n')
            {
                Section section;
                section.header = at;
                const auto lineEnd = text.indexOf(u'\n', at);
                section.body = lineEnd < 0 ? text.size() : lineEnd + 1;
                const auto next = lineEnd < 0 ? -1 : text.indexOf("\n["_L1, lineEnd);
                section.end = next < 0 ? text.size() : next + 1;
                return section;
            }
            from = at + 1;
        }
    }

    struct Line
    {
        qsizetype start = 0;
        // Without the line ending
        qsizetype length = 0;
        qsizetype next = 0;
    };

    // Calls visit for each line of a section's body until it returns true
    bool eachLine(const QString &text, const Section &section, const std::function<bool(const Line &, QStringView)> &visit)
    {
        for (auto pos = section.body; pos < section.end;)
        {
            auto newline = text.indexOf(u'\n', pos);
            if (newline < 0 || newline >= section.end)
                newline = section.end;
            Line line{pos, newline - pos, qMin(newline + 1, section.end)};
            if (line.length > 0 && text.at(pos + line.length - 1) == u'\r')
                --line.length;
            if (visit(line, QStringView{text}.mid(line.start, line.length)))
                return true;
            pos = line.next;
        }
        return false;
    }

    QString fileTimeNow()
    {
        const quint64 fileTime = (static_cast<quint64>(QDateTime::currentSecsSinceEpoch()) + 11644473600ULL) * 10000000ULL;
        return QString::number(fileTime, 16);
    }

    // ------------------------------------------------------------ registry files

    bool readHive(const QString &path, QString *text)
    {
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly))
            return false;
        // Latin-1 keeps every byte as it is, whatever the file holds
        *text = QString::fromLatin1(file.readAll());
        return true;
    }

    // What has been read out of one registry file. system.reg runs to megabytes, and the library asks about every game's
    // prefix whenever anything changes, so an answer is kept for as long as the file stays the same. Only the UI thread
    // uses this.
    struct Answers
    {
        QDateTime modified;
        qint64 size = -1;
        // By key and name. A null answer is an answer too: the value isn't there.
        QHash<QString, std::optional<QString>> values;
    };

    QHash<QString, Answers> &answerCache()
    {
        static QHash<QString, Answers> cache;
        return cache;
    }

    bool writeHive(const QString &path, const QString &text)
    {
        // The registry as it was before Kaon's latest edit, in case one ever goes wrong
        const auto backup = path + ".kaon-backup"_L1;
        QFile::remove(backup);
        if (!QFile::copy(path, backup))
            qCWarning(WinePrefixLog) << "Could not back up" << path;

        const auto bytes = text.toLatin1();
        QSaveFile out{path};
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        {
            qCWarning(WinePrefixLog) << "Could not write" << path << out.errorString();
            return false;
        }
        answerCache().remove(path);
        return true;
    }

    bool edit(const Game *game, WinePrefix::Hive hive, const std::function<bool(QString *)> &change, QString *error)
    {
        if (!game || game->winePrefix().isEmpty())
        {
            *error = "This game has no Wine prefix."_L1;
            return false;
        }
        if (WinePrefix::inUse(game))
        {
            *error = busy;
            return false;
        }

        const auto path = hivePath(game, hive);
        QString text;
        if (!readHive(path, &text))
        {
            *error = "Kaon couldn't read this game's Proton prefix."_L1;
            return false;
        }
        if (!WinePrefix::Text::isRegistry(text))
        {
            *error = "%1 in this game's prefix isn't a Wine registry Kaon can edit."_L1.arg(QFileInfo{path}.fileName());
            return false;
        }
        if (!change(&text))
            return true;
        if (!writeHive(path, text))
        {
            *error = "Kaon couldn't write this game's Proton prefix."_L1;
            return false;
        }
        qCInfo(WinePrefixLog) << "Edited" << path;
        return true;
    }

    // ------------------------------------------------------------ running processes

    QStringList prefixSpellings(const Game *game)
    {
        QStringList paths;
        const auto add = [&paths](const QString &path) {
            if (path.isEmpty())
                return;
            const auto clean = QDir::cleanPath(path);
            if (!paths.contains(clean))
                paths << clean;
            const auto canonical = QFileInfo{clean}.canonicalFilePath();
            if (!canonical.isEmpty() && !paths.contains(canonical))
                paths << canonical;
        };
        add(game->winePrefix());
        // A Flatpak game sees its prefix under the sandbox's path
        add(game->sandboxWinePrefix());
        return paths;
    }

    QHash<QString, QString> parseEnvironment(const QByteArray &block)
    {
        QHash<QString, QString> environment;
        for (const auto &entry : block.split('\0'))
        {
            if (const auto eq = entry.indexOf('='); eq > 0)
                environment.insert(QString::fromLocal8Bit(entry.left(eq)), QString::fromLocal8Bit(entry.mid(eq + 1)));
        }
        return environment;
    }

    QByteArray readProc(const QString &pid, QLatin1StringView file)
    {
        QFile f{"/proc/"_L1 + pid + '/'_L1 + file};
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
    }

    // WINEPREFIX exported in a shell profile is in the environment of everything that person runs. Only a Wine process
    // means the prefix is open.
    bool looksLikeWine(const QString &pid)
    {
        if (QFileInfo{QFile::symLinkTarget("/proc/"_L1 + pid + "/exe"_L1)}.fileName().startsWith("wine"_L1))
            return true;
        if (readProc(pid, "comm"_L1).startsWith("wine"))
            return true;
        const auto commandLine = readProc(pid, "cmdline"_L1).toLower();
        return commandLine.contains("wine") || commandLine.contains(".exe");
    }

    // The value of one variable in a /proc environ block, without taking the whole block apart
    QString environmentValue(const QByteArray &block, const QByteArray &name)
    {
        const QByteArray needle = name + '=';
        for (qsizetype from = 0;;)
        {
            const auto at = block.indexOf(needle, from);
            if (at < 0)
                return {};
            if (at == 0 || block.at(at - 1) == '\0')
            {
                const auto start = at + needle.size();
                const auto end = block.indexOf('\0', start);
                return QString::fromLocal8Bit(block.mid(start, end < 0 ? -1 : end - start));
            }
            from = at + 1;
        }
    }

    struct Running
    {
        // WINEPREFIX the way the process has it, and with symbolic links followed
        QString prefix;
        QString canonical;
        bool server = false;
        QHash<QString, QString> environment;
    };

    // Every prefix with a Wine process in it right now, each with the environment of one such process: the wineserver's
    // when there is one, since that is the one every other process in the prefix has to agree with.
    //
    // The library asks about every game whenever anything changes. One look a second is enough for all of them, and a
    // click that lands inside that second still sees a game that is actually running.
    const QList<Running> &runningPrefixes()
    {
        static QList<Running> found;
        static qint64 lookedAt = 0;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (lookedAt != 0 && now >= lookedAt && now - lookedAt < 1000)
            return found;
        lookedAt = now;
        found.clear();

        const auto entries = QDir{"/proc"_L1}.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &pid : entries)
        {
            bool numeric = false;
            pid.toLongLong(&numeric);
            if (!numeric)
                continue;
            const auto block = readProc(pid, "environ"_L1);
            const auto set = environmentValue(block, "WINEPREFIX"_ba);
            if (set.isEmpty())
                continue;

            const auto prefix = QDir::cleanPath(set);
            const bool server = readProc(pid, "comm"_L1).trimmed() == "wineserver";
            const auto known =
                std::find_if(found.begin(), found.end(), [&prefix](const Running &r) { return r.prefix == prefix; });
            if (known != found.end() && (known->server || !server))
                continue;
            if (!server && !looksLikeWine(pid))
                continue;

            if (known == found.end())
                found.push_back({prefix, QFileInfo{prefix}.canonicalFilePath(), server, parseEnvironment(block)});
            else
            {
                known->server = true;
                known->environment = parseEnvironment(block);
            }
        }
        return found;
    }

    const Running *runningIn(const Game *game)
    {
        const auto spellings = prefixSpellings(game);
        if (spellings.isEmpty())
            return nullptr;
        for (const auto &running : runningPrefixes())
            if (spellings.contains(running.prefix) ||
                (!running.canonical.isEmpty() && spellings.contains(running.canonical)))
                return &running;
        return nullptr;
    }
} // namespace

// ------------------------------------------------------------ Text

bool WinePrefix::Text::isRegistry(const QString &text)
{
    return text.startsWith("WINE REGISTRY Version 2"_L1);
}

QString WinePrefix::Text::quoted(const QString &text)
{
    QString out{u'"'};
    for (const auto c : text)
    {
        if (c == u'\\' || c == u'"')
            out += u'\\';
        out += c;
    }
    return out + u'"';
}

std::optional<QString> WinePrefix::Text::decoded(const QString &raw)
{
    if (raw.startsWith("dword:"_L1, Qt::CaseInsensitive))
    {
        bool ok = false;
        const auto number = raw.mid(6).trimmed().toUInt(&ok, 16);
        return ok ? std::optional{QString::number(number)} : std::nullopt;
    }
    if (!raw.startsWith(u'"'))
        return std::nullopt;

    QString out;
    for (qsizetype i = 1; i < raw.size(); ++i)
    {
        const auto c = raw.at(i);
        if (c == u'"')
            return out;
        if (c != u'\\' || i + 1 >= raw.size())
        {
            out += c;
            continue;
        }
        const auto escaped = raw.at(++i);
        if (escaped == u'n')
            out += u'\n';
        else if (escaped == u'r')
            out += u'\r';
        else if (escaped == u't')
            out += u'\t';
        else if (escaped == u'0')
            out += QChar{0};
        else if (escaped == u'x')
        {
            // Up to four hex digits of one UTF-16 unit
            const auto isHex = [](QChar digit) {
                return digit.isDigit() || (digit.toLower() >= u'a' && digit.toLower() <= u'f');
            };
            qsizetype digits = 0;
            while (digits < 4 && i + 1 + digits < raw.size() && isHex(raw.at(i + 1 + digits)))
                ++digits;
            bool ok = false;
            const auto unit = raw.mid(i + 1, digits).toUShort(&ok, 16);
            if (ok)
                out += QChar{unit};
            i += digits;
        }
        else
            out += escaped;
    }
    return std::nullopt;
}

std::optional<QString> WinePrefix::Text::rawValue(const QString &text, const QString &key, const QString &name)
{
    const auto section = findSection(text, key);
    if (section.header < 0)
        return std::nullopt;

    std::optional<QString> found;
    eachLine(text, section, [&](const Line &, QStringView line) {
        QString lineName, rest;
        if (!parseNamed(line, &lineName, &rest) || lineName.compare(name, Qt::CaseInsensitive) != 0)
            return false;
        found = rest;
        return true;
    });
    return found;
}

bool WinePrefix::Text::set(QString *text, const QString &key, const QString &name, const QString &raw)
{
    const auto eol = endOfLine(*text);
    const QString wanted = quoted(name) + '='_L1 + raw;

    const auto section = findSection(*text, key);
    if (section.header < 0)
    {
        QString addition;
        if (!text->isEmpty() && !text->endsWith(u'\n'))
            addition += eol;
        addition += eol;
        addition += "[%1] %2"_L1.arg(fileKey(key), QString::number(QDateTime::currentSecsSinceEpoch())) + eol;
        addition += "#time="_L1 + fileTimeNow() + eol;
        addition += wanted + eol;
        text->append(addition);
        return true;
    }

    // A new value goes below the lines that describe the key itself, which start with '#'
    auto insertAt = section.body;
    bool seenValue = false;
    Line existing;
    bool same = false;
    const bool present = eachLine(*text, section, [&](const Line &line, QStringView content) {
        if (!seenValue && content.startsWith(u'#'))
        {
            insertAt = line.next;
            return false;
        }
        seenValue = true;
        QString lineName, rest;
        if (!parseNamed(content, &lineName, &rest) || lineName.compare(name, Qt::CaseInsensitive) != 0)
            return false;
        existing = line;
        same = content == wanted;
        return true;
    });

    if (present)
    {
        if (same)
            return false;
        text->replace(existing.start, existing.length, wanted);
        return true;
    }

    // The last line of a file may come without a line ending
    const bool needsBreak = insertAt > 0 && text->at(insertAt - 1) != u'\n';
    text->insert(insertAt, (needsBreak ? eol : QString{}) + wanted + eol);
    return true;
}

bool WinePrefix::Text::remove(QString *text, const QString &key, const QString &name)
{
    const auto section = findSection(*text, key);
    if (section.header < 0)
        return false;

    Line existing;
    const bool present = eachLine(*text, section, [&](const Line &line, QStringView content) {
        QString lineName, rest;
        if (!parseNamed(content, &lineName, &rest) || lineName.compare(name, Qt::CaseInsensitive) != 0)
            return false;
        existing = line;
        return true;
    });
    if (!present)
        return false;
    text->remove(existing.start, existing.next - existing.start);
    return true;
}

// ------------------------------------------------------------ prefix

bool WinePrefix::isSetUp(const Game *game)
{
    if (!game || game->winePrefix().isEmpty())
        return false;
    const QDir prefix{game->winePrefix()};
    return QFileInfo{prefix.filePath("user.reg"_L1)}.isFile() && QFileInfo{prefix.filePath("system.reg"_L1)}.isFile() &&
           QFileInfo::exists(prefix.filePath("dosdevices/c:"_L1));
}

bool WinePrefix::inUse(const Game *game)
{
    return game && runningIn(game) != nullptr;
}

QStringList WinePrefix::prefixesInUse()
{
    QStringList prefixes;
    for (const auto &running : runningPrefixes())
        prefixes << running.prefix;
    prefixes.sort();
    return prefixes;
}

QHash<QString, QString> WinePrefix::runningEnvironment(const Game *game)
{
    const auto running = game ? runningIn(game) : nullptr;
    return running ? running->environment : QHash<QString, QString>{};
}

QHash<QString, QString> WinePrefix::environmentOfProcess(qint64 pid)
{
    return parseEnvironment(readProc(QString::number(pid), "environ"_L1));
}

QString WinePrefix::editHoldReason(const Game *game)
{
    if (!game)
        return {};
    if (!isSetUp(game))
        return game->store() == Game::Store::Custom ?
                   "The Wine prefix set for this game has no registry yet. Run the game once in it first."_L1 :
                   "Launch the game once so it has a Proton prefix for Kaon to set this up in."_L1;
    if (inUse(game))
        return busy;
    return {};
}

std::optional<QString> WinePrefix::value(const Game *game, Hive hive, const QString &key, const QString &name)
{
    if (!game || game->winePrefix().isEmpty())
        return std::nullopt;

    const auto path = hivePath(game, hive);
    const QFileInfo info{path};
    auto &cache = answerCache();
    if (!info.isFile())
    {
        cache.remove(path);
        return std::nullopt;
    }

    auto &answers = cache[path];
    if (answers.size != info.size() || answers.modified != info.lastModified())
        answers = {info.lastModified(), info.size(), {}};

    // The registry doesn't tell capitals apart
    const QString question = key.toLower() + u'\n' + name.toLower();
    if (const auto known = answers.values.constFind(question); known != answers.values.cend())
        return *known;

    QString text;
    if (!readHive(path, &text))
        return std::nullopt;
    const auto raw = Text::rawValue(text, key, name);
    const auto answer = raw ? Text::decoded(*raw) : std::nullopt;
    answers.values.insert(question, answer);
    return answer;
}

bool WinePrefix::setString(
    const Game *game, Hive hive, const QString &key, const QString &name, const QString &text, QString *error)
{
    return edit(game, hive, [&](QString *registry) { return Text::set(registry, key, name, Text::quoted(text)); }, error);
}

bool WinePrefix::setDword(
    const Game *game, Hive hive, const QString &key, const QString &name, quint32 number, QString *error)
{
    const QString raw = "dword:"_L1 + QString::number(number, 16).rightJustified(8, u'0');
    return edit(game, hive, [&](QString *registry) { return Text::set(registry, key, name, raw); }, error);
}

bool WinePrefix::remove(const Game *game, Hive hive, const QString &key, const QString &name, QString *error)
{
    // Nothing to take out of a registry that isn't there
    if (game && !QFileInfo::exists(hivePath(game, hive)))
        return true;
    return edit(game, hive, [&](QString *registry) { return Text::remove(registry, key, name); }, error);
}

bool WinePrefix::hasNativeDllOverride(const Game *game, const QString &dll)
{
    const auto order = value(game, Hive::User, dllOverridesKey, dll);
    return order && order->startsWith("native"_L1, Qt::CaseInsensitive);
}

bool WinePrefix::setNativeDllOverride(const Game *game, const QString &dll, QString *error)
{
    return setString(game, Hive::User, dllOverridesKey, dll, "native,builtin"_L1, error);
}

bool WinePrefix::removeDllOverride(const Game *game, const QString &dll, QString *error)
{
    return remove(game, Hive::User, dllOverridesKey, dll, error);
}
