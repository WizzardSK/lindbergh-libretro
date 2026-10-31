/*
 * The memory the libretro core (linuxloader_libretro.so, a process of the
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
#define LR_SHM_VERSION 6

#define LR_MAX_WIDTH 5120
#define LR_MAX_HEIGHT 2880

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

/* A gun player's aim, 0..65535 across the picture left to right and top to
 * bottom, and its buttons (bit LR_GUN_*) */
typedef struct LrGun {
    uint32_t x, y;
    uint32_t buttons;
} LrGun;

enum {
    LR_GUN_TRIGGER, LR_GUN_RELOAD, LR_GUN_BUTTON,
    LR_GUN_ACTION, LR_GUN_CHANGE,        /* Ghost Squad Evolution's */
    LR_GUN_PEDAL_LEFT, LR_GUN_PEDAL_RIGHT, /* Too Spicy's */
    LR_GUN_SHAKE,                        /* The House of the Dead 4's */
};

/* The gun games' layouts, which say what the core's buttons are */
enum { LR_GUNS_NONE, LR_GUNS_STANDARD, LR_GUNS_TOO_SPICY, LR_GUNS_GHOST_SQUAD, LR_GUNS_PRIMEVAL_HUNT, LR_GUNS_HOTD4 };

/* What one of a pad's buttons (LR_PAD_*) or axes (LR_AXIS_*) does in the
 * game, by the loader's bindings: the core's input descriptors */
typedef struct LrPadLabel {
    uint8_t pad;                 /* 0 or 1, RetroArch's port */
    uint8_t axis;                /* which is an LR_AXIS_*, else an LR_PAD_* */
    uint8_t which;
    uint8_t unused;
    char name[28];
} LrPadLabel;

#define LR_PAD_LABELS 64

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
    /* the gun games' players, from the core's Gun Aim (sticks or mouse) */
    LrGun guns[LR_PLAYERS];
    uint32_t guns_stick;         /* Gun Aim on Analog Stick */
    volatile uint32_t key_head;  /* written by the core */
    volatile uint32_t key_tail;  /* written by the loader */
    LrKey keys[LR_KEY_QUEUE];

    /* loader -> core */
    volatile uint32_t frame_seq; /* futex: a frame was handed over */
    volatile uint32_t loader_pid;
    /* the game aims with guns (guns[] go to it), LR_GUNS_* by its
     * buttons: the core draws the crosshairs */
    volatile uint32_t gun_game;
    uint32_t width, height;      /* of the frame, pitch width * 4 */
    uint32_t audio_rate;
    volatile uint32_t audio_write; /* in frames, wraps at LR_AUDIO_FRAMES */
    volatile uint32_t audio_read;  /* written by the core */
    /* the drive board's force feedback as player 1's rumble: strong (left)
     * and weak (right) 0..65535 for duration_ms, one more rumble_seq each */
    uint32_t rumble_strong, rumble_weak, rumble_ms;
    volatile uint32_t rumble_seq;
    /* the pads' buttons and axes the game's bindings use, one more
     * pad_labels_seq once they are written */
    uint32_t pad_label_count;
    volatile uint32_t pad_labels_seq;
    LrPadLabel pad_labels[LR_PAD_LABELS];
    int16_t audio[LR_AUDIO_FRAMES * 2];
    uint32_t pixels[LR_MAX_WIDTH * LR_MAX_HEIGHT]; /* XRGB8888, top row first */
} LrShm;

#endif
