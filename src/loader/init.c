#include <stdio.h>
#include <string.h>

#include "config/config.h"

#include "hardware/lindbergh/cardReader.h"
#include "hardware/lindbergh/baseBoard.h"
#include "hardware/lindbergh/driveBoard.h"
#include "hardware/lindbergh/eeprom.h"
#include "hardware/lindbergh/forceFeedback.h"
#include "graphics/fpsLimiter.h"
#include "graphics/gpuVendor.h"
#include "hardware/lindbergh/jvs.h"
#include "patching/patch.h"
#include "patching/patchResolution.h"
#include "hardware/lindbergh/rideBoard.h"
#include "graphics/sdlCalls.h"
#include "input/sdlInput.h"
#include "hardware/lindbergh/securityBoard.h"
#include "config/config.h"
#include "../minhook/include/MinHook.h"
#include "log/log.h"
#include "libretro/libretroBridge.h"

#if defined(__linux__)
#include "input/evdevInput.h"
#endif

uint32_t gId = 0;
int gGrp = -1;

int gWidth;
int gHeight;

extern FpsLimit fpsLimit;
extern SDLControllers sdlJoysticks;
extern char *configFolder;

#ifdef __linux__
Controllers controllers = {0};
#endif

void initMain(char *configPath, char *controlsPath)
{
#ifdef __linux__
    configPath = getenv("LINUX_LOADER_CONFIG_PATH");
    if (configPath == NULL || configPath[0] == '\0')
        configPath = "";
    controlsPath = getenv("LINUX_LOADER_CONTROLS_PATH");
    if (controlsPath == NULL || controlsPath[0] == '\0')
        controlsPath = "";

#endif
    initConfig(configPath);

    /* the libretro core's Resolution option, over linuxloader.ini's */
    const char *resolution = getenv("LINDBERGH_LIBRETRO_RESOLUTION");
    int resolutionW, resolutionH;
    if (resolution && sscanf(resolution, "%dx%d", &resolutionW, &resolutionH) == 2 && resolutionW > 0 && resolutionH > 0)
    {
        getConfig()->width = resolutionW;
        getConfig()->height = resolutionH;
        /* Virtua Fighter 5 renders nothing above 2880x1620: black at
         * 3840x2160 and 5120x2880 (NNshi) */
        if (getConfig()->gameGroup == GROUP_VF5 && (resolutionW > 2880 || resolutionH > 1620))
        {
            getConfig()->width = 2880;
            getConfig()->height = 1620;
            log_info("libretro: Virtua Fighter 5 at 2880x1620, its highest, in place of %dx%d\n", resolutionW, resolutionH);
        }
    }

    /* the libretro core's Debug options, over linuxloader.ini's */
    const char *debugIni = getenv("LINDBERGH_LIBRETRO_INI");
    if (debugIni && *debugIni)
        applyIniLines(debugIni);

    /* the libretro core's saves/Lindbergh: the game's EEPROM and SRAM there,
     * named by its DVP, so a game's test program and the game itself
     * (JenTest and Jennifer, both DVP-0015) share them (NNshi). A path the
     * linuxloader.ini sets stays; the files in the game's folder are copied
     * there the first time, so nothing set so far is lost. */
    const char *saves = getenv("LINDBERGH_LIBRETRO_SAVES");
    if (saves && *saves && strcmp(getConfig()->gameDVP, "DVP-XXXX") != 0)
    {
        char *paths[2] = {getConfig()->eepromPath, getConfig()->sramPath};
        const char *defaults[2] = {"eeprom.bin", "sram.bin"};
        const char *kinds[2] = {"eeprom", "sram"};
        for (int i = 0; i < 2; i++)
        {
            if (strcmp(paths[i], defaults[i]) != 0)
                continue;
            char target[MAX_PATH_LENGTH];
            snprintf(target, sizeof(target), "%s/%s.%s.bin", saves, getConfig()->gameDVP, kinds[i]);
            FILE *have = fopen(target, "rb");
            if (have)
                fclose(have);
            else
            {
                FILE *from = fopen(defaults[i], "rb");
                if (from)
                {
                    FILE *to = fopen(target, "wb");
                    char buf[4096];
                    size_t n;
                    while (to && (n = fread(buf, 1, sizeof(buf), from)) > 0)
                        fwrite(buf, 1, n, to);
                    if (to)
                        fclose(to);
                    fclose(from);
                    log_info("libretro: %s copied to %s\n", defaults[i], target);
                }
            }
            snprintf(paths[i], MAX_PATH_LENGTH, "%s", target);
        }
        log_info("libretro: EEPROM %s, SRAM %s\n", getConfig()->eepromPath, getConfig()->sramPath);
    }

    gId = getConfig()->crc32;
    gGrp = getConfig()->gameGroup;
    gWidth = getConfig()->width;
    gHeight = getConfig()->height;

    initFpsLimiter();

    getConfig()->GPUVendor = getGPUVendorID();

    if (getConfig()->GPUVendor == ERROR_GPU)
    {
        log_error("Failed to detect GPU\nUsing default values\n");
        getConfig()->GPUVendor = UNKNOWN_GPU;
    }

    if (MH_Initialize() != MH_OK)
    {
        log_error("Failed to initialize MinHook");
        exit(1);
    }

    // The libretro core's shared memory, and on Windows its audio hook
    libretroMode();

    if (initEeprom() != 0)
        exit(1);
    log_info("EEPROM initialized");

    if (initPatch() != 0)
        exit(1);
    log_info("Patches initialized");

    if (initResolutionPatches() != 0)
        exit(1);
    log_info("Resolution patches initialized");

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        log_error("Failed to enable hooks");
        exit(1);
    }

    if (initSDL() != 0)
        exit(1);
    log_info("SDL initialized");

    if (initBaseboard() != 0)
        exit(1);
    log_info("Baseboard initialized");

    if (initJVS() != 0)
        exit(1);
    log_info("JVS initialized");

    if (initSecurityBoard() != 0)
        exit(1);
    log_info("Security board initialized");

    if (getConfig()->emulateDriveboard)
    {
        if (initDriveboard() != 0)
            exit(1);
    }
    log_info("Driveboard initialized");

    if (getConfig()->emulateRideboard)
    {
        if (initRideboard() != 0)
            exit(1);
    }
    log_info("Rideboard initialized");

    if (getConfig()->emulateHW210CardReader)
    {
        if (initCardReader() != 0)
            exit(1);
    }
    log_info("Card reader initialized");

    // The libretro core's pads, before the input looks for controllers
    libretroAttachPads();

    if (initSdlInput(controlsPath) != 0)
        exit(1);
    log_info("SDL input initialized");
    // what the core's pads do, by the bindings, for RetroArch's controls list
    libretroPublishPadLabels();

#if defined(__linux__)
    if (initEvdevControllers(&controllers) != 0)
        exit(1);
    log_info("Evdev controllers initialized");
#endif

    // EVDEV mode: initSdlInput() skips sdlFfbInit(), so init FFB here
    if (getConfig()->inputMode == 2)
        sdlFfbInit();

    securityBoardSetDipResolution(getConfig()->width, getConfig()->height);
    log_info("Security board dip resolution set");

    printf("\nLinux Loader\nBy the Linux Loader Development Team 2026\n\n");
    printf("  GAME:        %s\n", getGameName());
    printf("  GAME ID:     %s\n", getGameId());
    printf("  DVP:         %s\n", getDvpName());
    printf("  GPU VENDOR:  %s\n", getConfig()->GPUVendorString);

#if defined(__linux__)
    for (int i = 0; i < controllers.count; i++)
    {
        if (controllers.controller[i].inUse)
        {
            printf("  CONTROLLER: %s\n", controllers.controller[i].name);
        }
    }
    printf("\n");
#endif

    for (int i = 0; i < MAX_JOYSTICKS; i++)
    {
        SDL_Joystick *joy = NULL;
        if (sdlJoysticks.controllers[i])
            joy = SDL_GetGamepadJoystick(sdlJoysticks.controllers[i]);
        else if (sdlJoysticks.joysticks[i])
            joy = sdlJoysticks.joysticks[i];

        if (joy != NULL)
            printf("  SDL CONTROLLER %d: %s\n", i, SDL_GetJoystickName(joy));
    }
    printf("\n");
}