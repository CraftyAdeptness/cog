/*
 * cog-mali-dispatch.h
 *
 * Direct, dlopen()-based dispatch to the proprietary ARM Mali (Bifrost)
 * driver blob ("libMali.so"), bypassing the system's GLVND libEGL.so.1
 * dispatcher and libepoxy entirely.
 *
 * BACKGROUND: on this platform, libGLESv2.so.2 is a direct symlink to
 * the Mali blob, but libEGL.so.1 is GLVND's vendor-neutral dispatcher,
 * which (with only a Mesa vendor JSON registered) ends up routing
 * generic/core EGL calls to Mesa instead of Mali. Something elsewhere
 * in the WPEWebKit process independently triggers Mesa's own generic
 * EGL init, and whichever context that leaves "current" on the thread
 * does not match the Mali-backed display/context this platform creates
 * via the EGL_KHR_platform_gbm extension -- so ordinary EGL/GLES calls
 * made through the normal (GLVND-routed) symbols end up operating on
 * the wrong vendor's context, which is undefined behaviour and was
 * observed to return NULL from glGetString() even though the driver
 * and hardware are confirmed fully functional in isolation.
 *
 * The fix: resolve EVERY EGL/GBM/GLES entry point this platform uses
 * directly from the Mali blob's own handle via dlopen()/dlsym(), the
 * same way a standalone test program (test-mali-dmabuf-render2.c,
 * confirmed working end-to-end: GBM -> DMA-BUF -> EGLImage -> GLES
 * texture -> shader -> FBO -> readback) already proved reliable. This
 * header defines macros so existing call sites (eglInitialize(...),
 * glClear(...), etc.) need no changes -- only the resolution differs.
 *
 * Usage: include this header AFTER <EGL/egl.h>, <EGL/eglext.h>,
 * <GLES2/gl2.h> and <gbm.h> (needed for the real type declarations),
 * then call cog_mali_dispatch_init() once, before ANY other EGL/GBM
 * activity in the process, before using any of the wrapped functions.
 */

#pragma once

/*
 * NOTE: this header deliberately does NOT include <EGL/egl.h>,
 * <EGL/eglext.h>, <GLES2/gl2.h> or <gbm.h> itself. Files that use it
 * already include epoxy's versions of these (<epoxy/egl.h>,
 * <epoxy/gl.h>) or the raw ones, which provide the same type
 * definitions (EGLDisplay, EGLContext, GLuint, etc). Mixing epoxy's
 * headers with the raw Khronos ones in the same translation unit
 * causes redeclaration errors, so this header must come AFTER
 * whichever of the two the including file already uses, and must not
 * duplicate them.
 */
#include <stdbool.h>

/* Call once, as early as possible (before any other EGL/GBM call in
 * the process). Returns false and logs an error if the Mali blob or
 * any required symbol could not be resolved. */
bool cog_mali_dispatch_init(void);

/* ---- EGL core ---- */
extern EGLBoolean (*cog_mali_eglBindAPI)(EGLenum);
extern EGLBoolean (*cog_mali_eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
extern EGLContext (*cog_mali_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
extern EGLSurface (*cog_mali_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *);
extern EGLBoolean (*cog_mali_eglDestroyContext)(EGLDisplay, EGLContext);
extern EGLBoolean (*cog_mali_eglDestroySurface)(EGLDisplay, EGLSurface);
extern EGLBoolean (*cog_mali_eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint *);
extern EGLBoolean (*cog_mali_eglGetConfigs)(EGLDisplay, EGLConfig *, EGLint, EGLint *);
extern EGLDisplay (*cog_mali_eglGetDisplay)(EGLNativeDisplayType);
extern EGLint     (*cog_mali_eglGetError)(void);
extern EGLBoolean (*cog_mali_eglInitialize)(EGLDisplay, EGLint *, EGLint *);
extern EGLBoolean (*cog_mali_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
extern const char *(*cog_mali_eglQueryString)(EGLDisplay, EGLint);
extern EGLBoolean (*cog_mali_eglReleaseThread)(void);
extern EGLBoolean (*cog_mali_eglSwapBuffers)(EGLDisplay, EGLSurface);
extern EGLBoolean (*cog_mali_eglTerminate)(EGLDisplay);
extern EGLContext (*cog_mali_eglGetCurrentContext)(void);
extern EGLDisplay (*cog_mali_eglGetCurrentDisplay)(void);
extern EGLSurface (*cog_mali_eglGetCurrentSurface)(EGLint);
extern __eglMustCastToProperFunctionPointerType (*cog_mali_eglGetProcAddress)(const char *);

/* ---- EGL extensions (resolved via Mali's own eglGetProcAddress) ---- */
extern PFNEGLGETPLATFORMDISPLAYEXTPROC       cog_mali_eglGetPlatformDisplayEXT;
extern PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC cog_mali_eglCreatePlatformWindowSurfaceEXT;

/* ---- GBM (also resolved from the Mali blob, matching the proven
 * test program -- Mali's blob provides its own "armsoc" GBM backend) ---- */
extern struct gbm_device *(*cog_mali_gbm_create_device)(int);
extern int      (*cog_mali_gbm_device_get_fd)(struct gbm_device *);
extern struct gbm_surface *(*cog_mali_gbm_surface_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t);
extern struct gbm_bo *(*cog_mali_gbm_surface_lock_front_buffer)(struct gbm_surface *);
extern void     (*cog_mali_gbm_surface_release_buffer)(struct gbm_surface *, struct gbm_bo *);
extern uint32_t (*cog_mali_gbm_bo_get_stride)(struct gbm_bo *);
extern uint32_t (*cog_mali_gbm_bo_get_stride_for_plane)(struct gbm_bo *, int);
extern uint32_t (*cog_mali_gbm_bo_get_offset)(struct gbm_bo *, int);
extern int      (*cog_mali_gbm_bo_get_plane_count)(struct gbm_bo *);
extern union gbm_bo_handle (*cog_mali_gbm_bo_get_handle)(struct gbm_bo *);
extern union gbm_bo_handle (*cog_mali_gbm_bo_get_handle_for_plane)(struct gbm_bo *, int);
extern uint64_t (*cog_mali_gbm_bo_get_modifier)(struct gbm_bo *);
extern void    *(*cog_mali_gbm_bo_get_user_data)(struct gbm_bo *);
extern void     (*cog_mali_gbm_bo_set_user_data)(struct gbm_bo *, void *, void (*)(struct gbm_bo *, void *));

/* ---- GLES2 ---- */
extern const GLubyte *(*cog_mali_glGetString)(GLenum);
extern GLenum (*cog_mali_glGetError)(void);
extern void (*cog_mali_glClear)(GLbitfield);
extern void (*cog_mali_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
extern void (*cog_mali_glViewport)(GLint, GLint, GLsizei, GLsizei);
extern GLuint (*cog_mali_glCreateShader)(GLenum);
extern void (*cog_mali_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
extern void (*cog_mali_glCompileShader)(GLuint);
extern void (*cog_mali_glGetShaderiv)(GLuint, GLenum, GLint *);
extern void (*cog_mali_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
extern void (*cog_mali_glDeleteShader)(GLuint);
extern GLuint (*cog_mali_glCreateProgram)(void);
extern void (*cog_mali_glAttachShader)(GLuint, GLuint);
extern void (*cog_mali_glBindAttribLocation)(GLuint, GLuint, const GLchar *);
extern void (*cog_mali_glLinkProgram)(GLuint);
extern void (*cog_mali_glGetProgramiv)(GLuint, GLenum, GLint *);
extern void (*cog_mali_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
extern void (*cog_mali_glDeleteProgram)(GLuint);
extern void (*cog_mali_glGenTextures)(GLsizei, GLuint *);
extern void (*cog_mali_glBindTexture)(GLenum, GLuint);
extern void (*cog_mali_glTexParameteri)(GLenum, GLenum, GLint);
extern void (*cog_mali_glDeleteTextures)(GLsizei, const GLuint *);
extern void (*cog_mali_glGenBuffers)(GLsizei, GLuint *);
extern void (*cog_mali_glBindBuffer)(GLenum, GLuint);
extern void (*cog_mali_glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
extern void (*cog_mali_glDeleteBuffers)(GLsizei, const GLuint *);
extern void (*cog_mali_glGenVertexArrays)(GLsizei, GLuint *);
extern void (*cog_mali_glBindVertexArray)(GLuint);
extern void (*cog_mali_glDeleteVertexArrays)(GLsizei, const GLuint *);
extern GLint (*cog_mali_glGetAttribLocation)(GLuint, const GLchar *);
extern void (*cog_mali_glUseProgram)(GLuint);
extern void (*cog_mali_glEnableVertexAttribArray)(GLuint);
extern void (*cog_mali_glDisableVertexAttribArray)(GLuint);
extern void (*cog_mali_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
extern void (*cog_mali_glActiveTexture)(GLenum);
extern void (*cog_mali_glDrawArrays)(GLenum, GLint, GLsizei);

/* ---- Macro redefinitions: every call site below this point in files
 * that include this header transparently uses the Mali-resolved
 * pointers instead of the normal (GLVND/epoxy-routed) symbols. ---- */
#undef eglBindAPI
#define eglBindAPI                      cog_mali_eglBindAPI
#undef eglChooseConfig
#define eglChooseConfig                 cog_mali_eglChooseConfig
#undef eglCreateContext
#define eglCreateContext                cog_mali_eglCreateContext
#undef eglCreateWindowSurface
#define eglCreateWindowSurface          cog_mali_eglCreateWindowSurface
#undef eglDestroyContext
#define eglDestroyContext               cog_mali_eglDestroyContext
#undef eglDestroySurface
#define eglDestroySurface               cog_mali_eglDestroySurface
#undef eglGetConfigAttrib
#define eglGetConfigAttrib              cog_mali_eglGetConfigAttrib
#undef eglGetConfigs
#define eglGetConfigs                   cog_mali_eglGetConfigs
#undef eglGetDisplay
#define eglGetDisplay                   cog_mali_eglGetDisplay
#undef eglGetError
#define eglGetError                     cog_mali_eglGetError
#undef eglInitialize
#define eglInitialize                   cog_mali_eglInitialize
#undef eglMakeCurrent
#define eglMakeCurrent                  cog_mali_eglMakeCurrent
#undef eglQueryString
#define eglQueryString                  cog_mali_eglQueryString
#undef eglReleaseThread
#define eglReleaseThread                cog_mali_eglReleaseThread
#undef eglSwapBuffers
#define eglSwapBuffers                  cog_mali_eglSwapBuffers
#undef eglTerminate
#define eglTerminate                    cog_mali_eglTerminate
#undef eglGetCurrentContext
#define eglGetCurrentContext            cog_mali_eglGetCurrentContext
#undef eglGetCurrentDisplay
#define eglGetCurrentDisplay            cog_mali_eglGetCurrentDisplay
#undef eglGetCurrentSurface
#define eglGetCurrentSurface            cog_mali_eglGetCurrentSurface
#undef eglGetProcAddress
#define eglGetProcAddress               cog_mali_eglGetProcAddress
#undef eglGetPlatformDisplayEXT
#define eglGetPlatformDisplayEXT        cog_mali_eglGetPlatformDisplayEXT
#undef eglCreatePlatformWindowSurfaceEXT
#define eglCreatePlatformWindowSurfaceEXT cog_mali_eglCreatePlatformWindowSurfaceEXT

#undef gbm_create_device
#define gbm_create_device               cog_mali_gbm_create_device
#undef gbm_device_get_fd
#define gbm_device_get_fd               cog_mali_gbm_device_get_fd
#undef gbm_surface_create
#define gbm_surface_create              cog_mali_gbm_surface_create
#undef gbm_surface_lock_front_buffer
#define gbm_surface_lock_front_buffer   cog_mali_gbm_surface_lock_front_buffer
#undef gbm_surface_release_buffer
#define gbm_surface_release_buffer      cog_mali_gbm_surface_release_buffer
#undef gbm_bo_get_stride
#define gbm_bo_get_stride               cog_mali_gbm_bo_get_stride
#undef gbm_bo_get_stride_for_plane
#define gbm_bo_get_stride_for_plane     cog_mali_gbm_bo_get_stride_for_plane
#undef gbm_bo_get_offset
#define gbm_bo_get_offset               cog_mali_gbm_bo_get_offset
#undef gbm_bo_get_plane_count
#define gbm_bo_get_plane_count          cog_mali_gbm_bo_get_plane_count
#undef gbm_bo_get_handle
#define gbm_bo_get_handle               cog_mali_gbm_bo_get_handle
#undef gbm_bo_get_handle_for_plane
#define gbm_bo_get_handle_for_plane     cog_mali_gbm_bo_get_handle_for_plane
#undef gbm_bo_get_modifier
#define gbm_bo_get_modifier             cog_mali_gbm_bo_get_modifier
#undef gbm_bo_get_user_data
#define gbm_bo_get_user_data            cog_mali_gbm_bo_get_user_data
#undef gbm_bo_set_user_data
#define gbm_bo_set_user_data            cog_mali_gbm_bo_set_user_data

#undef glGetString
#define glGetString                     cog_mali_glGetString
#undef glGetError
#define glGetError                      cog_mali_glGetError
#undef glClear
#define glClear                         cog_mali_glClear
#undef glClearColor
#define glClearColor                    cog_mali_glClearColor
#undef glViewport
#define glViewport                      cog_mali_glViewport
#undef glCreateShader
#define glCreateShader                  cog_mali_glCreateShader
#undef glShaderSource
#define glShaderSource                  cog_mali_glShaderSource
#undef glCompileShader
#define glCompileShader                 cog_mali_glCompileShader
#undef glGetShaderiv
#define glGetShaderiv                   cog_mali_glGetShaderiv
#undef glGetShaderInfoLog
#define glGetShaderInfoLog              cog_mali_glGetShaderInfoLog
#undef glDeleteShader
#define glDeleteShader                  cog_mali_glDeleteShader
#undef glCreateProgram
#define glCreateProgram                 cog_mali_glCreateProgram
#undef glAttachShader
#define glAttachShader                  cog_mali_glAttachShader
#undef glBindAttribLocation
#define glBindAttribLocation            cog_mali_glBindAttribLocation
#undef glLinkProgram
#define glLinkProgram                   cog_mali_glLinkProgram
#undef glGetProgramiv
#define glGetProgramiv                  cog_mali_glGetProgramiv
#undef glGetProgramInfoLog
#define glGetProgramInfoLog             cog_mali_glGetProgramInfoLog
#undef glDeleteProgram
#define glDeleteProgram                 cog_mali_glDeleteProgram
#undef glGenTextures
#define glGenTextures                   cog_mali_glGenTextures
#undef glBindTexture
#define glBindTexture                   cog_mali_glBindTexture
#undef glTexParameteri
#define glTexParameteri                 cog_mali_glTexParameteri
#undef glDeleteTextures
#define glDeleteTextures                cog_mali_glDeleteTextures
#undef glGenBuffers
#define glGenBuffers                    cog_mali_glGenBuffers
#undef glBindBuffer
#define glBindBuffer                    cog_mali_glBindBuffer
#undef glBufferData
#define glBufferData                    cog_mali_glBufferData
#undef glDeleteBuffers
#define glDeleteBuffers                 cog_mali_glDeleteBuffers
#undef glGenVertexArrays
#define glGenVertexArrays               cog_mali_glGenVertexArrays
#undef glBindVertexArray
#define glBindVertexArray               cog_mali_glBindVertexArray
#undef glDeleteVertexArrays
#define glDeleteVertexArrays            cog_mali_glDeleteVertexArrays
#undef glGetAttribLocation
#define glGetAttribLocation             cog_mali_glGetAttribLocation
#undef glUseProgram
#define glUseProgram                    cog_mali_glUseProgram
#undef glEnableVertexAttribArray
#define glEnableVertexAttribArray       cog_mali_glEnableVertexAttribArray
#undef glDisableVertexAttribArray
#define glDisableVertexAttribArray      cog_mali_glDisableVertexAttribArray
#undef glVertexAttribPointer
#define glVertexAttribPointer           cog_mali_glVertexAttribPointer
#undef glActiveTexture
#define glActiveTexture                 cog_mali_glActiveTexture
#undef glDrawArrays
#define glDrawArrays                    cog_mali_glDrawArrays
