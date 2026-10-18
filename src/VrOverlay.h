#pragma once

#include <QObject>
#include <QPointF>
#include <QQmlEngine>

#include <QFuture>
#include <QImage>
#include <QLockFile>

#include <memory>

#if defined(KAON_OPENVR)
    #include <openvr.h>
#endif

#if defined(KAON_OPENXR)
class OpenXrSession;
#endif

class QOffscreenSurface;
class QOpenGLContext;
class QQuickItem;
class QQuickWindow;
class QTimer;

// A Kaon tab in the SteamVR dashboard, so mod settings can be changed without
// taking the headset off. This mirrors the desktop window into a dashboard
// overlay (OpenVR IVROverlay, the same mechanism OpenVR Advanced Settings and
// similar tools use) and forwards the VR laser pointer as mouse input. Clicking
// a text field with the laser brings up SteamVR's keyboard, which types into it.
//
// Frames go up as an OpenGL texture, and only when the window actually
// changed. SetOverlayRaw is not used: SteamVR builds a fresh compositor image
// for every Raw upload and shows nothing until it has loaded, so each update
// flashed the tab transparent for a frame or two. A texture is copied into a
// shared image SteamVR keeps, so the old frame stays up until the new one
// replaces it. When
// SteamVR itself just booted, registration waits for it to settle, since
// registering into a half-initialized dashboard wedges it. No
// vrclient calls are made once SteamVR starts going away: the server drops a
// disconnected client's overlays by itself, and calling into it mid-shutdown
// crashes.
//
// SteamVR's OpenXR runtime does not expose XR_EXTX_overlay, so a running
// SteamVR scene uses the OpenVR dashboard tab. WiVRn and Monado do expose it:
// the window is an OpenXR overlay session composited on top of whatever else
// is running, or a regular session when that extension is missing. The OpenVR
// client library is linked statically and loads vrclient at runtime, and the
// OpenXR loader is opened only when needed, so Kaon still starts when neither
// runtime is installed. Everything here stays dormant until the switch in
// Settings is flipped.
class VrOverlay : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed FINAL)
    Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
    Q_PROPERTY(bool dashboardVisible READ dashboardVisible NOTIFY changed FINAL)
    Q_PROPERTY(QString status READ status NOTIFY changed FINAL)
    Q_PROPERTY(bool autoLaunch READ autoLaunch WRITE setAutoLaunch NOTIFY changed FINAL)
    Q_PROPERTY(bool openVrInstalled READ openVrInstalled NOTIFY changed FINAL)
    Q_PROPERTY(bool steamDashboard READ steamDashboard NOTIFY changed FINAL)

public:
    static VrOverlay *instance();
    static VrOverlay *create(QQmlEngine *, QJSEngine *);

    bool available() const { return m_available; }
    bool enabled() const { return m_enabled; }
    bool active() const { return m_connected; }
    bool dashboardVisible() const { return m_dashboardVisible; }
    QString status() const { return m_status; }
    bool autoLaunch() const { return m_autoLaunch; }
    bool openVrInstalled() const { return m_openVrInstalled; }
    bool steamDashboard() const { return m_backend == Backend::OpenVr && m_connected; }

    void setEnabled(bool enabled);
    void setAutoLaunch(bool on);

    // Opens the SteamVR dashboard on the Kaon tab. Enables the overlay first
    // when it is off.
    Q_INVOKABLE void showDashboard();
    // Re-checks for the SteamVR runtime and reconnects when due.
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
#if defined(KAON_OPENXR)
    friend class OpenXrSession;
#endif

    enum class Backend
    {
        None,
        OpenVr,
        OpenXr
    };

    explicit VrOverlay(QObject *parent = nullptr);
    ~VrOverlay() override;

    void setStatus(const QString &status);
    void refreshAvailability();
    QString unavailableStatus() const;
    void ensureConnected();
#if defined(KAON_OPENXR)
    void beginOpenXr();
    void onOpenXrConnected(bool overlaySession, quint64 generation);
    void onOpenXrFailed(const QString &message, quint64 generation);
    void onOpenXrLost(quint64 generation);
    void pushOpenXrFrame(bool force);
    void pointerMove(const QPointF &windowPos);
    void pointerButton(const QPointF &windowPos, bool down);
    void pointerScroll(const QPointF &windowPos, float dx, float dy);
#endif
    QImage captureWindow();
#if defined(KAON_OPENVR)
    void onServerDown(int error, quint64 generation);
    void onConnected(void *system, int error, quint64 generation, qint64 elapsedMs);
    void finishConnect();
    void registerManifest();
    bool createOverlay();
    void updateMouseScale();
#endif
    void shutdownVr();
    void onEventTick();
    void onFrameTick();
    void pushFrame(bool force = false);
#if defined(KAON_OPENVR)
    bool uploadTexture(const QImage &image);
    bool makeGlCurrent();
    void pollOverlay(vr::VROverlayHandle_t handle, bool thumbnail);
    void handleMouseMove(float x, float y);
    void handleMouseButton(float x, float y, uint32_t button, bool down);
    void updateKeyboard(QQuickWindow *window);
    bool isOwnKeyboardEvent(const vr::VREvent_t &event) const;
    void handleKeyboardInput(const QByteArray &input);
#endif
    bool takeOverlayLock();
    void handleScroll(float dx, float dy, float ageSeconds);
    void boostFrames();
    QQuickWindow *overlayWindow() const;
    static QQuickItem *focusedItem(QQuickWindow *window);
    static bool isEditableText(const QQuickItem *item);
    static bool isBlank(const QImage &image);
    QPointF toWindowPoint(float x, float y) const;
    QString writeThumbnail() const;

    QTimer *m_eventTimer = nullptr;
    QTimer *m_frameTimer = nullptr;
    QFuture<void> m_connectFuture;

    bool m_available = false;
    bool m_openVrInstalled = false;
    bool m_openXrInstalled = false;
    bool m_enabled = false;
    Backend m_backend = Backend::None;
#if defined(KAON_OPENXR)
    std::unique_ptr<OpenXrSession> m_openXr;
#endif
    bool m_autoLaunch = false;
    bool m_connected = false;
    bool m_connecting = false;
    bool m_dashboardVisible = false;
    bool m_showOnConnect = false;
    QString m_status;

#if defined(KAON_OPENVR)
    vr::VROverlayHandle_t m_mainHandle = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t m_thumbHandle = vr::k_ulOverlayHandleInvalid;
#endif

    // Size of the image currently uploaded to the overlay, and the factor that
    // maps overlay pixels back to desktop window points.
    int m_uploadWidth = 0;
    int m_uploadHeight = 0;
    float m_uploadScale = 1.0f;

    // Skips re-uploading a window that has not changed.
    QImage m_lastUploaded;

#if defined(KAON_OPENVR)
    // The overlay's texture lives in a private GL context, independent of
    // whatever Qt Quick renders with. SteamVR copies it on every
    // SetOverlayTexture, so one texture is reused for all frames and kept
    // across reconnects.
    std::unique_ptr<QOffscreenSurface> m_glSurface;
    std::unique_ptr<QOpenGLContext> m_glContext;
    uint m_texture = 0;
    int m_textureWidth = 0;
    int m_textureHeight = 0;
#endif

    // Bumped on every shutdown so a VR_Init that is still handshaking in the
    // background cannot resurrect (or tear down) the new state when it lands.
    // Only onConnected clears m_connecting, so at most one handshake is ever
    // in flight, even across disable/re-enable.
    quint64 m_initGeneration = 0;
    // Set while waiting out a fresh SteamVR boot before creating anything.
    bool m_settling = false;
    // Forces the next connect to re-register the manifest even when known.
    bool m_forceManifest = false;
    // Consecutive failed handshakes; backs the reconnect timer off.
    int m_failCount = 0;
    // Only one Kaon may own the dashboard tab. A second instance (e.g. the
    // desktop app plus a SteamVR auto-launch) uploading to the same overlay
    // key flickers and fights over input.
    std::unique_ptr<QLockFile> m_overlayLock;

    QPointF m_lastMouse;
    Qt::MouseButtons m_mouseButtons{};
    qint64 m_lastAttemptMs = 0;
    // Frames go up faster until then, while VR input is still animating.
    qint64 m_boostUntil = 0;
    // Wheel timestamps must keep increasing for Flickable to take them.
    quint64 m_lastWheelTimestamp = 0;
#if defined(KAON_OPENVR)
    // SteamVR's keyboard is up for the overlay. It hides itself when the user
    // closes it; that comes back as a closed event.
    bool m_keyboardOpen = false;
#endif
};
