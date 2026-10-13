#include "VrOverlay.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFuture>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPixmap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>
#include <QtConcurrentRun>

#include <chrono>

Q_LOGGING_CATEGORY(VrOverlayLog, "vr")

namespace
{
    constexpr const char *AppKey = "dev.lorendb.kaon";
    constexpr const char *OverlayKey = "dev.lorendb.kaon.dashboard";
    constexpr int EventIntervalMs = 20;
    constexpr int FrameIntervalMs = 200;
    // Right after VR input the window is usually animating (a wheel flick, a
    // page fade), which the idle rate would show as a slideshow.
    constexpr int BoostIntervalMs = 33;
    constexpr qint64 BoostMs = 1000;
    // Wheel angle per VR scroll notch. A desktop notch is 120, which felt slow
    // in VR; 180 scrolls a held stick about a third faster (around 1060 px/s
    // at SteamVR's ~17 notches a second). Flickable caps flick velocity, so
    // much past 240 adds little.
    constexpr float ScrollNotchAngle = 180.0f;
    // A second copy of one keystroke arrives within this window; a person
    // pressing the same key twice cannot.
    constexpr float KeyRepeatCopySeconds = 0.005f;
    constexpr int MaxUploadWidth = 1920;
    constexpr float OverlayWidthMeters = 2.5f;
    constexpr qint64 ReconnectIntervalMs = 5000;
    // A handshake that stalls this long means SteamVR itself was still coming
    // up, so the overlay waits a beat before registering anything.
    constexpr qint64 SlowHandshakeMs = 4000;
    constexpr qint64 SettleDelayMs = 8000;

    // Qt Quick measures pointer velocity from event timestamps, and a
    // Flickable drops a wheel event that is no newer than the last one it saw,
    // so synthetic input needs real ones; left at 0, only the first notch of
    // a scroll ever moved. Wayland stamps real input with CLOCK_MONOTONIC
    // milliseconds, which steady_clock is on Linux, so laser and desk mouse
    // agree.
    quint64 eventTimestamp()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
} // namespace

VrOverlay::VrOverlay(QObject *parent)
    : QObject{parent}
{
    QSettings settings;
    settings.beginGroup("vr"_L1);
    m_enabled = settings.value("enabled"_L1, false).toBool();
    m_autoLaunch = settings.value("autoLaunch"_L1, false).toBool();

    m_available = vr::VR_IsRuntimeInstalled();
    if (!m_available)
        m_status = "SteamVR was not found on this computer."_L1;
    else if (m_enabled)
        m_status = "Connecting to SteamVR."_L1;
    else
        m_status = "Off."_L1;

    m_eventTimer = new QTimer{this};
    m_eventTimer->setInterval(EventIntervalMs);
    connect(m_eventTimer, &QTimer::timeout, this, &VrOverlay::onEventTick);

    m_frameTimer = new QTimer{this};
    m_frameTimer->setInterval(FrameIntervalMs);
    connect(m_frameTimer, &QTimer::timeout, this, &VrOverlay::onFrameTick);

    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &VrOverlay::shutdownVr);

    if (m_enabled && m_available)
    {
        // The event timer drives reconnects, so it runs whenever the overlay
        // is wanted, not just while connected. The first handshake waits for
        // the event loop (and the window) instead of racing app startup,
        // exactly like enabling the switch by hand later does.
        m_eventTimer->start();
        QTimer::singleShot(0, this, &VrOverlay::ensureConnected);
    }
}

VrOverlay::~VrOverlay() = default;

VrOverlay *VrOverlay::instance()
{
    static auto v = new VrOverlay;
    return v;
}

VrOverlay *VrOverlay::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

void VrOverlay::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;

    QSettings settings;
    settings.beginGroup("vr"_L1);
    settings.setValue("enabled"_L1, enabled);

    if (enabled)
    {
        m_available = vr::VR_IsRuntimeInstalled();
        if (!m_available)
        {
            setStatus("SteamVR was not found on this computer."_L1);
            return;
        }
        setStatus("Connecting to SteamVR."_L1);
        m_eventTimer->start();
        ensureConnected();
    }
    else
    {
        m_showOnConnect = false;
        m_eventTimer->stop();
        m_frameTimer->stop();
        shutdownVr();
        setStatus(m_available ? "Off."_L1 : "SteamVR was not found on this computer."_L1);
    }
}

void VrOverlay::setAutoLaunch(bool on)
{
    if (on == m_autoLaunch)
        return;
    m_autoLaunch = on;

    QSettings settings;
    settings.beginGroup("vr"_L1);
    settings.setValue("autoLaunch"_L1, on);

    if (m_connected && vr::VRApplications() != nullptr)
    {
        const auto error = vr::VRApplications()->SetApplicationAutoLaunch(AppKey, on);
        if (error != vr::VRApplicationError_None)
            qCWarning(VrOverlayLog) << "Could not set SteamVR auto-launch:"
                                    << vr::VRApplications()->GetApplicationsErrorNameFromEnum(error);
    }
    emit changed();
}

void VrOverlay::showDashboard()
{
    if (!m_enabled)
    {
        m_showOnConnect = true;
        setEnabled(true);
        return;
    }
    if (m_connected && vr::VROverlay() != nullptr)
        vr::VROverlay()->ShowDashboard(OverlayKey);
    else
    {
        m_showOnConnect = true;
        ensureConnected();
    }
}

void VrOverlay::refresh()
{
    m_available = vr::VR_IsRuntimeInstalled();
    if (!m_available)
    {
        setStatus("SteamVR was not found on this computer."_L1);
        return;
    }
    if (m_enabled)
    {
        m_eventTimer->start();
        if (!m_connected && !m_connecting && !m_settling)
        {
            // "Retry the connection" also re-registers the manifest, in case
            // Kaon moved and SteamVR still points at the old path.
            m_forceManifest = true;
            setStatus("Connecting to SteamVR."_L1);
            ensureConnected();
        }
    }
    emit changed();
}

void VrOverlay::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    emit changed();
}

void VrOverlay::ensureConnected()
{
    if (m_connected || m_connecting || m_settling || !m_enabled)
        return;
    m_connecting = true;
    m_lastAttemptMs = QDateTime::currentMSecsSinceEpoch();

    // VR_Init blocks while SteamVR starts, which can take a while, so it runs
    // off the UI thread. The interface pointers are used back on it. The
    // generation lets onConnected tell a stale handshake apart from the
    // current one when a shutdown lands in between.
    const quint64 generation = m_initGeneration;
    const qint64 startedMs = QDateTime::currentMSecsSinceEpoch();
    m_connectFuture = QtConcurrent::run([this, generation, startedMs] {
        // Probe first: a Background handshake fails fast when SteamVR is down
        // and, crucially, never starts it. Kaon must only attach to a server
        // the user started themselves; bootstrapping one mid-handshake and
        // registering into it wedges the dashboard (the system button stops
        // opening it).
        auto probeError = vr::VRInitError_None;
        void *probe = vr::VR_Init(&probeError, vr::VRApplication_Background);
        if (probe == nullptr || probeError != vr::VRInitError_None)
        {
            QMetaObject::invokeMethod(this, [this, probeError, generation] { onServerDown(probeError, generation); });
            return;
        }
        vr::VR_Shutdown();

        auto error = vr::VRInitError_None;
        void *system = vr::VR_Init(&error, vr::VRApplication_Overlay);
        const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - startedMs;
        QMetaObject::invokeMethod(
            this, [this, system, error, generation, elapsedMs] { onConnected(system, error, generation, elapsedMs); });
    });
}

void VrOverlay::onServerDown(int error, quint64 generation)
{
    // Completion path like onConnected: only these clear the in-flight flag.
    m_connecting = false;
    if (generation != m_initGeneration)
        return;
    ++m_failCount;
    const auto code = static_cast<vr::HmdError>(error);
    if (error == vr::VRInitError_Init_NoServerForBackgroundApp)
    {
        qCDebug(VrOverlayLog) << "SteamVR is not running; waiting for it";
        setStatus("Waiting for SteamVR to start. Start it from Steam; the tab appears on its own."_L1);
    }
    else
    {
        qCWarning(VrOverlayLog) << "SteamVR probe failed:" << vr::VR_GetVRInitErrorAsEnglishDescription(code);
        setStatus("Could not reach SteamVR (%1). It will retry while this stays on."_L1.arg(
            QString::fromUtf8(vr::VR_GetVRInitErrorAsEnglishDescription(code))));
    }
}

void VrOverlay::onConnected(void *system, int error, quint64 generation, qint64 elapsedMs)
{
    // Only onConnected clears this, so at most one handshake is ever in
    // flight, even if a shutdown (which bumps the generation) lands mid-way.
    m_connecting = false;
    if (generation != m_initGeneration)
    {
        // Shut down (or disabled) while the handshake was in flight. Balance
        // a successful init so vrclient is not left half-open, unless a newer
        // connection is already live, which must not be touched.
        if (!m_connected && !m_settling && error == vr::VRInitError_None && system != nullptr)
            vr::VR_Shutdown();
        return;
    }
    if (error != vr::VRInitError_None || system == nullptr)
    {
        ++m_failCount;
        qCWarning(VrOverlayLog) << "VR_Init failed:"
                                << vr::VR_GetVRInitErrorAsEnglishDescription(static_cast<vr::HmdError>(error));
        setStatus("Could not reach SteamVR (%1). It will retry while this stays on."_L1.arg(
            QString::fromUtf8(vr::VR_GetVRInitErrorAsEnglishDescription(static_cast<vr::HmdError>(error)))));
        return;
    }
    const int failuresBefore = m_failCount;
    m_failCount = 0;

    if (failuresBefore > 0 || elapsedMs > SlowHandshakeMs)
    {
        // SteamVR just finished booting; registering overlays into a
        // half-initialized dashboard wedges it (e.g. the system button stops
        // opening it). Give it a moment to settle first.
        m_settling = true;
        setStatus("SteamVR is still starting; the tab appears once it settles."_L1);
        QTimer::singleShot(SettleDelayMs, this, [this, generation] {
            m_settling = false;
            // A newer disable/reconnect owns everything now; the session this
            // wait was holding was already closed by that shutdown.
            if (generation != m_initGeneration)
                return;
            finishConnect();
        });
        return;
    }
    finishConnect();
}

void VrOverlay::finishConnect()
{
    // The lock lives here (not in onConnected) so there is exactly one owner
    // check per connection, however it got here.
    if (!m_enabled)
    {
        shutdownVr();
        return;
    }
    if (!takeOverlayLock())
    {
        vr::VR_Shutdown();
        return;
    }

    registerManifest();
    if (!createOverlay())
    {
        m_overlayLock.reset();
        vr::VR_Shutdown();
        return;
    }

    if (vr::VRApplications() != nullptr)
    {
        const auto launchError = vr::VRApplications()->SetApplicationAutoLaunch(AppKey, m_autoLaunch);
        if (launchError != vr::VRApplicationError_None)
            qCWarning(VrOverlayLog) << "Could not set SteamVR auto-launch:"
                                    << vr::VRApplications()->GetApplicationsErrorNameFromEnum(launchError);
    }

    m_connected = true;
    m_eventTimer->start();
    m_frameTimer->start();
    setStatus("Kaon has a tab in the SteamVR dashboard."_L1);

    if (m_showOnConnect)
    {
        m_showOnConnect = false;
        if (vr::VROverlay() != nullptr)
            vr::VROverlay()->ShowDashboard(OverlayKey);
    }
    // A reconnect must repaint even when the window looks exactly like the
    // last uploaded frame: the new overlay starts out blank.
    pushFrame(true);
}

// Only one Kaon may own the dashboard tab: two instances uploading to the
// same overlay key flicker and fight over input.
bool VrOverlay::takeOverlayLock()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir{}.mkpath(dir);
    auto lock = std::make_unique<QLockFile>(dir + "/vr-overlay.lock"_L1);
    if (!lock->tryLock())
    {
        qCWarning(VrOverlayLog) << "Another Kaon already owns the VR overlay; staying out of VR";
        setStatus("Another Kaon is already showing in VR; its tab stays."_L1);
        return false;
    }
    m_overlayLock = std::move(lock);
    return true;
}

// SteamVR only remembers applications that ship a manifest. The binary path
// depends on how Kaon was installed (system package, AppImage, ...), so the
// manifest is written next to Kaon's data with the path of this process.
void VrOverlay::registerManifest()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir{}.mkpath(dir);
    const QString path = dir + "/kaon.vrmanifest"_L1;

    const QJsonObject strings{
        {"name"_L1, "Kaon"_L1},
        {"description"_L1, "Change VR mod settings without taking the headset off"_L1},
    };
    const QJsonObject app{
        {"app_key"_L1, AppKey},
        {"launch_type"_L1, "binary"_L1},
        {"binary_path_linux"_L1, QCoreApplication::applicationFilePath()},
        {"is_dashboard_overlay"_L1, true},
        {"strings"_L1, QJsonObject{{"en_us"_L1, strings}}},
    };
    const QJsonDocument manifest{QJsonObject{{"source"_L1, "builtin"_L1}, {"applications"_L1, QJsonArray{app}}}};

    QFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qCWarning(VrOverlayLog) << "Could not write the SteamVR manifest:" << file.errorString();
        return;
    }
    file.write(manifest.toJson());
    file.close();

    auto *apps = vr::VRApplications();
    if (apps == nullptr)
        return;
    // SteamVR remembers the manifest across runs (and a re-add mid-boot
    // churns the app list while the dashboard is still coming up), so only
    // register when unknown or when asked to. The file above is still
    // rewritten every time so the binary path never goes stale on disk.
    bool known = false;
    if (!m_forceManifest)
        known = apps->IsApplicationInstalled(AppKey);
    m_forceManifest = false;
    if (known)
    {
        qCDebug(VrOverlayLog) << "SteamVR already knows Kaon; leaving the registration alone";
        return;
    }
    // SteamVR can take a restart before a fresh manifest is fully known; the
    // overlay itself works regardless, so this is only logged.
    const auto error = apps->AddApplicationManifest(path.toUtf8().constData());
    if (error != vr::VRApplicationError_None)
        qCWarning(VrOverlayLog) << "Could not register with SteamVR:" << apps->GetApplicationsErrorNameFromEnum(error);
    else
        qCInfo(VrOverlayLog) << "Registered" << path << "with SteamVR";
}

bool VrOverlay::createOverlay()
{
    auto *overlay = vr::VROverlay();
    if (overlay == nullptr)
    {
        setStatus("SteamVR has no overlay interface."_L1);
        return false;
    }

    const auto error = overlay->CreateDashboardOverlay(OverlayKey, "Kaon", &m_mainHandle, &m_thumbHandle);
    if (error != vr::VROverlayError_None)
    {
        qCWarning(VrOverlayLog) << "CreateDashboardOverlay failed:" << overlay->GetOverlayErrorNameFromEnum(error);
        setStatus(
            "SteamVR refused the overlay (%1)."_L1.arg(QString::fromUtf8(overlay->GetOverlayErrorNameFromEnum(error))));
        return false;
    }

    overlay->SetOverlayWidthInMeters(m_mainHandle, OverlayWidthMeters);
    overlay->SetOverlayInputMethod(m_mainHandle, vr::VROverlayInputMethod_Mouse);
    // Without this, the compositor never sends joystick/trackpad scrolls.
    // Discrete scrolls are mouse wheel notches, which Qt Quick scrolls by
    // natively. Smooth ones are left off: with both flags SteamVR sends both
    // kinds for the same push, and every wheel event starts a flick, so a
    // stream of tiny smooth steps would race down the list.
    const auto flagError = overlay->SetOverlayFlag(m_mainHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);
    if (flagError != vr::VROverlayError_None)
        qCWarning(VrOverlayLog) << "Could not enable VR scroll events:" << overlay->GetOverlayErrorNameFromEnum(flagError);
    // The mirror is an opaque desktop window; blending its alpha channel can
    // only shimmer, never add information.
    overlay->SetOverlayFlag(m_mainHandle, vr::VROverlayFlags_IgnoreTextureAlpha, true);
    updateMouseScale();

    const QString thumb = writeThumbnail();
    if (!thumb.isEmpty())
    {
        const auto thumbError = overlay->SetOverlayFromFile(m_thumbHandle, thumb.toUtf8().constData());
        if (thumbError != vr::VROverlayError_None)
            qCDebug(VrOverlayLog) << "Could not set the dashboard icon:" << overlay->GetOverlayErrorNameFromEnum(thumbError);
    }
    return true;
}

void VrOverlay::updateMouseScale()
{
    if (m_mainHandle == vr::k_ulOverlayHandleInvalid || vr::VROverlay() == nullptr)
        return;
    const vr::HmdVector2_t scale{static_cast<float>(qMax(m_uploadWidth, 1)), static_cast<float>(qMax(m_uploadHeight, 1))};
    vr::VROverlay()->SetOverlayMouseScale(m_mainHandle, &scale);
}

void VrOverlay::shutdownVr()
{
    m_frameTimer->stop();
    m_showOnConnect = false;
    m_settling = false;
    // NOTE: m_connecting is deliberately left alone; only onConnected clears
    // it, so at most one handshake is ever in flight. Invalidating the
    // generation below is what stops a landed handshake from resurrecting.
    // This runs even when never connected so disabling mid-handshake cannot
    // connect behind the switch's back.
    ++m_initGeneration;
    // The next overlay starts blank: forget what was uploaded so the first
    // frame is always repainted instead of skipped as "unchanged".
    m_lastUploaded = QImage();
    m_uploadWidth = 0;
    m_uploadHeight = 0;
    m_overlayLock.reset();
    // Hold off the reconnect timer a little; SteamVR may still be on its way
    // down (or up).
    m_lastAttemptMs = QDateTime::currentMSecsSinceEpoch();
    // A settle wait holds an open session without being "connected" yet.
    if (!m_connected && !m_settling)
        return;

    // Deliberately no DestroyOverlay: the server drops a disconnected client's
    // overlays by itself, and calling into vrclient while SteamVR is going
    // away is what crashes. VR_Shutdown unloads vrclient, so no interface
    // pointers may be touched again until the next VR_Init.
    m_mainHandle = vr::k_ulOverlayHandleInvalid;
    m_thumbHandle = vr::k_ulOverlayHandleInvalid;
    // SteamVR drops the keyboard with the connection.
    m_keyboardOpen = false;
    // vrclient made GL objects in the texture's context to share it; with
    // that context current it can release them instead of leaking them.
    const bool glCurrent = m_glContext != nullptr && m_glContext->makeCurrent(m_glSurface.get());
    vr::VR_Shutdown();
    if (glCurrent)
        m_glContext->doneCurrent();
    m_connected = false;
    m_settling = false;
    if (m_dashboardVisible)
    {
        m_dashboardVisible = false;
        emit changed();
    }
    qCInfo(VrOverlayLog) << "Closed the SteamVR overlay connection";
}

void VrOverlay::onEventTick()
{
    if (!m_enabled)
        return;
    if (!m_connected)
    {
        // SteamVR may start after Kaon (or fail transiently while starting);
        // keep trying with a backoff instead of giving up after one attempt.
        if (!m_connecting)
        {
            const qint64 backoff = ReconnectIntervalMs * (1LL << qMin(m_failCount, 4));
            if (QDateTime::currentMSecsSinceEpoch() - m_lastAttemptMs >= qMin(backoff, qint64(60000)))
                ensureConnected();
        }
        return;
    }

    auto *overlay = vr::VROverlay();
    if (overlay == nullptr)
        return;

    pollOverlay(m_mainHandle, false);
    if (m_thumbHandle != vr::k_ulOverlayHandleInvalid)
        pollOverlay(m_thumbHandle, true);

    const bool visible = overlay->IsDashboardVisible();
    if (visible != m_dashboardVisible)
    {
        m_dashboardVisible = visible;
        emit changed();
        if (visible)
            pushFrame();
    }
}

void VrOverlay::onFrameTick()
{
    if (m_frameTimer->interval() != FrameIntervalMs && QDateTime::currentMSecsSinceEpoch() > m_boostUntil)
        m_frameTimer->setInterval(FrameIntervalMs);
    if (m_enabled && m_connected)
        pushFrame();
}

void VrOverlay::pushFrame(bool force)
{
    if (!m_enabled || !m_connected)
        return;
    auto *overlay = vr::VROverlay();
    if (overlay == nullptr || m_mainHandle == vr::k_ulOverlayHandleInvalid)
        return;
    if (!overlay->IsOverlayVisible(m_mainHandle) && !overlay->IsOverlayVisible(m_thumbHandle) && !m_dashboardVisible)
        return;

    QQuickWindow *window = overlayWindow();
    if (window == nullptr || !window->isExposed() || window->width() <= 0)
        return;

    QImage image = window->grabWindow();
    if (image.isNull())
        return;
    // A grab taken mid-render can come back empty; uploading it would blank
    // the tab. The next tick tries again.
    if (isBlank(image))
        return;
    if (image.width() > MaxUploadWidth)
        image = image.scaledToWidth(MaxUploadWidth, Qt::FastTransformation);
    if (image.format() != QImage::Format_RGBA8888)
        image = image.convertToFormat(QImage::Format_RGBA8888);

    if (image.width() != m_uploadWidth || image.height() != m_uploadHeight)
    {
        m_uploadWidth = image.width();
        m_uploadHeight = image.height();
        updateMouseScale();
    }
    m_uploadScale = static_cast<float>(image.width()) / static_cast<float>(window->width());

    // The dashboard is mostly static; re-uploading identical pixels is
    // wasted work.
    if (!force && image == m_lastUploaded)
        return;

    if (uploadTexture(image))
        m_lastUploaded = image;
}

bool VrOverlay::uploadTexture(const QImage &image)
{
    if (!makeGlCurrent())
        return false;

    // GL textures are bottom-up: SteamVR shows row 0 along the bottom edge.
    const QImage rows = image.flipped(Qt::Vertical);
    auto *gl = m_glContext->functions();
    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    if (rows.width() != m_textureWidth || rows.height() != m_textureHeight)
    {
        gl->glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, rows.width(), rows.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rows.constBits());
        m_textureWidth = rows.width();
        m_textureHeight = rows.height();
    }
    else
    {
        gl->glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, rows.width(), rows.height(), GL_RGBA, GL_UNSIGNED_BYTE, rows.constBits());
    }

    // SteamVR copies the texture into its own shared image within this call
    // (on this context's command stream), so the next frame can overwrite it.
    const vr::Texture_t texture{
        reinterpret_cast<void *>(static_cast<uintptr_t>(m_texture)), vr::TextureType_OpenGL, vr::ColorSpace_Auto};
    const auto error = vr::VROverlay()->SetOverlayTexture(m_mainHandle, &texture);
    m_glContext->doneCurrent();
    if (error != vr::VROverlayError_None)
    {
        static bool warned = false;
        if (!warned)
        {
            qCWarning(VrOverlayLog) << "SetOverlayTexture failed:" << vr::VROverlay()->GetOverlayErrorNameFromEnum(error);
            warned = true;
        }
        return false;
    }
    return true;
}

bool VrOverlay::makeGlCurrent()
{
    if (m_glContext != nullptr)
        return m_glContext->makeCurrent(m_glSurface.get());

    auto surface = std::make_unique<QOffscreenSurface>();
    surface->create();
    auto context = std::make_unique<QOpenGLContext>();
    if (!context->create() || !context->makeCurrent(surface.get()))
    {
        static bool warned = false;
        if (!warned)
        {
            qCWarning(VrOverlayLog) << "Could not create an OpenGL context for the VR overlay";
            warned = true;
        }
        return false;
    }
    m_glSurface = std::move(surface);
    m_glContext = std::move(context);

    auto *gl = m_glContext->functions();
    gl->glGenTextures(1, &m_texture);
    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    qCDebug(VrOverlayLog) << "Overlay texture context:" << reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER));
    return true;
}

void VrOverlay::pollOverlay(vr::VROverlayHandle_t handle, bool thumbnail)
{
    auto *overlay = vr::VROverlay();
    if (overlay == nullptr || handle == vr::k_ulOverlayHandleInvalid)
        return;

    // SteamVR can report one keystroke both as an overlay event and as a
    // global one (see VREvent_KeyboardCharInput), and global events reach
    // every overlay queue here. A copy lands in the same batch as the
    // original with the same age, so skip it instead of typing twice.
    QByteArray lastKey;
    float lastKeyAge = -1.0f;

    vr::VREvent_t event{};
    while (overlay->PollNextOverlayEvent(handle, &event, sizeof(event)))
    {
        // Mouse moves stream constantly; everything else is rare enough to
        // trace when diagnosing input issues.
        if (event.eventType != vr::VREvent_MouseMove)
            qCDebug(VrOverlayLog) << "overlay event" << event.eventType << "on" << (thumbnail ? "thumb" : "main");
        switch (event.eventType)
        {
        case vr::VREvent_MouseMove:
            if (!thumbnail)
                handleMouseMove(event.data.mouse.x, event.data.mouse.y);
            break;
        case vr::VREvent_MouseButtonDown:
            if (!thumbnail)
                handleMouseButton(event.data.mouse.x, event.data.mouse.y, event.data.mouse.button, true);
            break;
        case vr::VREvent_MouseButtonUp:
            if (!thumbnail)
                handleMouseButton(event.data.mouse.x, event.data.mouse.y, event.data.mouse.button, false);
            break;
        case vr::VREvent_ScrollDiscrete:
            if (!thumbnail)
                handleScroll(event.data.scroll.xdelta, event.data.scroll.ydelta, event.eventAgeSeconds);
            break;
        case vr::VREvent_KeyboardCharInput:
            if (!thumbnail && isOwnKeyboardEvent(event))
            {
                const QByteArray key{
                    event.data.keyboard.cNewInput,
                    static_cast<qsizetype>(qstrnlen(event.data.keyboard.cNewInput, sizeof(event.data.keyboard.cNewInput)))};
                if (key == lastKey && qAbs(event.eventAgeSeconds - lastKeyAge) < KeyRepeatCopySeconds)
                    break;
                lastKey = key;
                lastKeyAge = event.eventAgeSeconds;
                handleKeyboardInput(key);
            }
            break;
        case vr::VREvent_KeyboardClosed:
        case vr::VREvent_KeyboardClosed_Global:
            if (isOwnKeyboardEvent(event))
                m_keyboardOpen = false;
            break;
        case vr::VREvent_OverlayShown:
            if (!thumbnail)
                pushFrame(true);
            break;
        case vr::VREvent_DashboardActivated:
            m_dashboardVisible = true;
            emit changed();
            pushFrame(true);
            break;
        case vr::VREvent_DashboardDeactivated:
            m_dashboardVisible = false;
            // The keyboard belongs to the tab; put it away with the dashboard
            // so the next click on a field reliably brings it back.
            if (m_keyboardOpen)
            {
                overlay->HideKeyboard();
                m_keyboardOpen = false;
            }
            emit changed();
            break;
        case vr::VREvent_ImageFailed:
            // Only the icon is loaded from an image; frames go up as textures.
            if (thumbnail)
                qCDebug(VrOverlayLog) << "SteamVR rejected the dashboard icon";
            break;
        case vr::VREvent_Quit:
            // SteamVR is going away (or asking overlays to quit). Kaon is a
            // desktop app first, so it stays running and reconnects later
            // rather than exiting like Valve's sample does.
            qCInfo(VrOverlayLog) << "SteamVR quit; closing the overlay connection";
            shutdownVr();
            setStatus("SteamVR stopped. This will reconnect when it is back."_L1);
            return;
        default:
            break;
        }
    }
}

void VrOverlay::handleMouseMove(float x, float y)
{
    QQuickWindow *window = overlayWindow();
    if (window == nullptr)
        return;
    m_lastMouse = toWindowPoint(x, y);
    const QPointF global = window->mapToGlobal(m_lastMouse.toPoint());
    QMouseEvent event{QEvent::MouseMove, m_lastMouse, m_lastMouse, global, Qt::NoButton, m_mouseButtons, Qt::NoModifier};
    event.setTimestamp(eventTimestamp());
    QCoreApplication::sendEvent(window, &event);
}

void VrOverlay::handleMouseButton(float x, float y, uint32_t button, bool down)
{
    QQuickWindow *window = overlayWindow();
    if (window == nullptr)
        return;

    Qt::MouseButton qtButton = Qt::LeftButton;
    if (button == vr::VRMouseButton_Right)
        qtButton = Qt::RightButton;
    else if (button == vr::VRMouseButton_Middle)
        qtButton = Qt::MiddleButton;

    if (down)
        m_mouseButtons |= qtButton;
    else
        m_mouseButtons &= ~qtButton;

    m_lastMouse = toWindowPoint(x, y);
    if (down)
        window->requestActivate();
    const QPointF global = window->mapToGlobal(m_lastMouse.toPoint());
    QMouseEvent event{down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                      m_lastMouse,
                      m_lastMouse,
                      global,
                      qtButton,
                      m_mouseButtons,
                      Qt::NoModifier};
    event.setTimestamp(eventTimestamp());
    QCoreApplication::sendEvent(window, &event);
    if (!down && qtButton == Qt::LeftButton)
        updateKeyboard(window);
    // A click changes what is on screen; show it in VR without waiting for
    // the next refresh tick.
    boostFrames();
    pushFrame();
}

void VrOverlay::handleScroll(float dx, float dy, float ageSeconds)
{
    QQuickWindow *window = overlayWindow();
    if (window == nullptr)
        return;
    // The deltas count wheel notches. Positive means up/left, like a mouse
    // wheel, which shows earlier content.
    qCDebug(VrOverlayLog) << "scroll" << dx << dy;
    const QPoint angleDelta{qRound(dx * ScrollNotchAngle), qRound(dy * ScrollNotchAngle)};
    if (angleDelta.isNull())
        return;
    // Scrolls can arrive before the laser ever moved (e.g. the dashboard was
    // opened straight onto the tab); aim at the middle then, not the corner.
    const QPointF pos = m_lastMouse.isNull() ? QPointF{window->width() / 2.0, window->height() / 2.0} : m_lastMouse;

    // A real wheel event, so Qt Quick picks the target exactly as for a
    // desktop mouse: past the visor frame and dialog layer drawn over the
    // pages, to the innermost list under the laser that can move, and never
    // through a modal dialog. Hit-testing by hand stopped at those layers.
    QWheelEvent event{
        pos, window->mapToGlobal(pos), QPoint{}, angleDelta, m_mouseButtons, Qt::NoModifier, Qt::NoScrollPhase, false};
    // Stamped with when SteamVR saw the notch, not when it was polled.
    // Notches that queue up between polls would otherwise land at the same
    // millisecond (which Flickable drops as stale) or one apart (which it
    // reads as a violent spin).
    const auto age = static_cast<quint64>(qBound(0.0f, ageSeconds, 1.0f) * 1000.0f);
    m_lastWheelTimestamp = qMax(eventTimestamp() - age, m_lastWheelTimestamp + 1);
    event.setTimestamp(m_lastWheelTimestamp);
    QCoreApplication::sendEvent(window, &event);
    boostFrames();
    pushFrame();
}

// Like a phone: clicking a text field brings up SteamVR's keyboard, and
// clicking anywhere else puts it away. Focus that moves without a click (Tab,
// a page focusing its own field) leaves the keyboard alone.
void VrOverlay::updateKeyboard(QQuickWindow *window)
{
    auto *overlay = vr::VROverlay();
    if (!m_connected || overlay == nullptr)
        return;
    QQuickItem *field = focusedItem(window);
    const bool onField = isEditableText(field) && field->contains(field->mapFromScene(m_lastMouse));
    if (onField && !m_keyboardOpen)
    {
        // The setup SteamVR's own dashboard uses for desktop windows: keys
        // arrive one by one as they are typed, so the search filters live,
        // with arrow keys and a hide key instead of Done.
        const auto lineMode = field->inherits("QQuickTextEdit") ? vr::k_EGamepadTextInputLineModeMultipleLines :
                                                                  vr::k_EGamepadTextInputLineModeSingleLine;
        const auto error = overlay->ShowKeyboardForOverlay(m_mainHandle,
                                                           vr::k_EGamepadTextInputModeNormal,
                                                           lineMode,
                                                           vr::KeyboardFlag_Minimal | vr::KeyboardFlag_ShowArrowKeys |
                                                               vr::KeyboardFlag_HideDoneKey,
                                                           "",
                                                           1024,
                                                           "",
                                                           0);
        if (error != vr::VROverlayError_None)
        {
            qCWarning(VrOverlayLog) << "Could not show the SteamVR keyboard:" << overlay->GetOverlayErrorNameFromEnum(error);
            return;
        }
        overlay->SetKeyboardPositionForOverlay(m_mainHandle, vr::HmdRect2_t{});
        m_keyboardOpen = true;
    }
    else if (!onField && m_keyboardOpen)
    {
        overlay->HideKeyboard();
        m_keyboardOpen = false;
    }
}

// Keyboard events can be global, e.g. for a game's own keyboard; only take
// the ones for the keyboard opened here.
bool VrOverlay::isOwnKeyboardEvent(const vr::VREvent_t &event) const
{
    const auto owner = event.data.keyboard.overlayHandle;
    return m_keyboardOpen && (owner == m_mainHandle || owner == vr::k_ulOverlayHandleInvalid);
}

// Minimal mode sends what each key types: text, "\b" for delete, "\n" for
// return, and ANSI escape sequences for the arrow keys.
void VrOverlay::handleKeyboardInput(const QByteArray &input)
{
    QQuickWindow *window = overlayWindow();
    QQuickItem *field = window != nullptr ? focusedItem(window) : nullptr;
    if (!isEditableText(field))
        return;

    int key = Qt::Key_unknown;
    QString text;
    if (input == "\b")
        key = Qt::Key_Backspace;
    else if (input == "\n" || input == "\r")
    {
        key = Qt::Key_Return;
        text = "\r"_L1;
    }
    else if (input == "\x1b[D")
        key = Qt::Key_Left;
    else if (input == "\x1b[C")
        key = Qt::Key_Right;
    else if (input == "\x1b[A")
        key = Qt::Key_Up;
    else if (input == "\x1b[B")
        key = Qt::Key_Down;
    else if (!input.isEmpty() && !input.startsWith('\x1b'))
    {
        text = QString::fromUtf8(input);
        // Qt key codes for a single character are its uppercase code point.
        if (text.size() == 1)
            key = text.at(0).toUpper().unicode();
    }
    if (key == Qt::Key_unknown && text.isEmpty())
        return;

    for (const auto type : {QEvent::KeyPress, QEvent::KeyRelease})
    {
        QKeyEvent event{type, key, Qt::NoModifier, text};
        event.setTimestamp(eventTimestamp());
        // Straight to the field and up through its parents, like Qt Quick's
        // own key delivery, but without needing the desktop window to be the
        // active one, and without app shortcuts: an arrow typed in VR must
        // move the cursor, not the library selection.
        QQuickItem *item = field;
        do
        {
            event.accept();
            QCoreApplication::sendEvent(item, &event);
        } while (!event.isAccepted() && (item = item->parentItem()) != nullptr);
    }
    boostFrames();
    pushFrame();
}

void VrOverlay::boostFrames()
{
    m_boostUntil = QDateTime::currentMSecsSinceEpoch() + BoostMs;
    if (m_frameTimer->interval() != BoostIntervalMs)
        m_frameTimer->setInterval(BoostIntervalMs);
}

// The item that has, or would have, active focus. Qt Quick grants active
// focus only while the desktop window is active, and a laser click cannot
// always make it so (a Wayland compositor may refuse to activate a window
// without real input), so follow the focus scopes down instead.
QQuickItem *VrOverlay::focusedItem(QQuickWindow *window)
{
    QQuickItem *item = window->contentItem();
    while (item != nullptr && item->isFocusScope() && item->scopedFocusItem() != nullptr)
        item = item->scopedFocusItem();
    return item;
}

bool VrOverlay::isEditableText(const QQuickItem *item)
{
    return item != nullptr && (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit")) &&
           !item->property("readOnly").toBool() && item->isVisible() && item->isEnabled();
}

QQuickWindow *VrOverlay::overlayWindow() const
{
    for (QWindow *window : QGuiApplication::allWindows())
    {
        auto *quick = qobject_cast<QQuickWindow *>(window);
        if (quick != nullptr && quick->isVisible())
            return quick;
    }
    return nullptr;
}

// Kaon's window is opaque, so a fully transparent grab missed the render.
bool VrOverlay::isBlank(const QImage &image)
{
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0)
        return true;
    // A partial grab (top rendered, bottom empty) is the common shape of a
    // miss; the opaque shell means a transparent bottom row is never real.
    bool bottomClear = true;
    for (int x = 0; x < width; ++x)
    {
        if (qAlpha(image.pixel(x, height - 1)) != 0)
        {
            bottomClear = false;
            break;
        }
    }
    if (bottomClear)
        return true;
    for (int i = 0; i < 8; ++i)
    {
        if (qAlpha(image.pixel(width * (i + 1) / 9, height * (i + 1) / 9)) != 0)
            return false;
    }
    return true;
}

// SteamVR reports the laser hit in GL space (origin bottom-left); the uploaded
// image is top-down, and may be downscaled from the window.
QPointF VrOverlay::toWindowPoint(float x, float y) const
{
    QQuickWindow *window = overlayWindow();
    const float windowWidth = window != nullptr ? static_cast<float>(qMax(window->width(), 1)) : 1.0f;
    const float windowHeight = window != nullptr ? static_cast<float>(qMax(window->height(), 1)) : 1.0f;
    const float scale = m_uploadScale > 0 ? m_uploadScale : 1.0f;
    const float wx = std::clamp(x / scale, 0.0f, windowWidth - 1.0f);
    const float wy = std::clamp((static_cast<float>(m_uploadHeight) - y) / scale, 0.0f, windowHeight - 1.0f);
    return {wx, wy};
}

QString VrOverlay::writeThumbnail() const
{
    QIcon icon = QIcon::fromTheme("kaon"_L1);
    if (icon.isNull())
        icon = QIcon{":/qt/qml/dev/lorendb/kaon/qml/icons/kaon.svg"_L1};
    const QPixmap pixmap = icon.pixmap(256, 256);
    if (pixmap.isNull())
        return {};

    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/kaon-vr-thumb.png"_L1;
    if (!pixmap.save(path, "PNG"))
    {
        qCDebug(VrOverlayLog) << "Could not write the dashboard icon to" << path;
        return {};
    }
    return path;
}
