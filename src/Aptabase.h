#pragma once

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>

class Aptabase : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)

public:
    static void init(const QString &host, const QString &key);
    static Aptabase *instance();
    static Aptabase *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool enabled() const { return m_enabled; }

    void setEnabled(bool state);

    void track(const QString &event, const QJsonObject &properties = {}) const;

signals:
    void enabledChanged(bool state);

private:
    explicit Aptabase(const QString &host, const QString &key, QObject *parent = nullptr);
    static Aptabase *s_instance;

    QString m_host;
    QString m_key;
    int m_sessionId{0};
    bool m_enabled{true};
};
