/*
 * check_egl_gbm.c
 *
 * Diagnostic tool: opens a DRM render node, creates a GBM device on it
 * (same sequence Cog's DRM platform uses), and prints which EGL client
 * and display extensions the driver actually reports — specifically
 * whether it's EGL_KHR_platform_gbm, EGL_MESA_platform_gbm, both, or
 * neither, which is what cog-drm-gles-renderer.c branches on.
 *
 * Build (on the R36S itself, dArkOS already has the headers/libs needed):
 *   gcc -o check_egl_gbm check_egl_gbm.c $(pkg-config --cflags --libs egl gbm) -ldrm -I/usr/include/libdrm
 *
 * Run (try both nodes if one fails to open):
 *   ./check_egl_gbm /dev/dri/card0
 *   ./check_egl_gbm /dev/dri/renderD128
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int has_ext(const char *exts, const char *needle) {
    return exts && strstr(exts, needle) != NULL;
}

int main(int argc, char **argv) {
    const char *node = argc > 1 ? argv[1] : "/dev/dri/card0";

    int fd = open(node, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    printf("Opened %s (fd=%d)\n", node, fd);

    struct gbm_device *gbm = gbm_create_device(fd);
    if (!gbm) {
        fprintf(stderr, "gbm_create_device failed\n");
        return 1;
    }
    printf("GBM device created OK\n\n");

    /* Client extensions must be queried against EGL_NO_DISPLAY, before
     * any display is created. This is a common gotcha and worth
     * checking in Cog's code too. */
    const char *client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    printf("=== EGL CLIENT extensions (EGL_NO_DISPLAY) ===\n%s\n\n",
           client_exts ? client_exts : "(null / not supported)");

    printf("EGL_EXT_platform_base   : %s\n", has_ext(client_exts, "EGL_EXT_platform_base") ? "YES" : "no");
    printf("EGL_KHR_platform_gbm    : %s\n", has_ext(client_exts, "EGL_KHR_platform_gbm") ? "YES" : "no");
    printf("EGL_MESA_platform_gbm   : %s\n", has_ext(client_exts, "EGL_MESA_platform_gbm") ? "YES" : "no");
    printf("\n");

    EGLDisplay dpy = EGL_NO_DISPLAY;

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (get_platform_display && has_ext(client_exts, "EGL_KHR_platform_gbm")) {
        dpy = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
        printf("Tried eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR): %s\n", dpy != EGL_NO_DISPLAY ? "OK" : "FAILED");
    }
    if (dpy == EGL_NO_DISPLAY && get_platform_display && has_ext(client_exts, "EGL_MESA_platform_gbm")) {
        dpy = get_platform_display(0x31D7 /* EGL_PLATFORM_GBM_MESA/KHR value */, gbm, NULL);
        printf("Tried eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA): %s\n", dpy != EGL_NO_DISPLAY ? "OK" : "FAILED");
    }
    if (dpy == EGL_NO_DISPLAY) {
        dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
        printf("Fell back to plain eglGetDisplay(gbm): %s\n", dpy != EGL_NO_DISPLAY ? "OK" : "FAILED");
    }

    if (dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "\nCould not obtain any EGLDisplay at all. Stopping here.\n");
        return 1;
    }

    EGLint major, minor;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "\neglInitialize FAILED (error 0x%x)\n", eglGetError());
        return 1;
    }
    printf("\neglInitialize OK: EGL %d.%d\n", major, minor);
    printf("EGL_VENDOR : %s\n", eglQueryString(dpy, EGL_VENDOR));
    printf("EGL_VERSION: %s\n", eglQueryString(dpy, EGL_VERSION));

    const char *display_exts = eglQueryString(dpy, EGL_EXTENSIONS);
    printf("\n=== EGL DISPLAY extensions (this dpy) ===\n%s\n\n", display_exts ? display_exts : "(null)");

    printf("EGL_KHR_platform_gbm on display : %s\n", has_ext(display_exts, "EGL_KHR_platform_gbm") ? "YES" : "no");
    printf("EGL_MESA_platform_gbm on display: %s\n", has_ext(display_exts, "EGL_MESA_platform_gbm") ? "YES" : "no");

    void *p1 = eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
    printf("\neglGetProcAddress(\"eglCreatePlatformWindowSurfaceEXT\") = %p (%s)\n", p1, p1 ? "resolved" : "NULL!");

    return 0;
}
