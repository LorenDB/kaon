#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// Reads and writes the small text configs mods install, changing only the lines the player edited. Comments, keys a
// newer release added, and the file's line endings stay as they were.
namespace ConfigText
{

    enum class Syntax
    {
        // Portal's config.txt: Key=value, with an optional " # comment"
        Equals,
        // Doorstop's ini: key = value, under [Section] headers that are left alone
        Ini,
        // vrperfkit.yml: nested "key: value", two spaces per level
        Yaml,
    };

    struct Spec
    {
        QString section;
        QString label;
        QString detail;
        // "bool", "number", "choice", "text" or "keys" (a comma-separated list written as ["a", "b"])
        QString kind;
        // Equals and ini: key names, camelCase and snake_case for the same setting. Yaml: one dotted path,
        // "upscaling.enabled".
        QStringList aliases;
        // What to show when the file doesn't have the key. Also the value that means "don't add the key on save".
        QString missing;
        QStringList choiceIds;
        QStringList choiceLabels;
        // Leave the setting out of the form when the file doesn't already have it
        bool onlyIfPresent = false;
    };

    struct Field
    {
        Spec spec;
        QString value;
        QString original;
        int line = -1;
        QString key;
        QString prefix;
        QString separator;
        QString suffix;
        QString parentPath;
        int indent = 0;
        bool commented = false;
    };

    struct Document
    {
        Syntax syntax = Syntax::Equals;
        QString newline = "\n";
        bool trailingNewline = true;
        QStringList lines;
        QList<Field> fields;
        QHash<QString, int> sectionLines;
        QHash<QString, int> sectionIndents;
        bool snakeKeys = false;
    };

    Document loadText(Syntax syntax, const QString &text, const QList<Spec> &specs);
    // False, and error set, when a number isn't a number. out is the full file.
    bool saveText(const Document &document, QString *out, QString *error);

    // Round-trip checks for the three syntaxes. False means the editor would corrupt a file.
    bool selfCheck();

} // namespace ConfigText
