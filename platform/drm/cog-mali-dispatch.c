/*
 * cog-mali-dispatch.c
 * See cog-mali-dispatch.h for the rationale.
 */

#define _GNU_SOURCE

/* Undo the header's macro redefinitions for THIS file: we need to
 * declare/assign the real function pointer variables using their
 * "cog_mali_*" names directly, not have them macro'd back onto
 * themselves. Including the header still gives us the extern decls
 * and the real EGL/GBM/GLES type definitions we need. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>

#include <dlfcn.h>
#include <stdio.h>

/* Now bring in the extern declarations (the macros defined afterward
 * in the header only affect code AFTER the #include, and we don't
 * call the bare names ourselves in this file, so they're harmless
 * here too -- but to be safe and explicit we don't rely on that). */
#include "cog-mali-dispatch.h"

#undef eglBindAPI
#undef eglChooseConfig
#undef eglCreateContext
#undef eglCreateWindowSurface
#undef eglDestroyContext
#undef eglDestroySurface
#undef eglGetConfigAttrib
#undef eglGetConfigs
#undef eglGetDisplay
#undef eglGetError
#undef eglInitialize
#undef eglMakeCurrent
#undef eglQueryString
#undef eglReleaseThread
#undef eglSwapBuffers
#undef eglTerminate
#undef eglGetCurrentContext
#undef eglGetCurrentDisplay
#undef eglGetCurrentSurface
#undef eglGetProcAddress
#undef eglGetPlatformDisplayEXT
#undef eglCreatePlatformWindowSurfaceEXT
#undef gbm_create_device
#undef gbm_device_get_fd
#undef gbm_surface_create
#undef gbm_surface_lock_front_buffer
#undef gbm_surface_release_buffer
#undef gbm_bo_get_stride
#undef gbm_bo_get_stride_for_plane
#undef gbm_bo_get_offset
#undef gbm_bo_get_plane_count
#undef gbm_bo_get_handle
#undef gbm_bo_get_handle_for_plane
#undef gbm_bo_get_modifier
#undef gbm_bo_get_user_data
#undef gbm_bo_set_user_data
#undef glGetString
#undef glGetError
#undef glClear
#undef glClearColor
#undef glViewport
#undef glCreateShader
#undef glShaderSource
#undef glCompileShader
#undef glGetShaderiv
#undef glGetShaderInfoLog
#undef glDeleteShader
#undef glCreateProgram
#undef glAttachShader
#undef glBindAttribLocation
#undef glLinkProgram
#undef glGetProgramiv
#undef glGetProgramInfoLog
#undef glDeleteProgram
#undef glGenTextures
#undef glBindTexture
#undef glTexParameteri
#undef glDeleteTextures
#undef glGenBuffers
#undef glBindBuffer
#undef glBufferData
#undef glDeleteBuffers
#undef glGenVertexArrays
#undef glBindVertexArray
#undef glDeleteVertexArrays
#undef glGetAttribLocation
#undef glUseProgram
#undef glEnableVertexAttribArray
#undef glDisableVertexAttribArray
#undef glVertexAttribPointer
#undef glActiveTexture
#undef glDrawArrays

/* ---- storage for every pointer ---- */
EGLBoolean (*cog_mali_eglBindAPI)(EGLenum);
EGLBoolean (*cog_mali_eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
EGLContext (*cog_mali_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
EGLSurface (*cog_mali_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *);
EGLBoolean (*cog_mali_eglDestroyContext)(EGLDisplay, EGLContext);
EGLBoolean (*cog_mali_eglDestroySurface)(EGLDisplay, EGLSurface);
EGLBoolean (*cog_mali_eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint *);
EGLBoolean (*cog_mali_eglGetConfigs)(EGLDisplay, EGLConfig *, EGLint, EGLint *);
EGLDisplay (*cog_mali_eglGetDisplay)(EGLNativeDisplayType);
EGLint     (*cog_mali_eglGetError)(void);
EGLBoolean (*cog_mali_eglInitialize)(EGLDisplay, EGLint *, EGLint *);
EGLBoolean (*cog_mali_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
const char *(*cog_mali_eglQueryString)(EGLDisplay, EGLint);
EGLBoolean (*cog_mali_eglReleaseThread)(void);
EGLBoolean (*cog_mali_eglSwapBuffers)(EGLDisplay, EGLSurface);
EGLBoolean (*cog_mali_eglTerminate)(EGLDisplay);
EGLContext (*cog_mali_eglGetCurrentContext)(void);
EGLDisplay (*cog_mali_eglGetCurrentDisplay)(void);
EGLSurface (*cog_mali_eglGetCurrentSurface)(EGLint);
__eglMustCastToProperFunctionPointerType (*cog_mali_eglGetProcAddress)(const char *);

PFNEGLGETPLATFORMDISPLAYEXTPROC        cog_mali_eglGetPlatformDisplayEXT;
PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC cog_mali_eglCreatePlatformWindowSurfaceEXT;

struct gbm_device *(*cog_mali_gbm_create_device)(int);
int      (*cog_mali_gbm_device_get_fd)(struct gbm_device *);
struct gbm_surface *(*cog_mali_gbm_surface_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t);
struct gbm_bo *(*cog_mali_gbm_surface_lock_front_buffer)(struct gbm_surface *);
void     (*cog_mali_gbm_surface_release_buffer)(struct gbm_surface *, struct gbm_bo *);
uint32_t (*cog_mali_gbm_bo_get_stride)(struct gbm_bo *);
uint32_t (*cog_mali_gbm_bo_get_stride_for_plane)(struct gbm_bo *, int);
uint32_t (*cog_mali_gbm_bo_get_offset)(struct gbm_bo *, int);
int      (*cog_mali_gbm_bo_get_plane_count)(struct gbm_bo *);
union gbm_bo_handle (*cog_mali_gbm_bo_get_handle)(struct gbm_bo *);
union gbm_bo_handle (*cog_mali_gbm_bo_get_handle_for_plane)(struct gbm_bo *, int);
uint64_t (*cog_mali_gbm_bo_get_modifier)(struct gbm_bo *);
void    *(*cog_mali_gbm_bo_get_user_data)(struct gbm_bo *);
void     (*cog_mali_gbm_bo_set_user_data)(struct gbm_bo *, void *, void (*)(struct gbm_bo *, void *));

const GLubyte *(*cog_mali_glGetString)(GLenum);
GLenum (*cog_mali_glGetError)(void);
void (*cog_mali_glClear)(GLbitfield);
void (*cog_mali_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
void (*cog_mali_glViewport)(GLint, GLint, GLsizei, GLsizei);
GLuint (*cog_mali_glCreateShader)(GLenum);
void (*cog_mali_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
void (*cog_mali_glCompileShader)(GLuint);
void (*cog_mali_glGetShaderiv)(GLuint, GLenum, GLint *);
void (*cog_mali_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
void (*cog_mali_glDeleteShader)(GLuint);
GLuint (*cog_mali_glCreateProgram)(void);
void (*cog_mali_glAttachShader)(GLuint, GLuint);
void (*cog_mali_glBindAttribLocation)(GLuint, GLuint, const GLchar *);
void (*cog_mali_glLinkProgram)(GLuint);
void (*cog_mali_glGetProgramiv)(GLuint, GLenum, GLint *);
void (*cog_mali_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
void (*cog_mali_glDeleteProgram)(GLuint);
void (*cog_mali_glGenTextures)(GLsizei, GLuint *);
void (*cog_mali_glBindTexture)(GLenum, GLuint);
void (*cog_mali_glTexParameteri)(GLenum, GLenum, GLint);
void (*cog_mali_glDeleteTextures)(GLsizei, const GLuint *);
void (*cog_mali_glGenBuffers)(GLsizei, GLuint *);
void (*cog_mali_glBindBuffer)(GLenum, GLuint);
void (*cog_mali_glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
void (*cog_mali_glDeleteBuffers)(GLsizei, const GLuint *);
void (*cog_mali_glGenVertexArrays)(GLsizei, GLuint *);
void (*cog_mali_glBindVertexArray)(GLuint);
void (*cog_mali_glDeleteVertexArrays)(GLsizei, const GLuint *);
GLint (*cog_mali_glGetAttribLocation)(GLuint, const GLchar *);
void (*cog_mali_glUseProgram)(GLuint);
void (*cog_mali_glEnableVertexAttribArray)(GLuint);
void (*cog_mali_glDisableVertexAttribArray)(GLuint);
void (*cog_mali_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
void (*cog_mali_glActiveTexture)(GLenum);
void (*cog_mali_glDrawArrays)(GLenum, GLint, GLsizei);

#define DLSYM_REQ(h, n, v)                                                     \
    do {                                                                      \
        dlerror();                                                            \
        *(void **) (&(v)) = dlsym((h), (n));                                  \
        const char *_e = dlerror();                                           \
        if (_e || !(v)) {                                                     \
            fprintf(stderr, "cog-mali-dispatch: dlsym(%s) failed: %s\n", (n), \
                    _e ? _e : "symbol is NULL");                              \
            return false;                                                     \
        }                                                                     \
    } while (0)

#define EGLPROC_REQ(n, v)                                                              \
    do {                                                                              \
        *(void **) (&(v)) = (void *) cog_mali_eglGetProcAddress((n));                 \
        if (!(v)) {                                                                   \
            fprintf(stderr, "cog-mali-dispatch: eglGetProcAddress(%s) failed\n", (n)); \
            return false;                                                             \
        }                                                                             \
    } while (0)

bool
cog_mali_dispatch_init(void)
{
    static bool done = false;
    static bool ok = false;
    if (done)
        return ok;
    done = true;

    const char *paths[] = {
        "/usr/lib/aarch64-linux-gnu/libMali.so",
        "/lib/aarch64-linux-gnu/libMali.so",
        "libMali.so",
        NULL,
    };
    void *mali = NULL;
    for (int i = 0; paths[i]; i++) {
        mali = dlopen(paths[i], RTLD_NOW | RTLD_GLOBAL);
        if (mali) {
            fprintf(stderr, "cog-mali-dispatch: loaded %s\n", paths[i]);
            break;
        }
    }
    if (!mali) {
        fprintf(stderr, "cog-mali-dispatch: cannot dlopen libMali.so: %s\n", dlerror());
        return false;
    }

    DLSYM_REQ(mali, "eglBindAPI", cog_mali_eglBindAPI);
    DLSYM_REQ(mali, "eglChooseConfig", cog_mali_eglChooseConfig);
    DLSYM_REQ(mali, "eglCreateContext", cog_mali_eglCreateContext);
    DLSYM_REQ(mali, "eglCreateWindowSurface", cog_mali_eglCreateWindowSurface);
    DLSYM_REQ(mali, "eglDestroyContext", cog_mali_eglDestroyContext);
    DLSYM_REQ(mali, "eglDestroySurface", cog_mali_eglDestroySurface);
    DLSYM_REQ(mali, "eglGetConfigAttrib", cog_mali_eglGetConfigAttrib);
    DLSYM_REQ(mali, "eglGetConfigs", cog_mali_eglGetConfigs);
    DLSYM_REQ(mali, "eglGetDisplay", cog_mali_eglGetDisplay);
    DLSYM_REQ(mali, "eglGetError", cog_mali_eglGetError);
    DLSYM_REQ(mali, "eglInitialize", cog_mali_eglInitialize);
    DLSYM_REQ(mali, "eglMakeCurrent", cog_mali_eglMakeCurrent);
    DLSYM_REQ(mali, "eglQueryString", cog_mali_eglQueryString);
    DLSYM_REQ(mali, "eglReleaseThread", cog_mali_eglReleaseThread);
    DLSYM_REQ(mali, "eglSwapBuffers", cog_mali_eglSwapBuffers);
    DLSYM_REQ(mali, "eglTerminate", cog_mali_eglTerminate);
    DLSYM_REQ(mali, "eglGetCurrentContext", cog_mali_eglGetCurrentContext);
    DLSYM_REQ(mali, "eglGetCurrentDisplay", cog_mali_eglGetCurrentDisplay);
    DLSYM_REQ(mali, "eglGetCurrentSurface", cog_mali_eglGetCurrentSurface);
    DLSYM_REQ(mali, "eglGetProcAddress", cog_mali_eglGetProcAddress);

    EGLPROC_REQ("eglGetPlatformDisplayEXT", cog_mali_eglGetPlatformDisplayEXT);
    EGLPROC_REQ("eglCreatePlatformWindowSurfaceEXT", cog_mali_eglCreatePlatformWindowSurfaceEXT);

    DLSYM_REQ(mali, "gbm_create_device", cog_mali_gbm_create_device);
    DLSYM_REQ(mali, "gbm_device_get_fd", cog_mali_gbm_device_get_fd);
    DLSYM_REQ(mali, "gbm_surface_create", cog_mali_gbm_surface_create);
    DLSYM_REQ(mali, "gbm_surface_lock_front_buffer", cog_mali_gbm_surface_lock_front_buffer);
    DLSYM_REQ(mali, "gbm_surface_release_buffer", cog_mali_gbm_surface_release_buffer);
    DLSYM_REQ(mali, "gbm_bo_get_stride", cog_mali_gbm_bo_get_stride);
    DLSYM_REQ(mali, "gbm_bo_get_stride_for_plane", cog_mali_gbm_bo_get_stride_for_plane);
    DLSYM_REQ(mali, "gbm_bo_get_offset", cog_mali_gbm_bo_get_offset);
    DLSYM_REQ(mali, "gbm_bo_get_plane_count", cog_mali_gbm_bo_get_plane_count);
    DLSYM_REQ(mali, "gbm_bo_get_handle", cog_mali_gbm_bo_get_handle);
    DLSYM_REQ(mali, "gbm_bo_get_handle_for_plane", cog_mali_gbm_bo_get_handle_for_plane);
    DLSYM_REQ(mali, "gbm_bo_get_modifier", cog_mali_gbm_bo_get_modifier);
    DLSYM_REQ(mali, "gbm_bo_get_user_data", cog_mali_gbm_bo_get_user_data);
    DLSYM_REQ(mali, "gbm_bo_set_user_data", cog_mali_gbm_bo_set_user_data);

    DLSYM_REQ(mali, "glGetString", cog_mali_glGetString);
    DLSYM_REQ(mali, "glGetError", cog_mali_glGetError);
    DLSYM_REQ(mali, "glClear", cog_mali_glClear);
    DLSYM_REQ(mali, "glClearColor", cog_mali_glClearColor);
    DLSYM_REQ(mali, "glViewport", cog_mali_glViewport);
    DLSYM_REQ(mali, "glCreateShader", cog_mali_glCreateShader);
    DLSYM_REQ(mali, "glShaderSource", cog_mali_glShaderSource);
    DLSYM_REQ(mali, "glCompileShader", cog_mali_glCompileShader);
    DLSYM_REQ(mali, "glGetShaderiv", cog_mali_glGetShaderiv);
    DLSYM_REQ(mali, "glGetShaderInfoLog", cog_mali_glGetShaderInfoLog);
    DLSYM_REQ(mali, "glDeleteShader", cog_mali_glDeleteShader);
    DLSYM_REQ(mali, "glCreateProgram", cog_mali_glCreateProgram);
    DLSYM_REQ(mali, "glAttachShader", cog_mali_glAttachShader);
    DLSYM_REQ(mali, "glBindAttribLocation", cog_mali_glBindAttribLocation);
    DLSYM_REQ(mali, "glLinkProgram", cog_mali_glLinkProgram);
    DLSYM_REQ(mali, "glGetProgramiv", cog_mali_glGetProgramiv);
    DLSYM_REQ(mali, "glGetProgramInfoLog", cog_mali_glGetProgramInfoLog);
    DLSYM_REQ(mali, "glDeleteProgram", cog_mali_glDeleteProgram);
    DLSYM_REQ(mali, "glGenTextures", cog_mali_glGenTextures);
    DLSYM_REQ(mali, "glBindTexture", cog_mali_glBindTexture);
    DLSYM_REQ(mali, "glTexParameteri", cog_mali_glTexParameteri);
    DLSYM_REQ(mali, "glDeleteTextures", cog_mali_glDeleteTextures);
    DLSYM_REQ(mali, "glGenBuffers", cog_mali_glGenBuffers);
    DLSYM_REQ(mali, "glBindBuffer", cog_mali_glBindBuffer);
    DLSYM_REQ(mali, "glBufferData", cog_mali_glBufferData);
    DLSYM_REQ(mali, "glDeleteBuffers", cog_mali_glDeleteBuffers);
    DLSYM_REQ(mali, "glGenVertexArrays", cog_mali_glGenVertexArrays);
    DLSYM_REQ(mali, "glBindVertexArray", cog_mali_glBindVertexArray);
    DLSYM_REQ(mali, "glDeleteVertexArrays", cog_mali_glDeleteVertexArrays);
    DLSYM_REQ(mali, "glGetAttribLocation", cog_mali_glGetAttribLocation);
    DLSYM_REQ(mali, "glUseProgram", cog_mali_glUseProgram);
    DLSYM_REQ(mali, "glEnableVertexAttribArray", cog_mali_glEnableVertexAttribArray);
    DLSYM_REQ(mali, "glDisableVertexAttribArray", cog_mali_glDisableVertexAttribArray);
    DLSYM_REQ(mali, "glVertexAttribPointer", cog_mali_glVertexAttribPointer);
    DLSYM_REQ(mali, "glActiveTexture", cog_mali_glActiveTexture);
    DLSYM_REQ(mali, "glDrawArrays", cog_mali_glDrawArrays);

    fprintf(stderr, "cog-mali-dispatch: all symbols resolved OK\n");
    ok = true;
    return true;
}
