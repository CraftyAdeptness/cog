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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <gbm.h>

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

    fprintf(stderr, "[egl_vendor_mali] mali_load() OK, handle=%p\n", mali_handle);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Imports que GLVND nos pide implementar                              */
/* ------------------------------------------------------------------ */

static const __EGLapiExports *g_exports = NULL;

/* CONFIRMADO en el dispositivo, en dos pasos:
 *
 * 1) (check_glvnd_vendor, primera prueba) pasarle
 *    eglGetDisplay(EGL_DEFAULT_DISPLAY) a Mali (native_display == NULL)
 *    falla en eglInitialize con EGL_NOT_INITIALIZED (0x3001). A
 *    diferencia de Mesa, el blob de Mali no auto-detecta ni abre un nodo
 *    DRM por su cuenta cuando no le pasas nada -- necesita un
 *    gbm_device* real.
 *
 * 2) (WPEWebProcess real, con Cog ya corriendo como DRM master en
 *    /dev/dri/card0 para el scanout) usar el nodo PRIMARIO (card0)
 *    desde este proceso secundario hace que Mali rechace el
 *    eglGetDisplay (devuelve NULL sin error explícito) -- Mali
 *    detecta que el master de ese nodo ya lo tiene otro proceso.
 *    WPEWebProcess no hace scanout, solo necesita un contexto GLES
 *    para renderizar a un buffer que Cog compone después -- exactamente
 *    para lo que existen los RENDER NODES (renderD1xx): acceso a la
 *    GPU sin ninguna noción de master/mode-setting, sin contienda con
 *    quien sí es master del nodo primario.
 *
 * Por eso: preferimos /dev/dri/renderD128 siempre que exista, y solo
 * caemos a /dev/dri/card0 como respaldo (útil para pruebas aisladas
 * tipo check_glvnd_vendor donde no hay ningún otro proceso siendo
 * master de nada). */

static struct gbm_device *mali_default_gbm = NULL;
static int mali_default_gbm_fd = -1;

static struct gbm_device *mali_get_default_gbm(void)
{
    if (mali_default_gbm)
        return mali_default_gbm;

    static const char *const candidates[] = {
        "/dev/dri/renderD128",
        "/dev/dri/card0",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        mali_default_gbm_fd = open(candidates[i], O_RDWR);
        if (mali_default_gbm_fd < 0) {
            fprintf(stderr, "[egl_vendor_mali] open(%s) failed: %s\n",
                    candidates[i], strerror(errno));
            continue;
        }

        mali_default_gbm = gbm_create_device(mali_default_gbm_fd);
        if (!mali_default_gbm) {
            fprintf(stderr, "[egl_vendor_mali] gbm_create_device(%s) failed\n", candidates[i]);
            close(mali_default_gbm_fd);
            mali_default_gbm_fd = -1;
            continue;
        }

        fprintf(stderr, "[egl_vendor_mali] using %s for gbm_device\n", candidates[i]);
        return mali_default_gbm;
    }

    return NULL;
}

static EGLDisplay mali_getPlatformDisplay(EGLenum platform, void *native_display,
                                           const EGLAttrib *attrib_list)
{
    (void) attrib_list;

    fprintf(stderr, "[egl_vendor_mali] getPlatformDisplay(platform=0x%x, native_display=%p)\n",
            platform, native_display);

    if (!mali_load())
        return EGL_NO_DISPLAY;

    /* CONFIRMADO en el WebProcess real: WPEBackend-fdo siempre pide
     * EGL_PLATFORM_WAYLAND_KHR (0x31d8) con un native_display propio
     * (un wl_display sintético que arma internamente, sin compositor
     * real detrás), incluso corriendo sobre el backend DRM puro de
     * Cog. Pasarle ese puntero a Mali directamente no sirve de nada
     * -- Mali no entiende Wayland, solo GBM -- y devolvía EGL_NO_DISPLAY,
     * lo que hacía que GLVND cayera a Mesa (y ahí terminaba fallando
     * eglCreateContext con EGL_BAD_MATCH, produciendo el "no provider
     * of glViewport/glTexParameteri" original).
     *
     * En este dispositivo solo existe UNA pantalla real, vía DRM/GBM.
     * No importa qué "platform" o native_display diga pedir GLVND:
     * siempre usamos nuestro propio gbm_device sintético. */
    struct gbm_device *gbm = mali_get_default_gbm();
    if (!gbm) {
        fprintf(stderr, "[egl_vendor_mali] mali_get_default_gbm() failed\n");
        return EGL_NO_DISPLAY;
    }

    EGLDisplay dpy = real_eglGetDisplay((EGLNativeDisplayType) gbm);

    fprintf(stderr, "[egl_vendor_mali] getPlatformDisplay -> %p\n", (void *) dpy);
    return dpy;
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

    fprintf(stderr, "[egl_vendor_mali] __egl_Main called, version=0x%x\n", version);

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
