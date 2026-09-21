/*
 * Moondust, a free game engine for platform game making
 * Copyright (c) 2014-2026 Vitaly Novichkov <admin@wohlnet.ru>
 *
 * This software is licensed under a dual license system (MIT or GPL version 3 or later).
 * This means you are free to choose with which of both licenses (MIT or GPL version 3 or later)
 * you want to use this software.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You can see text of MIT license in the LICENSE.mit file you can see in Engine folder,
 * or see https://mit-license.org/.
 *
 * You can see text of GPLv3 license in the LICENSE.gpl3 file you can see in Engine folder,
 * or see <http://www.gnu.org/licenses/>.
 */

#include <locale.h>
#include <SDL2/SDL.h>
#include <Logger/logger.h>

#include "sdl_core.h"


bool CoreSDL::init(const CmdLineSetup_t &setup)
{
    (void)(setup);

    bool res;

    // if(g_config.background_work)
    // apply this unconditionally -- otherwise, need to restart game for background-work to function as expected
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    
    // Disable the audio capture at all (some systems do ask microphone permission because of that)
    SDL_SetHint("SDL_AUDIO_DISABLE_CAPTURE", "1");

#if defined(__ANDROID__) || defined(THEXTECH_IOS)
    // Restrict the landscape orientation only
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif

#if defined(__ANDROID__)
    SDL_setenv("SDL_AUDIODRIVER", "openslES", 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
#endif

#if defined(__ANDROID__) || defined(THEXTECH_IOS) || defined(THEXTECH_TVOS)
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif

#if defined(THEXTECH_IOS)
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "1");
#endif

    Uint32 sdlInitFlags = 0;
    // Prepare flags for SDL initialization
#if !defined(__EMSCRIPTEN__) && !defined(SDL_TIMERS_DISABLED)
    sdlInitFlags |= SDL_INIT_TIMER;
#endif
#if !defined(SDL_AUDIO_DISABLED)
    sdlInitFlags |= SDL_INIT_AUDIO;
#endif
#if !defined(__WII__) && !defined(__3DS__)
    sdlInitFlags |= SDL_INIT_VIDEO;
    sdlInitFlags |= SDL_INIT_EVENTS;
#endif
#if !defined(__WII__) && !defined(__3DS__) && !defined(SDL_JOYSTICK_DISABLED)
    sdlInitFlags |= SDL_INIT_JOYSTICK;
    sdlInitFlags |= SDL_INIT_GAMECONTROLLER;
#endif
#if !defined(__WII__) && !defined(__3DS__) && !defined(SDL_HAPTIC_DISABLED)
    sdlInitFlags |= SDL_INIT_HAPTIC;
#endif

    // Initialize SDL
    res = (SDL_Init(sdlInitFlags) >= 0);

    // Workaround: https://discourse.libsdl.org/t/26995
    setlocale(LC_NUMERIC, "C");

    const char *error = SDL_GetError();
    if(*error != '\0')
        pLogWarning("Error while SDL Initialization: %s", error);
    SDL_ClearError();

    SDL_version compiled, runtime;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&runtime);
    pLogDebug("Compiled with SDL %d.%d.%d headers, running with SDL %d.%d.%d",
        compiled.major, compiled.minor, compiled.patch,
        runtime.major, runtime.minor, runtime.patch);

    return res;
}

void CoreSDL::quit()
{
    SDL_Quit();
}

/*
 * HarmonyOS NAPI bridge entry. The HAP shell (libentry.so) statically links
 * its own copy of SDL2, so SDL_PushEvent called from there feeds a different
 * event queue than the one this engine polls. The shell calls this exported
 * wrapper instead, which runs inside libthextech.so's SDL instance and thus
 * reaches EventsSDL::doEvents() / SDL_GetKeyboardState().
 */
extern "C" __attribute__((visibility("default")))
int thetech_push_sdl_event(SDL_Event *ev)
{
    return SDL_PushEvent(ev);
}

/*
 * TheXTech reads input by polling SDL_GetKeyboardState() every frame
 * (src/control/keyboard.cpp). SDL2 only updates that state array inside
 * SDL_SendKeyboardKey() on the hardware event path -- events queued via
 * SDL_PushEvent() never touch it. So injected keys must poke the array
 * directly (same SDL instance here), in addition to queueing the event
 * for any event-driven consumer.
 */
extern "C" __attribute__((visibility("default")))
void thetech_set_key_state(int scancode, int down)
{
    int numkeys = 0;
    const Uint8 *state = SDL_GetKeyboardState(&numkeys);
    if (state && scancode >= 0 && scancode < numkeys)
        const_cast<Uint8 *>(state)[scancode] = down ? 1 : 0;

    SDL_Event ev;
    SDL_zero(ev);
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.keysym.scancode = (SDL_Scancode)scancode;
    ev.key.keysym.sym = SDL_SCANCODE_TO_KEYCODE(scancode);
    ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.repeat = 0;
    SDL_PushEvent(&ev);
}

/*
 * HarmonyOS native input bridges (input methods the engine natively supports
 * instead of keyboard emulation):
 *
 * Touch bridging lives in the HAP shell (thextech_main.cpp): SDL's internal
 * touch symbols are hidden, so calls here would statically bind to this
 * library's SDL copy -- not the instance the engine reads at runtime.
 *
 * Gamepad: TheXTech's Joystick input method only opens devices SDL reports
 * via SDL_JOYDEVICEADDED. The SDL virtual joystick driver (built with
 * SDL_JOYSTICK_VIRTUAL) provides one; SDL_JoystickAttachVirtual posts the
 * added event automatically, and SetVirtual* feeds the state that
 * SDL_JoystickGetButton/Axis/Hat (polled by the engine) return.
 */
static SDL_Joystick *s_virtualJoy = NULL;
static int s_virtualJoyIndex = -1;

extern "C" __attribute__((visibility("default")))
int thetech_attach_virtual_gamepad(void)
{
    if (s_virtualJoy)
        return 0;
    /* AttachVirtual returns the new device *index* (not instance id); the
     * engine opens the device itself through the SDL_JOYDEVICEADDED event
     * this attach posts. Only attach once, then retry just the open. */
    if (s_virtualJoyIndex < 0) {
        s_virtualJoyIndex = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                                       4, 16, 1);
        if (s_virtualJoyIndex < 0)
            return -1;
    }
    s_virtualJoy = SDL_JoystickOpen(s_virtualJoyIndex);
    return s_virtualJoy ? 0 : -1;
}

extern "C" __attribute__((visibility("default")))
void thetech_virtual_gamepad_button(int button, int down)
{
    if (!s_virtualJoy)
        return;
    SDL_JoystickSetVirtualButton(s_virtualJoy, button, down ? 1 : 0);
}

extern "C" __attribute__((visibility("default")))
void thetech_virtual_gamepad_hat(int mask)
{
    if (!s_virtualJoy)
        return;
    SDL_JoystickSetVirtualHat(s_virtualJoy, 0, (Uint8)(mask & 0xF));
    /* The default joystick profile binds the dpad to both hat 0 (primary)
     * and left-stick axes 0/1 (secondary); mirror the dpad onto the axes. */
    int x = 0, y = 0;
    if (mask & 0x1) y -= 32767; /* up    */
    if (mask & 0x4) y += 32767; /* down  */
    if (mask & 0x8) x -= 32767; /* left  */
    if (mask & 0x2) x += 32767; /* right */
    SDL_JoystickSetVirtualAxis(s_virtualJoy, 0, (Sint16)x);
    SDL_JoystickSetVirtualAxis(s_virtualJoy, 1, (Sint16)y);
}

extern "C" __attribute__((visibility("default")))
void thetech_virtual_gamepad_axis(int axis, int value)
{
    if (!s_virtualJoy)
        return;
    SDL_JoystickSetVirtualAxis(s_virtualJoy, axis, (Sint16)value);
}
