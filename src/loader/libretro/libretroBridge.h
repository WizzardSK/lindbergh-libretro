#ifndef LIBRETRO_BRIDGE_H
#define LIBRETRO_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

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
/* The drive board's force feedback to the core's rumble, 0..1 each */
void libretroRumble(float left, float right, int duration_ms);
// The games that play through ALSA (alsa2sdl.c) open their SDL device
// directly, not by SDL_OpenAudioDeviceStream as FAudio does
void libretroAttachAudioDevice(uint32_t dev);
/* How far the game's clocks are behind the real ones, in microseconds, for
 * RetroArch's pauses (libretroFrame) */
int64_t libretroPausedUs(void);

#endif
