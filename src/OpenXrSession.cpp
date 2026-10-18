#include "OpenXrSession.h"

#include "VrOverlay.h"

#include <QColor>
#include <QDateTime>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QSurfaceFormat>
#include <QThread>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qopenglcontext_platform.h>

#include <openxr/openxr.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <optional>
#include <vector>

Q_LOGGING_CATEGORY(OpenXrLog, "vr.openxr")

namespace
{
    // Composited later than the main session and any overlay that picks a
    // lower slot, so the window stays on top. The extension defines the range
    // as a uint32.
    constexpr uint32_t kOverlayPlacement = UINT32_MAX;
    constexpr float kQuadWidthM = 1.15f;
    constexpr float kQuadDistanceM = 1.55f;
    constexpr float kQuadDropM = 0.10f;
    constexpr float kBeamThicknessM = 0.008f;
    constexpr float kBeamReachM = 1.6f;
    constexpr int64_t kSwapchainWaitNs = 100000000;
    constexpr float kTriggerClick = 0.6f;
    constexpr float kScrollDeadzone = 0.55f;
    constexpr qint64 kScrollRepeatMs = 160;

    // Numeric on purpose: the GL headers macro these names, and a swapchain
    // format is just the GLenum the runtime asked for.
    constexpr int64_t kGlRgba8 = 0x8058;
    constexpr int64_t kGlSrgb8Alpha8 = 0x8C43;
    constexpr uint32_t kGlUnpackRowLength = 0x0CF2;

    struct Vec3
    {
        float x;
        float y;
        float z;
    };

    Vec3 operator+(Vec3 a, Vec3 b)
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    Vec3 operator-(Vec3 a, Vec3 b)
    {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    Vec3 operator*(Vec3 a, float s)
    {
        return {a.x * s, a.y * s, a.z * s};
    }
    float dot(Vec3 a, Vec3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 cross(Vec3 a, Vec3 b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    Vec3 normalize(Vec3 v)
    {
        const float n = std::sqrt(dot(v, v));
        return n < 1e-6f ? Vec3{0, 0, 1} : v * (1.f / n);
    }

    // Columns are the local +X, +Y and +Z axes.
    XrQuaternionf quatFromBasis(Vec3 x, Vec3 y, Vec3 z)
    {
        const float m[3][3] = {{x.x, y.x, z.x}, {x.y, y.y, z.y}, {x.z, y.z, z.z}};
        XrQuaternionf q{};
        const float trace = m[0][0] + m[1][1] + m[2][2];
        if (trace > 0.f)
        {
            const float s = std::sqrt(trace + 1.f) * 2.f;
            q.w = 0.25f * s;
            q.x = (m[2][1] - m[1][2]) / s;
            q.y = (m[0][2] - m[2][0]) / s;
            q.z = (m[1][0] - m[0][1]) / s;
        }
        else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
        {
            const float s = std::sqrt(1.f + m[0][0] - m[1][1] - m[2][2]) * 2.f;
            q.w = (m[2][1] - m[1][2]) / s;
            q.x = 0.25f * s;
            q.y = (m[0][1] + m[1][0]) / s;
            q.z = (m[0][2] + m[2][0]) / s;
        }
        else if (m[1][1] > m[2][2])
        {
            const float s = std::sqrt(1.f + m[1][1] - m[0][0] - m[2][2]) * 2.f;
            q.w = (m[0][2] - m[2][0]) / s;
            q.x = (m[0][1] + m[1][0]) / s;
            q.y = 0.25f * s;
            q.z = (m[1][2] + m[2][1]) / s;
        }
        else
        {
            const float s = std::sqrt(1.f + m[2][2] - m[0][0] - m[1][1]) * 2.f;
            q.w = (m[1][0] - m[0][1]) / s;
            q.x = (m[0][2] + m[2][0]) / s;
            q.y = (m[1][2] + m[2][1]) / s;
            q.z = 0.25f * s;
        }
        return q;
    }

    Vec3 rotate(XrQuaternionf q, Vec3 v)
    {
        const Vec3 u{q.x, q.y, q.z};
        const Vec3 t = Vec3{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x} * 2.0f;
        const Vec3 cross{u.y * t.z - u.z * t.y, u.z * t.x - u.x * t.z, u.x * t.y - u.y * t.x};
        return v + t * q.w + cross;
    }

    // The grab keeps the window's translucency. On a black void that alpha
    // throws the whole panel away; paint it over an opaque fill first.
    // grabWindow tags the screen scale. Leaving that set makes the painter
    // draw the window at 1/scale and the fill shows as bars around it.
    QImage opaqueCopy(const QImage &image)
    {
        QImage src = image.convertToFormat(QImage::Format_RGBA8888);
        src.setDevicePixelRatio(1);
        QImage flat{src.size(), QImage::Format_RGBA8888};
        flat.fill(QColor{0x1b, 0x1b, 0x22});
        QPainter painter{&flat};
        painter.drawImage(QPoint{0, 0}, src);
        return flat;
    }

    QSurfaceFormat openGlFormat()
    {
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CompatibilityProfile);
        return format;
    }

    // A blit into WiVRn's imported image is a no-op on this GPU. Drawing a
    // textured triangle into the swapchain framebuffer is the write that lands.
    bool drawStaging(QOpenGLExtraFunctions *gl, GLuint staging, int width, int height)
    {
        static QOpenGLShaderProgram *program = nullptr;
        static bool failed = false;
        if (failed)
            return false;
        if (program == nullptr)
        {
            program = new QOpenGLShaderProgram;
            const bool linked = program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                                                 "#version 330\n"
                                                                 "out vec2 vUv;\n"
                                                                 "void main() {\n"
                                                                 "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & "
                                                                 "2);\n"
                                                                 "    vUv = p;\n"
                                                                 "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                                                 "}\n") &&
                                program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                                 "#version 330\n"
                                                                 "in vec2 vUv;\n"
                                                                 "uniform sampler2D img;\n"
                                                                 "out vec4 color;\n"
                                                                 "void main() { color = texture(img, vUv); }\n") &&
                                program->link();
            if (!linked)
            {
                qCWarning(OpenXrLog) << "Headset image shader failed:" << program->log();
                delete program;
                program = nullptr;
                failed = true;
                return false;
            }
        }

        static GLuint vao = 0;
        if (vao == 0)
            gl->glGenVertexArrays(1, &vao);

        program->bind();
        program->setUniformValue("img", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, staging);
        gl->glBindVertexArray(vao);
        gl->glViewport(0, 0, width, height);
        gl->glDisable(GL_BLEND);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_CULL_FACE);
        gl->glDisable(GL_SCISSOR_TEST);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        program->release();
        return true;
    }

    void copyName(char *dst, size_t size, const char *src)
    {
        std::strncpy(dst, src, size - 1);
        dst[size - 1] = '\0';
    }

    using XrVoidFn = void (*)();
    using EglGetProc = XrVoidFn (*)(const char *);

    // Layouts match the OpenXR graphics-binding structs. They are declared
    // here so this file does not need the X11, EGL, or Wayland headers; Qt
    // already holds the native pointers.
    struct GraphicsBindingEgl
    {
        XrStructureType type;
        const void *next;
        EglGetProc getProcAddress;
        void *display;
        void *config;
        void *context;
    };

    struct GraphicsBindingWayland
    {
        XrStructureType type;
        const void *next;
        void *display;
    };

    struct GraphicsBindingXlib
    {
        XrStructureType type;
        const void *next;
        void *xDisplay;
        uint32_t visualid;
        void *glxFBConfig;
        unsigned long glxDrawable;
        void *glxContext;
    };

    struct GraphicsRequirementsOpenGL
    {
        XrStructureType type;
        void *next;
        XrVersion minApiVersionSupported;
        XrVersion maxApiVersionSupported;
    };

    struct SwapchainImageOpenGL
    {
        XrStructureType type;
        void *next;
        uint32_t image;
    };

    struct XrApi
    {
        PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = nullptr;
        PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties = nullptr;
        PFN_xrCreateInstance xrCreateInstance = nullptr;
        PFN_xrDestroyInstance xrDestroyInstance = nullptr;
        PFN_xrGetInstanceProperties xrGetInstanceProperties = nullptr;
        PFN_xrResultToString xrResultToString = nullptr;
        PFN_xrGetSystem xrGetSystem = nullptr;
        PFN_xrGetSystemProperties xrGetSystemProperties = nullptr;
        PFN_xrEnumerateViewConfigurations xrEnumerateViewConfigurations = nullptr;
        PFN_xrEnumerateEnvironmentBlendModes xrEnumerateEnvironmentBlendModes = nullptr;
        PFN_xrCreateSession xrCreateSession = nullptr;
        PFN_xrDestroySession xrDestroySession = nullptr;
        PFN_xrBeginSession xrBeginSession = nullptr;
        PFN_xrEndSession xrEndSession = nullptr;
        PFN_xrRequestExitSession xrRequestExitSession = nullptr;
        PFN_xrPollEvent xrPollEvent = nullptr;
        PFN_xrWaitFrame xrWaitFrame = nullptr;
        PFN_xrBeginFrame xrBeginFrame = nullptr;
        PFN_xrEndFrame xrEndFrame = nullptr;
        PFN_xrEnumerateSwapchainFormats xrEnumerateSwapchainFormats = nullptr;
        PFN_xrCreateSwapchain xrCreateSwapchain = nullptr;
        PFN_xrDestroySwapchain xrDestroySwapchain = nullptr;
        PFN_xrEnumerateSwapchainImages xrEnumerateSwapchainImages = nullptr;
        PFN_xrAcquireSwapchainImage xrAcquireSwapchainImage = nullptr;
        PFN_xrWaitSwapchainImage xrWaitSwapchainImage = nullptr;
        PFN_xrReleaseSwapchainImage xrReleaseSwapchainImage = nullptr;
        PFN_xrCreateReferenceSpace xrCreateReferenceSpace = nullptr;
        PFN_xrDestroySpace xrDestroySpace = nullptr;
        PFN_xrLocateSpace xrLocateSpace = nullptr;
        PFN_xrStringToPath xrStringToPath = nullptr;
        PFN_xrCreateActionSet xrCreateActionSet = nullptr;
        PFN_xrDestroyActionSet xrDestroyActionSet = nullptr;
        PFN_xrCreateAction xrCreateAction = nullptr;
        PFN_xrDestroyAction xrDestroyAction = nullptr;
        PFN_xrSuggestInteractionProfileBindings xrSuggestInteractionProfileBindings = nullptr;
        PFN_xrAttachSessionActionSets xrAttachSessionActionSets = nullptr;
        PFN_xrSyncActions xrSyncActions = nullptr;
        PFN_xrGetActionStateBoolean xrGetActionStateBoolean = nullptr;
        PFN_xrGetActionStateFloat xrGetActionStateFloat = nullptr;
        PFN_xrGetActionStateVector2f xrGetActionStateVector2f = nullptr;
        PFN_xrGetActionStatePose xrGetActionStatePose = nullptr;
        PFN_xrCreateActionSpace xrCreateActionSpace = nullptr;
        XrResult (*xrGetOpenGLGraphicsRequirementsKHR)(XrInstance, XrSystemId, GraphicsRequirementsOpenGL *) = nullptr;

        template<typename Fn>
        bool load(XrInstance instance, const char *name, Fn *out)
        {
            PFN_xrVoidFunction fn = nullptr;
            if (xrGetInstanceProcAddr == nullptr || XR_FAILED(xrGetInstanceProcAddr(instance, name, &fn)) || fn == nullptr)
                return false;
            *out = reinterpret_cast<Fn>(fn);
            return true;
        }
    };

    bool runtimeMissing(XrResult result)
    {
        return result == XR_ERROR_RUNTIME_UNAVAILABLE || result == XR_ERROR_FORM_FACTOR_UNAVAILABLE ||
               result == XR_ERROR_FORM_FACTOR_UNSUPPORTED || result == XR_ERROR_INITIALIZATION_FAILED;
    }

    EglGetProc eglGetProcAddress()
    {
        auto *sym = dlsym(RTLD_DEFAULT, "eglGetProcAddress");
        if (sym == nullptr)
        {
            // Qt has usually loaded EGL already. If it has not, open it for
            // the rest of the process; the binding needs the pointer to stay
            // valid for the session.
            void *lib = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
            if (lib != nullptr)
                sym = dlsym(lib, "eglGetProcAddress");
        }
        return reinterpret_cast<EglGetProc>(sym);
    }

#if QT_CONFIG(xcb_glx_plugin)
    bool fillXlibBinding(void *x11Display, void *glxContext, GraphicsBindingXlib *out)
    {
        if (x11Display == nullptr || glxContext == nullptr)
            return false;
        void *gl = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        void *x11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
        if (gl == nullptr || x11 == nullptr)
            return false;
        auto queryContext = reinterpret_cast<int (*)(void *, void *, int, int *)>(dlsym(gl, "glXQueryContext"));
        auto chooseConfig = reinterpret_cast<void **(*)(void *, int, const int *, int *)>(dlsym(gl, "glXChooseFBConfig"));
        auto getAttrib = reinterpret_cast<int (*)(void *, void *, int, int *)>(dlsym(gl, "glXGetFBConfigAttrib"));
        auto currentDrawable = reinterpret_cast<unsigned long (*)()>(dlsym(gl, "glXGetCurrentDrawable"));
        auto defaultScreen = reinterpret_cast<int (*)(void *)>(dlsym(x11, "XDefaultScreen"));
        auto xfree = reinterpret_cast<int (*)(void *)>(dlsym(x11, "XFree"));
        if (queryContext == nullptr || chooseConfig == nullptr || getAttrib == nullptr || currentDrawable == nullptr ||
            defaultScreen == nullptr || xfree == nullptr)
            return false;

        constexpr int GLX_VISUAL_ID_VALUE = 0x800B;
        constexpr int GLX_FBCONFIG_ID_VALUE = 0x8013;
        int fbId = 0;
        if (queryContext(x11Display, glxContext, GLX_FBCONFIG_ID_VALUE, &fbId) != 0 || fbId == 0)
            return false;
        const int attribs[] = {GLX_FBCONFIG_ID_VALUE, fbId, 0};
        int count = 0;
        void **configs = chooseConfig(x11Display, defaultScreen(x11Display), attribs, &count);
        if (configs == nullptr || count < 1)
            return false;
        int visual = 0;
        const bool attribOk = getAttrib(x11Display, configs[0], GLX_VISUAL_ID_VALUE, &visual) == 0 && visual != 0;
        void *fbConfig = configs[0];
        xfree(configs);
        if (!attribOk)
            return false;

        *out = {};
        out->type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
        out->xDisplay = x11Display;
        out->visualid = static_cast<uint32_t>(visual);
        out->glxFBConfig = fbConfig;
        out->glxDrawable = currentDrawable();
        out->glxContext = glxContext;
        return out->glxDrawable != 0;
    }
#endif
} // namespace

class OpenXrSession::Frame
{
public:
    void set(const QImage &image, QSize windowSize)
    {
        QMutexLocker lock{&mutex};
        pending = image;
        window = windowSize;
        dirty = true;
    }

    bool take(QImage *image, QSize *windowSize)
    {
        QMutexLocker lock{&mutex};
        if (!dirty)
            return false;
        *image = pending;
        *windowSize = window;
        dirty = false;
        return true;
    }

private:
    QMutex mutex;
    QImage pending;
    QSize window;
    bool dirty = false;
};

struct OpenXrLoop
{
    OpenXrSession *self = nullptr;
    XrApi api;
    QString error;
    bool overlaySession = false;
    bool haveGlExt = false;
    bool haveEglExt = false;
    bool sessionRunning = false;
    bool inputReady = false;

    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;
    XrSpace quadSpace = XR_NULL_HANDLE;
    // Monado draws the +Z face of an unrotated quad. This starts head-locked
    // and is rewritten into the room once the head pose can be located.
    XrPosef quadPose{{0, 0, 0, 1}, {0, -kQuadDropM, -kQuadDistanceM}};
    bool quadPlaced = false;
    bool swapSrgb = false;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    XrSwapchain raySwapchain = XR_NULL_HANDLE;
    bool rayReady = false;
    XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    std::vector<SwapchainImageOpenGL> images;
    int swapW = 0;
    int swapH = 0;
    int imageW = 0;
    int imageH = 0;
    QSize windowSize;
    bool haveImage = false;
    int failedSwapW = 0;
    int failedSwapH = 0;

    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction aimAction = XR_NULL_HANDLE;
    XrAction clickAction = XR_NULL_HANDLE;
    XrAction triggerAction = XR_NULL_HANDLE;
    XrAction scrollAction = XR_NULL_HANDLE;
    XrPath handPath[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XrSpace aimSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};

    struct Pointer
    {
        bool down = false;
        QPointF last{-1, -1};
        qint64 lastScrollMs = 0;
    } pointer;

    struct Beam
    {
        bool show = false;
        XrPosef pose{{0, 0, 0, 1}, {0, 0, 0}};
        float length = 0;
    } beams[2];

    bool fail(const QString &message, bool quiet = false)
    {
        error = message;
        if (quiet)
            qCDebug(OpenXrLog) << message;
        else
            qCWarning(OpenXrLog) << message;
        return false;
    }

    QString resultString(XrResult result) const
    {
        char buffer[XR_MAX_RESULT_STRING_SIZE];
        if (instance != XR_NULL_HANDLE && api.xrResultToString != nullptr &&
            XR_SUCCEEDED(api.xrResultToString(instance, result, buffer)))
            return QString::fromUtf8(buffer);
        return QString::number(static_cast<int>(result));
    }

    bool check(XrResult result, const char *what)
    {
        if (XR_SUCCEEDED(result))
            return true;
        if (runtimeMissing(result))
            return fail("Waiting for the OpenXR runtime. Start WiVRn or Monado; Kaon attaches on its own."_L1, true);
        return fail(QString::fromUtf8(what) + " failed ("_L1 + resultString(result) + ")."_L1);
    }

    bool loadCore(void *library)
    {
        api.xrGetInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(library, "xrGetInstanceProcAddr"));
        if (api.xrGetInstanceProcAddr == nullptr)
            return fail("The OpenXR loader has no xrGetInstanceProcAddr."_L1);
        if (!api.load(
                XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", &api.xrEnumerateInstanceExtensionProperties) ||
            !api.load(XR_NULL_HANDLE, "xrCreateInstance", &api.xrCreateInstance))
            return fail("The OpenXR loader is missing its instance entry points."_L1);
        return true;
    }

    bool loadInstance()
    {
        const bool ok =
            api.load(instance, "xrDestroyInstance", &api.xrDestroyInstance) &&
            api.load(instance, "xrGetInstanceProperties", &api.xrGetInstanceProperties) &&
            api.load(instance, "xrResultToString", &api.xrResultToString) &&
            api.load(instance, "xrGetSystem", &api.xrGetSystem) &&
            api.load(instance, "xrGetSystemProperties", &api.xrGetSystemProperties) &&
            api.load(instance, "xrEnumerateViewConfigurations", &api.xrEnumerateViewConfigurations) &&
            api.load(instance, "xrEnumerateEnvironmentBlendModes", &api.xrEnumerateEnvironmentBlendModes) &&
            api.load(instance, "xrCreateSession", &api.xrCreateSession) &&
            api.load(instance, "xrDestroySession", &api.xrDestroySession) &&
            api.load(instance, "xrBeginSession", &api.xrBeginSession) &&
            api.load(instance, "xrEndSession", &api.xrEndSession) &&
            api.load(instance, "xrRequestExitSession", &api.xrRequestExitSession) &&
            api.load(instance, "xrPollEvent", &api.xrPollEvent) && api.load(instance, "xrWaitFrame", &api.xrWaitFrame) &&
            api.load(instance, "xrBeginFrame", &api.xrBeginFrame) && api.load(instance, "xrEndFrame", &api.xrEndFrame) &&
            api.load(instance, "xrEnumerateSwapchainFormats", &api.xrEnumerateSwapchainFormats) &&
            api.load(instance, "xrCreateSwapchain", &api.xrCreateSwapchain) &&
            api.load(instance, "xrDestroySwapchain", &api.xrDestroySwapchain) &&
            api.load(instance, "xrEnumerateSwapchainImages", &api.xrEnumerateSwapchainImages) &&
            api.load(instance, "xrAcquireSwapchainImage", &api.xrAcquireSwapchainImage) &&
            api.load(instance, "xrWaitSwapchainImage", &api.xrWaitSwapchainImage) &&
            api.load(instance, "xrReleaseSwapchainImage", &api.xrReleaseSwapchainImage) &&
            api.load(instance, "xrCreateReferenceSpace", &api.xrCreateReferenceSpace) &&
            api.load(instance, "xrDestroySpace", &api.xrDestroySpace) &&
            api.load(instance, "xrLocateSpace", &api.xrLocateSpace) &&
            api.load(instance, "xrStringToPath", &api.xrStringToPath) &&
            api.load(instance, "xrCreateActionSet", &api.xrCreateActionSet) &&
            api.load(instance, "xrDestroyActionSet", &api.xrDestroyActionSet) &&
            api.load(instance, "xrCreateAction", &api.xrCreateAction) &&
            api.load(instance, "xrDestroyAction", &api.xrDestroyAction) &&
            api.load(instance, "xrSuggestInteractionProfileBindings", &api.xrSuggestInteractionProfileBindings) &&
            api.load(instance, "xrAttachSessionActionSets", &api.xrAttachSessionActionSets) &&
            api.load(instance, "xrSyncActions", &api.xrSyncActions) &&
            api.load(instance, "xrGetActionStateBoolean", &api.xrGetActionStateBoolean) &&
            api.load(instance, "xrGetActionStateFloat", &api.xrGetActionStateFloat) &&
            api.load(instance, "xrGetActionStateVector2f", &api.xrGetActionStateVector2f) &&
            api.load(instance, "xrGetActionStatePose", &api.xrGetActionStatePose) &&
            api.load(instance, "xrCreateActionSpace", &api.xrCreateActionSpace);
        if (!ok)
            return fail("The OpenXR runtime is missing a required entry point."_L1);
        return true;
    }

    bool createInstance()
    {
        uint32_t count = 0;
        XrResult result = api.xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
        if (!check(result, "xrEnumerateInstanceExtensionProperties"))
            return false;
        std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
        if (count > 0 && !check(api.xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data()),
                                "xrEnumerateInstanceExtensionProperties"))
            return false;
        const auto has = [&](const char *name) {
            return std::any_of(props.begin(), props.end(), [&](const XrExtensionProperties &prop) {
                return std::strcmp(prop.extensionName, name) == 0;
            });
        };

        haveGlExt = has("XR_KHR_opengl_enable");
        haveEglExt = has("XR_MNDX_egl_enable");
        overlaySession = has(XR_EXTX_OVERLAY_EXTENSION_NAME);
        if (!haveGlExt && !haveEglExt)
            return fail("This OpenXR runtime has no OpenGL graphics extension."_L1);

        std::vector<const char *> enabled;
        if (haveGlExt)
            enabled.push_back("XR_KHR_opengl_enable");
        if (haveEglExt)
            enabled.push_back("XR_MNDX_egl_enable");
        if (overlaySession)
            enabled.push_back(XR_EXTX_OVERLAY_EXTENSION_NAME);

        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        copyName(info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Kaon");
        info.applicationInfo.applicationVersion = 1;
        copyName(info.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "Kaon");
        info.applicationInfo.engineVersion = 1;
        info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        info.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
        info.enabledExtensionNames = enabled.data();

        result = api.xrCreateInstance(&info, &instance);
        // 1.0 headers only provide XR_CURRENT_API_VERSION. 1.1 headers also
        // define XR_API_VERSION_1_0, which a 1.0 runtime will accept.
#if defined(XR_API_VERSION_1_0)
        if (result == XR_ERROR_API_VERSION_UNSUPPORTED)
        {
            info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
            result = api.xrCreateInstance(&info, &instance);
        }
#endif
        if (!check(result, "xrCreateInstance"))
            return false;
        if (!loadInstance())
            return false;

        XrInstanceProperties instanceProps{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(api.xrGetInstanceProperties(instance, &instanceProps)))
            qCInfo(OpenXrLog) << "OpenXR runtime" << instanceProps.runtimeName
                              << (overlaySession ? "(overlay session)" : "(regular session)");
        return true;
    }

    bool bindingFromContext(QOpenGLContext *context, const void **next)
    {
        static GraphicsBindingEgl egl;
        static GraphicsBindingWayland wayland;
        static GraphicsBindingXlib xlib;
        egl = {};
        wayland = {};
        xlib = {};

#if QT_CONFIG(egl)
        if (haveEglExt)
        {
            if (auto *native = context->nativeInterface<QNativeInterface::QEGLContext>())
            {
                const auto getProc = eglGetProcAddress();
                if (getProc != nullptr && native->display() != nullptr && native->nativeContext() != nullptr)
                {
                    egl.type = XR_TYPE_GRAPHICS_BINDING_EGL_MNDX;
                    egl.getProcAddress = getProc;
                    egl.display = native->display();
                    egl.config = native->config();
                    egl.context = native->nativeContext();
                    *next = &egl;
                    qCInfo(OpenXrLog) << "OpenXR graphics binding: EGL";
                    return true;
                }
            }
        }
#else
        Q_UNUSED(context);
#endif

#if QT_CONFIG(wayland)
        if (haveGlExt && self->m_waylandDisplay != nullptr)
        {
            wayland.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WAYLAND_KHR;
            wayland.display = self->m_waylandDisplay;
            *next = &wayland;
            qCInfo(OpenXrLog) << "OpenXR graphics binding: OpenGL Wayland";
            return true;
        }
#endif

#if QT_CONFIG(xcb_glx_plugin)
        if (haveGlExt)
        {
            void *glxContext = nullptr;
            if (auto *native = context->nativeInterface<QNativeInterface::QGLXContext>())
                glxContext = native->nativeContext();
            if (fillXlibBinding(self->m_x11Display, glxContext, &xlib))
            {
                *next = &xlib;
                qCInfo(OpenXrLog) << "OpenXR graphics binding: OpenGL GLX";
                return true;
            }
        }
#endif
        return fail("Kaon's OpenGL context has no graphics binding this OpenXR runtime accepts."_L1);
    }

    bool createSession(QOpenGLContext *context)
    {
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (!check(api.xrGetSystem(instance, &systemInfo, &systemId), "xrGetSystem"))
            return false;

        uint32_t viewCount = 0;
        if (!check(api.xrEnumerateViewConfigurations(instance, systemId, 0, &viewCount, nullptr),
                   "xrEnumerateViewConfigurations"))
            return false;
        std::vector<XrViewConfigurationType> views(viewCount);
        if (viewCount == 0 ||
            !check(api.xrEnumerateViewConfigurations(instance, systemId, viewCount, &viewCount, views.data()),
                   "xrEnumerateViewConfigurations") ||
            std::find(views.begin(), views.end(), XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) == views.end())
            return fail("This OpenXR runtime has no stereo view configuration."_L1);

        uint32_t blendCount = 0;
        if (!check(api.xrEnumerateEnvironmentBlendModes(
                       instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blendCount, nullptr),
                   "xrEnumerateEnvironmentBlendModes"))
            return false;
        std::vector<XrEnvironmentBlendMode> blends(blendCount);
        if (blendCount == 0 ||
            !check(
                api.xrEnumerateEnvironmentBlendModes(
                    instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blendCount, &blendCount, blends.data()),
                "xrEnumerateEnvironmentBlendModes"))
            return fail("This OpenXR runtime has no environment blend mode."_L1);
        // A lone quad with an opaque blend replaces the whole view with
        // black. Alpha blend leaves the runtime's scene (and any other app)
        // in place and draws the window on top of it.
        blend = blends.front();
        for (const auto want :
             {XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND, XR_ENVIRONMENT_BLEND_MODE_ADDITIVE, XR_ENVIRONMENT_BLEND_MODE_OPAQUE})
        {
            if (std::find(blends.begin(), blends.end(), want) != blends.end())
            {
                blend = want;
                break;
            }
        }

        if (haveGlExt)
        {
            if (!api.load(instance, "xrGetOpenGLGraphicsRequirementsKHR", &api.xrGetOpenGLGraphicsRequirementsKHR))
                return fail("The OpenXR runtime did not provide xrGetOpenGLGraphicsRequirementsKHR."_L1);
            GraphicsRequirementsOpenGL requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
            if (!check(api.xrGetOpenGLGraphicsRequirementsKHR(instance, systemId, &requirements),
                       "xrGetOpenGLGraphicsRequirementsKHR"))
                return false;
        }

        const void *graphics = nullptr;
        if (!bindingFromContext(context, &graphics))
            return false;

        XrSessionCreateInfoOverlayEXTX overlayInfo{XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX};
        overlayInfo.next = graphics;
        overlayInfo.sessionLayersPlacement = kOverlayPlacement;

        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionInfo.next = overlaySession ? static_cast<const void *>(&overlayInfo) : graphics;
        sessionInfo.systemId = systemId;

        const XrResult created = api.xrCreateSession(instance, &sessionInfo, &session);
        if (XR_FAILED(created) && overlaySession)
        {
            // The runtime advertised the extension but refused the overlay
            // (another overlay already owns the only slot, for example).
            // A regular session is still the documented fallback.
            qCWarning(OpenXrLog) << "Overlay session was refused (" << resultString(created)
                                 << "); trying a regular session";
            overlaySession = false;
            sessionInfo.next = graphics;
            if (!check(api.xrCreateSession(instance, &sessionInfo, &session), "xrCreateSession"))
                return false;
        }
        else if (!check(created, "xrCreateSession"))
        {
            return false;
        }

        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        spaceInfo.poseInReferenceSpace.orientation.w = 1;
        if (!check(api.xrCreateReferenceSpace(session, &spaceInfo, &viewSpace), "xrCreateReferenceSpace"))
            return false;
        quadSpace = viewSpace;
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        if (XR_FAILED(api.xrCreateReferenceSpace(session, &spaceInfo, &localSpace)))
            localSpace = XR_NULL_HANDLE;

        setupInput();
        return true;
    }

    void setupInput()
    {
        if (XR_FAILED(api.xrStringToPath(instance, "/user/hand/left", &handPath[0])) ||
            XR_FAILED(api.xrStringToPath(instance, "/user/hand/right", &handPath[1])))
            return;

        XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        copyName(setInfo.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "kaon");
        copyName(setInfo.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "Kaon");
        setInfo.priority = 0;
        if (XR_FAILED(api.xrCreateActionSet(instance, &setInfo, &actionSet)))
            return;

        const auto makeAction = [&](const char *name, const char *localized, XrActionType type, XrAction *action) {
            XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
            copyName(info.actionName, XR_MAX_ACTION_NAME_SIZE, name);
            copyName(info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, localized);
            info.actionType = type;
            info.countSubactionPaths = 2;
            info.subactionPaths = handPath;
            return XR_SUCCEEDED(api.xrCreateAction(actionSet, &info, action));
        };
        if (!makeAction("aim_pose", "Aim", XR_ACTION_TYPE_POSE_INPUT, &aimAction) ||
            !makeAction("select_click", "Select", XR_ACTION_TYPE_BOOLEAN_INPUT, &clickAction) ||
            !makeAction("trigger_value", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, &triggerAction) ||
            !makeAction("scroll", "Scroll", XR_ACTION_TYPE_VECTOR2F_INPUT, &scrollAction))
            return;

        const auto suggest = [&](const char *profile, const char *click, const char *trigger, const char *scroll) {
            XrPath profilePath = XR_NULL_PATH;
            if (XR_FAILED(api.xrStringToPath(instance, profile, &profilePath)))
                return;
            std::vector<XrActionSuggestedBinding> bindings;
            const auto add = [&](XrAction action, const QByteArray &path) {
                XrPath binding = XR_NULL_PATH;
                if (XR_FAILED(api.xrStringToPath(instance, path.constData(), &binding)))
                    return false;
                bindings.push_back({action, binding});
                return true;
            };
            for (const char *hand : {"/user/hand/left", "/user/hand/right"})
            {
                if (!add(aimAction, QByteArray{hand} + "/input/aim/pose"))
                    return;
                if (click != nullptr && !add(clickAction, QByteArray{hand} + click))
                    return;
                if (trigger != nullptr && !add(triggerAction, QByteArray{hand} + trigger))
                    return;
                if (scroll != nullptr && !add(scrollAction, QByteArray{hand} + scroll))
                    return;
            }
            XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            suggested.interactionProfile = profilePath;
            suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
            suggested.suggestedBindings = bindings.data();
            if (XR_FAILED(api.xrSuggestInteractionProfileBindings(instance, &suggested)))
                qCDebug(OpenXrLog) << "Skipping interaction profile" << profile;
        };

        suggest("/interaction_profiles/khr/simple_controller", "/input/select/click", nullptr, nullptr);
        suggest("/interaction_profiles/oculus/touch_controller", nullptr, "/input/trigger/value", "/input/thumbstick");
        suggest("/interaction_profiles/meta/touch_plus_controller", nullptr, "/input/trigger/value", "/input/thumbstick");
        suggest("/interaction_profiles/meta/touch_pro_controller", nullptr, "/input/trigger/value", "/input/thumbstick");
        suggest("/interaction_profiles/valve/index_controller",
                "/input/trigger/click",
                "/input/trigger/value",
                "/input/thumbstick");
        suggest(
            "/interaction_profiles/htc/vive_controller", "/input/trigger/click", "/input/trigger/value", "/input/trackpad");
        suggest(
            "/interaction_profiles/bytedance/pico_neo3_controller", nullptr, "/input/trigger/value", "/input/thumbstick");
        suggest("/interaction_profiles/bytedance/pico4_controller", nullptr, "/input/trigger/value", "/input/thumbstick");
        suggest("/interaction_profiles/ext/hand_interaction", nullptr, "/input/pinch_ext/value", nullptr);

        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1;
        attach.actionSets = &actionSet;
        if (XR_FAILED(api.xrAttachSessionActionSets(session, &attach)))
        {
            qCWarning(OpenXrLog) << "Could not attach OpenXR actions; the window will show without laser input";
            return;
        }

        for (int hand = 0; hand < 2; ++hand)
        {
            XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            spaceInfo.action = aimAction;
            spaceInfo.subactionPath = handPath[hand];
            spaceInfo.poseInActionSpace.orientation.w = 1;
            if (XR_FAILED(api.xrCreateActionSpace(session, &spaceInfo, &aimSpace[hand])))
                return;
        }
        inputReady = true;
    }

    // Freeze the panel in the room, in front of wherever the head is now.
    // Until this succeeds the panel stays on the view so it is still visible.
    void placeQuad(XrTime time)
    {
        if (quadPlaced || localSpace == XR_NULL_HANDLE)
            return;
        XrSpaceLocation located{XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(api.xrLocateSpace(viewSpace, localSpace, time, &located)))
            return;
        constexpr XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if ((located.locationFlags & need) != need)
            return;
        const XrQuaternionf ori = located.pose.orientation;
        const Vec3 head{located.pose.position.x, located.pose.position.y, located.pose.position.z};
        const Vec3 pos = head + rotate(ori, {0, -kQuadDropM, -kQuadDistanceM});
        quadPose.orientation = ori;
        quadPose.position = {pos.x, pos.y, pos.z};
        quadSpace = localSpace;
        quadPlaced = true;
    }

    void destroySwapchain()
    {
        if (swapchain != XR_NULL_HANDLE && api.xrDestroySwapchain != nullptr)
            api.xrDestroySwapchain(swapchain);
        swapchain = XR_NULL_HANDLE;
        images.clear();
        swapW = 0;
        swapH = 0;
    }

    bool ensureSwapchain(int width, int height)
    {
        if (swapchain != XR_NULL_HANDLE && swapW == width && swapH == height)
            return true;
        if (failedSwapW == width && failedSwapH == height)
            return false;
        destroySwapchain();

        uint32_t formatCount = 0;
        if (XR_FAILED(api.xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr)) || formatCount == 0)
            return false;
        std::vector<int64_t> formats(formatCount);
        if (XR_FAILED(api.xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data())))
            return false;
        // WiVRn samples an sRGB swapchain and encodes again for the headset.
        // A linear format stores the grab's sRGB bytes and that second encode
        // crushes the picture.
        int64_t format = formats.front();
        for (const int64_t want : {kGlSrgb8Alpha8, kGlRgba8})
        {
            if (std::find(formats.begin(), formats.end(), want) != formats.end())
            {
                format = want;
                break;
            }
        }

        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags =
            XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = format;
        info.sampleCount = 1;
        info.width = static_cast<uint32_t>(width);
        info.height = static_cast<uint32_t>(height);
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        if (XR_FAILED(api.xrCreateSwapchain(session, &info, &swapchain)))
        {
            failedSwapW = width;
            failedSwapH = height;
            qCWarning(OpenXrLog) << "Could not create an OpenXR swapchain of" << width << "x" << height;
            return false;
        }
        uint32_t imageCount = 0;
        if (XR_FAILED(api.xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr)) || imageCount == 0)
        {
            destroySwapchain();
            failedSwapW = width;
            failedSwapH = height;
            return false;
        }
        images.assign(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        if (XR_FAILED(api.xrEnumerateSwapchainImages(
                swapchain, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data()))))
        {
            destroySwapchain();
            failedSwapW = width;
            failedSwapH = height;
            return false;
        }
        swapW = width;
        swapH = height;
        swapSrgb = format == kGlSrgb8Alpha8;
        return true;
    }

    bool upload(QOpenGLExtraFunctions *gl, const QImage &image)
    {
        if (!ensureSwapchain(image.width(), image.height()))
            return false;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        uint32_t index = 0;
        if (XR_FAILED(api.xrAcquireSwapchainImage(swapchain, &acquire, &index)) || index >= images.size())
            return false;
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kSwapchainWaitNs;
        const bool writable = XR_SUCCEEDED(api.xrWaitSwapchainImage(swapchain, &wait));
        bool wrote = false;
        if (writable)
        {
            // OpenGL stores row 0 at the bottom. The grab is top-down, and
            // WiVRn flips a GL layer again on the way to the headset, so flip
            // here and the two inversions leave the window upright. A blit
            // into the imported image does not land; draw into its framebuffer.
            // An sRGB target would encode these already-encoded bytes on the
            // way out, so leave that conversion off.
            for (GLenum stale = gl->glGetError(); stale != GL_NO_ERROR; stale = gl->glGetError())
            {
            }
            if (swapSrgb)
                gl->glDisable(GL_FRAMEBUFFER_SRGB);
            const QImage rows = opaqueCopy(image).flipped(Qt::Vertical);
            const int rowLength = rows.bytesPerLine() / 4;

            GLuint staging = 0;
            GLuint drawFbo = 0;
            gl->glGenTextures(1, &staging);
            gl->glBindTexture(GL_TEXTURE_2D, staging);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            gl->glPixelStorei(kGlUnpackRowLength, rowLength);
            gl->glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8, rows.width(), rows.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rows.constBits());
            gl->glPixelStorei(kGlUnpackRowLength, 0);

            gl->glGenFramebuffers(1, &drawFbo);
            gl->glBindFramebuffer(GL_FRAMEBUFFER, drawFbo);
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[index].image, 0);
            const bool drawOk = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (drawOk && drawStaging(gl, staging, swapW, swapH))
                wrote = true;
            else
            {
                gl->glBindTexture(GL_TEXTURE_2D, images[index].image);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                gl->glPixelStorei(kGlUnpackRowLength, rowLength);
                gl->glTexSubImage2D(
                    GL_TEXTURE_2D, 0, 0, 0, rows.width(), rows.height(), GL_RGBA, GL_UNSIGNED_BYTE, rows.constBits());
                gl->glPixelStorei(kGlUnpackRowLength, 0);
                wrote = true;
            }
            // Finish while the staging texture still exists, then drop it.
            gl->glFinish();
            gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            gl->glDeleteFramebuffers(1, &drawFbo);
            gl->glDeleteTextures(1, &staging);
            if (const GLenum err = gl->glGetError(); err != GL_NO_ERROR)
            {
                wrote = false;
                static bool warned = false;
                if (!warned)
                {
                    qCWarning(OpenXrLog) << "OpenGL rejected the headset image upload (" << int(err) << ")";
                    warned = true;
                }
            }
        }
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        api.xrReleaseSwapchainImage(swapchain, &release);
        return wrote;
    }

    // WiVRn draws its own controller rays only while its menu is up. A streamed
    // app is expected to draw the pointers, so this is a solid colour they can
    // be cut from.
    bool ensureRayImage(QOpenGLExtraFunctions *gl)
    {
        if (rayReady)
            return true;
        int64_t format = swapSrgb ? kGlSrgb8Alpha8 : kGlRgba8;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = format;
        info.sampleCount = 1;
        info.width = 8;
        info.height = 8;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        if (XR_FAILED(api.xrCreateSwapchain(session, &info, &raySwapchain)))
            return false;
        const auto failRay = [&] {
            if (raySwapchain != XR_NULL_HANDLE && api.xrDestroySwapchain != nullptr)
                api.xrDestroySwapchain(raySwapchain);
            raySwapchain = XR_NULL_HANDLE;
            return false;
        };
        uint32_t imageCount = 0;
        if (XR_FAILED(api.xrEnumerateSwapchainImages(raySwapchain, 0, &imageCount, nullptr)) || imageCount == 0)
            return failRay();
        std::vector<SwapchainImageOpenGL> rayImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        if (XR_FAILED(api.xrEnumerateSwapchainImages(
                raySwapchain, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader *>(rayImages.data()))))
            return failRay();
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        uint32_t index = 0;
        if (XR_FAILED(api.xrAcquireSwapchainImage(raySwapchain, &acquire, &index)) || index >= rayImages.size())
            return failRay();
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kSwapchainWaitNs;
        if (XR_FAILED(api.xrWaitSwapchainImage(raySwapchain, &wait)))
        {
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            api.xrReleaseSwapchainImage(raySwapchain, &release);
            return failRay();
        }
        GLuint fbo = 0;
        gl->glGenFramebuffers(1, &fbo);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rayImages[index].image, 0);
        if (swapSrgb)
            gl->glDisable(GL_FRAMEBUFFER_SRGB);
        gl->glViewport(0, 0, 8, 8);
        gl->glClearColor(0.35f, 0.78f, 1.f, 1.f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        gl->glFinish();
        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl->glDeleteFramebuffers(1, &fbo);
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        api.xrReleaseSwapchainImage(raySwapchain, &release);
        rayReady = true;
        return true;
    }

    std::optional<QPointF> hitQuad(const XrPosef &aim) const
    {
        if (imageW <= 0 || imageH <= 0 || windowSize.width() <= 0 || windowSize.height() <= 0)
            return std::nullopt;
        const XrQuaternionf ori = quadPose.orientation;
        const Vec3 center{quadPose.position.x, quadPose.position.y, quadPose.position.z};
        const Vec3 axisX = rotate(ori, {1, 0, 0});
        const Vec3 axisY = rotate(ori, {0, 1, 0});
        // +Z is the face Monado draws. The pointer ray still comes from the viewer.
        const Vec3 normal = rotate(ori, {0, 0, 1});
        const float quadHeight = kQuadWidthM * (static_cast<float>(imageH) / static_cast<float>(imageW));
        const Vec3 origin{aim.position.x, aim.position.y, aim.position.z};
        const Vec3 dir = rotate(aim.orientation, {0, 0, -1});
        const float denom = dot(dir, normal);
        if (std::fabs(denom) < 1e-5f)
            return std::nullopt;
        const float t = dot(center - origin, normal) / denom;
        if (t < 0)
            return std::nullopt;
        const Vec3 rel = origin + dir * t - center;
        const float lx = dot(rel, axisX);
        const float ly = dot(rel, axisY);
        if (std::fabs(lx) > kQuadWidthM * 0.5f || std::fabs(ly) > quadHeight * 0.5f)
            return std::nullopt;
        // Facing the +Z side, +X is to the viewer's right and +Y is up.
        const float u = 0.5f + lx / kQuadWidthM;
        const float v = 0.5f - ly / quadHeight;
        const float scale = static_cast<float>(imageW) / static_cast<float>(windowSize.width());
        return QPointF{static_cast<qreal>((u * static_cast<float>(imageW)) / scale),
                       static_cast<qreal>((v * static_cast<float>(imageH)) / scale)};
    }

    void pollInput(XrTime time)
    {
        if (!inputReady || !haveImage)
            return;
        XrActiveActionSet active{actionSet, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        if (XR_FAILED(api.xrSyncActions(session, &sync)))
            return;

        struct Hand
        {
            bool hit = false;
            bool select = false;
            float scroll = 0;
            QPointF pos;
        } hands[2];

        for (int i = 0; i < 2; ++i)
        {
            XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
            get.subactionPath = handPath[i];
            get.action = aimAction;
            XrActionStatePose poseState{XR_TYPE_ACTION_STATE_POSE};
            if (XR_FAILED(api.xrGetActionStatePose(session, &get, &poseState)) || !poseState.isActive)
                continue;
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            if (XR_FAILED(api.xrLocateSpace(aimSpace[i], quadSpace, time, &location)))
                continue;
            const auto valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if ((location.locationFlags & valid) != valid)
                continue;
            if (auto hit = hitQuad(location.pose))
            {
                hands[i].hit = true;
                hands[i].pos = *hit;
            }

            get.action = clickAction;
            XrActionStateBoolean click{XR_TYPE_ACTION_STATE_BOOLEAN};
            if (XR_SUCCEEDED(api.xrGetActionStateBoolean(session, &get, &click)) && click.isActive && click.currentState)
                hands[i].select = true;
            get.action = triggerAction;
            XrActionStateFloat trigger{XR_TYPE_ACTION_STATE_FLOAT};
            if (XR_SUCCEEDED(api.xrGetActionStateFloat(session, &get, &trigger)) && trigger.isActive &&
                trigger.currentState >= kTriggerClick)
                hands[i].select = true;
            get.action = scrollAction;
            XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
            if (XR_SUCCEEDED(api.xrGetActionStateVector2f(session, &get, &stick)) && stick.isActive)
                hands[i].scroll = stick.currentState.y;
        }

        int hand = -1;
        for (int i = 0; i < 2 && hand < 0; ++i)
        {
            if (hands[i].hit && hands[i].select)
                hand = i;
        }
        for (int i = 0; i < 2 && hand < 0; ++i)
        {
            if (hands[i].hit)
                hand = i;
        }

        if (pointer.down && (hand < 0 || !hands[hand].select))
        {
            self->queueButton(pointer.last, false);
            pointer.down = false;
        }
        if (hand < 0)
            return;

        if (pointer.last != hands[hand].pos)
        {
            pointer.last = hands[hand].pos;
            self->queueMove(hands[hand].pos);
        }
        if (hands[hand].select && !pointer.down)
        {
            pointer.down = true;
            self->queueButton(hands[hand].pos, true);
        }
        if (std::fabs(hands[hand].scroll) >= kScrollDeadzone)
        {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - pointer.lastScrollMs >= kScrollRepeatMs)
            {
                pointer.lastScrollMs = now;
                self->queueScroll(hands[hand].pos, 0, hands[hand].scroll > 0 ? 1.0f : -1.0f);
            }
        }
    }

    void handleState(XrSessionState state)
    {
        switch (state)
        {
        case XR_SESSION_STATE_READY:
        {
            XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
            begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (check(api.xrBeginSession(session, &begin), "xrBeginSession"))
                sessionRunning = true;
            else
                self->m_stop = true;
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            sessionRunning = false;
            api.xrEndSession(session);
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            sessionRunning = false;
            self->m_stop = true;
            break;
        default:
            break;
        }
    }

    bool pollEvents()
    {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while (api.xrPollEvent(instance, &event) == XR_SUCCESS)
        {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto *changed = reinterpret_cast<const XrEventDataSessionStateChanged *>(&event);
                handleState(changed->state);
            }
            else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            {
                self->m_stop = true;
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
            if (self->m_stop && !sessionRunning)
                break;
        }
        return !self->m_stop || sessionRunning;
    }

    // A ribbon from the controller toward where it is pointing. WiVRn hides its
    // own rays while an app is streamed, so the ribbon is the pointer.
    void updateBeams(XrTime time)
    {
        beams[0].show = beams[1].show = false;
        if (!inputReady)
            return;
        Vec3 head{0, 0, 0};
        XrSpaceLocation headLoc{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(api.xrLocateSpace(viewSpace, quadSpace, time, &headLoc)) &&
            (headLoc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0)
            head = {headLoc.pose.position.x, headLoc.pose.position.y, headLoc.pose.position.z};

        const XrQuaternionf ori = quadPose.orientation;
        const Vec3 center{quadPose.position.x, quadPose.position.y, quadPose.position.z};
        const Vec3 axisX = rotate(ori, {1, 0, 0});
        const Vec3 axisY = rotate(ori, {0, 1, 0});
        const Vec3 normal = rotate(ori, {0, 0, 1});
        const float quadHeight = imageW > 0 ? kQuadWidthM * (static_cast<float>(imageH) / static_cast<float>(imageW)) : 0;

        for (int i = 0; i < 2; ++i)
        {
            if (aimSpace[i] == XR_NULL_HANDLE)
                continue;
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            constexpr XrSpaceLocationFlags need =
                XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if (XR_FAILED(api.xrLocateSpace(aimSpace[i], quadSpace, time, &location)) ||
                (location.locationFlags & need) != need)
                continue;
            const Vec3 origin{location.pose.position.x, location.pose.position.y, location.pose.position.z};
            const Vec3 dir = normalize(rotate(location.pose.orientation, {0, 0, -1}));
            float length = kBeamReachM;
            const float denom = dot(dir, normal);
            if (std::fabs(denom) > 1e-5f && quadHeight > 0)
            {
                const float t = dot(center - origin, normal) / denom;
                if (t > 0.04f)
                {
                    const Vec3 rel = origin + dir * t - center;
                    if (std::fabs(dot(rel, axisX)) <= kQuadWidthM * 0.5f && std::fabs(dot(rel, axisY)) <= quadHeight * 0.5f)
                        length = t - 0.015f;
                }
            }
            const Vec3 mid = origin + dir * (length * 0.5f);
            Vec3 side = (head - mid) - dir * dot(head - mid, dir);
            if (dot(side, side) < 1e-6f)
            {
                const Vec3 helper = std::fabs(dir.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                side = helper - dir * dot(helper, dir);
            }
            const Vec3 z = normalize(side);
            const Vec3 x = normalize(cross(dir, z));
            beams[i].pose.orientation = quatFromBasis(x, dir, cross(x, dir));
            beams[i].pose.position = {mid.x, mid.y, mid.z};
            beams[i].length = length;
            beams[i].show = true;
        }
    }

    // False means the runtime is gone and the caller should reconnect.
    bool pump(QOpenGLExtraFunctions *gl)
    {
        if (!pollEvents())
            return !self->m_stop;
        if (self->m_stop)
            return true;
        if (!sessionRunning)
        {
            QThread::msleep(10);
            return true;
        }

        XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        if (!check(api.xrWaitFrame(session, &waitInfo, &frameState), "xrWaitFrame"))
            return false;
        XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        if (!check(api.xrBeginFrame(session, &beginInfo), "xrBeginFrame"))
            return false;

        // Events are polled at the start of the next frame. Ending the
        // session from inside an open frame is not allowed.
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerQuad beamLayer[2];
        const XrCompositionLayerBaseHeader *layers[3];
        uint32_t layerCount = 0;
        if (frameState.shouldRender && !self->m_stop)
        {
            QImage grabbed;
            QSize grabbedWindow;
            if (self->m_frame->take(&grabbed, &grabbedWindow))
            {
                if (upload(gl, grabbed))
                {
                    imageW = grabbed.width();
                    imageH = grabbed.height();
                    windowSize = grabbedWindow;
                    haveImage = true;
                }
            }
            if (haveImage && swapchain != XR_NULL_HANDLE)
            {
                placeQuad(frameState.predictedDisplayTime);
                pollInput(frameState.predictedDisplayTime);
                const float quadHeight = kQuadWidthM * (static_cast<float>(imageH) / static_cast<float>(imageW));
                // Leave alpha to the runtime. Asking for the real alpha channel
                // makes an unwritten image fully clear, so the panel vanishes.
                quad.layerFlags = 0;
                quad.space = quadSpace;
                quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                quad.subImage.swapchain = swapchain;
                quad.subImage.imageRect.offset = {0, 0};
                quad.subImage.imageRect.extent = {swapW, swapH};
                quad.pose = quadPose;
                quad.size = {kQuadWidthM, quadHeight};
                layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad);
                layerCount = 1;
                if (ensureRayImage(gl))
                {
                    updateBeams(frameState.predictedDisplayTime);
                    for (int i = 0; i < 2; ++i)
                    {
                        if (!beams[i].show)
                            continue;
                        XrCompositionLayerQuad &beam = beamLayer[i];
                        beam = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                        beam.layerFlags = 0;
                        beam.space = quadSpace;
                        beam.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                        beam.subImage.swapchain = raySwapchain;
                        beam.subImage.imageRect.offset = {0, 0};
                        beam.subImage.imageRect.extent = {8, 8};
                        beam.pose = beams[i].pose;
                        beam.size = {kBeamThicknessM, beams[i].length};
                        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&beam);
                    }
                }
            }
        }

        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = blend;
        endInfo.layerCount = layerCount;
        endInfo.layers = layerCount == 0 ? nullptr : layers;
        if (!check(api.xrEndFrame(session, &endInfo), "xrEndFrame"))
            return false;
        return !self->m_stop;
    }

    void shutdown()
    {
        destroySwapchain();
        if (raySwapchain != XR_NULL_HANDLE && api.xrDestroySwapchain != nullptr)
            api.xrDestroySwapchain(raySwapchain);
        raySwapchain = XR_NULL_HANDLE;
        rayReady = false;
        if (api.xrDestroySpace != nullptr)
        {
            for (XrSpace space : aimSpace)
            {
                if (space != XR_NULL_HANDLE)
                    api.xrDestroySpace(space);
            }
            if (viewSpace != XR_NULL_HANDLE)
                api.xrDestroySpace(viewSpace);
            if (localSpace != XR_NULL_HANDLE)
                api.xrDestroySpace(localSpace);
        }
        aimSpace[0] = aimSpace[1] = XR_NULL_HANDLE;
        quadSpace = XR_NULL_HANDLE;
        viewSpace = XR_NULL_HANDLE;
        localSpace = XR_NULL_HANDLE;
        if (session != XR_NULL_HANDLE && api.xrDestroySession != nullptr)
            api.xrDestroySession(session);
        session = XR_NULL_HANDLE;
        if (api.xrDestroyAction != nullptr)
        {
            for (XrAction action : {aimAction, clickAction, triggerAction, scrollAction})
            {
                if (action != XR_NULL_HANDLE)
                    api.xrDestroyAction(action);
            }
        }
        aimAction = clickAction = triggerAction = scrollAction = XR_NULL_HANDLE;
        if (actionSet != XR_NULL_HANDLE && api.xrDestroyActionSet != nullptr)
            api.xrDestroyActionSet(actionSet);
        actionSet = XR_NULL_HANDLE;
        if (instance != XR_NULL_HANDLE && api.xrDestroyInstance != nullptr)
            api.xrDestroyInstance(instance);
        instance = XR_NULL_HANDLE;
    }
};

OpenXrSession::OpenXrSession(VrOverlay *overlay)
    : QObject{nullptr},
      m_overlay{overlay},
      m_frame{std::make_unique<Frame>()}
{}

OpenXrSession::~OpenXrSession()
{
    stop();
}

bool OpenXrSession::loaderPresent()
{
    static const bool present = [] {
        void *lib = dlopen("libopenxr_loader.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (lib == nullptr)
            lib = dlopen("libopenxr_loader.so", RTLD_LAZY | RTLD_LOCAL);
        const bool ok = lib != nullptr;
        if (lib != nullptr)
            dlclose(lib);
        return ok;
    }();
    return present;
}

void OpenXrSession::start(quint64 generation)
{
    stop();
    m_stop = false;
    m_acceptInput = false;
    m_generation = generation;
    m_waylandDisplay = nullptr;
    m_x11Display = nullptr;
#if QT_CONFIG(wayland)
    if (auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
        m_waylandDisplay = wayland->display();
#endif
#if QT_CONFIG(xcb)
    if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
        m_x11Display = x11->display();
#endif

    if (m_surface == nullptr)
    {
        m_surface = std::make_unique<QOffscreenSurface>();
        m_surface->setFormat(openGlFormat());
        m_surface->create();
    }
    if (!m_surface->isValid())
    {
        emit failed("Could not create an OpenGL surface for OpenXR."_L1, generation);
        return;
    }

    m_thread = QThread::create([this] { run(); });
    m_thread->setObjectName("openxr"_L1);
    m_thread->start();
}

void OpenXrSession::stop()
{
    m_acceptInput = false;
    m_stop = true;
    if (m_thread == nullptr)
        return;
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
}

void OpenXrSession::submitFrame(const QImage &image, QSize windowSize)
{
    if (m_frame != nullptr)
        m_frame->set(image, windowSize);
}

void OpenXrSession::queueMove(QPointF pos)
{
    if (!m_acceptInput)
        return;
    QMetaObject::invokeMethod(this, [this, pos] { deliverMove(pos); }, Qt::QueuedConnection);
}

void OpenXrSession::queueButton(QPointF pos, bool down)
{
    // Presses stop when the session is going away. Releases still go through,
    // so a trigger let go during shutdown can't leave the mouse button stuck.
    if (down && !m_acceptInput)
        return;
    QMetaObject::invokeMethod(this, [this, pos, down] { deliverButton(pos, down); }, Qt::QueuedConnection);
}

void OpenXrSession::queueScroll(QPointF pos, float dx, float dy)
{
    if (!m_acceptInput)
        return;
    QMetaObject::invokeMethod(this, [this, pos, dx, dy] { deliverScroll(pos, dx, dy); }, Qt::QueuedConnection);
}

void OpenXrSession::deliverMove(QPointF pos)
{
    if (m_overlay != nullptr)
        m_overlay->pointerMove(pos);
}

void OpenXrSession::deliverButton(QPointF pos, bool down)
{
    if (m_overlay != nullptr)
        m_overlay->pointerButton(pos, down);
}

void OpenXrSession::deliverScroll(QPointF pos, float dx, float dy)
{
    if (m_overlay != nullptr)
        m_overlay->pointerScroll(pos, dx, dy);
}

void OpenXrSession::run()
{
    const quint64 generation = m_generation;
    auto context = std::make_unique<QOpenGLContext>();
    context->setFormat(m_surface->format());
    if (!context->create() || !context->makeCurrent(m_surface.get()))
    {
        emit failed("Could not create an OpenGL context for OpenXR."_L1, generation);
        return;
    }

    void *library = dlopen("libopenxr_loader.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (library == nullptr)
        library = dlopen("libopenxr_loader.so", RTLD_LAZY | RTLD_LOCAL);
    if (library == nullptr)
    {
        context->doneCurrent();
        emit failed("The OpenXR loader (libopenxr_loader.so.1) was not found."_L1, generation);
        return;
    }

    OpenXrLoop loop;
    loop.self = this;
    bool lost = false;
    if (!loop.loadCore(library) || !loop.createInstance() || !loop.createSession(context.get()))
    {
        loop.shutdown();
        context->doneCurrent();
        dlclose(library);
        emit failed(loop.error.isEmpty() ? "Could not start an OpenXR session."_L1 : loop.error, generation);
        return;
    }

    qCInfo(OpenXrLog) << (loop.overlaySession ? "OpenXR overlay session is running" : "OpenXR session is running");
    m_acceptInput = true;
    emit connected(loop.overlaySession, generation);

    auto *gl = context->extraFunctions();
    gl->initializeOpenGLFunctions();
    while (!m_stop)
    {
        if (!loop.pump(gl))
        {
            lost = true;
            break;
        }
    }
    m_acceptInput = false;
    if (loop.pointer.down)
        queueButton(loop.pointer.last, false);
    loop.shutdown();
    context->doneCurrent();
    dlclose(library);
    if (lost)
        emit disconnected(generation);
}
