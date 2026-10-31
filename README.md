# linuxloader libretro core

A libretro core for Sega Lindbergh games, built on [Linux Loader](https://github.com/lindbergh-loader/linuxloader) (formerly Lindbergh Loader). It runs the games in RetroArch on Linux x86-64, Linux aarch64 and Windows x86-64.

## Installing

The core is the one file to install: `linuxloader_libretro.so` (Linux) or `linuxloader_libretro.dll` (Windows) goes in RetroArch's `cores/`, its `.info` in `info/`. The core carries the loader and writes it to RetroArch's `system/linuxloader/` at load, when that folder is missing or holds another build's (its `build` file names the build), so the first load after an update takes a moment longer.

## Content

Content is the game's ELF from its dump (for example `disk0/elf/` or the game's program folder), or a `.lindbergh` text file holding the ELF's path, absolute or relative to the text file. Most games' programs have no extension (Primeval Hunt's is `game.exe`), so RetroArch's file browser lists them only with Settings > User Interface > File Browser > Filter Unknown Extensions turned off (NNshi). The game's folders stay as the dump has them: a game opens its data relative to its program's folder (House of the Dead 4 expects `../fs/`). The game's `linuxloader.ini` and `controls.ini` in its folder work as they do standalone (`linuxloader --create config` writes a default one); the core has the loader keep a game's settings in RetroArch's `saves/Lindbergh/`, by the game's DVP (`DVP-0015.eeprom.bin` and `.sram.bin`), so a game's test program and the game share them; the `eeprom.bin` and `sram.bin` in the game's folder are copied there the first time. Which games run, and which ELF each one starts from, is in the loader's [Linux](docs/guide-linux.md) and [Windows](docs/guide-windows.md) guides.

### Game folders

Some dumps need their folders moved before the game runs; the log does not say so (NNshi):

| Game | What it takes |
|---|---|
| After Burner Climax | As dumped (`abc` and `game` on `disk0`, `rom` and `shader` on `disk1`), or with all four on `disk1` as the loader has them. Debug > ABURNER_MOTION_OFF (on by default) sets motion off in the EEPROM, without which it does not start on a Deluxe cabinet; it takes effect from the second start, once the game has written its EEPROM. Rev. B (DVP-0009B) does not start. |
| The House of the Dead 4 | The `hod4data` folder as dumped, or renamed to `fs` as the loader has it. |

### Known quirks

- Virtua Fighter 5 renders at most 2880x1620 (above it the loader on its own shows a black screen): at Resolution 3840x2160 or 5120x2880 the core runs it at 2880x1620 (NNshi).
- After Burner Climax shows an error on its first start, while its EEPROM is new: close it and start it again (NNshi).
- Gun games: with Gun Aim on Analog Stick the pad's L, L2, R and R2 (LB, LT, RB, RT) are the gun's buttons; on Mouse the mouse's left, right and middle buttons are, and shooting off the picture reloads. Which button does what per game is under Gun Aim below.

On Windows with an NVIDIA GPU, a program profile in the NVIDIA Control Panel for `system/linuxloader/linuxloader.exe` with Anisotropic Filtering at 16x sharpens the games' textures; forcing MSAA or MFAA there changes nothing (NNshi).

## Core options

Grouped into Video, Input, Audio and Debug, each with a tooltip; Steering Inner Zone and Steering Effect show only with Steering Response on logarithmic.

| Option | Values | Takes effect |
|---|---|---|
| Resolution | native (the `linuxloader.ini`'s), 1280x720, 1920x1080, 2560x1440, 3840x2160, 5120x2880: the size the games the loader can patch render at; every frame goes through system memory to RetroArch, so the highest ones cost speed; above the display's resolution some games break or do not start (NNshi) | at the next start of the content |
| Force 16:9 | disabled, enabled: every game in widescreen (the loader's `KEEP_ASPECT_RATIO` off); may break or stretch some games | at the next start of the content |
| Service and Test Buttons | enabled, disabled: L3 as the cabinet's Service button and R3 as its Test button on both pads; disabled keeps them from being pressed by accident, the keyboard's S and T stay | on the fly |
| Gun Aim | stick, mouse: what aims in the gun games for both players. Analog Stick: each player's left stick or D-pad moves the aim, R2 fires, R reloads, L2 is the gun's button, L shakes the gun in The House of the Dead 4; Too Spicy: L2 reloads, L and R are the pedals; Ghost Squad Evolution: L2 Action, L Change; Primeval Hunt: L2 pumps. Mouse: RetroArch's light gun on each player's port, which follows the mouse; left button fires, right reloads, middle is the gun's button, off the picture reloads too; Ghost Squad Evolution: right Action, middle Change; The House of the Dead 4: right the gun's button, middle shakes; Too Spicy: middle the left pedal, right the right one; Primeval Hunt: middle pumps, the touch screen takes the clicks | on the fly |
| Gun Pointer Sensitivity | 50 to 200% (100): how fast the sticks move the aim, as fast across as up and down; at 100% a full tilt crosses the picture's height in about a second | on the fly |
| Crosshair | enabled, disabled: the gun games' crosshairs over the picture, blue for player 1, red for player 2 (shown once that player aims) | on the fly |
| Steering Response | linear, logarithmic: the left stick's X on a curve with less of the turn around the centre and steeper towards the ends, as the Sega System 32 racers' dip switch has it | on the fly |
| Steering Inner Zone | 5 to 50% (25): with logarithmic, the share of the turn at the centre | on the fly |
| Force Feedback to Rumble | enabled, disabled: the drive board's force feedback as rumble on player 1's pad in the driving games (OutRun 2 SP SDX, Initial D 4 and 5, Hummer), with the drive board emulated, which Debug > EMULATE_DRIVEBOARD's Default (per game) does for those games | on the fly |
| Steering Effect | cubic (strong), square (weak): with logarithmic, the outer part's curve | on the fly |
| Debug | every `linuxloader.ini` setting (the core's defaults differ in two: HUMMER_FLICKER_FIX on, PRIMEVAL_HUNT_SCREEN_MODE 1) the core does not otherwise cover (free play, written to the game's EEPROM at start; TEST_MODE, the content started in test mode as with the loader's `-t`, not a `linuxloader.ini` setting; board emulation, region, cabinet colour, debug messages, the game-specific and graphics switches, crosshairs, network patches), each Default first, which leaves the game's own or the `linuxloader.ini`'s; one set goes over the file's | at the next start of the content |
| Audio Thread | enabled, disabled: the sound handed to RetroArch's audio thread as the loader mixes it, which keeps its pace where a game drops frames; on by default, though it loses OutRun 2 SP's announcer | at the next start of the content |

## Controls

Without a `controls.ini`, the loader's defaults:

- Pads: RetroArch's ports 1 and 2 are players 1 and 2, port 2 with the same buttons as port 1 in the button games (Virtua Fighter 5: Punch, Kick, Guard; Virtua Tennis 3: Top Spin, Slice, and Button 3 as Lob, both together, as the game has no third button); Start is start, Select a coin, L3 service and R3 test (unless Service and Test Buttons is off). The machine's own controllers are left to RetroArch, and RetroArch's analog dead zone applies (the loader adds none of its own; a `controls.ini` can still set one).
- Keyboard: RetroArch's keyboard is the game's: 5 and 6 coin, 1 and 2 start, T test, S service, the arrows.
- Test menu: Test (R3 or T) during play opens it, as on a cabinet. A game that quits for it (most do: Rambo, Virtua Fighter 5, After Burner Climax) is started again in test mode, as the loader's `-t` does; a game with a separate test program runs that instead, which the core ends the game for after 5 s if it does not quit: OutRun 2 SP SDX's JenTest (in a JenTest folder beside Jennifer's), Virtua Tennis 3's vt3_testmode, The House of the Dead 4's hod4testM, The House of the Dead EX's hodextestR and Apache's apachetestM (beside the game), with or without `.elf`. A game that keeps running has its test menu in the game (Initial D). Leaving test mode starts the game again, and so does a game that quits by itself after its first 10 s.
- Guns: both players, by the Gun Aim option (the sticks by default). Every gun game needs its guns calibrated in the test menu (R3) before it plays right. Too Spicy's pedals are on the keyboard's left and right arrows as well.
- Touch screens: RetroArch's light gun, which follows the mouse, or its pointer while touched; the mouse's buttons click.
- RetroArch's controls list (Quick Menu > Controls) names each pad's buttons and sticks by what they do in the game: the loader's bindings for its type of game (driving, flying, the fighting and other button games), and the gun games' sticks and buttons with Gun Aim on Analog Stick. The buttons stay as they are; RetroArch's remapping rebinds them, also per game (NNshi).
- Rumble: the drive board's force feedback, which the loader turns into left and right rumble, reaches player 1's pad in the driving games (OutRun 2 SP, Initial D 4 and 5, Hummer): the core's per-game default emulates the drive board for those games, the loader alone for OutRun 2 SP only (NNshi).

A `controls.ini` maps them as standalone, since RetroArch's pads reach the game as SDL gamepads; see the [Controls Guide](docs/Controls-Guide.md).

## How it works

The games are 32-bit Linux programs, so the core does not run them itself: it starts the loader as a process of its own and exchanges the frames, the audio and the input with it through shared memory (`src/libretro/`, the loader's side in `src/loader/libretro/libretroBridge.c`). The game draws one frame per RetroArch frame and its window stays hidden. On Linux aarch64 the loader and the game run through box64 (built with BOX32, for 32-bit x86), which comes with the core. On Windows the loader's own Windows build (`linuxloader.exe` and `ll-deps/`) loads the game's ELF; its console output goes to `system/linuxloader/loader.log`.

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
