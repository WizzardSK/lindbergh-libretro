/*
 * The loader's side of the libretro core (src/libretro): with
 * LINDBERGH_LIBRETRO_SHM set, the game runs for the core instead of in a
 * window of its own. Each buffer swap hands the frame over through the
 * shared memory (libretro_shm.h) and waits for the core's next run; the
 * core's pads drive virtual SDL gamepads and its pointer and keys become
 * SDL events, so the controls mapping works as it does standalone; and the
 * audio mix goes into the shared ring instead of to a sound card.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__
#include <glad/gl.h>
#include <SDL3/SDL.h>

#include "../../libretro/libretro_shm.h"
#include "libretroBridge.h"
#include "../log/log.h"

extern SDL_Window *g_SdlWindow;
extern int fboInitialized;
extern GLuint fboId;
extern int blitWidth;
extern int blitHeight;
extern int gameIsOutrunChihiroMode;

static LrShm *shm;
static bool checked;
static uint32_t lastRun;
static SDL_Joystick *pads[LR_PLAYERS];
static uint32_t lastButtons[LR_PLAYERS];
static uint32_t lastPointerButtons;
static uint32_t lastPointerX = UINT32_MAX, lastPointerY = UINT32_MAX;
static uint32_t *rowBuffer;

static int futexWait(volatile uint32_t *addr, uint32_t value, int ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    return syscall(SYS_futex, addr, FUTEX_WAIT, value, ms >= 0 ? &ts : NULL, NULL, 0);
}

static void futexWake(volatile uint32_t *addr)
{
    syscall(SYS_futex, addr, FUTEX_WAKE, 0x7fffffff, NULL, NULL, 0);
}

bool libretroMode()
{
    if (checked)
        return shm != NULL;
    checked = true;
    const char *name = getenv("LINDBERGH_LIBRETRO_SHM");
    if (!name || !*name)
        return false;
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0)
    {
        log_error("libretro: cannot open the shared memory %s: %s\n", name, strerror(errno));
        return false;
    }
    void *p = mmap(NULL, sizeof(LrShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED || ((LrShm *)p)->magic != LR_SHM_MAGIC || ((LrShm *)p)->version != LR_SHM_VERSION)
    {
        log_error("libretro: the shared memory %s is not the core's (another build?)\n", name);
        return false;
    }
    shm = p;
    shm->loader_pid = getpid();
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
            SDL_SetJoystickVirtualAxis(pads[i], a, pad.axes[a]);
    }

    int w = 0, h = 0;
    if (g_SdlWindow)
        SDL_GetWindowSize(g_SdlWindow, &w, &h);
    if (shm->pointer_valid && w > 0 && h > 0)
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

    while (shm->key_tail != shm->key_head)
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
    futexWake(&shm->frame_seq);
    /* the core's next run; it may sit in RetroArch's menu for a while */
    while (shm->run_seq == lastRun)
    {
        if (shm->quit)
            _exit(0);
        futexWait(&shm->run_seq, lastRun, 200);
    }
    lastRun = shm->run_seq;
    if (shm->quit)
        _exit(0);
}

/* The audio mix, at the end of SDL's mixing for the device FAudio plays to */
static void SDLCALL postmix(void *userdata, const SDL_AudioSpec *spec, float *buffer, int buflen)
{
    (void)userdata;
    if (!shm || spec->channels <= 0)
        return;
    const int channels = spec->channels;
    const int frames = buflen / (int)sizeof(float) / channels;
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
}

/* FAudio (libsegaapi) opens its stream here: the device it plays to gets
 * the post-mix hook that copies the mix to the core */
SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec,
                                           SDL_AudioStreamCallback callback, void *userdata)
{
    SDL_AudioStream *(*real)(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *) =
        dlsym(RTLD_NEXT, "SDL_OpenAudioDeviceStream");
    SDL_AudioStream *stream = real(devid, spec, callback, userdata);
    if (stream && libretroMode())
    {
        SDL_AudioDeviceID dev = SDL_GetAudioStreamDevice(stream);
        if (!SDL_SetAudioPostmixCallback(dev, postmix, NULL))
            log_error("libretro: no post-mix callback: %s\n", SDL_GetError());
    }
    return stream;
}
