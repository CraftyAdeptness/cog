/*
 * check_glvnd_vendor.c
 *
 * Prueba aislada, SIN Cog ni WebKit: enlaza contra libEGL.so.1 y
 * libGLESv2.so.2 (GLVND), no directo contra Mesa ni contra libMali.so.
 * El objetivo es responder una sola pregunta: cuando el sistema hace
 * una llamada EGL genérica (eglGetDisplay(EGL_DEFAULT_DISPLAY)), ¿a
 * qué vendor la enruta GLVND una vez que 10_mali.json está instalado?
 *
 * EGL_VENDOR describe la IMPLEMENTACIÓN EGL que respondió, no
 * necesariamente el hardware que termina renderizando -- por eso
 * imprimimos también GL_VENDOR/GL_RENDERER tras crear un contexto
 * real, que es la comprobación que de verdad importa (ver nota en
 * learnings-and-tooling: "EGL_VENDOR refleja la implementación EGL,
 * GL_VENDOR/GL_RENDERER es lo que hay que chequear para el backend
 * de hardware real").
 *
 * Compilar:
 *   gcc -o check_glvnd_vendor check_glvnd_vendor.c -lEGL -lGLESv2
 *
 * Uso:
 *   ./check_glvnd_vendor
 *
 * Salida esperada SI el wrapper funciona:
 *   EGL_VENDOR      : ARM   (o similar, no "Mesa Project")
 *   GL_VENDOR       : ARM
 *   GL_RENDERER     : Mali-G31
 */

#include <stdio.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

static void die(const char *step)
{
    fprintf(stderr, "FALLÓ en: %s (eglGetError=0x%x)\n", step, eglGetError());
    exit(1);
}

int main(void)
{
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY)
        die("eglGetDisplay");

    EGLint major, minor;
    if (!eglInitialize(dpy, &major, &minor))
        die("eglInitialize");

    printf("EGL version    : %d.%d\n", major, minor);
    printf("EGL_VENDOR     : %s\n", eglQueryString(dpy, EGL_VENDOR));
    printf("EGL_VERSION    : %s\n", eglQueryString(dpy, EGL_VERSION));

    if (!eglBindAPI(EGL_OPENGL_ES_API))
        die("eglBindAPI");

    /* Config mínima para un pbuffer GLES2 -- no necesitamos ventana,
     * DRM/GBM ni pantalla para esta prueba. */
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs = 0;
    if (!eglChooseConfig(dpy, config_attribs, &config, 1, &num_configs) || num_configs < 1)
        die("eglChooseConfig");

    const EGLint pbuffer_attribs[] = {
        EGL_WIDTH, 4,
        EGL_HEIGHT, 4,
        EGL_NONE
    };
    EGLSurface surface = eglCreatePbufferSurface(dpy, config, pbuffer_attribs);
    if (surface == EGL_NO_SURFACE)
        die("eglCreatePbufferSurface");

    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, context_attribs);
    if (ctx == EGL_NO_CONTEXT)
        die("eglCreateContext");

    if (!eglMakeCurrent(dpy, surface, surface, ctx))
        die("eglMakeCurrent");

    /* Estas tres son las que de verdad importan: confirman qué
     * driver GLES está detrás del contexto ya current. */
    printf("GL_VENDOR      : %s\n", glGetString(GL_VENDOR));
    printf("GL_RENDERER    : %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION     : %s\n", glGetString(GL_VERSION));

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surface);
    eglTerminate(dpy);

    return 0;
}
