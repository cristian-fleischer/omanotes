#include "warmup.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0) && QT_CONFIG(wayland)
#define CAN_WARM_EGL
#include <dlfcn.h>
#endif

// The part of a launch that waits longest on one thing is the driver: loading
// Mesa and everything under it, and initialising the screen. Qt does it when
// the window is made. An EGLDisplay belongs to the Wayland connection it was
// asked for, so initialising it here is initialising the one Qt will ask for:
// its own call finds it done, or waits for the rest of it.
std::thread warmGraphics() {
#ifdef CAN_WARM_EGL
    if (!qgetenv("QT_QUICK_BACKEND").isEmpty() || !qgetenv("QSG_RHI_BACKEND").isEmpty())
        return {};
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    void *connection = wayland ? static_cast<void *>(wayland->display()) : nullptr;
    if (!connection)
        return {};
    return std::thread([connection] {
        // Not linked: Qt's own plugin brings it, and only where it is used.
        void *egl = dlopen("libEGL.so.1", RTLD_LAZY);
        if (!egl)
            return;
        using Proc = void (*)();
        using GetProcAddress = Proc (*)(const char *);
        using GetPlatformDisplay = void *(*)(unsigned int, void *, const int *);
        using Initialize = unsigned int (*)(void *, int *, int *);
        const auto getProcAddress =
            reinterpret_cast<GetProcAddress>(dlsym(egl, "eglGetProcAddress"));
        const auto initialize = reinterpret_cast<Initialize>(dlsym(egl, "eglInitialize"));
        if (!getProcAddress || !initialize)
            return;
        const auto getPlatformDisplay =
            reinterpret_cast<GetPlatformDisplay>(getProcAddress("eglGetPlatformDisplayEXT"));
        if (!getPlatformDisplay)
            return;
        constexpr unsigned int platformWayland = 0x31d8; // EGL_PLATFORM_WAYLAND_EXT
        if (void *display = getPlatformDisplay(platformWayland, connection, nullptr))
            initialize(display, nullptr, nullptr);
    });
#else
    return {};
#endif
}
