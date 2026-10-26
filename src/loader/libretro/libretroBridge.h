#ifndef LIBRETRO_BRIDGE_H
#define LIBRETRO_BRIDGE_H

#include <stdbool.h>

/* Whether the game runs for the libretro core (LINDBERGH_LIBRETRO_SHM);
 * first asked in init.c, after MinHook is set up and before its hooks are
 * enabled, as the Windows build hooks SDL's audio then */
bool libretroMode();
/* Virtual SDL gamepads for the core's pads; after SDL is initialized and
 * before initSdlInput, which opens the controllers it finds then */
void libretroAttachPads();
/* The core's input as SDL gamepad, mouse and key state; in pollEvents */
void libretroPollInput();
/* At the game's buffer swap: the frame to the core, then its next run */
void libretroFrame();

#endif
