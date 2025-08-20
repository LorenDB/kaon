#pragma once

#include <QObject>
#include <QQmlEngine>

class UpdateChecker : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

signals:
    void updateAvailable(const QString &version, const QString &url);
};
