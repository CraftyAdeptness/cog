/*
 * egl_vendor_mali.c
 *
 * GLVND EGL "vendor" wrapper para el blob propietario libMali.so
 * (Bifrost G31, r13p0-01rel0) en el R36S / dArkOS.
 *
 * Objetivo: que GLVND (libEGL.so.1 del sistema) pueda enrutar las
 * llamadas EGL genéricas de WPEWebKit's WebProcess hacia Mali en vez
 * de Mesa, sin tocar el código fuente de WebKit.
 *
 * Reutiliza la misma estrategia de resolución dlopen/dlsym que ya
 * usamos en cog-mali-dispatch.c para libMali.so. Este archivo NO
 * reemplaza eso -- es una capa nueva y separada que se registra ante
 * GLVND como vendor (10_mali.json), mientras que cog-mali-dispatch.c
 * sigue siendo el bypass directo que usa el propio proceso Cog.
 *
 * ABI de referencia: NVIDIA/libglvnd include/glvnd/libeglabi.h
 * (ABI version 0.2 al momento de escribir esto).
 *
 * CONFIRMADO en el dispositivo real (find_mali_blob.sh, secciones 5
 * y 6): libmali-bifrost-g31-rxp0-gbm.so NO exporta eglGetPlatformDisplay
 * ni eglGetPlatformDisplayEXT bajo ningún nombre, en ninguna de las
 * dos arquitecturas (aarch64 ni armhf). Solo implementa el
 * eglGetDisplay() clásico de EGL 1.4. Sí expone un símbolo interno
 * no-público "egl_winsys_get_implementation_gbm", lo que indica que
 * el blob autodetecta el tipo de native-display (GBM, X11, etc.)
 * inspeccionando el propio puntero que se le pasa a eglGetDisplay(),
 * al estilo pre-EGL-1.5. Por eso este wrapper NUNCA intenta resolver
 * ninguna variante "Platform" del lado de Mali -- ver
 * mali_getPlatformDisplay() más abajo, que siempre delega a
 * eglGetDisplay() clásico sin importar qué "platform" pida GLVND.
 *
 * También confirmado: /usr/lib/aarch64-linux-gnu/libEGL.so.1 (el
 * SONAME que cargan los binarios en runtime) SÍ es el dispatcher real
 * de GLVND -- el symlink de desarrollo libEGL.so (sin versión) apunta
 * directo a libMali.so, pero eso solo afecta a binarios compilados
 * EN el propio dispositivo con -lEGL; los binarios que traemos desde
 * la CI (cross-compilados contra el libegl-dev genérico de Debian
 * trixie) quedan con DT_NEEDED=libEGL.so.1, o sea sí pasan por GLVND.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glvnd/libeglabi.h>

/* ------------------------------------------------------------------ */
/* Carga perezosa de libMali.so                                        */
/* ------------------------------------------------------------------ */

static void *mali_handle = NULL;

/* Firmas reales, confirmadas presentes en el .dynsym del blob. */
typedef EGLDisplay (*PFN_eglGetDisplay)(EGLNativeDisplayType);
typedef EGLBoolean (*PFN_eglBindAPI)(EGLenum);
typedef void *(*PFN_eglGetProcAddress)(const char *);

static PFN_eglGetDisplay      real_eglGetDisplay;
static PFN_eglBindAPI         real_eglBindAPI;
static PFN_eglGetProcAddress  real_eglGetProcAddress;

static int mali_load(void)
{
    if (mali_handle)
        return 1;

    /* Confirmado en el dispositivo: /usr/lib/aarch64-linux-gnu/libMali.so
     * (symlink -> libmali-bifrost-g31-rxp0-gbm.so). dlopen sin ruta
     * absoluta basta porque ese directorio está en el ld.so search
     * path por defecto en aarch64. */
    mali_handle = dlopen("libMali.so", RTLD_NOW | RTLD_GLOBAL);
    if (!mali_handle) {
        fprintf(stderr, "[egl_vendor_mali] dlopen(libMali.so) failed: %s\n", dlerror());
        return 0;
    }

    real_eglGetDisplay     = (PFN_eglGetDisplay)     dlsym(mali_handle, "eglGetDisplay");
    real_eglBindAPI        = (PFN_eglBindAPI)        dlsym(mali_handle, "eglBindAPI");
    real_eglGetProcAddress = (PFN_eglGetProcAddress) dlsym(mali_handle, "eglGetProcAddress");

    /* Los tres símbolos anteriores SÍ aparecieron limpios en el
     * .dynsym (confirmado con nm -D y readelf --dyn-syms), a
     * diferencia de otros símbolos GLES que sabemos que vienen
     * malformados -- por eso aquí no hace falta el fallback vía
     * eglGetProcAddress que usamos en cog-mali-dispatch.c. Si esto
     * llegara a fallar en la práctica, ese es el primer lugar donde
     * agregar el mismo workaround. */

    if (!real_eglGetDisplay || !real_eglBindAPI || !real_eglGetProcAddress) {
        fprintf(stderr, "[egl_vendor_mali] missing required Mali symbols\n");
        return 0;
    }

    return 1;
}

/* ------------------------------------------------------------------ */
/* Imports que GLVND nos pide implementar                              */
/* ------------------------------------------------------------------ */

static const __EGLapiExports *g_exports = NULL;

static EGLDisplay mali_getPlatformDisplay(EGLenum platform, void *native_display,
                                           const EGLAttrib *attrib_list)
{
    (void) platform;
    (void) attrib_list;

    if (!mali_load())
        return EGL_NO_DISPLAY;

    /* Ver nota grande al inicio del archivo: el blob no distingue
     * "platform", así que siempre delegamos a eglGetDisplay clásico
     * con el native_display tal cual nos llegue (EGL_DEFAULT_DISPLAY,
     * un gbm_device*, etc.) -- Mali lo autodetecta internamente. */
    return real_eglGetDisplay((EGLNativeDisplayType) native_display);
}

static EGLBoolean mali_getSupportsAPI(EGLenum api)
{
    /* El blob es GLES-only en este dispositivo. */
    return (api == EGL_OPENGL_ES_API) ? EGL_TRUE : EGL_FALSE;
}

static void *mali_getProcAddress(const char *procname)
{
    if (!mali_load())
        return NULL;

    void *addr = dlsym(mali_handle, procname);
    if (!addr)
        addr = real_eglGetProcAddress(procname);
    return addr;
}

static void *mali_getDispatchAddress(const char *procname)
{
    /* Sin despacho de extensiones EGL a nivel display por ahora.
     * Suficiente para levantar contexto GLES básico; hay que revisar
     * si WebKit necesita alguna EGL display extension puntual
     * (p.ej. EGL_KHR_image_base) antes de dar esto por cerrado. */
    (void) procname;
    return NULL;
}

static void mali_setDispatchIndex(const char *procname, int index)
{
    (void) procname;
    (void) index;
}

/* ------------------------------------------------------------------ */
/* Entry point exigido por GLVND                                       */
/* ------------------------------------------------------------------ */

EGLBoolean __egl_Main(uint32_t version, const __EGLapiExports *exports,
                       __EGLvendorInfo *vendor, __EGLapiImports *imports)
{
    (void) vendor;

    if (EGL_VENDOR_ABI_GET_MAJOR_VERSION(version) != EGL_VENDOR_ABI_MAJOR_VERSION) {
        fprintf(stderr, "[egl_vendor_mali] ABI major version mismatch (got %u, want %u)\n",
                EGL_VENDOR_ABI_GET_MAJOR_VERSION(version), EGL_VENDOR_ABI_MAJOR_VERSION);
        return EGL_FALSE;
    }

    g_exports = exports;

    memset(imports, 0, sizeof(*imports));
    imports->getPlatformDisplay  = mali_getPlatformDisplay;
    imports->getSupportsAPI      = mali_getSupportsAPI;
    imports->getProcAddress      = mali_getProcAddress;
    imports->getDispatchAddress  = mali_getDispatchAddress;
    imports->setDispatchIndex    = mali_setDispatchIndex;

    /* Optativos que dejamos NULL por ahora:
     *   getVendorString, isPatchSupported/initiatePatch/releasePatch,
     *   patchThreadAttach, findNativeDisplayPlatform.
     * findNativeDisplayPlatform podría valer la pena implementarlo
     * después para que eglGetDisplay(gbm_device) nos identifique
     * directo como vendor sin pasar por Mesa primero. */

    return EGL_TRUE;
}
