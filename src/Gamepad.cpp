#include "Gamepad.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>

#include <utility>

#include <linux/input.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(GamepadLog, "gamepad")

namespace
{
    struct Axis
    {
        bool present = false;
        int minimum = 0;
        int maximum = 0;
        int value = 0;
        int flat = 0;

        // -1..1, with the kernel deadzone already removed. 0 while the stick is inside it.
        double normalized() const
        {
            if (!present || maximum == minimum)
                return 0;
            const double span = double(maximum) - double(minimum);
            const double raw = (double(value) - double(minimum)) / span * 2.0 - 1.0;
            double dead = 0.22;
            if (flat > 0)
                dead = std::max(dead, (2.0 * double(flat)) / span);
            dead = std::min(dead, 0.9);
            if (std::abs(raw) <= dead)
                return 0;
            const double sign = raw < 0 ? -1.0 : 1.0;
            return sign * std::clamp((std::abs(raw) - dead) / (1.0 - dead), 0.0, 1.0);
        }
    };

    struct Pad
    {
        QString path;
        QString name;
        int fd = -1;
        QSocketNotifier *notifier = nullptr;
        QByteArray pending;
        bool closing = false;
        // Cheap pads send arrow keys instead of a hat. Real keyboards are not opened.
        bool arrowsAreDpad = false;

        Axis x;
        Axis y;
        Axis rx;
        Axis ry;
        Axis hatX;
        Axis hatY;

        bool dpadLeft = false;
        bool dpadRight = false;
        bool dpadUp = false;
        bool dpadDown = false;
        bool preferHorizontal = true;

        int stickX = 0;
        int stickY = 0;
        int dirX = 0;
        int dirY = 0;
        double rightX = 0;
        double rightY = 0;
    };

    bool testBit(const unsigned long *bits, int bit)
    {
        const int width = int(sizeof(unsigned long) * 8);
        return (bits[bit / width] >> (bit % width)) & 1UL;
    }

    bool readBits(int fd, unsigned int eventType, unsigned long *bits, size_t byteCount)
    {
        std::memset(bits, 0, byteCount);
        return ioctl(fd, EVIOCGBIT(eventType, byteCount), bits) >= 0;
    }

    Axis readAxis(int fd, int code, const unsigned long *absBits)
    {
        Axis axis;
        if (!testBit(absBits, code))
            return axis;
        input_absinfo info{};
        if (ioctl(fd, EVIOCGABS(code), &info) < 0)
            return axis;
        axis.present = true;
        axis.minimum = info.minimum;
        axis.maximum = info.maximum;
        axis.value = info.value;
        axis.flat = info.flat;
        return axis;
    }

    // One axis at a time, and the choice sticks until the stick returns near center.
    void updateStick(Pad &pad)
    {
        const double x = pad.x.normalized();
        const double y = pad.y.normalized();
        const double ax = std::abs(x);
        const double ay = std::abs(y);
        pad.rightX = pad.rx.normalized();
        pad.rightY = pad.ry.normalized();

        if (ax < 0.25 && ay < 0.25)
        {
            pad.stickX = 0;
            pad.stickY = 0;
            return;
        }

        if (ax >= ay)
        {
            pad.stickY = 0;
            if (ax > 0.55)
                pad.stickX = x > 0 ? 1 : -1;
            else if (ax < 0.25)
                pad.stickX = 0;
        }
        else
        {
            pad.stickX = 0;
            if (ay > 0.55)
                pad.stickY = y > 0 ? 1 : -1;
            else if (ay < 0.25)
                pad.stickY = 0;
        }
    }

    int hatDirection(const Axis &axis)
    {
        if (!axis.present)
            return 0;
        const int mid = (axis.minimum + axis.maximum) / 2;
        if (axis.value == mid)
            return 0;
        return axis.value > mid ? 1 : -1;
    }

    void updateDirection(Pad &pad)
    {
        updateStick(pad);

        int dx = 0;
        int dy = 0;
        if (pad.dpadLeft)
            dx -= 1;
        if (pad.dpadRight)
            dx += 1;
        if (pad.dpadUp)
            dy -= 1;
        if (pad.dpadDown)
            dy += 1;
        if (dx == 0)
            dx = hatDirection(pad.hatX);
        if (dy == 0)
            dy = hatDirection(pad.hatY);
        dx = std::clamp(dx, -1, 1);
        dy = std::clamp(dy, -1, 1);

        if (dx != 0 && dy != 0)
        {
            if (pad.preferHorizontal)
                dy = 0;
            else
                dx = 0;
        }

        // The stick only moves the highlight when the d-pad is released.
        if (dx == 0 && dy == 0)
        {
            dx = pad.stickX;
            dy = pad.stickY;
        }

        pad.dirX = dx;
        pad.dirY = dy;
    }
} // namespace

class GamepadPrivate
{
public:
    explicit GamepadPrivate(Gamepad *owner)
        : q(owner)
    {}

    ~GamepadPrivate()
    {
        shuttingDown = true;
        const auto paths = padPaths();
        for (const auto &path : paths)
            drop(path);
    }

    bool uiActive() const
    {
        const auto *window = QGuiApplication::focusWindow();
        // A native file dialog is a different window. Ignore the pad until Kaon itself is in front.
        return qobject_cast<const QQuickWindow *>(window) && window->isActive();
    }

    void start()
    {
        watcher.addPath("/dev/input"_L1);
        QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, q, [this] { rescanTimer.start(); });

        rescanTimer.setSingleShot(true);
        rescanTimer.setInterval(200);
        QObject::connect(&rescanTimer, &QTimer::timeout, q, [this] { rescan(); });

        // inotify misses some device nodes, so look again every few seconds. The check is a directory listing.
        pollTimer.setInterval(3000);
        QObject::connect(&pollTimer, &QTimer::timeout, q, [this] { rescan(); });
        pollTimer.start();

        navTimer.setSingleShot(false);
        QObject::connect(&navTimer, &QTimer::timeout, q, [this] { onNavTick(); });

        scrollTimer.setInterval(16);
        QObject::connect(&scrollTimer, &QTimer::timeout, q, [this] {
            if (!uiActive())
                return;
            emit q->scroll(scrollX, scrollY);
        });

        rescan();
    }

    void rescan()
    {
        if (scanning)
            return;
        scanning = true;

        QDir dir{"/dev/input"_L1};
        const auto names = dir.entryList({"event*"_L1}, QDir::System | QDir::Files);
        QSet<QString> live;
        for (const auto &name : names)
        {
            const auto path = dir.absoluteFilePath(name);
            live.insert(path);
            if (!padByPath(path))
                tryOpen(path);
        }

        const auto open = padPaths();
        for (const auto &path : open)
        {
            if (!live.contains(path))
                drop(path);
        }

        scanning = false;
        publishConnection();
        publishMotion();
    }

    void tryOpen(const QString &path)
    {
        const int fd = ::open(path.toUtf8().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
        {
            if (errno != EACCES && errno != ENODEV)
                qCDebug(GamepadLog) << "Could not open" << path << strerror(errno);
            return;
        }

        unsigned long evBits[(EV_MAX / (sizeof(unsigned long) * 8)) + 1]{};
        unsigned long keyBits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1]{};
        unsigned long absBits[(ABS_MAX / (sizeof(unsigned long) * 8)) + 1]{};
        if (!readBits(fd, 0, evBits, sizeof(evBits)) || !testBit(evBits, EV_KEY) ||
            !readBits(fd, EV_KEY, keyBits, sizeof(keyBits)) || !testBit(keyBits, BTN_SOUTH))
        {
            ::close(fd);
            return;
        }
        readBits(fd, EV_ABS, absBits, sizeof(absBits));

        auto *pad = new Pad;
        pad->path = path;
        pad->fd = fd;
        char name[256]{};
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0)
            pad->name = QString::fromUtf8(name);
        pad->x = readAxis(fd, ABS_X, absBits);
        pad->y = readAxis(fd, ABS_Y, absBits);
        pad->rx = readAxis(fd, ABS_RX, absBits);
        pad->ry = readAxis(fd, ABS_RY, absBits);
        pad->hatX = readAxis(fd, ABS_HAT0X, absBits);
        pad->hatY = readAxis(fd, ABS_HAT0Y, absBits);
        const bool keyboard = testBit(keyBits, KEY_Q) && testBit(keyBits, KEY_A) && testBit(keyBits, KEY_Z);
        pad->arrowsAreDpad = !keyboard && testBit(keyBits, KEY_UP) && testBit(keyBits, KEY_LEFT);

        pad->notifier = new QSocketNotifier(fd, QSocketNotifier::Read, q);
        QObject::connect(pad->notifier,
                         &QSocketNotifier::activated,
                         q,
                         [this, pad](QSocketDescriptor, QSocketNotifier::Type) { readPad(pad); });
        pads.append(pad);
        updateDirection(*pad);
        qCInfo(GamepadLog) << "Gamepad connected:" << (pad->name.isEmpty() ? path : pad->name);
    }

    void readPad(Pad *pad)
    {
        if (!pad || pad->closing || pad->fd < 0)
            return;

        char buf[64 * sizeof(input_event)];
        while (true)
        {
            const ssize_t count = ::read(pad->fd, buf, sizeof(buf));
            if (count < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                if (errno == EINTR)
                    continue;
                drop(pad->path);
                return;
            }
            if (count == 0)
            {
                drop(pad->path);
                return;
            }
            pad->pending.append(buf, int(count));
        }

        while (pad->pending.size() >= int(sizeof(input_event)))
        {
            input_event event{};
            std::memcpy(&event, pad->pending.constData(), sizeof(event));
            pad->pending.remove(0, int(sizeof(event)));
            handleEvent(*pad, event);
        }
    }

    void handleEvent(Pad &pad, const input_event &event)
    {
        if (event.type == EV_KEY)
        {
            const bool down = event.value != 0;
            switch (event.code)
            {
            case BTN_SOUTH:
                if (event.value == 1 && uiActive())
                    emit q->activate();
                break;
            case BTN_EAST:
                if (event.value == 1 && uiActive())
                    emit q->back();
                break;
            case BTN_TL:
                if (event.value == 1 && uiActive())
                    emit q->tab(-1);
                break;
            case BTN_TR:
                if (event.value == 1 && uiActive())
                    emit q->tab(1);
                break;
            case BTN_START:
                if (event.value == 1 && uiActive())
                    emit q->primary();
                break;
            case BTN_SELECT:
                if (event.value == 1 && uiActive())
                    emit q->search();
                break;
            case BTN_DPAD_LEFT:
                pad.dpadLeft = down;
                if (down)
                    pad.preferHorizontal = true;
                break;
            case BTN_DPAD_RIGHT:
                pad.dpadRight = down;
                if (down)
                    pad.preferHorizontal = true;
                break;
            case BTN_DPAD_UP:
                pad.dpadUp = down;
                if (down)
                    pad.preferHorizontal = false;
                break;
            case BTN_DPAD_DOWN:
                pad.dpadDown = down;
                if (down)
                    pad.preferHorizontal = false;
                break;
            case KEY_LEFT:
                if (!pad.arrowsAreDpad)
                    return;
                pad.dpadLeft = down;
                if (down)
                    pad.preferHorizontal = true;
                break;
            case KEY_RIGHT:
                if (!pad.arrowsAreDpad)
                    return;
                pad.dpadRight = down;
                if (down)
                    pad.preferHorizontal = true;
                break;
            case KEY_UP:
                if (!pad.arrowsAreDpad)
                    return;
                pad.dpadUp = down;
                if (down)
                    pad.preferHorizontal = false;
                break;
            case KEY_DOWN:
                if (!pad.arrowsAreDpad)
                    return;
                pad.dpadDown = down;
                if (down)
                    pad.preferHorizontal = false;
                break;
            default:
                break;
            }
        }
        else if (event.type == EV_ABS)
        {
            switch (event.code)
            {
            case ABS_X:
                pad.x.value = event.value;
                break;
            case ABS_Y:
                pad.y.value = event.value;
                break;
            case ABS_RX:
                pad.rx.value = event.value;
                break;
            case ABS_RY:
                pad.ry.value = event.value;
                break;
            case ABS_HAT0X:
                pad.hatX.value = event.value;
                if (event.value != (pad.hatX.minimum + pad.hatX.maximum) / 2)
                    pad.preferHorizontal = true;
                break;
            case ABS_HAT0Y:
                pad.hatY.value = event.value;
                if (event.value != (pad.hatY.minimum + pad.hatY.maximum) / 2)
                    pad.preferHorizontal = false;
                break;
            default:
                break;
            }
        }
        else if (event.type != EV_SYN)
        {
            return;
        }

        updateDirection(pad);
        publishMotion();
    }

    void publishMotion()
    {
        if (shuttingDown)
        {
            navTimer.stop();
            scrollTimer.stop();
            return;
        }

        int dx = 0;
        int dy = 0;
        double rx = 0;
        double ry = 0;
        double bestStick = 0;
        for (const auto *pad : std::as_const(pads))
        {
            if (pad->dirX || pad->dirY)
            {
                dx = pad->dirX;
                dy = pad->dirY;
            }
            const double magnitude = pad->rightX * pad->rightX + pad->rightY * pad->rightY;
            if (magnitude > bestStick)
            {
                bestStick = magnitude;
                rx = pad->rightX;
                ry = pad->rightY;
            }
        }

        if (dx != heldX || dy != heldY)
        {
            heldX = dx;
            heldY = dy;
            if (dx == 0 && dy == 0)
            {
                navTimer.stop();
            }
            else
            {
                if (uiActive())
                {
                    qCDebug(GamepadLog) << "navigate" << dx << dy;
                    emit q->navigate(dx, dy);
                }
                navTimer.setInterval(360);
                navTimer.start();
            }
        }

        scrollX = rx;
        scrollY = ry;
        if (bestStick > 0.01)
        {
            if (!scrollTimer.isActive())
                scrollTimer.start();
        }
        else
        {
            scrollTimer.stop();
        }
    }

    void onNavTick()
    {
        if (heldX == 0 && heldY == 0)
        {
            navTimer.stop();
            return;
        }
        if (uiActive())
        {
            qCDebug(GamepadLog) << "navigate" << heldX << heldY;
            emit q->navigate(heldX, heldY);
        }
        if (navTimer.interval() != 130)
            navTimer.setInterval(130);
    }

    void drop(const QString &path)
    {
        Pad *pad = padByPath(path);
        if (!pad || pad->closing)
            return;
        pad->closing = true;
        qCInfo(GamepadLog) << "Gamepad disconnected:" << (pad->name.isEmpty() ? pad->path : pad->name);
        if (pad->notifier)
        {
            // The notifier may be the object whose slot is running, so don't delete it inline.
            pad->notifier->disconnect();
            pad->notifier->setEnabled(false);
            pad->notifier->deleteLater();
            pad->notifier = nullptr;
        }
        if (pad->fd >= 0)
        {
            ::close(pad->fd);
            pad->fd = -1;
        }
        pads.removeAll(pad);
        delete pad;
        publishConnection();
        publishMotion();
    }

    void publishConnection()
    {
        if (shuttingDown)
            return;
        const bool now = !pads.isEmpty();
        if (now == connected)
            return;
        connected = now;
        emit q->connectedChanged();
    }

    Pad *padByPath(const QString &path) const
    {
        for (auto *pad : pads)
        {
            if (pad->path == path)
                return pad;
        }
        return nullptr;
    }

    QStringList padPaths() const
    {
        QStringList paths;
        for (const auto *pad : pads)
            paths.append(pad->path);
        return paths;
    }

    Gamepad *q;
    QList<Pad *> pads;
    QFileSystemWatcher watcher;
    QTimer rescanTimer;
    QTimer pollTimer;
    QTimer navTimer;
    QTimer scrollTimer;
    bool shuttingDown = false;
    bool scanning = false;
    bool connected = false;
    int heldX = 0;
    int heldY = 0;
    double scrollX = 0;
    double scrollY = 0;
};

Gamepad::Gamepad(QObject *parent)
    : QObject{parent},
      m_private{new GamepadPrivate{this}}
{
    m_private->start();
}

Gamepad::~Gamepad()
{
    delete m_private;
}

Gamepad *Gamepad::instance()
{
    static auto *pad = new Gamepad;
    return pad;
}

Gamepad *Gamepad::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

bool Gamepad::connected() const
{
    return m_private->connected;
}

void Gamepad::postKey(int key)
{
    auto *window = QGuiApplication::focusWindow();
    if (!window)
        return;
    const auto qtKey = static_cast<Qt::Key>(key);
    // Posted, so delivery goes through the normal key path after this call returns.
    QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyPress, qtKey, Qt::NoModifier));
    QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyRelease, qtKey, Qt::NoModifier));
}
