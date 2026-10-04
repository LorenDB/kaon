#pragma once

#include <QObject>
#include <QQmlEngine>

class GamepadPrivate;

// Reads Linux gamepads (/dev/input/event*) and turns them into UI actions. The kernel's gamepad
// specification names the buttons (south is A, east is B), so Xbox, PlayStation, and Deck pads match.
class Gamepad : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged FINAL)

public:
    static Gamepad *instance();
    static Gamepad *create(QQmlEngine *, QJSEngine *);

    bool connected() const;

    // Deliver a key to the focused window the same way a physical key arrives.
    Q_INVOKABLE void postKey(int key);

signals:
    void connectedChanged();

    // dx and dy are each -1, 0, or 1. One axis is set at a time.
    void navigate(int dx, int dy);
    void activate();
    void back();
    // -1 for the previous main tab, +1 for the next
    void tab(int direction);
    void primary();
    void search();
    // Right stick, each axis in -1..1. Positive y scrolls down.
    void scroll(qreal dx, qreal dy);

private:
    explicit Gamepad(QObject *parent = nullptr);
    ~Gamepad() override;

    GamepadPrivate *m_private;
};
