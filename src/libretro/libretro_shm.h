/*
 * The memory the libretro core (lindbergh_libretro.so, a process of the
 * frontend) and the loader running the game (linuxloader.so, preloaded into
 * the 32-bit game; linuxloader.exe on Windows) share. Its name is in
 * LINDBERGH_LIBRETRO_SHM; the core creates it (shm_open, or a named file
 * mapping on Windows) and the loader maps it.
 *
 * Fixed-width fields only, the same layout for the 64-bit core and the
 * 32-bit loader. The two take turns on a frame with run_seq and frame_seq
 * (futexes, or events on Windows: libretro_ipc.h):
 *
 *   core: input written, run_seq++ and woken     loader: waits for run_seq
 *   loader: at the game's buffer swap, the frame copied in, frame_seq++ and
 *           woken, then waits for the next run_seq
 *
 * so the game draws one frame per retro_run, paced by the frontend.
 */

#ifndef LINDBERGH_LIBRETRO_SHM_H
#define LINDBERGH_LIBRETRO_SHM_H

#include <stdint.h>

#define LR_SHM_MAGIC 0x4C424C52u /* "RLBL" */
#define LR_SHM_VERSION 1

#define LR_MAX_WIDTH 2048
#define LR_MAX_HEIGHT 1536

/* A ring of interleaved stereo int16 frames at audio_rate */
#define LR_AUDIO_FRAMES 16384

#define LR_PLAYERS 2
#define LR_KEY_QUEUE 64

enum {
    LR_PAD_A, LR_PAD_B, LR_PAD_X, LR_PAD_Y,
    LR_PAD_BACK, LR_PAD_START,
    LR_PAD_LSTICK, LR_PAD_RSTICK,
    LR_PAD_LSHOULDER, LR_PAD_RSHOULDER,
    LR_PAD_UP, LR_PAD_DOWN, LR_PAD_LEFT, LR_PAD_RIGHT,
    LR_PAD_GUIDE,
    LR_PAD_BUTTONS
};

enum {
    LR_AXIS_LX, LR_AXIS_LY, LR_AXIS_RX, LR_AXIS_RY,
    LR_AXIS_LT, LR_AXIS_RT,
    LR_AXES
};

typedef struct LrPad {
    uint32_t buttons;            /* bit LR_PAD_* */
    int16_t axes[LR_AXES];       /* sticks -32768..32767, triggers 0..32767 */
} LrPad;

typedef struct LrKey {
    uint32_t scancode;           /* SDL_Scancode */
    uint32_t down;
} LrKey;

typedef struct LrShm {
    uint32_t magic;
    uint32_t version;

    /* core -> loader */
    volatile uint32_t run_seq;   /* futex: one more frame may be drawn */
    volatile uint32_t quit;      /* the core is closing: the game exits */
    LrPad pads[LR_PLAYERS];
    /* the pointer (mouse or light gun) over the picture, 0..65535 */
    uint32_t pointer_x, pointer_y;
    uint32_t pointer_buttons;    /* bit 0 left, 1 right, 2 middle */
    uint32_t pointer_valid;
    volatile uint32_t key_head;  /* written by the core */
    volatile uint32_t key_tail;  /* written by the loader */
    LrKey keys[LR_KEY_QUEUE];

    /* loader -> core */
    volatile uint32_t frame_seq; /* futex: a frame was handed over */
    volatile uint32_t loader_pid;
    uint32_t width, height;      /* of the frame, pitch width * 4 */
    uint32_t audio_rate;
    volatile uint32_t audio_write; /* in frames, wraps at LR_AUDIO_FRAMES */
    volatile uint32_t audio_read;  /* written by the core */
    int16_t audio[LR_AUDIO_FRAMES * 2];
    uint32_t pixels[LR_MAX_WIDTH * LR_MAX_HEIGHT]; /* XRGB8888, top row first */
} LrShm;

#endif
