#ifndef LIBRETRO_BRIDGE_H
#define LIBRETRO_BRIDGE_H

#include <stdbool.h>

#ifdef __linux__

/* Whether the game runs for the libretro core (LINDBERGH_LIBRETRO_SHM) */
bool libretroMode();
/* Virtual SDL gamepads for the core's pads; after SDL is initialized */
void libretroAttachPads();
/* The core's input as SDL gamepad, mouse and key state; in pollEvents */
void libretroPollInput();
/* At the game's buffer swap: the frame to the core, then its next run */
void libretroFrame();
#else
/* The core runs the loader on Linux only so far */
static inline bool libretroMode() { return false; }
static inline void libretroAttachPads() {}
static inline void libretroPollInput() {}
static inline void libretroFrame() {}
#endif

#endif
