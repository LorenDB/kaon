#include "ConfigText.h"

#include <QLocale>
#include <QRegularExpression>

namespace ConfigText
{
    namespace
    {
        QString parentPath(const QString &path)
        {
            const auto dot = path.lastIndexOf('.'_L1);
            return dot < 0 ? QString{} : path.left(dot);
        }

        QString lastComponent(const QString &path)
        {
            const auto dot = path.lastIndexOf('.'_L1);
            return dot < 0 ? path : path.mid(dot + 1);
        }

        QStringList splitLines(const QString &text, QString *newline, bool *trailingNewline)
        {
            *newline = text.contains("\r\n"_L1) ? "\r\n"_L1 : "\n"_L1;
            auto norm = text;
            norm.replace("\r\n"_L1, "\n"_L1);
            norm.replace('\r'_L1, '\n'_L1);
            *trailingNewline = norm.endsWith('\n'_L1);
            if (*trailingNewline)
                norm.chop(1);
            return norm.isEmpty() && !*trailingNewline ? QStringList{} : norm.split('\n'_L1);
        }

        QString joinLines(const QStringList &lines, const QString &newline, bool trailingNewline)
        {
            auto out = lines.join(newline);
            if (trailingNewline)
                out += newline;
            return out;
        }

        QString normalizeKeys(const QString &raw)
        {
            auto body = raw.trimmed();
            if (body.startsWith('['_L1))
            {
                body = body.mid(1);
                if (body.endsWith(']'_L1))
                    body.chop(1);
                QStringList parts;
                for (auto part : body.split(','_L1))
                {
                    part = part.trimmed();
                    if ((part.startsWith('"'_L1) && part.endsWith('"'_L1)) ||
                        (part.startsWith('\''_L1) && part.endsWith('\''_L1)))
                        part = part.mid(1, part.size() - 2);
                    if (!part.isEmpty())
                        parts << part;
                }
                return parts.join(", "_L1);
            }
            return body;
        }

        QString writeKeys(const QString &value)
        {
            QStringList parts;
            for (auto part : value.split(','_L1))
            {
                part = part.trimmed();
                if (!part.isEmpty())
                    parts << u'"' + part + u'"';
            }
            return '[' + parts.join(", "_L1) + ']';
        }

        // False when a bool isn't a bool. Everything else is kept as text.
        bool normalize(const Spec &spec, const QString &raw, QString *value)
        {
            const auto trimmed = raw.trimmed();
            if (spec.kind == "bool"_L1)
            {
                const auto lower = trimmed.toLower();
                if (lower == "true"_L1 || lower == "yes"_L1 || lower == "1"_L1)
                    *value = "true"_L1;
                else if (lower == "false"_L1 || lower == "no"_L1 || lower == "0"_L1)
                    *value = "false"_L1;
                else
                    return false;
                return true;
            }
            if (spec.kind == "keys"_L1)
                *value = normalizeKeys(trimmed);
            else
                *value = trimmed;
            return true;
        }

        QString formatValue(const Field &field)
        {
            if (field.spec.kind == "keys"_L1)
                return writeKeys(field.value);
            if (field.spec.kind == "bool"_L1)
                return field.value == "true"_L1 ? "true"_L1 : "false"_L1;
            return field.value.trimmed();
        }

        QString formatLine(Syntax syntax, const Field &field)
        {
            const auto value = formatValue(field);
            if (field.line < 0)
            {
                if (syntax == Syntax::Yaml)
                    return QString{field.indent, u' '} + field.key + ": "_L1 + value;
                if (syntax == Syntax::Ini)
                    return field.key + " = "_L1 + value;
                return field.key + u'=' + value;
            }
            if (field.commented)
                return QString{field.indent, u' '} + field.key + ": "_L1 + value;
            return field.prefix + field.key + field.separator + value + field.suffix;
        }

        QString pickAlias(const Spec &spec, bool snake)
        {
            if (spec.aliases.isEmpty())
                return {};
            for (const auto &alias : spec.aliases)
                if (alias.contains('_'_L1) == snake)
                    return alias;
            return spec.aliases.constFirst();
        }

        struct Parsed
        {
            int index = -1;
            QString path;
            QString key;
            QString prefix;
            QString separator;
            QString raw;
            QString suffix;
            int indent = 0;
            bool commented = false;
            bool isMap = false;
        };

        QString takeComment(QString *rest)
        {
            const auto hash = rest->indexOf(" #"_L1);
            if (hash < 0)
                return {};
            const auto suffix = rest->mid(hash);
            *rest = rest->left(hash);
            return suffix;
        }

        QList<Parsed> parseYaml(const QStringList &lines,
                                QHash<QString, int> *sectionLines,
                                QHash<QString, int> *sectionIndents)
        {
            QList<Parsed> parsed;
            struct Frame
            {
                int indent = 0;
                QString key;
            };
            QList<Frame> stack;
            static const QRegularExpression keyRe{R"(^([A-Za-z_][A-Za-z0-9_]*)(\s*:\s*)(.*)$)"};

            for (int i = 0; i < lines.size(); ++i)
            {
                const auto &line = lines.at(i);
                int indent = 0;
                while (indent < line.size() && (line.at(indent) == ' '_L1 || line.at(indent) == '\t'_L1))
                    ++indent;
                auto body = QStringView{line}.mid(indent);
                auto commented = false;
                if (body.startsWith('#'_L1))
                {
                    commented = true;
                    body = body.mid(1).trimmed();
                }
                const auto match = keyRe.matchView(body);
                if (!match.hasMatch())
                    continue;

                auto rest = match.captured(3);
                const auto suffix = takeComment(&rest);
                const auto raw = rest.trimmed();
                const auto isMap = raw.isEmpty() && !commented;

                auto look = stack;
                while (!look.isEmpty() && look.constLast().indent >= indent)
                    look.removeLast();

                QStringList parts;
                for (const auto &frame : look)
                    parts << frame.key;
                parts << match.captured(1);

                Parsed row;
                row.index = i;
                row.path = parts.join('.'_L1);
                row.key = match.captured(1);
                row.prefix = line.left(indent);
                row.separator = match.captured(2);
                row.raw = raw;
                row.suffix = suffix;
                row.indent = indent;
                row.commented = commented;
                row.isMap = isMap;
                parsed << row;

                if (commented)
                    continue;
                stack = look;
                if (isMap)
                {
                    stack.append({indent, row.key});
                    sectionLines->insert(row.path, i);
                    sectionIndents->insert(row.path, indent);
                }
            }
            return parsed;
        }

        QList<Parsed> parseKeyed(const QStringList &lines, bool *snake)
        {
            QList<Parsed> parsed;
            static const QRegularExpression keyRe{R"(^(\s*)([A-Za-z0-9_]+)(\s*=\s*)(.*)$)"};
            for (int i = 0; i < lines.size(); ++i)
            {
                const auto match = keyRe.match(lines.at(i));
                if (!match.hasMatch())
                    continue;
                auto rest = match.captured(4);
                const auto suffix = takeComment(&rest);
                Parsed row;
                row.index = i;
                row.key = match.captured(2);
                row.path = row.key;
                row.prefix = match.captured(1);
                row.separator = match.captured(3);
                row.raw = rest.trimmed();
                row.suffix = suffix;
                parsed << row;
                if (row.key.contains('_'_L1))
                    *snake = true;
            }
            return parsed;
        }

        int insertAt(const QStringList &lines, int sectionLine, int sectionIndent)
        {
            for (int i = sectionLine + 1; i < lines.size(); ++i)
            {
                const auto trimmed = lines.at(i).trimmed();
                if (trimmed.isEmpty() || trimmed.startsWith('#'_L1))
                    continue;
                int indent = 0;
                while (indent < lines.at(i).size() &&
                       (lines.at(i).at(indent) == ' '_L1 || lines.at(i).at(indent) == '\t'_L1))
                    ++indent;
                if (indent <= sectionIndent)
                    return i;
            }
            return lines.size();
        }
    } // namespace

    Document loadText(Syntax syntax, const QString &text, const QList<Spec> &specs)
    {
        Document document;
        document.syntax = syntax;
        document.lines = splitLines(text, &document.newline, &document.trailingNewline);

        QList<Parsed> parsed;
        if (syntax == Syntax::Yaml)
            parsed = parseYaml(document.lines, &document.sectionLines, &document.sectionIndents);
        else
            parsed = parseKeyed(document.lines, &document.snakeKeys);

        QList<int> used;
        for (const auto &spec : specs)
        {
            const Parsed *found = nullptr;
            for (const auto &row : parsed)
            {
                if (row.isMap || used.contains(row.index))
                    continue;
                if (row.commented && spec.kind != "text"_L1)
                    continue;
                if (spec.aliases.contains(syntax == Syntax::Yaml ? row.path : row.key))
                {
                    found = &row;
                    break;
                }
            }

            if (!found && spec.onlyIfPresent)
                continue;

            Field field;
            field.spec = spec;
            field.value = spec.missing;
            field.original = spec.missing;
            field.parentPath = syntax == Syntax::Yaml ? parentPath(spec.aliases.value(0)) : QString{};
            field.indent = syntax == Syntax::Yaml ? spec.aliases.value(0).count('.'_L1) * 2 : 0;
            field.key = syntax == Syntax::Yaml ? lastComponent(spec.aliases.value(0)) : pickAlias(spec, document.snakeKeys);

            if (found)
            {
                used << found->index;
                QString value;
                // A commented-out key is not set. The text after it is an example, not the player's value.
                if (!found->commented && !normalize(spec, found->raw, &value))
                    continue;
                if (!found->commented)
                {
                    field.value = value;
                    field.original = value;
                }
                field.line = found->index;
                field.key = found->key;
                field.prefix = found->prefix;
                field.separator = found->separator;
                field.suffix = found->suffix;
                field.indent = found->indent;
                field.commented = found->commented;
            }
            document.fields << field;
        }
        return document;
    }

    bool saveText(const Document &document, QString *out, QString *error)
    {
        for (const auto &field : document.fields)
        {
            if (field.value == field.original)
                continue;
            if (field.spec.kind == "number"_L1)
            {
                bool ok = false;
                QLocale::c().toDouble(field.value.trimmed(), &ok);
                if (!ok)
                {
                    if (error)
                        *error = "%1 has to be a number."_L1.arg(field.spec.label);
                    return false;
                }
            }
        }

        auto lines = document.lines;
        QList<QPair<int, QString>> inserts;
        // Sections this save is about to add. Indexes refer to the file before any insert.
        QHash<QString, int> plannedSections;

        const auto queueSection = [&](const auto &queue, const QString &path) -> void {
            if (path.isEmpty() || document.sectionLines.contains(path) || plannedSections.contains(path))
                return;
            const auto parent = parentPath(path);
            queue(queue, parent);
            auto at = lines.size();
            if (!parent.isEmpty() && document.sectionLines.contains(parent))
                at = insertAt(lines, document.sectionLines.value(parent), document.sectionIndents.value(parent));
            else if (!parent.isEmpty())
                at = plannedSections.value(parent);
            inserts.append({at, QString{path.count('.'_L1) * 2, u' '} + lastComponent(path) + u':'});
            plannedSections.insert(path, at);
        };

        // A new ini key belongs to the first section. Appending it would put it under whatever section comes last.
        const auto endOfFirstSection = [&lines]() -> int {
            auto seenHeader = false;
            for (int i = 0; i < lines.size(); ++i)
            {
                const auto trimmed = lines.at(i).trimmed();
                if (!trimmed.startsWith('['_L1) || !trimmed.endsWith(']'_L1))
                    continue;
                if (seenHeader)
                {
                    auto at = i;
                    while (at > 0 && lines.at(at - 1).trimmed().isEmpty())
                        --at;
                    return at;
                }
                seenHeader = true;
            }
            return static_cast<int>(lines.size());
        };

        for (const auto &field : document.fields)
        {
            if (field.value == field.original)
                continue;
            const auto formatted = formatLine(document.syntax, field);
            if (field.line >= 0 && field.line < lines.size())
                lines[field.line] = formatted;
            else if (field.line < 0)
            {
                auto at = lines.size();
                if (document.syntax == Syntax::Yaml && !field.parentPath.isEmpty())
                {
                    if (!document.sectionLines.contains(field.parentPath))
                        queueSection(queueSection, field.parentPath);
                    at = document.sectionLines.contains(field.parentPath) ?
                             insertAt(lines,
                                      document.sectionLines.value(field.parentPath),
                                      document.sectionIndents.value(field.parentPath)) :
                             plannedSections.value(field.parentPath);
                }
                else if (document.syntax == Syntax::Ini)
                    at = endOfFirstSection();
                inserts.append({at, formatted});
            }
        }
        // Bottom-up, so an earlier insert does not shift a later index. Same index keeps the queued order.
        for (int i = inserts.size() - 1; i >= 0; --i)
            lines.insert(inserts.at(i).first, inserts.at(i).second);

        if (out)
            *out = joinLines(lines, document.newline, document.trailingNewline);
        return true;
    }

    namespace
    {
        Spec field(QString kind, QString alias, QString missing)
        {
            Spec spec;
            spec.kind = std::move(kind);
            spec.aliases = {std::move(alias)};
            spec.label = spec.aliases.constFirst();
            spec.missing = std::move(missing);
            return spec;
        }

        bool roundTrip(Syntax syntax, const QString &text, const QList<Spec> &specs, QString *why)
        {
            const auto loaded = loadText(syntax, text, specs);
            QString saved;
            QString error;
            if (!saveText(loaded, &saved, &error) || saved != text)
            {
                *why = error.isEmpty() ? "save changed a file nothing edited"_L1 : error;
                return false;
            }
            return true;
        }
    } // namespace

    bool selfCheck()
    {
        QString why;
        const auto portal = "TurnSpeed=0.15\r\nVRScale=43.2 # Real word units to source units scale\r\n6DOF=true\r\n"_L1;
        const QList<Spec> portalSpecs{field("number"_L1, "TurnSpeed"_L1, "0.15"_L1),
                                      field("number"_L1, "VRScale"_L1, "43.2"_L1),
                                      field("bool"_L1, "6DOF"_L1, "true"_L1),
                                      field("bool"_L1, "SnapTurning"_L1, "false"_L1)};
        if (!roundTrip(Syntax::Equals, portal, portalSpecs, &why))
            return false;

        auto edited = loadText(Syntax::Equals, portal, portalSpecs);
        edited.fields[1].value = "40"_L1;
        QString saved;
        if (!saveText(edited, &saved, &why) ||
            saved != "TurnSpeed=0.15\r\nVRScale=40 # Real word units to source units scale\r\n6DOF=true\r\n"_L1)
            return false;
        edited.fields[1].value = edited.fields[1].original;
        edited.fields[3].value = "true"_L1;
        if (!saveText(edited, &saved, &why) || !saved.endsWith("SnapTurning=true\r\n"_L1) ||
            !saved.contains("VRScale=43.2 #"_L1))
            return false;

        const auto ini = "[General]\r\nenabled = true\r\ntarget_assembly = BepInEx\\core\\BepInEx.Preloader.dll\r\n\r\n"
                         "[UnityMono]\r\ndebug_enabled = false\r\n"_L1;
        auto ignore = field("bool"_L1, "ignoreDisableSwitch"_L1, "false"_L1);
        ignore.aliases << "ignore_disable_switch"_L1;
        const QList<Spec> iniSpecs{field("bool"_L1, "enabled"_L1, "true"_L1), ignore};
        if (!roundTrip(Syntax::Ini, ini, iniSpecs, &why))
            return false;
        auto iniDoc = loadText(Syntax::Ini, ini, iniSpecs);
        iniDoc.fields[1].value = "true"_L1;
        if (!saveText(iniDoc, &saved, &why) || !saved.contains("ignore_disable_switch = true\r\n\r\n[UnityMono]"_L1) ||
            saved.contains("ignoreDisableSwitch"_L1))
            return false;

        const auto yml = "upscaling:\r\n  enabled: true\r\n  method: cas\r\n\r\nfixedFoveated:\r\n  enabled: true\r\n"
                         "  #overrideSingleEyeOrder: LRLRLR\r\n\r\ndebugMode: false\r\n"
                         "hotkeys:\r\n  cycleUpscalingMethod: [\"ctrl\", \"f2\"]\r\n"_L1;
        auto eye = field("text"_L1, "fixedFoveated.overrideSingleEyeOrder"_L1, {});
        auto sharpness = field("number"_L1, "upscaling.sharpness"_L1, "0.7"_L1);
        const QList<Spec> ymlSpecs{field("bool"_L1, "upscaling.enabled"_L1, "true"_L1),
                                   field("choice"_L1, "upscaling.method"_L1, "cas"_L1),
                                   sharpness,
                                   field("bool"_L1, "fixedFoveated.enabled"_L1, "true"_L1),
                                   eye,
                                   field("bool"_L1, "debugMode"_L1, "false"_L1),
                                   field("keys"_L1, "hotkeys.cycleUpscalingMethod"_L1, {})};
        if (!roundTrip(Syntax::Yaml, yml, ymlSpecs, &why))
            return false;
        auto ymlDoc = loadText(Syntax::Yaml, yml, ymlSpecs);
        if (ymlDoc.fields[4].value != ""_L1 || ymlDoc.fields[6].value != "ctrl, f2"_L1)
            return false;
        ymlDoc.fields[4].value = "LRLR"_L1;
        ymlDoc.fields[2].value = "0.5"_L1;
        ymlDoc.fields[6].value = "ctrl, f3"_L1;
        if (!saveText(ymlDoc, &saved, &why))
            return false;
        if (!saved.contains("  sharpness: 0.5\r\n"_L1) || !saved.contains("  overrideSingleEyeOrder: LRLR\r\n"_L1) ||
            saved.contains("#overrideSingleEyeOrder"_L1) || !saved.contains("cycleUpscalingMethod: [\"ctrl\", \"f3\"]"_L1) ||
            !saved.contains("method: cas"_L1))
            return false;

        ymlDoc.fields[2].value = "nope"_L1;
        if (saveText(ymlDoc, &saved, &why) || !why.contains("number"_L1))
            return false;

        // A key whose section was removed still has to come back under that section, not as an orphan line.
        const auto bare = "debugMode: false\n"_L1;
        auto bareDoc = loadText(Syntax::Yaml, bare, {sharpness});
        bareDoc.fields[0].value = "0.4"_L1;
        return saveText(bareDoc, &saved, &why) && saved == "debugMode: false\nupscaling:\n  sharpness: 0.4\n"_L1;
    }

} // namespace ConfigText
