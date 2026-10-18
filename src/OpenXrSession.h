#pragma once

#include <QImage>
#include <QObject>
#include <QSize>

#include <atomic>
#include <memory>

class QOffscreenSurface;
class QThread;
class VrOverlay;

// An OpenXR session that mirrors Kaon's window into the headset.
//
// When the runtime advertises XR_EXTX_overlay (WiVRn and Monado do; SteamVR's
// OpenXR runtime does not), the session is created as an overlay: its
// composition layers are drawn on top of whatever else is running, so Kaon
// can sit beside a game. Otherwise this is an ordinary session and Kaon is
// the main headset app. Layers are submitted with xrEndFrame either way.
//
// The OpenXR loader is opened at runtime (libopenxr_loader.so.1). Kaon still
// starts when it is not installed; this session simply never comes up.
class OpenXrSession : public QObject
{
    Q_OBJECT

public:
    explicit OpenXrSession(VrOverlay *overlay);
    ~OpenXrSession() override;

    // True when the loader library can be dlopen'd. Does not contact a runtime.
    static bool loaderPresent();

    void start(quint64 generation);
    void stop();

    // Latest window grab, top-down RGBA8888. Copied off for the session thread.
    void submitFrame(const QImage &image, QSize windowSize);

signals:
    void connected(bool overlaySession, quint64 generation);
    void failed(const QString &message, quint64 generation);
    void disconnected(quint64 generation);

private:
    friend class OpenXrLoop;

    void run();
    void queueMove(QPointF pos);
    void queueButton(QPointF pos, bool down);
    void queueScroll(QPointF pos, float dx, float dy);
    void deliverMove(QPointF pos);
    void deliverButton(QPointF pos, bool down);
    void deliverScroll(QPointF pos, float dx, float dy);

    VrOverlay *m_overlay = nullptr;
    QThread *m_thread = nullptr;
    std::unique_ptr<QOffscreenSurface> m_surface;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_acceptInput{false};
    quint64 m_generation = 0;
    // Captured on the UI thread; the session thread only reads them.
    void *m_waylandDisplay = nullptr;
    void *m_x11Display = nullptr;

    // Written on the UI thread, read on the session thread.
    class Frame;
    std::unique_ptr<Frame> m_frame;
};
