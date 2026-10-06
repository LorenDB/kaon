#include "LaunchOptions.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <iterator>

namespace
{
    const auto command = "%command%"_L1;
    const auto dllOverrides = "WINEDLLOVERRIDES"_L1;

    // Splits on whitespace outside quotes. The quotes stay in the tokens.
    QStringList tokens(const QString &text)
    {
        QStringList out;
        QString current;
        QChar quote;
        for (const auto c : text)
        {
            if (!quote.isNull())
            {
                current += c;
                if (c == quote)
                    quote = QChar{};
            }
            else if (c == u'"' || c == u'\'')
            {
                quote = c;
                current += c;
            }
            else if (c.isSpace())
            {
                if (!current.isEmpty())
                    out << current;
                current.clear();
            }
            else
                current += c;
        }
        if (!current.isEmpty())
            out << current;
        return out;
    }

    QString unquoted(const QString &value)
    {
        if (value.size() >= 2 && (value.startsWith(u'"') || value.startsWith(u'\'')) && value.endsWith(value.at(0)))
            return value.mid(1, value.size() - 2);
        return value;
    }

    // WINEDLLOVERRIDES is always written with quotes, the way every guide shows it. Anything else gets them only when
    // the shell would need them.
    QString assignment(const QString &name, const QString &value)
    {
        static const QRegularExpression plain{R"(^[A-Za-z0-9_./:,+@%=-]*$)"_L1};
        if (name != dllOverrides && plain.match(value).hasMatch())
            return name + '='_L1 + value;
        return name + "=\""_L1 + value + '"'_L1;
    }

    bool isFlag(const QString &token)
    {
        return token.startsWith(u'-') || token.startsWith(u'+');
    }

    // "-width" for the unit "-width 1280"
    QString flagOf(const QString &unit)
    {
        return unit.section(u' ', 0, 0);
    }

    // An argument together with the values that follow it: "-width 1280" and "+mat_vsync 0" each stay in one piece
    QStringList units(const QStringList &arguments)
    {
        QStringList out;
        for (const auto &argument : arguments)
        {
            if (!isFlag(argument) && !out.isEmpty() && isFlag(out.constLast()))
                out.last() += ' '_L1 + argument;
            else
                out << argument;
        }
        return out;
    }

    // "a,b=n,b;c=d" as a list of (mode, DLLs). DLLs that share a mode stay in one clause.
    using Overrides = QList<QPair<QString, QStringList>>;

    // Wine lets the last mention of a DLL decide, so a DLL that turns up again moves to its new mode
    void absorbOverrides(Overrides &modes, const QString &spec)
    {
        for (const auto &clause : spec.split(u';', Qt::SkipEmptyParts))
        {
            const auto eq = clause.indexOf(u'=');
            if (eq < 0)
                continue;
            const auto mode = clause.mid(eq + 1).trimmed();
            for (auto dll : clause.left(eq).split(u',', Qt::SkipEmptyParts))
            {
                dll = dll.trimmed();
                if (dll.isEmpty())
                    continue;
                for (auto &other : modes)
                    if (other.first != mode)
                        other.second.removeIf(
                            [&dll](const QString &listed) { return listed.compare(dll, Qt::CaseInsensitive) == 0; });

                auto it = std::find_if(modes.begin(), modes.end(), [&](const auto &entry) { return entry.first == mode; });
                if (it == modes.end())
                {
                    modes.push_back({mode, {}});
                    it = std::prev(modes.end());
                }
                if (!it->second.contains(dll, Qt::CaseInsensitive))
                    it->second << dll;
            }
        }
    }

    QString mergeOverrides(const QString &a, const QString &b)
    {
        Overrides modes;
        absorbOverrides(modes, a);
        absorbOverrides(modes, b);
        QStringList clauses;
        for (const auto &mode : std::as_const(modes))
            if (!mode.second.isEmpty())
                clauses << mode.second.join(u',') + u'=' + mode.first;
        return clauses.join(u';');
    }

    // Whether every DLL in want is loaded the same way first in have. "native,builtin" and "n,b" are the same thing.
    bool overridesCover(const QString &have, const QString &want)
    {
        Overrides haveModes, wantModes;
        absorbOverrides(haveModes, have);
        absorbOverrides(wantModes, want);
        for (const auto &wanted : std::as_const(wantModes))
        {
            for (const auto &dll : wanted.second)
            {
                const auto match = std::find_if(haveModes.cbegin(), haveModes.cend(), [&](const auto &entry) {
                    return entry.second.contains(dll, Qt::CaseInsensitive);
                });
                if (match == haveModes.cend() || match->first.isEmpty() || wanted.first.isEmpty() ||
                    match->first.at(0).toLower() != wanted.first.at(0).toLower())
                    return false;
            }
        }
        return true;
    }

    QString compose(const QStringList &envNames,
                    const QHash<QString, QString> &env,
                    const QString &wrapper,
                    const QStringList &argumentUnits)
    {
        QStringList bits;
        for (const auto &name : envNames)
            bits << assignment(name, env.value(name));
        if (!wrapper.isEmpty())
            bits << wrapper;

        const auto arguments = argumentUnits.join(u' ');
        // Steam appends options without %command% to the game's own command line
        if (bits.isEmpty())
            return arguments;
        bits << command;
        if (!arguments.isEmpty())
            bits << arguments;
        return bits.join(u' ');
    }

    bool containsSequence(const QStringList &haystack, const QStringList &needle)
    {
        if (needle.isEmpty())
            return true;
        for (qsizetype i = 0; i + needle.size() <= haystack.size(); ++i)
        {
            bool same = true;
            for (qsizetype j = 0; same && j < needle.size(); ++j)
                same = haystack.at(i + j).compare(needle.at(j), Qt::CaseInsensitive) == 0;
            if (same)
                return true;
        }
        return false;
    }
} // namespace

LaunchOptions::Parsed LaunchOptions::parse(const QString &text)
{
    Parsed out;
    const auto at = text.indexOf(command, 0, Qt::CaseInsensitive);
    out.hasCommand = at >= 0;
    out.arguments = tokens(at < 0 ? text : text.mid(at + command.size()));
    if (at < 0)
        return out;

    // The shell reads NAME=value words as variables up to the first word that isn't one
    static const QRegularExpression variable{R"(^([A-Za-z_][A-Za-z0-9_]*)=(.*)$)"_L1,
                                             QRegularExpression::DotMatchesEverythingOption};
    QStringList wrapper;
    for (const auto &token : tokens(text.left(at)))
    {
        const auto match = wrapper.isEmpty() ? variable.match(token) : QRegularExpressionMatch{};
        if (!match.hasMatch())
        {
            wrapper << token;
            continue;
        }
        const auto name = match.captured(1);
        const auto value = unquoted(match.captured(2));
        // The shell keeps the last value a variable is given
        if (!out.env.contains(name))
            out.envNames << name;
        out.env.insert(name, value);
    }
    out.wrapper = wrapper.join(u' ');
    return out;
}

QString LaunchOptions::merge(const QStringList &parts)
{
    QStringList envNames;
    QHash<QString, QString> env;
    QString wrapper;
    QStringList argumentUnits;

    for (const auto &part : parts)
    {
        const auto parsed = parse(part);
        for (const auto &name : parsed.envNames)
        {
            if (!env.contains(name))
            {
                envNames << name;
                env.insert(name, parsed.env.value(name));
            }
            else if (name == dllOverrides)
                env[name] = mergeOverrides(env.value(name), parsed.env.value(name));
        }
        if (wrapper.isEmpty())
            wrapper = parsed.wrapper;
        for (const auto &unit : units(parsed.arguments))
            if (!argumentUnits.contains(unit, Qt::CaseInsensitive))
                argumentUnits << unit;
    }
    return compose(envNames, env, wrapper, argumentUnits);
}

bool LaunchOptions::isSimple(const QString &text)
{
    if (text.count(command, Qt::CaseInsensitive) > 1 || text.contains("$("_L1) || text.contains(u'`'))
        return false;

    QChar quote;
    for (const auto c : text)
    {
        if (!quote.isNull())
        {
            if (c == quote)
                quote = QChar{};
        }
        else if (c == u'"' || c == u'\'')
            quote = c;
        else if (c == u';' || c == u'|' || c == u'&' || c == u'<' || c == u'>')
            return false;
    }
    // An unclosed quote
    return quote.isNull();
}

QStringList LaunchOptions::conflictsIn(const QString &current, const QStringList &conflicts)
{
    QStringList found;
    for (const auto &argument : parse(current).arguments)
        if (conflicts.contains(argument, Qt::CaseInsensitive) && !found.contains(argument, Qt::CaseInsensitive))
            found << argument;
    return found;
}

bool LaunchOptions::covers(const QString &current, const QString &required, const QStringList &conflicts)
{
    const auto have = parse(current);
    const auto want = parse(required);

    for (const auto &name : want.envNames)
    {
        if (!have.env.contains(name))
            return false;
        if (name == dllOverrides ? !overridesCover(have.env.value(name), want.env.value(name)) :
                                   have.env.value(name) != want.env.value(name))
            return false;
    }
    for (const auto &unit : units(want.arguments))
        if (!containsSequence(have.arguments, unit.split(u' ')))
            return false;

    return conflictsIn(current, conflicts).isEmpty();
}

QString LaunchOptions::combined(const QString &current, const QString &required, const QStringList &conflicts)
{
    if (current.trimmed().isEmpty() || !isSimple(current))
        return merge({required});

    auto parsed = parse(current);
    const auto wanted = parse(required);
    const auto wantedUnits = units(wanted.arguments);

    QStringList kept;
    for (const auto &unit : units(parsed.arguments))
    {
        const auto flag = flagOf(unit);
        if (conflicts.contains(flag, Qt::CaseInsensitive))
            continue;
        // "-width 1920" makes way for the mod's "-width 1280". Leaving both in would leave it to the game which one counts.
        const bool replaced =
            isFlag(flag) && std::any_of(wantedUnits.cbegin(), wantedUnits.cend(), [&flag](const QString &wantedUnit) {
                return flagOf(wantedUnit).compare(flag, Qt::CaseInsensitive) == 0;
            });
        if (!replaced)
            kept << unit;
    }

    // The same goes for a variable the mod sets, except the DLL overrides, which are added to
    for (const auto &name : wanted.envNames)
    {
        if (name != dllOverrides && parsed.env.remove(name) > 0)
            parsed.envNames.removeAll(name);
    }

    // Options without %command% are arguments only. They need it once a variable has to go in front.
    auto cleaned = compose(parsed.envNames, parsed.env, parsed.wrapper, kept);
    if (!parsed.hasCommand && !kept.isEmpty())
        cleaned = command + ' '_L1 + cleaned;
    return merge({cleaned, required});
}
