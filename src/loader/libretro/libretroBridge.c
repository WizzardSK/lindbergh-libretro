/*
 * The loader's side of the libretro core (src/libretro): with
 * LINDBERGH_LIBRETRO_SHM set, the game runs for the core instead of in a
 * window of its own. Each buffer swap hands the frame over through the
 * shared memory (libretro_shm.h) and waits for the core's next run; the
 * core's pads drive virtual SDL gamepads and its pointer and keys become
 * SDL events, so the controls mapping works as it does standalone; and the
 * audio mix goes into the shared ring instead of to a sound card.
 *
 * On Linux the game is a 32-bit process with linuxloader.so preloaded; on
 * Windows it is linuxloader.exe, which loads the game's ELF itself. Only
 * the shared memory, the waits (libretro_ipc.h) and the way to the audio
 * mix differ.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include "MinHook.h"
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#endif

#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__
#include <glad/gl.h>
#include <SDL3/SDL.h>

#include "../../libretro/libretro_ipc.h"
#include "../../libretro/libretro_shm.h"
#include "libretroBridge.h"
#include "../input/sdlInput.h"
#include "../log/log.h"

extern SDL_Window *g_SdlWindow;
extern int fboInitialized;
extern GLuint fboId;
extern int blitWidth;
extern int blitHeight;
extern int gameIsOutrunChihiroMode;

static LrShm *shm;
static LrEvent runEvent, frameEvent;
static bool checked;
static uint32_t lastRun;
static SDL_Joystick *pads[LR_PLAYERS];
static uint32_t lastButtons[LR_PLAYERS];
static uint32_t lastPointerButtons;
static uint32_t lastPointerX = UINT32_MAX, lastPointerY = UINT32_MAX;
static uint32_t *rowBuffer;

/* RetroArch's pause, as the game sees it (NNshi): while the core is not
 * run for longer than a frame takes, the game's wall clock stands still and
 * its sound is not handed on, so a game that keeps time by the clock
 * (Initial D 4) does not jump ahead on resuming and one whose sound thread
 * runs on (Too Spicy) is quiet. The pauses so far and the start of the one
 * going on, on a monotonic clock, in seconds and microseconds, without
 * 64-bit division: the 32-bit build has no libgcc for it. Under pauseLock. */
#define LR_PAUSE_AFTER_US 50000
static int32_t pausedSec, pausedUsec, pauseStartSec, pauseStartUsec;
static int pausing, pauseLock;

static void monoNow(int32_t *sec, int32_t *usec)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    *sec = (int32_t)(c.QuadPart / f.QuadPart);
    *usec = (int32_t)(c.QuadPart % f.QuadPart * 1000000 / f.QuadPart);
#else
    struct timespec ts;
    syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
    *sec = (int32_t)ts.tv_sec;
    *usec = (int32_t)(ts.tv_nsec / 1000);
#endif
}

static void pauseLockTake(void)
{
    while (__atomic_exchange_n(&pauseLock, 1, __ATOMIC_ACQUIRE))
        ;
}

static void pauseLockGive(void)
{
    __atomic_store_n(&pauseLock, 0, __ATOMIC_RELEASE);
}

/* How far the game's clock is behind the real one: the pauses so far and
 * the one going on */
void libretroPausedTime(int32_t *sec, int32_t *usec)
{
    *sec = *usec = 0;
    if (!__atomic_load_n(&pausing, __ATOMIC_RELAXED) && !__atomic_load_n(&pausedSec, __ATOMIC_RELAXED) &&
        !__atomic_load_n(&pausedUsec, __ATOMIC_RELAXED))
        return;
    pauseLockTake();
    *sec = pausedSec;
    *usec = pausedUsec;
    if (pausing)
    {
        int32_t s, u;
        monoNow(&s, &u);
        *sec += s - pauseStartSec;
        *usec += u - pauseStartUsec;
    }
    pauseLockGive();
    while (*usec < 0)
    {
        *usec += 1000000;
        (*sec)--;
    }
    while (*usec >= 1000000)
    {
        *usec -= 1000000;
        (*sec)++;
    }
}

int64_t libretroPausedUs(void)
{
    int32_t sec, usec;
    libretroPausedTime(&sec, &usec);
    return (int64_t)sec * 1000000 + usec;
}

static bool libretroPaused(void)
{
    return __atomic_load_n(&pausing, __ATOMIC_RELAXED) != 0;
}

#ifndef _WIN32
/* The game's wall clock, which the games keep time by, held back by the
 * pauses; on Windows the libc bridge does the same (libcBridge.cpp). The
 * monotonic clock, which SDL's own timing reads, is left as it is. */
int gettimeofday(struct timeval *tv, void *tz)
{
    static int (*real)(struct timeval *, void *);
    if (!real)
        real = (int (*)(struct timeval *, void *))dlsym(RTLD_NEXT, "gettimeofday");
    const int r = real(tv, tz);
    int32_t sec, usec;
    if (r == 0 && tv && (libretroPausedTime(&sec, &usec), sec || usec))
    {
        tv->tv_sec -= sec;
        tv->tv_usec -= usec;
        if (tv->tv_usec < 0)
        {
            tv->tv_usec += 1000000;
            tv->tv_sec--;
        }
    }
    return r;
}

int clock_gettime(clockid_t clk, struct timespec *tp)
{
    static int (*real)(clockid_t, struct timespec *);
    if (!real)
        real = (int (*)(clockid_t, struct timespec *))dlsym(RTLD_NEXT, "clock_gettime");
    const int r = real(clk, tp);
    int32_t sec, usec;
    if (r == 0 && tp && clk == CLOCK_REALTIME && (libretroPausedTime(&sec, &usec), sec || usec))
    {
        tp->tv_sec -= sec;
        tp->tv_nsec -= usec * 1000;
        if (tp->tv_nsec < 0)
        {
            tp->tv_nsec += 1000000000;
            tp->tv_sec--;
        }
    }
    return r;
}

time_t time(time_t *tloc)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tloc)
        *tloc = tv.tv_sec;
    return tv.tv_sec;
}
#endif

static void *mapShm(const char *name)
{
#ifdef _WIN32
    char eventName[256];
    snprintf(eventName, sizeof(eventName), "%s" LR_EVENT_RUN, name);
    runEvent = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, eventName);
    snprintf(eventName, sizeof(eventName), "%s" LR_EVENT_FRAME, name);
    frameEvent = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, eventName);
    HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!runEvent || !frameEvent || !mapping)
    {
        log_error("libretro: cannot open the shared memory %s: error %lu\n", name, GetLastError());
        return NULL;
    }
    void *p = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LrShm));
    CloseHandle(mapping);
    return p;
#else
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0)
    {
        log_error("libretro: cannot open the shared memory %s: %s\n", name, strerror(errno));
        return NULL;
    }
    void *p = mmap(NULL, sizeof(LrShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return p == MAP_FAILED ? NULL : p;
#endif
}

#ifdef _WIN32
static void hookAudio();
#endif

bool libretroMode()
{
    if (checked)
        return shm != NULL;
    checked = true;
    const char *name = getenv("LINDBERGH_LIBRETRO_SHM");
    if (!name || !*name)
        return false;
    void *p = mapShm(name);
    if (!p)
        return false;
    if (((LrShm *)p)->magic != LR_SHM_MAGIC || ((LrShm *)p)->version != LR_SHM_VERSION)
    {
        log_error("libretro: the shared memory %s is not the core's (another build?)\n", name);
        return false;
    }
    shm = p;
    /* The game's window stays hidden, so it never has the focus, and SDL
     * drops joystick input then - the core's virtual pads too */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    /* Small device blocks, so the mix reaches the core in steady steps */
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
    /* The dummy driver waits a block's length in whole milliseconds after
     * each (5 ms for 5.33), so it mixed some 6 % fast, and the core cut
     * the backlog every second or two: streamed music skipped (NNshi).
     * postmix keeps the pace by the clock instead. */
    SDL_SetHint(SDL_HINT_AUDIO_DUMMY_TIMESCALE, "0");
#ifdef _WIN32
    shm->loader_pid = GetCurrentProcessId();
    hookAudio();
#else
    shm->loader_pid = getpid();
#endif
    lastRun = shm->run_seq;
    log_info("libretro: running for the libretro core\n");
    return true;
}

/* SDL_GamepadButton for each LR_PAD_* */
static const int padButton[LR_PAD_BUTTONS] = {
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_GUIDE,
};

void libretroAttachPads()
{
    if (!libretroMode())
        return;
    for (int i = 0; i < LR_PLAYERS; i++)
    {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = i == 0 ? "RetroArch Player 1" : "RetroArch Player 2";
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        if (!id)
        {
            log_error("libretro: no virtual gamepad: %s\n", SDL_GetError());
            continue;
        }
        pads[i] = SDL_OpenJoystick(id);
    }
}

static void pushMouseButton(int button, bool down, float x, float y)
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.windowID = g_SdlWindow ? SDL_GetWindowID(g_SdlWindow) : 0;
    ev.button.button = button;
    ev.button.down = down;
    ev.button.clicks = 1;
    ev.button.x = x;
    ev.button.y = y;
    SDL_PushEvent(&ev);
}

void libretroPollInput()
{
    if (!libretroMode())
        return;
    for (int i = 0; i < LR_PLAYERS; i++)
    {
        if (!pads[i])
            continue;
        const LrPad pad = shm->pads[i];
        for (int b = 0; b < LR_PAD_BUTTONS; b++)
        {
            bool down = pad.buttons & (1u << b);
            if (down != (bool)(lastButtons[i] & (1u << b)))
                SDL_SetJoystickVirtualButton(pads[i], padButton[b], down);
        }
        lastButtons[i] = pad.buttons;
        for (int a = 0; a < LR_AXES; a++)
        {
            /* A trigger is a joystick axis from -32768 at rest to 32767
             * held, as SDL's drivers report a pad's; the core's 0..32767
             * read as half held at rest (NNshi: no gas or brake) */
            int v = pad.axes[a];
            if (a == LR_AXIS_LT || a == LR_AXIS_RT)
                v = v * 2 - 32768;
            SDL_SetJoystickVirtualAxis(pads[i], a, (Sint16)(v > 32767 ? 32767 : v));
        }
    }

    int w = 0, h = 0;
    if (g_SdlWindow)
        SDL_GetWindowSize(g_SdlWindow, &w, &h);
    /* a gun game: both players' aim and buttons straight to the game, and
     * the pointer not as the mouse, which would aim player 1 too; Primeval
     * Hunt's touch screen takes the pointer, where with Gun Aim on Mouse
     * player 1's buttons touch rather than fire */
    shm->gun_game = sdlInputGunGame();
    if (shm->gun_game)
    {
        bool touching = false;
        if (shm->gun_game == LR_GUNS_PRIMEVAL_HUNT && shm->pointer_valid && w > 0 && h > 0)
            touching = sdlInputPhTouch((float)shm->pointer_x * w / 65536.0f, (float)shm->pointer_y * h / 65536.0f,
                                       shm->pointer_buttons & 1) &&
                       !shm->guns_stick;
        for (int p = 0; p < LR_PLAYERS; p++)
        {
            const LrGun g = shm->guns[p];
            sdlInputSetGun((JVSPlayer)(PLAYER_1 + p), g.x / 65535.0f, g.y / 65535.0f,
                           p == 0 && touching ? 0 : g.buttons);
        }
    }
    else if (shm->pointer_valid && w > 0 && h > 0)
    {
        float x = (float)shm->pointer_x * w / 65536.0f, y = (float)shm->pointer_y * h / 65536.0f;
        if (shm->pointer_x != lastPointerX || shm->pointer_y != lastPointerY)
        {
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.windowID = SDL_GetWindowID(g_SdlWindow);
            ev.motion.x = x;
            ev.motion.y = y;
            if (lastPointerX != UINT32_MAX)
            {
                ev.motion.xrel = x - (float)lastPointerX * w / 65536.0f;
                ev.motion.yrel = y - (float)lastPointerY * h / 65536.0f;
            }
            SDL_PushEvent(&ev);
            SDL_WarpMouseInWindow(g_SdlWindow, x, y);
            lastPointerX = shm->pointer_x;
            lastPointerY = shm->pointer_y;
        }
        static const int sdlButton[3] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};
        for (int b = 0; b < 3; b++)
        {
            bool down = shm->pointer_buttons & (1u << b);
            if (down != (bool)(lastPointerButtons & (1u << b)))
                pushMouseButton(sdlButton[b], down, x, y);
        }
        lastPointerButtons = shm->pointer_buttons;
    }

    while (shm->key_tail != __atomic_load_n(&shm->key_head, __ATOMIC_ACQUIRE))
    {
        LrKey k = shm->keys[shm->key_tail % LR_KEY_QUEUE];
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = k.down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        ev.key.windowID = g_SdlWindow ? SDL_GetWindowID(g_SdlWindow) : 0;
        ev.key.scancode = (SDL_Scancode)k.scancode;
        ev.key.key = SDL_GetKeyFromScancode((SDL_Scancode)k.scancode, SDL_KMOD_NONE, false);
        ev.key.down = k.down;
        SDL_PushEvent(&ev);
        shm->key_tail++;
    }
}

/* The frame the game drew, out of the loader's blit FBO (blitStretching.c),
 * which holds it at the game's resolution */
static void copyFrame()
{
    if (!fboInitialized || !fboId)
        return;
    int x0 = gameIsOutrunChihiroMode ? 80 : 0;
    int w = blitWidth - 2 * x0, h = blitHeight;
    if (w <= 0 || h <= 0 || w > LR_MAX_WIDTH || h > LR_MAX_HEIGHT)
        return;
    if (!rowBuffer)
        rowBuffer = malloc((size_t)LR_MAX_WIDTH * LR_MAX_HEIGHT * 4);
    GLint readFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fboId);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glReadPixels(x0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, rowBuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    /* GL's rows go bottom up */
    for (int y = 0; y < h; y++)
        memcpy(&shm->pixels[(size_t)y * w], &rowBuffer[(size_t)(h - 1 - y) * w], (size_t)w * 4);
    shm->width = w;
    shm->height = h;
}

void libretroFrame()
{
    if (!libretroMode())
        return;
    copyFrame();
    shm->frame_seq++;
    lrWake(frameEvent, &shm->frame_seq);
    /* the core's next run; it may sit in RetroArch's menu for a while,
     * paused for the game once it is longer than a frame takes */
    int32_t waitSec, waitUsec, nowSec, nowUsec;
    monoNow(&waitSec, &waitUsec);
    while (shm->run_seq == lastRun)
    {
        if (shm->quit)
            _exit(0);
        lrWait(runEvent, &shm->run_seq, lastRun, 20);
        if (libretroPaused() || shm->run_seq != lastRun)
            continue;
        monoNow(&nowSec, &nowUsec);
        if ((nowSec - waitSec) * 1000000 + (nowUsec - waitUsec) > LR_PAUSE_AFTER_US)
        {
            pauseLockTake();
            pauseStartSec = nowSec;
            pauseStartUsec = nowUsec;
            pausing = 1;
            pauseLockGive();
        }
    }
    if (libretroPaused())
    {
        monoNow(&nowSec, &nowUsec);
        pauseLockTake();
        pausedSec += nowSec - pauseStartSec;
        pausedUsec += nowUsec - pauseStartUsec;
        while (pausedUsec < 0)
        {
            pausedUsec += 1000000;
            pausedSec--;
        }
        while (pausedUsec >= 1000000)
        {
            pausedUsec -= 1000000;
            pausedSec++;
        }
        pausing = 0;
        pauseLockGive();
    }
    lastRun = shm->run_seq;
    if (shm->quit)
        _exit(0);
}

void libretroPublishPadLabels()
{
    if (!libretroMode() || !shm)
        return;
    uint32_t n = 0;
    for (int p = 0; p < LR_PLAYERS; p++)
    {
        /* the guide button has no RetroPad button */
        for (int b = 0; b < LR_PAD_GUIDE && n < LR_PAD_LABELS; b++)
        {
            LrPadLabel l = {(uint8_t)p, 0, (uint8_t)b, 0, {0}};
            if (sdlInputPadLabel(p, false, padButton[b], l.name, sizeof(l.name)))
                shm->pad_labels[n++] = l;
        }
        /* LR_AXIS_* are SDL's gamepad axes, in its order */
        for (int a = 0; a < LR_AXES && n < LR_PAD_LABELS; a++)
        {
            LrPadLabel l = {(uint8_t)p, 1, (uint8_t)a, 0, {0}};
            if (sdlInputPadLabel(p, true, a, l.name, sizeof(l.name)))
                shm->pad_labels[n++] = l;
        }
    }
    shm->pad_label_count = n;
    __atomic_store_n(&shm->pad_labels_seq, shm->pad_labels_seq + 1, __ATOMIC_RELEASE);
}

void libretroRumble(float left, float right, int duration_ms)
{
    if (!shm)
        return;
    shm->rumble_strong = (uint32_t)(SDL_clamp(left, 0.0f, 1.0f) * 65535.0f);
    shm->rumble_weak = (uint32_t)(SDL_clamp(right, 0.0f, 1.0f) * 65535.0f);
    shm->rumble_ms = duration_ms > 0 ? (uint32_t)duration_ms : 0;
    __atomic_add_fetch(&shm->rumble_seq, 1, __ATOMIC_RELEASE);
}

/* The device's blocks at the rate they would play at: each mixed block waits
 * until the clock reaches its end, as a sound card would take it. After a
 * stall of more than 100 ms the clock starts over rather than catching up. */
static void pace(int frames, int freq)
{
    /* the time mixed so far in whole microseconds and a remainder, without
     * 64-bit division: the 32-bit build has no libgcc for it */
    static Uint64 start, mixed_us;
    static Uint32 rest;
    static int rate;
    if (frames <= 0 || frames > 4000 || freq <= 0)
        return;
    const Uint64 now = SDL_GetTicksNS();
    if (rate != freq || !start)
    {
        rate = freq;
        start = now;
        mixed_us = 0;
        rest = 0;
    }
    rest += (Uint32)frames * 1000000u;
    mixed_us += rest / (Uint32)freq;
    rest %= (Uint32)freq;
    const Uint64 due = start + mixed_us * SDL_NS_PER_US;
    if (due > now)
        SDL_DelayPrecise(due - now);
    else if (now - due > 100 * SDL_NS_PER_MS)
    {
        start = now;
        mixed_us = 0;
        rest = 0;
    }
}

/* The audio mix, at the end of SDL's mixing for the device FAudio plays to */
static void SDLCALL postmix(void *userdata, const SDL_AudioSpec *spec, float *buffer, int buflen)
{
    (void)userdata;
    if (!shm || spec->channels <= 0)
        return;
    const int channels = spec->channels;
    const int frames = buflen / (int)sizeof(float) / channels;
    if (libretroPaused())
    {
        pace(frames, spec->freq);
        return;
    }
    shm->audio_rate = spec->freq;
    uint32_t write = shm->audio_write;
    for (int i = 0; i < frames; i++)
    {
        const float *f = &buffer[i * channels];
        float l = f[0], r = channels > 1 ? f[1] : f[0];
        if (channels >= 6)
        {
            /* 5.1: centre and rears into the front pair */
            l += 0.707f * f[2] + 0.707f * f[4];
            r += 0.707f * f[2] + 0.707f * f[5];
        }
        if ((uint32_t)(write - shm->audio_read) >= LR_AUDIO_FRAMES)
            break; /* the core is not reading: drop rather than overwrite */
        int16_t *out = &shm->audio[(write % LR_AUDIO_FRAMES) * 2];
        l = l > 1.0f ? 1.0f : l < -1.0f ? -1.0f : l;
        r = r > 1.0f ? 1.0f : r < -1.0f ? -1.0f : r;
        out[0] = (int16_t)(l * 32767.0f);
        out[1] = (int16_t)(r * 32767.0f);
        write++;
    }
    shm->audio_write = write;
    pace(frames, spec->freq);
}

/* The device the game's sound plays to: its mix goes to the core too */
void libretroAttachAudioDevice(uint32_t dev)
{
    if (!libretroMode())
        return;
    if (!SDL_SetAudioPostmixCallback(dev, postmix, NULL))
        log_error("libretro: no post-mix callback: %s\n", SDL_GetError());
}

static void attachPostmix(SDL_AudioStream *stream)
{
    libretroAttachAudioDevice(SDL_GetAudioStreamDevice(stream));
}

/* FAudio (libsegaapi) opens its stream here: the device it plays to gets
 * the post-mix hook that copies the mix to the core */
#ifdef _WIN32
/* SDL and FAudio are linked into linuxloader.exe: the function is hooked
 * (MinHook, enabled with the loader's own hooks in init.c) */
static SDL_AudioStream *(*realOpenAudioDeviceStream)(SDL_AudioDeviceID, const SDL_AudioSpec *,
                                                     SDL_AudioStreamCallback, void *);

static SDL_AudioStream *SDLCALL hookedOpenAudioDeviceStream(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec,
                                                            SDL_AudioStreamCallback callback, void *userdata)
{
    SDL_AudioStream *stream = realOpenAudioDeviceStream(devid, spec, callback, userdata);
    if (stream)
        attachPostmix(stream);
    return stream;
}

static void hookAudio()
{
    if (MH_CreateHook((void *)SDL_OpenAudioDeviceStream, (void *)hookedOpenAudioDeviceStream,
                      (void **)&realOpenAudioDeviceStream) != MH_OK)
        log_error("libretro: cannot hook SDL_OpenAudioDeviceStream, no audio\n");
}
#else
SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec,
                                           SDL_AudioStreamCallback callback, void *userdata)
{
    SDL_AudioStream *(*real)(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *) =
        dlsym(RTLD_NEXT, "SDL_OpenAudioDeviceStream");
    SDL_AudioStream *stream = real(devid, spec, callback, userdata);
    if (stream && libretroMode())
        attachPostmix(stream);
    return stream;
}
#endif
