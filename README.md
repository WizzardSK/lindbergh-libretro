# Lindbergh libretro core

A libretro core for Sega Lindbergh games, built on [Linux Loader](https://github.com/lindbergh-loader/linuxloader) (formerly Lindbergh Loader). It runs the games in RetroArch on Linux x86-64, Linux aarch64 and Windows x86-64.

## Installing

The core is the one file to install: `lindbergh_libretro.so` (Linux) or `lindbergh_libretro.dll` (Windows) goes in RetroArch's `cores/`, its `.info` in `info/`. The core carries the loader and writes it to RetroArch's `system/lindbergh/` at load, when that folder is missing or holds another build's (its `build` file names the build), so the first load after an update takes a moment longer.

## Content

Content is the game's ELF from its dump (for example `disk0/elf/` or the game's program folder), or a `.lindbergh` text file holding the ELF's path, absolute or relative to the text file. The game's folders stay as the dump has them: a game opens its data relative to its program's folder (House of the Dead 4 expects `../fs/`). The game's `linuxloader.ini` and `controls.ini` in its folder work as they do standalone (`linuxloader --create config` writes a default one); the loader keeps a game's settings in `eeprom.bin` and `sram.bin` there too. Which games run, and which ELF each one starts from, is in the loader's [Linux](docs/guide-linux.md) and [Windows](docs/guide-windows.md) guides.

### Game folders

Some dumps need their folders moved before the game runs; the log does not say so (NNshi):

| Game | What it takes |
|---|---|
| After Burner Climax | The `abc` and `game` folders under `disk1` instead of `disk0`. On a cabinet set to Deluxe, motion off in the test mode. |
| The House of the Dead 4 | The `hod4data` folder renamed to `fs`. |

On Windows with an NVIDIA GPU, a program profile in the NVIDIA Control Panel for `system/lindbergh/linuxloader.exe` with Anisotropic Filtering at 16x sharpens the games' textures; forcing MSAA or MFAA there changes nothing (NNshi).

## Core options

Grouped into Video, Input, Audio and System, each with a tooltip; Steering Inner Zone and Steering Effect show only with Steering Response on logarithmic.

| Option | Values | Takes effect |
|---|---|---|
| Test Mode | disabled, enabled: the game started in test mode, as the loader's `-t` does | at the next start of the content |
| Resolution | native (the `linuxloader.ini`'s), 1280x720, 1920x1080, 2560x1440, 3840x2160, 5120x2880: the size the games the loader can patch render at; every frame goes through system memory to RetroArch, so the highest ones cost speed | at the next start of the content |
| Pointer | lightgun, mouse: what aims in the gun games, RetroArch's light gun (trigger, reload and aux A as the buttons) or its pointer (the mouse's absolute position, or touch); the mouse's buttons fire with either | on the fly |
| Free Play | default (the game's own setting), enabled, disabled: written to the game's EEPROM at start, as the `linuxloader.ini`'s `FREEPLAY` | at the next start of the content |
| Steering Response | linear, logarithmic: the left stick's X on a curve with less of the turn around the centre and steeper towards the ends, as the Sega System 32 racers' dip switch has it | on the fly |
| Steering Inner Zone | 5 to 50% (25): with logarithmic, the share of the turn at the centre | on the fly |
| Force Feedback to Rumble | enabled, disabled: the drive board's force feedback as rumble on player 1's pad (OutRun 2 SP SDX; other games only with `EMULATE_DRIVEBOARD = 1`) | on the fly |
| Steering Effect | cubic (strong), square (weak): with logarithmic, the outer part's curve | on the fly |
| Audio Thread | disabled, enabled: the sound handed to RetroArch's audio thread as the loader mixes it, which keeps its pace where a game drops frames; off by default, as it loses OutRun 2 SP's announcer | at the next start of the content |

## Controls

Without a `controls.ini`, the loader's defaults:

- Pads: RetroArch's ports 1 and 2 are players 1 and 2; Start is start, Select a coin, L3 service and R3 test. The machine's own controllers are left to RetroArch, and RetroArch's analog dead zone applies (the loader adds none of its own; a `controls.ini` can still set one).
- Keyboard: RetroArch's keyboard is the game's: 5 and 6 coin, 1 and 2 start, T test, S service, the arrows.
- Light gun or mouse: the pointer, as the Pointer option says.
- Rumble: the drive board's force feedback, which the loader turns into left and right rumble, reaches player 1's pad (OutRun 2 SP; Initial D runs without the drive board's emulation, `EMULATE_DRIVEBOARD`).

A `controls.ini` maps them as standalone, since RetroArch's pads reach the game as SDL gamepads; see the [Controls Guide](docs/Controls-Guide.md).

## How it works

The games are 32-bit Linux programs, so the core does not run them itself: it starts the loader as a process of its own and exchanges the frames, the audio and the input with it through shared memory (`src/libretro/`, the loader's side in `src/loader/libretro/libretroBridge.c`). The game draws one frame per RetroArch frame and its window stays hidden. On Linux aarch64 the loader and the game run through box64 (built with BOX32, for 32-bit x86), which comes with the core. On Windows the loader's own Windows build (`linuxloader.exe` and `ll-deps/`) loads the game's ELF; its console output goes to `system/lindbergh/loader.log`.

## Building

`.github/workflows/libretro.yml` builds the core for every platform on the `libretro` branch: the loader as the project's own AppImage and Windows builds make it, zipped into the core (`make -f Makefile.libretro SYSTEM_ZIP=... BUILD_STAMP=...`). What changed in the core is in [CHANGELOG.md](CHANGELOG.md).

## The loader

This core is a fork of Linux Loader, whose goal is to run all the SEGA Lindbergh games on Linux and Windows and, in the future, other arcade systems such as Namco N2 / ES1 and Raw Thrills. Its standalone builds, documentation and support are upstream: [lindbergh-loader/linuxloader](https://github.com/lindbergh-loader/linuxloader), its [Patreon](https://www.patreon.com/c/LinuxLoader) and the [arcade community discord](https://arcade.community). Questions about the core belong here, not with the loader's authors.

## License

<p xmlns:cc="http://creativecommons.org/ns#" xmlns:dct="http://purl.org/dc/terms/"><a property="dct:title" rel="cc:attributionURL" href="https://github.com/lindbergh-loader/">Linux Loader</a> by <a rel="cc:attributionURL dct:creator" property="cc:attributionName" href="https://github.com/lindbergh-loader/">Linux Loader Development Team</a> is licensed under <a href="https://creativecommons.org/licenses/by-nc-sa/4.0/?ref=chooser-v1" target="_blank" rel="license noopener noreferrer" style="display:inline-block;">Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International<img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/cc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/by.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/nc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/sa.svg?ref=chooser-v1" alt=""></a></p>

Our project is open source, and our primary goal is to preserve and maintain arcade machines, ensuring they continue to operate in arcades. We encourage individuals to use the information provided for their own open source projects and contribute to the development of the loader to improve it for the benefit of the community. However, we do not permit the use of any of our code in pay-to-play/subscription/commercial ventures without prior consent from the Linux Loader Development Team. If we become aware of any such use, we reserve the right to take legal action.

## References and libraries

This project has been built using the following libraries:

- [minhook](https://github.com/TsudaKageTora/minhook)
- [libxdiff](http://www.xmailserver.org/xdiff-lib.html)
- [ELFIO](https://github.com/serge1/ELFIO)
- [SDL3](https://github.com/libsdl-org/SDL)
- [glad](https://github.com/Dav1dde/glad)
- [FAudio](https://github.com/faudio/faudio)

## Thanks

This project has been built by referencing various earlier projects and would like to extend it's thanks to everyone that has contributed to the Arcade Scene.

## Takedown Notices

The Linux Loader Development Team respects intellectual property rights and is committed to ensuring that no copyrighted material is shared without proper authorization. If you believe that we are infringing on your intellectual property or have any concerns regarding our activities, please contact us in the [arcade community discord](https://arcade.community). We are more than happy to address any issues and discuss them further.
