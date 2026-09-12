/*
 * cog-gl-utils.c
 * Copyright (C) 2021-2022 Igalia S.L.
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE /* for dlinfo()/RTLD_DI_LINKMAP, a glibc extension */

#include "cog-gl-utils.h"

#include "../../core/cog.h"
#include <dlfcn.h>
#include <link.h>
#include <stdio.h>
#include <string.h>

/*
 * epoxy's dispatch for glGetString() itself resolves incorrectly on at
 * least one proprietary ARM Mali (Bifrost) driver setup: EGL client-type
 * queries (EGL_CONTEXT_CLIENT_TYPE / EGL_CONTEXT_CLIENT_VERSION) all
 * correctly report an OpenGL ES 2 context is current, yet epoxy's
 * generated "core function" resolver (epoxy_get_core_proc_address(),
 * used for old/core entry points like glGetString) unconditionally
 * dlsym()s them from the DESKTOP GL library on this platform, which has
 * no current context and so returns NULL/empty results. This is very
 * likely related to malformed .dynsym tables observed in this driver's
 * libEGL.so/libGLESv2.so (visible as linker warnings at build time:
 * ".dynsym local symbol at index N (>= sh_info of 3)").
 *
 * epoxy/gl.h -- included by cog-gl-utils.h -- macro-redefines
 * glGetString itself, so simply calling "glGetString" from this file
 * still goes through the same broken epoxy dispatch; it does NOT bypass
 * it. To get a genuinely direct answer we resolve the real symbol
 * ourselves from libGLESv2.so.2 via dlopen/dlsym, exactly mirroring how
 * a normal dynamically-linked GLES application would resolve it (which
 * is confirmed to work correctly against this same driver).
 */
static gboolean
gl_has_extension_direct(const char *name)
{
    /* FIRST THING, before touching any dlopen: check what EGL_VENDOR the
     * currently-current display already reports, using ONLY functions
     * epoxy already has resolved (no new dlopen calls yet), to rule out
     * whether our own dlopen() calls further below are themselves the
     * ones disturbing the current context. */
    fprintf(stderr, "[gl_has_extension_direct] ENTRY: eglGetCurrentDisplay()=%p eglGetCurrentContext()=%p "
                    "EGL_VENDOR(via epoxy)=%s\n",
            (void *)eglGetCurrentDisplay(), (void *)eglGetCurrentContext(),
            eglQueryString(eglGetCurrentDisplay(), EGL_VENDOR));

    static const GLubyte *(*real_glGetString)(GLenum) = NULL;
    static gboolean       resolved = FALSE;

    if (!resolved) {
        resolved = TRUE;
        dlerror(); /* clear any pending error */
        void *handle = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
        fprintf(stderr, "[gl_has_extension_direct] dlopen(\"libGLESv2.so.2\", RTLD_NOW) = %p, dlerror=%s\n",
                handle, dlerror());
        if (!handle) {
            /* RTLD_NOW forces eager symbol resolution; this driver's
             * .dynsym table is known to be malformed, which can make
             * eager resolution fail even though lazy binding (what the
             * dynamic linker used for our normal, already-working
             * dependency on this same library) tolerates it fine. Retry
             * with RTLD_LAZY before giving up. */
            dlerror();
            handle = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_GLOBAL);
            fprintf(stderr, "[gl_has_extension_direct] retry dlopen(RTLD_LAZY) = %p, dlerror=%s\n",
                    handle, dlerror());
        }
        if (handle) {
            dlerror();
            real_glGetString = dlsym(handle, "glGetString");
            fprintf(stderr, "[gl_has_extension_direct] dlsym(\"glGetString\") = %p, dlerror=%s\n",
                    (void *)real_glGetString, dlerror());

            struct link_map *lm = NULL;
            if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) == 0 && lm) {
                fprintf(stderr, "[gl_has_extension_direct] our dlopen'd libGLESv2.so.2 real path = %s\n", lm->l_name);
            } else {
                fprintf(stderr, "[gl_has_extension_direct] dlinfo(RTLD_DI_LINKMAP) failed: %s\n", dlerror());
            }
        }

        /* Enumerate ALL mapped modules matching GLESv2/EGL/mali in this
         * process, in case more than one copy of these libraries is
         * loaded simultaneously (e.g. one pulled in by WebKit itself
         * through a different path than the one our own dlopen finds). */
        FILE *maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[512];
            fprintf(stderr, "[gl_has_extension_direct] --- /proc/self/maps entries matching GLESv2/EGL/mali ---\n");
            while (fgets(line, sizeof(line), maps)) {
                if (strstr(line, "GLESv2") || strstr(line, "libEGL") || strstr(line, "mali"))
                    fprintf(stderr, "  %s", line);
            }
            fclose(maps);
            fprintf(stderr, "[gl_has_extension_direct] --- end maps ---\n");
        }
    }

    if (!real_glGetString) {
        fprintf(stderr, "[gl_has_extension_direct] real_glGetString is NULL, returning FALSE for \"%s\"\n", name);
        return FALSE;
    }

    const char *exts = (const char *)real_glGetString(GL_EXTENSIONS);
    fprintf(stderr, "[gl_has_extension_direct] real_glGetString(GL_EXTENSIONS) = %s\n",
            exts ? exts : "(NULL)");
    fprintf(stderr, "[gl_has_extension_direct] eglGetCurrentContext()=%p eglGetCurrentDisplay()=%p "
                    "eglGetCurrentSurface(DRAW)=%p glGetError()=0x%x\n",
            (void *)eglGetCurrentContext(), (void *)eglGetCurrentDisplay(),
            (void *)eglGetCurrentSurface(EGL_DRAW), real_glGetString ? 0u : 0u);
    fprintf(stderr, "[gl_has_extension_direct] real_glGetString(GL_VERSION) = %s\n",
            (const char *)real_glGetString(GL_VERSION));

    /* Resolve eglQueryString directly too (bypassing epoxy's EGL dispatch),
     * to see which vendor's EGL is ACTUALLY backing the currently-current
     * context/display at this exact point -- in case something else (e.g.
     * WebKit's own compositor setup) replaced Cog's Mali context with a
     * different, software-Mesa one on this same thread before this check
     * runs (note: libEGL_mesa.so.0.0.0 and libEGL.so.1.1.0 both appeared
     * in /proc/self/maps above, alongside the real Mali blob). */
    {
        void *egl_handle = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
        const char *(*real_eglQueryString)(void *, int) =
            egl_handle ? dlsym(egl_handle, "eglQueryString") : NULL;
        if (real_eglQueryString) {
            const char *vendor = real_eglQueryString(eglGetCurrentDisplay(), EGL_VENDOR);
            const char *version = real_eglQueryString(eglGetCurrentDisplay(), EGL_VERSION);
            fprintf(stderr, "[gl_has_extension_direct] REAL current EGL_VENDOR=%s EGL_VERSION=%s\n",
                    vendor ? vendor : "(NULL)", version ? version : "(NULL)");
        } else {
            fprintf(stderr, "[gl_has_extension_direct] could not resolve real eglQueryString\n");
        }
    }

    return exts && strstr(exts, name) != NULL;
}

void
cog_gl_shader_id_destroy(CogGLShaderId *shader_id)
{
    if (shader_id && *shader_id) {
        glDeleteShader(*shader_id);
        *shader_id = 0;
    }
}

CogGLShaderId
cog_gl_shader_id_steal(CogGLShaderId *shader_id)
{
    g_assert(shader_id);
    CogGLShaderId result = *shader_id;
    *shader_id = 0;
    return result;
}

CogGLShaderId
cog_gl_load_shader(const char *source, GLenum kind, GError **error)
{
    g_assert(source != NULL);
    g_assert(kind == GL_VERTEX_SHADER || kind == GL_FRAGMENT_SHADER);

    g_auto(CogGLShaderId) shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, NULL);

    GLenum err;
    if ((err = glGetError()) != GL_NO_ERROR) {
        g_set_error_literal(error, COG_PLATFORM_EGL_ERROR, err, "Cannot set shader source");
        return 0;
    }

    glCompileShader(shader);
    if ((err = glGetError()) != GL_NO_ERROR) {
        g_set_error_literal(error, COG_PLATFORM_EGL_ERROR, err, "Cannot compile shader");
        return 0;
    }

    GLint shaderCompiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &shaderCompiled);
    if (shaderCompiled == GL_TRUE)
        return cog_gl_shader_id_steal(&shader);

    GLint log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    g_autofree char *log = g_new0(char, log_length + 1);
    glGetShaderInfoLog(shader, log_length, NULL, log);
    g_set_error(error, COG_PLATFORM_EGL_ERROR, 0, "Shader compilation: %s", log);
    return 0;
}

bool
cog_gl_link_program(GLuint program, GError **error)
{
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status)
        return true;

    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    g_autofree char *log = g_new0(char, log_length + 1);
    glGetProgramInfoLog(program, log_length, NULL, log);
    g_set_error(error, COG_PLATFORM_EGL_ERROR, 0, "Shader linking: %s", log);
    return false;
}

bool
cog_gl_renderer_initialize(CogGLRenderer *self, GError **error)
{
    g_assert(self);
    g_assert(!self->program);
    g_assert(eglGetCurrentContext() != EGL_NO_CONTEXT);

    static const char *required_gl_extensions[] = {
        "GL_OES_EGL_image",
    };
    for (unsigned i = 0; i < G_N_ELEMENTS(required_gl_extensions); i++) {
        if (!gl_has_extension_direct(required_gl_extensions[i])) {
            g_set_error(error, COG_PLATFORM_WPE_ERROR, COG_PLATFORM_WPE_ERROR_INIT, "GL extension %s missing",
                        required_gl_extensions[i]);
            return false;
        }
    }

    static const char vertex_shader_source[] = "#version 100\n"
                                               "attribute vec2 position;\n"
                                               "attribute vec2 texture;\n"
                                               "varying vec2 v_texture;\n"
                                               "void main() {\n"
                                               "  v_texture = texture;\n"
                                               "  gl_Position = vec4(position, 0, 1);\n"
                                               "}\n";
    static const char fragment_shader_source[] = "#version 100\n"
                                                 "precision mediump float;\n"
                                                 "uniform sampler2D u_texture;\n"
                                                 "varying vec2 v_texture;\n"
                                                 "void main() {\n"
                                                 "  gl_FragColor = texture2D(u_texture, v_texture);\n"
                                                 "}\n";

    g_auto(CogGLShaderId) vertex_shader = cog_gl_load_shader(vertex_shader_source, GL_VERTEX_SHADER, error);
    if (!vertex_shader)
        return false;

    g_auto(CogGLShaderId) fragment_shader = cog_gl_load_shader(fragment_shader_source, GL_FRAGMENT_SHADER, error);
    if (!fragment_shader)
        return false;

    if (!(self->program = glCreateProgram())) {
        g_set_error_literal(error, COG_PLATFORM_EGL_ERROR, glGetError(), "Cannot create shader program");
        return false;
    }

    glAttachShader(self->program, vertex_shader);
    glAttachShader(self->program, fragment_shader);
    glBindAttribLocation(self->program, 0, "position");
    glBindAttribLocation(self->program, 1, "texture");

    if (!cog_gl_link_program(self->program, error)) {
        glDeleteProgram(self->program);
        self->program = 0;
        return false;
    }

    self->attrib_position = glGetAttribLocation(self->program, "position");
    self->attrib_texture = glGetAttribLocation(self->program, "texture");

    g_assert(self->attrib_position >= 0 && self->attrib_texture >= 0 && self->uniform_texture >= 0);

    /* Create texture. */
    glGenTextures(1, &self->texture);
    glBindTexture(GL_TEXTURE_2D, self->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Create vertex buffer */
    if (epoxy_is_desktop_gl() || epoxy_gl_version() >= 30) {
        glGenVertexArrays(1, &self->vao);
        glBindVertexArray(self->vao);
    } else {
        self->vao = 0;
    }

    /* clang-format off */
    static const GLfloat vertices[] = {
        /* position */
        -1.0f,  1.0f, 1.0f,  1.0f,
        -1.0f, -1.0f, 1.0f, -1.0f,
        /* texture */
        /* COG_GL_RENDERER_ROTATION_0 */
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 1.0f, 1.0f,
        /* COG_GL_RENDERER_ROTATION_90 */
        1.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        /* COG_GL_RENDERER_ROTATION_180 */
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        /* COG_GL_RENDERER_ROTATION_270 */
        0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 0.0f,
    };
    /* clang-format on */

    glGenBuffers(1, &self->buffer_vertex);
    glBindBuffer(GL_ARRAY_BUFFER, self->buffer_vertex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (self->vao > 0)
        glBindVertexArray(0);

    return true;
}

void
cog_gl_renderer_finalize(CogGLRenderer *self)
{
    g_assert(self);

    if (self->texture) {
        glDeleteTextures(1, &self->texture);
        self->texture = 0;
    }

    if (self->program) {
        glDeleteProgram(self->program);
        self->program = 0;
    }

    if (self->vao > 0) {
        glDeleteVertexArrays(1, &self->vao);
        self->vao = 0;
    }

    if (self->buffer_vertex) {
        glDeleteBuffers(1, &self->buffer_vertex);
        self->buffer_vertex = 0;
    }

    self->attrib_position = 0;
    self->attrib_texture = 0;
    self->uniform_texture = 0;
}

void
cog_gl_renderer_paint(CogGLRenderer *self, EGLImage *image, CogGLRendererRotation rotation)
{
    g_assert(self);
    g_assert(image != EGL_NO_IMAGE);
    g_assert(eglGetCurrentContext() != EGL_NO_CONTEXT);
    g_assert(rotation == COG_GL_RENDERER_ROTATION_0 || rotation == COG_GL_RENDERER_ROTATION_90 ||
             rotation == COG_GL_RENDERER_ROTATION_180 || rotation <= COG_GL_RENDERER_ROTATION_270);

    glUseProgram(self->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, self->texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
    glUniform1i(self->uniform_texture, 0);

    if (self->vao > 0)
        glBindVertexArray(self->vao);

    glBindBuffer(GL_ARRAY_BUFFER, self->buffer_vertex);

    glVertexAttribPointer(self->attrib_position, 2, GL_FLOAT, GL_FALSE, 0, (void *) 0);
    glVertexAttribPointer(self->attrib_texture, 2, GL_FLOAT, GL_FALSE, 0,
                          (void *) ((rotation + 1) * 2 * 4 * sizeof(GLfloat)));

    glEnableVertexAttribArray(self->attrib_position);
    glEnableVertexAttribArray(self->attrib_texture);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisableVertexAttribArray(self->attrib_position);
    glDisableVertexAttribArray(self->attrib_texture);

    if (self->vao > 0)
        glBindVertexArray(0);
}
