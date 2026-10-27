# Changelog of the libretro core

What changed in the libretro core of this fork, newest first. Changes that come with a merge of upstream lindbergh-loader are in upstream's own history (`docs/changelog`).

## Unreleased

- Audio: the loader mixes by the clock. SDL's dummy device, which hands the mix to the core, waited a block's length in whole milliseconds (5 ms for 5.33), so it mixed some 6 % fast and the core cut the backlog every second or two; streamed music skipped, short effects hid it (NNshi).
- The Windows build's artifact holds `lindbergh_libretro.dll` and `lindbergh_libretro.info` side by side, with no folders, no zip inside and no `system/` (NNshi).
- New option Audio Thread (Restart), off by default: the sound goes to RetroArch's audio thread (SET_AUDIO_CALLBACK) as the loader mixes it, by the loader's clock as in the loader alone, so it keeps its pace where a game drops frames, as Initial D 4's car select at 40 fps; OutRun 2 SP's announcer is lost with it (NNshi). Where RetroArch has no audio thread to give (recording), a frame's worth each run as with it off.
- The core carries the loader and writes it to `system/lindbergh/` at load when what is there is from another build or missing, so the core is the one file to install (NNshi). Its zip is made by the build (miniz, MIT, in `src/libretro/miniz/` reads it).
- The pads' triggers (L2 and R2, gas and brake in the driving games) reach the game: the core's pads sent them from 0 at rest, where SDL reads a trigger from -32768 at rest as a pad's driver reports it, so they sat half held and the games took no gas or brake (NNshi, OutRun 2 SP, Initial D 4).
- The players are the core's pads, RetroArch's ports 1 and 2, and nothing else: on Windows the loader also saw the machine's own controllers and gave them P1 and P2 ahead of the core's, bypassing RetroArch's mapping (NNshi).
- Sticks have no dead zone of the loader's own, RetroArch's analog dead zone applies: the loader's 8000 jumped from the centre straight to a quarter turn, which steered as a dead zone and then too sharply. A `controls.ini` can still set one (NNshi).
- Without a `linuxloader.ini` the loader no longer logs a garbled file name on Windows; it read an uninitialised path and used the defaults anyway.
- New option Resolution (Restart): native (linuxloader.ini's), 1280x720, 1920x1080, 2560x1440, 3840x2160 or 5120x2880, the width and height the games the loader can patch render at; frames up to 5120x2880 (NNshi).
- Audio: one frame's worth each run out of a small cushion, and SDL in the loader mixes in smaller blocks: the sound was scratchy, with underruns (NNshi).
- Input: the core's pads are there before the loader looks for controllers, and SDL takes their input although the game's window stays hidden; without a `controls.ini` Start is start, Select a coin, L3 service and R3 test, and RetroArch's keyboard reaches the game (NNshi).
- Windows: the core starts `linuxloader.exe`, which loads the game's ELF itself; shared memory through a named file mapping and events, the audio through a hook on SDL's stream.
- The libretro core: the game runs in the loader as a process of its own, with frames, audio and input exchanged through shared memory; Linux x86-64, Linux aarch64 through box64.
