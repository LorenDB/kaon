#pragma once

#include <QObject>
#include <QQmlEngine>

class UpdateChecker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(QString ignore READ ignore WRITE setIgnore NOTIFY ignoreChanged FINAL)

public:
    static UpdateChecker *instance();
    static UpdateChecker *create(QQmlEngine *, QJSEngine *);

    Q_INVOKABLE void checkUpdates();

    bool enabled() const { return m_enabled; }
    QString ignore() const { return m_ignore; }

    void setEnabled(bool state);
    void setIgnore(const QString &ignore);

signals:
    void updateAvailable(const QString &version, const QString &url);

    void enabledChanged(bool state);
    void ignoreChanged(const QString &ignore);

private:
    explicit UpdateChecker(QObject *parent = nullptr);
    static UpdateChecker *s_instance;

    bool m_enabled{true};
    QString m_ignore;
};
