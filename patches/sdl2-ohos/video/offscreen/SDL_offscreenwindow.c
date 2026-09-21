/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_OFFSCREEN

#include "../SDL_sysvideo.h"
#include "../SDL_egl_c.h"

#include "SDL_offscreenwindow.h"

#ifdef __OHOS__
#include <native_window/external_window.h>
#endif

/* OHOS: injected native window (OH_NativeWindow*). When set, OPENGL windows
 * use a real EGL window surface instead of an offscreen PBuffer. */
static void *g_native_window = NULL;
static int g_native_width = 0;
static int g_native_height = 0;

void SDL_OHOS_SetNativeWindow(void *native_window)
{
    g_native_window = native_window;
}

void SDL_OHOS_SetNativeWindowWithSize(void *native_window, int w, int h)
{
    g_native_window = native_window;
    g_native_width = w;
    g_native_height = h;
}

void *SDL_OHOS_GetNativeWindow(void)
{
    return g_native_window;
}

int SDL_OHOS_GetNativeWidth(void)
{
    return g_native_width;
}

int SDL_OHOS_GetNativeHeight(void)
{
    return g_native_height;
}

int OFFSCREEN_CreateWindow(_THIS, SDL_Window *window)
{
    OFFSCREEN_Window *offscreen_window = SDL_calloc(1, sizeof(OFFSCREEN_Window));

    if (!offscreen_window) {
        return SDL_OutOfMemory();
    }

    window->driverdata = offscreen_window;

    if (window->x == SDL_WINDOWPOS_UNDEFINED) {
        window->x = 0;
    }

    if (window->y == SDL_WINDOWPOS_UNDEFINED) {
        window->y = 0;
    }

    offscreen_window->sdl_window = window;

#ifdef SDL_VIDEO_OPENGL_EGL
    if (window->flags & SDL_WINDOW_OPENGL) {

        if (!_this->egl_data) {
            return SDL_SetError("Cannot create an OPENGL window invalid egl_data");
        }

        if (g_native_window) {
            /* OHOS: sync the native window's buffer format with the EGL config
             * before creating the EGL window surface, otherwise eglCreateWindowSurface
             * fails with EGL_BAD_MATCH. This is the HarmonyOS equivalent of
             * Android's ANativeWindow_setBuffersGeometry(). */
            if (SDL_EGL_ChooseConfig(_this) == 0) {
                EGLint visual_id = 0;
                _this->egl_data->eglGetConfigAttrib(_this->egl_data->egl_display,
                                                     _this->egl_data->egl_config,
                                                     EGL_NATIVE_VISUAL_ID, &visual_id);
                if (visual_id != 0) {
                    OH_NativeWindow_NativeWindowHandleOpt((OHNativeWindow*)g_native_window,
                                                           SET_FORMAT, visual_id);
                }
            }

            offscreen_window->egl_surface = (EGLSurface)SDL_EGL_CreateSurface(_this, (NativeWindowType)g_native_window);

            /* OHOS: report the real window size so the app sets its viewport
             * to the full surface instead of the requested (default) size. */
            if (g_native_width > 0 && g_native_height > 0) {
                window->w = g_native_width;
                window->h = g_native_height;
            }
        } else {
            offscreen_window->egl_surface = SDL_EGL_CreateOffscreenSurface(_this, window->w, window->h);
        }

        if (offscreen_window->egl_surface == EGL_NO_SURFACE) {
            return SDL_SetError("Failed to created an offscreen surface (EGL display: %p)",
                                _this->egl_data->egl_display);
        }
    } else {
        offscreen_window->egl_surface = EGL_NO_SURFACE;
    }
#endif /* SDL_VIDEO_OPENGL_EGL */

    return 0;
}

void OFFSCREEN_DestroyWindow(_THIS, SDL_Window *window)
{
    OFFSCREEN_Window *offscreen_window = window->driverdata;

    if (offscreen_window) {
#ifdef SDL_VIDEO_OPENGL_EGL
        SDL_EGL_DestroySurface(_this, offscreen_window->egl_surface);
#endif
        SDL_free(offscreen_window);
    }

    window->driverdata = NULL;
}

#endif /* SDL_VIDEO_DRIVER_OFFSCREEN */

/* vi: set ts=4 sw=4 expandtab: */
