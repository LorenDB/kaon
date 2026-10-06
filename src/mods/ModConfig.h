#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>

#include "ConfigText.h"
#include "Mod.h"

class Game;

// One open config file: a row per setting, in the order the form shows them.
class ModConfigDocument : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Opened by ModConfigs")

    Q_PROPERTY(QString title READ title CONSTANT FINAL)
    Q_PROPERTY(QString path READ path CONSTANT FINAL)
    Q_PROPERTY(QString note READ note CONSTANT FINAL)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)

public:
    QString title() const { return m_title; }
    QString path() const { return m_path; }
    QString note() const { return m_note; }
    QString error() const { return m_error; }
    bool dirty() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setField(int row, const QString &value);
    Q_INVOKABLE bool save();

signals:
    void errorChanged();
    void dirtyChanged();

private:
    friend class ModConfigs;

    ModConfigDocument(QString title,
                      QString path,
                      QString note,
                      ConfigText::Syntax syntax,
                      QList<ConfigText::Spec> specs,
                      ConfigText::Document document,
                      QObject *parent = nullptr);

    enum Roles
    {
        Heading = Qt::UserRole + 1,
        Label,
        Detail,
        Kind,
        Value,
        Choices,
    };

    QString m_title;
    QString m_path;
    QString m_note;
    QString m_error;
    ConfigText::Syntax m_syntax;
    QList<ConfigText::Spec> m_specs;
    ConfigText::Document m_document;
};

// Which installed mods have a config file, and the one the settings page is editing.
class ModConfigs : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(ModConfigDocument *document READ document NOTIFY documentChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)

public:
    static ModConfigs *instance();
    static ModConfigs *create(QQmlEngine *, QJSEngine *);

    ModConfigDocument *document() const { return m_document; }
    QString error() const { return m_error; }

    // revision is unused. A QML binding passes GameStatus.revision so it runs again when installs change.
    Q_INVOKABLE bool available(Mod *mod, Game *game, int revision = 0) const;
    Q_INVOKABLE bool open(Mod *mod, Game *game);

signals:
    void documentChanged();
    void errorChanged();

private:
    explicit ModConfigs(QObject *parent = nullptr);

    ModConfigDocument *m_document = nullptr;
    QString m_error;
};
