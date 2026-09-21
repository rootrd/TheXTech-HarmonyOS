#include <native_window/external_window.h>
#include <hilog/log.h>
#include "napi/native_api.h"
#include <SDL.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#undef LOG_TAG
#undef LOG_DOMAIN
#define LOG_TAG "TheXTechHOS"
#define LOG_DOMAIN 0x3200
#define LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, LOG_DOMAIN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_DOMAIN, LOG_TAG, __VA_ARGS__)
#define LOGW(...) OH_LOG_Print(LOG_APP, LOG_WARN, LOG_DOMAIN, LOG_TAG, __VA_ARGS__)

// SDL offscreen-driver injection (implemented in our patched libSDL2.a).
extern "C" {
    void SDL_OHOS_SetNativeWindowWithSize(void *native_window, int w, int h);
}

// TheXTech entry point signature (linked at compile time).
extern "C" int thetech_main(int argc, char**argv);

// Event injection into the engine's SDL2 instance. libentry.so statically
// links its own SDL2 copy whose event queue the engine never polls; pushing
// there is a no-op for the game. These wrappers live inside libthextech.so
// and feed the queue EventsSDL::doEvents() actually reads.
extern "C" int thetech_push_sdl_event(SDL_Event* ev);
// Sets a key in the engine's SDL_GetKeyboardState() array (the array is only
// updated by SDL's hardware event path, not by queued events) and queues the
// KEYDOWN/KEYUP event. This is what actually drives TheXTech's input polling.
extern "C" void thetech_set_key_state(int scancode, int down);
// Native gamepad bridge (virtual joystick inside libthextech's SDL; the
// public API calls route to the instance the engine polls).
extern "C" int thetech_attach_virtual_gamepad(void);
extern "C" void thetech_virtual_gamepad_button(int button, int down);
extern "C" void thetech_virtual_gamepad_hat(int mask);
extern "C" void thetech_virtual_gamepad_axis(int axis, int value);

// SDL internal touch API. These are hidden symbols resolved statically
// against THIS module's libSDL2.a copy -- which is the SDL instance the
// engine actually uses at runtime (public SDL symbols interpose to whichever
// library loads first, and libentry's copy wins), so touch devices must be
// registered HERE, not inside libthextech.so.
extern "C" {
int SDL_AddTouch(SDL_TouchID id, SDL_TouchDeviceType type, const char *name);
int SDL_SendTouch(SDL_TouchID id, SDL_FingerID fingerid, SDL_Window *window,
                  SDL_bool down, float x, float y, float pressure);
int SDL_SendTouchMotion(SDL_TouchID id, SDL_FingerID fingerid, SDL_Window *window,
                        float x, float y, float pressure);
}
static SDL_TouchID g_ohosTouchId = 0x4F484F53; /* "OHOS" */

static void RegisterVirtualTouchscreen()
{
    if (SDL_GetNumTouchDevices() == 0) {
        int rc = SDL_AddTouch(g_ohosTouchId, SDL_TOUCH_DEVICE_DIRECT, "OHOS touchscreen");
        LOGI("RegisterVirtualTouchscreen rc=%{public}d devices=%{public}d",
             rc, SDL_GetNumTouchDevices());
    }
}

static int PushEventToEngine(SDL_Event* ev)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOGI("SDL_PushEvent addr: local(libentry)=%{public}p engine(libthextech)=%{public}p",
             (void*)SDL_PushEvent, (void*)thetech_push_sdl_event);
    }
    return thetech_push_sdl_event(ev);
}

struct TheXTechState {
    OHNativeWindow* window = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> quitRequested{false};
    int width = 0;
    int height = 0;
    std::string gamePath;
    std::string saveDir;
    std::string gameRoot;
    std::string gameHome;
    void* thetechHandle = nullptr;

    std::atomic<int> fps{0};
};

static TheXTechState g_state;
static std::mutex g_mutex;

// Redirect stdio (stdout/stderr) to hilog so printf/cout output is visible.
static void RedirectStdioToHilog()
{
    // Create a pipe; stdout/stderr write end, read end monitored in a thread.
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        LOGE("pipe creation failed");
        return;
    }

    // Make read end non-blocking is not needed; we read in a thread.
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    std::thread([pipefd]() {
        char buf[1024];
        std::string line;
        while (true) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
            if (n <= 0) break;
            buf[n] = '\0';
            line += buf;
            size_t pos;
            while ((pos = line.find('\n')) != std::string::npos) {
                std::string single = line.substr(0, pos);
                line.erase(0, pos + 1);
                if (!single.empty()) {
                    LOGI("thextech: %{public}s", single.c_str());
                }
            }
        }
        close(pipefd[0]);
    }).detach();

    LOGI("stdio redirected to hilog");
}

static void SDLLogToHilog(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    (void)userdata;
    (void)category;
    switch (priority) {
        case SDL_LOG_PRIORITY_ERROR:
        case SDL_LOG_PRIORITY_CRITICAL:
            LOGE("SDL: %{public}s", message);
            break;
        case SDL_LOG_PRIORITY_WARN:
            LOGW("SDL: %{public}s", message);
            break;
        case SDL_LOG_PRIORITY_INFO:
        case SDL_LOG_PRIORITY_DEBUG:
        case SDL_LOG_PRIORITY_VERBOSE:
        default:
            LOGI("SDL: %{public}s", message);
            break;
    }
}

static uint64_t GetSurfaceId(napi_env env, napi_value arg)
{
    uint64_t id = 0;
    napi_valuetype type = napi_undefined;
    napi_typeof(env, arg, &type);
    if (type == napi_string) {
        size_t len = 0;
        napi_get_value_string_utf8(env, arg, nullptr, 0, &len);
        std::string buf(len, '\0');
        if (len > 0) {
            napi_get_value_string_utf8(env, arg, &buf[0], len + 1, &len);
        }
        id = strtoull(buf.c_str(), nullptr, 10);
    } else if (type == napi_number) {
        double v = 0.0;
        napi_get_value_double(env, arg, &v);
        id = (uint64_t)v;
    }
    return id;
}

static int32_t GetIntArg(napi_env env, napi_value arg, int32_t def)
{
    int32_t v = def;
    napi_get_value_int32(env, arg, &v);
    return v;
}

static std::string GetStringArg(napi_env env, napi_value arg)
{
    std::string out;
    napi_valuetype type = napi_undefined;
    napi_typeof(env, arg, &type);
    if (type == napi_string) {
        size_t len = 0;
        napi_get_value_string_utf8(env, arg, nullptr, 0, &len);
        if (len > 0) {
            out.resize(len);
            napi_get_value_string_utf8(env, arg, &out[0], len + 1, &len);
        }
    }
    return out;
}

static void TheXTechThreadMain(OHNativeWindow* win, int w, int h,
                                std::string gamePath, std::string saveDir,
                                std::string gameRoot, std::string gameHome)
{
    LOGI("thextech thread start %{public}dx%{public}d", w, h);

    // Set HOME to the app's writable directory so save data lands correctly.
    if (!saveDir.empty()) {
        SDL_setenv("HOME", saveDir.c_str(), 1);
    }
    if (!gameHome.empty()) {
        SDL_setenv("HOME", gameHome.c_str(), 1);
    }

    // Force the offscreen SDL video driver (no X11/Wayland on OHOS).
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");
    // OHOS ships EGL/GLES under plain names.
    SDL_setenv("SDL_VIDEO_GL_DRIVER", "libGLESv3.so", 1);
    SDL_setenv("SDL_VIDEO_EGL_DRIVER", "libEGL.so", 1);
    SDL_LogSetOutputFunction(SDLLogToHilog, nullptr);

    LOGI("SDL hint set, compiled SDL version %{public}d.%{public}d.%{public}d",
         SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);

    // Inject the real native window so the offscreen driver creates a real
    // EGL window surface and reports the real surface size.
    SDL_OHOS_SetNativeWindowWithSize((void*)win, w, h);
    LOGI("native window injected %{public}p size %{public}dx%{public}d", win, w, h);

    // Initialize SDL.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        LOGE("SDL_Init failed: %{public}s", SDL_GetError());
        g_state.running = false;
        return;
    }
    LOGI("SDL_Init OK (video+events)");

    // Register the virtual touchscreen before the engine starts: its
    // touch-screen controller scans devices once during startup (~1s later)
    // and never activates the native touch input method without one.
    RegisterVirtualTouchscreen();

    // OHOS has GLES only. TheXTech sets its GL profile only after window
    // creation, but the offscreen driver runs EGL chooseConfig during
    // SDL_CreateWindow, when SDL still defaults to desktop GL
    // (EGL_OPENGL_BIT) -> no matching config on OHOS. Set ES profile now,
    // after SDL_Init (which resets attributes), so offscreen picks ES2.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    LOGI("calling thetech_main directly (linked at compile time)");

    char arg0[] = "thextech";
    char argVerbose[] = "--verbose";
    char argC[] = "-c";
    char argU[] = "-u";

    int argc = 2;
    char *argv[8] = { arg0, argVerbose, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

    if (!gameRoot.empty()) {
        argv[2] = argC;
        argv[3] = const_cast<char*>(gameRoot.c_str());
        argc = 4;
        LOGI("asset pack (-c): %{public}s", gameRoot.c_str());
    }
    if (!gameHome.empty()) {
        argv[argc] = argU;
        argv[argc + 1] = const_cast<char*>(gameHome.c_str());
        argc += 2;
        LOGI("user dir (-u): %{public}s", gameHome.c_str());
    }

    LOGI("calling thetech_main argc=%{public}d", argc);
    int retval = thetech_main(argc, argv);
    LOGI("thetech_main returned %{public}d", retval);

    SDL_Quit();
    g_state.running = false;
    LOGI("thextech thread exit");
}

static napi_value InitRender(napi_env env, napi_callback_info info)
{
    size_t argc = 5;
    napi_value args[5] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.running) {
        LOGE("already running");
        napi_value ret = nullptr;
        napi_create_int32(env, 0, &ret);
        return ret;
    }

    uint64_t surfaceId = (argc >= 1) ? GetSurfaceId(env, args[0]) : 0;
    int32_t w = (argc >= 2) ? GetIntArg(env, args[1], 1280) : 1280;
    int32_t h = (argc >= 3) ? GetIntArg(env, args[2], 720) : 720;
    std::string gamePath = (argc >= 4) ? GetStringArg(env, args[3]) : std::string();
    std::string saveDir = (argc >= 5) ? GetStringArg(env, args[4]) : std::string();

    if (surfaceId == 0) {
        LOGE("invalid surfaceId");
        return nullptr;
    }

    OHNativeWindow* win = nullptr;
    int32_t r = OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfaceId, &win);
    if (r != 0 || win == nullptr) {
        LOGE("CreateNativeWindowFromSurfaceId failed ret=%{public}d", r);
        return nullptr;
    }

    g_state.window = win;
    g_state.width = w;
    g_state.height = h;
    g_state.gamePath = gamePath;
    g_state.saveDir = saveDir;
    g_state.running = true;
    g_state.quitRequested = false;

    g_state.thread = std::thread(TheXTechThreadMain, win, (int)w, (int)h,
                                  gamePath, saveDir,
                                  g_state.gameRoot, g_state.gameHome);

    LOGI("initRender OK %{public}dx%{public}d game=%{public}s", w, h, gamePath.c_str());
    napi_value ret = nullptr;
    napi_create_int32(env, 0, &ret);
    return ret;
}

static napi_value DestroyRender(napi_env env, napi_callback_info info)
{
    (void)env;
    (void)info;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.running) {
        // Ask thetech to quit by posting a SDL_QUIT event into its event loop.
        g_state.quitRequested = true;
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = SDL_QUIT;
        PushEventToEngine(&ev);

        if (g_state.thread.joinable()) {
            g_state.thread.join();
        }
        g_state.running = false;
    }
    if (g_state.window) {
        OH_NativeWindow_DestroyNativeWindow(g_state.window);
        g_state.window = nullptr;
    }
    LOGI("destroyRender done");
    return nullptr;
}

// Injects a touch event into the SDL event queue. action: 0=down, 1=up,
// 2=move, 3=cancel. nx/ny are normalized to [0, 1] in window coordinates.
static napi_value InjectTouch(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value args[3] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!g_state.running) {
        return nullptr;
    }

    int32_t action = (argc >= 1) ? GetIntArg(env, args[0], 0) : 0;
    double nx = 0.0;
    double ny = 0.0;
    if (argc >= 3) {
        napi_get_value_double(env, args[1], &nx);
        napi_get_value_double(env, args[2], &ny);
    }

    if (nx < 0.0) nx = 0.0;
    if (nx > 1.0) nx = 1.0;
    if (ny < 0.0) ny = 0.0;
    if (ny > 1.0) ny = 1.0;

    int px = (int)(nx * g_state.width);
    int py = (int)(ny * g_state.height);

    SDL_Event ev;
    SDL_zero(ev);

    switch (action) {
        case 0: // down
            ev.type = SDL_FINGERDOWN;
            break;
        case 3: // cancel -> up
        case 1: // up
            ev.type = SDL_FINGERUP;
            break;
        default: // move
            ev.type = SDL_FINGERMOTION;
            break;
    }

    ev.tfinger.touchId = 1;
    ev.tfinger.fingerId = 0;
    ev.tfinger.x = (float)nx;
    ev.tfinger.y = (float)ny;
    ev.tfinger.dx = 0.0f;
    ev.tfinger.dy = 0.0f;
    ev.tfinger.pressure = 1.0f;
    PushEventToEngine(&ev);

    // Synthesize a mouse event so mouse-based input works on the touchscreen too.
    SDL_zero(ev);
    switch (action) {
        case 0: // down
            ev.type = SDL_MOUSEBUTTONDOWN;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.clicks = 1;
            ev.button.x = px;
            ev.button.y = py;
            break;
        case 1: // up
        case 3: // cancel
            ev.type = SDL_MOUSEBUTTONUP;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.clicks = 1;
            ev.button.x = px;
            ev.button.y = py;
            break;
        default: // move
            ev.type = SDL_MOUSEMOTION;
            ev.motion.x = px;
            ev.motion.y = py;
            ev.motion.xrel = 0;
            ev.motion.yrel = 0;
            break;
    }
    ev.button.which = SDL_TOUCH_MOUSEID;
    PushEventToEngine(&ev);

    return nullptr;
}

static napi_value InjectKey(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!g_state.running) {
        LOGW("InjectKey called but game not running");
        return nullptr;
    }

    int32_t scancode = (argc >= 1) ? GetIntArg(env, args[0], 0) : 0;
    int32_t down = (argc >= 2) ? GetIntArg(env, args[1], 0) : 0;

    thetech_set_key_state(scancode, down ? 1 : 0);

    LOGI("InjectKey scancode=%{public}d down=%{public}d", scancode, down);

    return nullptr;
}

static napi_value InjectGamepadButton(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!g_state.running) {
        return nullptr;
    }

    int32_t button = (argc >= 1) ? GetIntArg(env, args[0], 0) : 0;
    int32_t down = (argc >= 2) ? GetIntArg(env, args[1], 0) : 0;

    SDL_Event ev;
    SDL_zero(ev);
    ev.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
    ev.jbutton.which = 0;
    ev.jbutton.button = (Uint8)button;
    ev.jbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
    PushEventToEngine(&ev);

    // Also push as controller button for SDL_GameController API.
    SDL_zero(ev);
    ev.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
    ev.cbutton.which = 0;
    ev.cbutton.button = (Uint8)button;
    ev.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
    PushEventToEngine(&ev);

    return nullptr;
}

static napi_value InjectGamepadAxis(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!g_state.running) {
        return nullptr;
    }

    int32_t axis = (argc >= 1) ? GetIntArg(env, args[0], 0) : 0;
    int32_t value = (argc >= 2) ? GetIntArg(env, args[1], 0) : 0;

    // Clamp value to SDL axis range [-32768, 32767].
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;

    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_JOYAXISMOTION;
    ev.jaxis.which = 0;
    ev.jaxis.axis = (Uint8)axis;
    ev.jaxis.value = (Sint16)value;
    PushEventToEngine(&ev);

    // Also push as controller axis for SDL_GameController API.
    SDL_zero(ev);
    ev.type = SDL_CONTROLLERAXISMOTION;
    ev.caxis.which = 0;
    ev.caxis.axis = (Uint8)axis;
    ev.caxis.value = (Sint16)value;
    PushEventToEngine(&ev);

    return nullptr;
}

static napi_value GetFps(napi_env env, napi_callback_info info)
{
    (void)info;
    int32_t fps = g_state.fps.load();
    napi_value ret = nullptr;
    napi_create_int32(env, fps, &ret);
    return ret;
}

// Raw touch passthrough into the SDL touch device state the engine polls:
// action 0=down 1=up 2=move, finger = multi-touch id, nx/ny normalized [0,1].
static napi_value SendTouch(napi_env env, napi_callback_info info)
{
    size_t argc = 4;
    napi_value args[4] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (!g_state.running) {
        return nullptr;
    }

    int32_t action = (argc >= 1) ? GetIntArg(env, args[0], 0) : 0;
    int32_t finger = (argc >= 2) ? GetIntArg(env, args[1], 0) : 0;
    double nx = 0.0, ny = 0.0;
    if (argc >= 4) {
        napi_get_value_double(env, args[2], &nx);
        napi_get_value_double(env, args[3], &ny);
    }
    if (nx < 0.0) nx = 0.0;
    if (nx > 1.0) nx = 1.0;
    if (ny < 0.0) ny = 0.0;
    if (ny > 1.0) ny = 1.0;

    RegisterVirtualTouchscreen();

    SDL_FingerID fid = (SDL_FingerID)finger;
    int rc;
    if (action == 0) {
        rc = SDL_SendTouch(g_ohosTouchId, fid, NULL, SDL_TRUE, (float)nx, (float)ny, 1.0f);
    } else if (action == 1) {
        rc = SDL_SendTouch(g_ohosTouchId, fid, NULL, SDL_FALSE, (float)nx, (float)ny, 1.0f);
    } else {
        rc = SDL_SendTouchMotion(g_ohosTouchId, fid, NULL, (float)nx, (float)ny, 1.0f);
    }

    static int s_logCount = 0;
    if (s_logCount < 12) {
        s_logCount++;
        LOGI("SendTouch a=%{public}d f=%{public}d (%{public}.3f,%{public}.3f) rc=%{public}d dev=%{public}d fing=%{public}d",
             action, finger, nx, ny, rc, SDL_GetNumTouchDevices(),
             SDL_GetNumTouchDevices() > 0 ? SDL_GetNumTouchFingers(g_ohosTouchId) : -1);
    }
    return nullptr;
}

static napi_value AttachVirtualGamepad(napi_env env, napi_callback_info info)
{
    (void)info;
    if (!g_state.running) {
        return nullptr;
    }
    int ret = thetech_attach_virtual_gamepad();
    LOGI("AttachVirtualGamepad ret=%{public}d", ret);
    napi_value out = nullptr;
    napi_create_int32(env, ret, &out);
    return out;
}

static int g_joyBtnLog = 0;
static int g_joyHatLog = 0;

static napi_value VirtualGamepadButton(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (!g_state.running) {
        return nullptr;
    }
    int32_t button = (argc >= 1) ? GetIntArg(env, args[0], 0) : 0;
    int32_t down = (argc >= 2) ? GetIntArg(env, args[1], 0) : 0;
    thetech_attach_virtual_gamepad();
    thetech_virtual_gamepad_button(button, down ? 1 : 0);
    if (g_joyBtnLog++ < 6) {
        LOGI("VirtualGamepadButton btn=%{public}d down=%{public}d", button, down);
    }
    return nullptr;
}

static napi_value VirtualGamepadHat(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (!g_state.running) {
        return nullptr;
    }
    int32_t mask = (argc >= 1) ? GetIntArg(env, args[0], 0) : 0;
    thetech_attach_virtual_gamepad();
    thetech_virtual_gamepad_hat(mask);
    if (g_joyHatLog++ < 6) {
        LOGI("VirtualGamepadHat mask=%{public}d", mask);
    }
    return nullptr;
}

static napi_value VirtualGamepadAxis(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (!g_state.running) {
        return nullptr;
    }
    int32_t axis = (argc >= 1) ? GetIntArg(env, args[0], 0) : 0;
    int32_t value = (argc >= 2) ? GetIntArg(env, args[1], 0) : 0;
    thetech_attach_virtual_gamepad();
    thetech_virtual_gamepad_axis(axis, value);
    return nullptr;
}

static napi_value PrepareGameEnv(napi_env env, napi_callback_info info)
{
    (void)env;
    (void)info;

    // Redirect stdio to hilog so printf/cout output is visible in device logs.
    RedirectStdioToHilog();

    // Set common environment variables for thetech.
    SDL_setenv("SDL_VIDEO_GL_DRIVER", "libGLESv3.so", 1);
    SDL_setenv("SDL_VIDEO_EGL_DRIVER", "libEGL.so", 1);
    SDL_setenv("SDL_AUDIODRIVER", "ohos", 1);
    SDL_setenv("SDL_THREAD_STACK_SIZE", "262144", 0);

    LOGI("game environment prepared");
    return nullptr;
}

static napi_value SetGameRoot(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.gameRoot = (argc >= 1) ? GetStringArg(env, args[0]) : std::string();
    g_state.gameHome = (argc >= 2) ? GetStringArg(env, args[1]) : std::string();

    LOGI("gameRoot=%{public}s gameHome=%{public}s",
         g_state.gameRoot.c_str(), g_state.gameHome.c_str());

    return nullptr;
}

static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"initRender", nullptr, InitRender, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"destroyRender", nullptr, DestroyRender, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"injectTouch", nullptr, InjectTouch, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"injectKey", nullptr, InjectKey, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"injectGamepadButton", nullptr, InjectGamepadButton, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"injectGamepadAxis", nullptr, InjectGamepadAxis, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getFps", nullptr, GetFps, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sendTouch", nullptr, SendTouch, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"attachVirtualGamepad", nullptr, AttachVirtualGamepad, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"virtualGamepadButton", nullptr, VirtualGamepadButton, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"virtualGamepadHat", nullptr, VirtualGamepadHat, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"virtualGamepadAxis", nullptr, VirtualGamepadAxis, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"prepareGameEnv", nullptr, PrepareGameEnv, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setGameRoot", nullptr, SetGameRoot, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

static napi_module thetechModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void*)0),
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void)
{
    napi_module_register(&thetechModule);
}