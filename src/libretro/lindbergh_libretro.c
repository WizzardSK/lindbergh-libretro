/*
 * The libretro core: a SEGA Lindbergh game run by the loader as a process
 * of its own (the games are 32-bit Linux programs, the frontend is not),
 * handed its input and taking its frames and audio through shared memory
 * (libretro_shm.h, the loader's side in src/loader/libretro/libretroBridge.c).
 *
 * The loader comes from <system>/lindbergh/. On Linux that is the loader's
 * AppDir, the launcher, linuxloader.so and the 32-bit libraries the games
 * need, run through its AppRun as standalone runs it. On Windows it is
 * linuxloader.exe and its ll-deps/, as in the loader's Windows release.
 * Content is the game's ELF, or a .lindbergh text file naming it.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif

#include "libretro.h"
#include "libretro_ipc.h"
#include "libretro_shm.h"
#ifdef LR_EMBEDDED_SYSTEM
#include "miniz/miniz.h"
extern const unsigned char lr_system_zip[];
extern const unsigned char lr_system_zip_end[];
#endif

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static LrShm *shm;
/* The shared memory as the audio callback sees it, on RetroArch's audio
 * thread: taken away (audio_release_shm) before it is unmapped */
static LrShm *audio_shm;
static int audio_busy;
/* Whether RetroArch calls the audio callback (SET_AUDIO_CALLBACK) */
static bool audio_async;
static char shm_name[64];
static LrEvent run_event, frame_event;
#ifdef _WIN32
static HANDLE shm_mapping;
static HANDLE loader; /* the loader's process */
#else
static pid_t loader;
#endif
static uint32_t last_frame;
static unsigned frame_w = 640, frame_h = 480;
static bool test_mode;
/* The Audio Thread option: audio on RetroArch's audio thread */
static bool audio_thread;
/* The game of the content and where the loader runs it from */
static char s_elf[PATH_MAX], s_dir[PATH_MAX], s_root[PATH_MAX];
/* RetroArch's saves/Lindbergh, where the loader keeps each game's EEPROM and
 * SRAM by its DVP (init.c); empty without a saves directory */
static char s_saves[PATH_MAX];
static bool pointer_lightgun = true;
/* The Steering Response option: the left stick's X on a curve, with its
 * Inner Zone (the share of the turn at the centre) and Effect (the power of
 * the outer part, 2 or 3) */
static bool steering_log;
/* Input > Force Feedback to Rumble */
static bool ffb_rumble = true;
static float steering_inner = 0.25f;
static int steering_power = 3;
/* WIDTHxHEIGHT for the loader, or "native" for linuxloader.ini's */
static char resolution[16] = "native";
/* The Free Play option: "1" or "0" for the loader, NULL for the game's own */
static const char *freeplay;
/* RetroArch's rumble, for the drive board's force feedback */
static struct retro_rumble_interface rumble;
/* The last force feedback taken, and the frames it has still to run */
static uint32_t rumble_seq;
static unsigned rumble_frames;
static int16_t *audio_out;
static double sample_rate = 48000.0;

static void lr_log(enum retro_log_level level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (log_cb)
        log_cb(level, "[Lindbergh] %s", buf);
    else
        fprintf(stderr, "[Lindbergh] %s", buf);
}

static const char *get_option(const char *key, const char *fallback)
{
    struct retro_variable var = {key, NULL};
    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        return var.value;
    return fallback;
}

static bool update_option_display(void);

static void read_options(void)
{
    test_mode = !strcmp(get_option("lindbergh_test_mode", "disabled"), "enabled");
    audio_thread = !strcmp(get_option("lindbergh_audio_thread", "disabled"), "enabled");
    pointer_lightgun = !strcmp(get_option("lindbergh_pointer", "lightgun"), "lightgun");
    steering_log = !strcmp(get_option("lindbergh_steering", "linear"), "logarithmic");
    ffb_rumble = strcmp(get_option("lindbergh_ffb_rumble", "enabled"), "disabled") != 0;
    steering_inner = atoi(get_option("lindbergh_steering_inner", "25")) / 100.0f;
    if (steering_inner < 0.05f || steering_inner > 0.5f)
        steering_inner = 0.25f;
    steering_power = !strcmp(get_option("lindbergh_steering_effect", "cubic"), "square") ? 2 : 3;
    if (environ_cb)
        update_option_display();
    snprintf(resolution, sizeof(resolution), "%s", get_option("lindbergh_resolution", "native"));
    const char *fp = get_option("lindbergh_freeplay", "default");
    freeplay = !strcmp(fp, "enabled") ? "1" : !strcmp(fp, "disabled") ? "0" : NULL;
}

/* The core options, with their tooltips (RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2;
 * a frontend without it gets them as plain variables, built from these) */
static struct retro_core_option_v2_category option_categories[] = {
    {"video", "Video", "The resolution the games render at."},
    {"input", "Input", "The pointer, steering and force feedback."},
    {"audio", "Audio", "How the sound reaches RetroArch."},
    {"system", "System", "Test mode and free play."},
    {NULL, NULL, NULL},
};

static struct retro_core_option_v2_definition option_definitions[] = {
    {"lindbergh_resolution", "Resolution (Restart)", NULL,
     "The width and height the games the loader can patch render at. Native keeps the linuxloader.ini's. Every frame goes through system memory to RetroArch, so the highest ones cost speed.",
     NULL, "video",
     {{"native", "Native"}, {"1280x720", NULL}, {"1920x1080", NULL}, {"2560x1440", NULL}, {"3840x2160", NULL}, {"5120x2880", NULL}, {NULL, NULL}},
     "native"},
    {"lindbergh_pointer", "Pointer", NULL,
     "What aims in the gun games. Light Gun: RetroArch's light gun, its trigger, reload and aux A as the buttons. Mouse: RetroArch's pointer, the mouse's position or touch. The mouse's buttons fire with either.",
     NULL, "input",
     {{"lightgun", "Light Gun"}, {"mouse", "Mouse"}, {NULL, NULL}},
     "lightgun"},
    {"lindbergh_ffb_rumble", "Force Feedback to Rumble", NULL,
     "The drive board's force feedback as rumble on player 1's pad. Supported: OutRun 2 SP SDX (all revisions), the game the loader emulates the drive board for. Others only with EMULATE_DRIVEBOARD = 1 in their linuxloader.ini, if they drive that board (Initial D does not).",
     NULL, "input",
     {{"enabled", "Enabled"}, {"disabled", "Disabled"}, {NULL, NULL}},
     "enabled"},
    {"lindbergh_steering", "Steering Response", NULL,
     "Linear: the left stick's X as it is. Logarithmic: on a curve with less of the turn around the centre and steeper towards the ends, as the Sega System 32 racers' dip switch has it, so a stick steers less twitchily. Its shape is set by Steering Inner Zone and Steering Effect.",
     NULL, "input",
     {{"linear", "Linear"}, {"logarithmic", "Logarithmic"}, {NULL, NULL}},
     "linear"},
    {"lindbergh_steering_inner", "Steering Inner Zone", NULL,
     "With logarithmic steering: the share of the turn at the centre. Lower is calmer around the centre. OutRun 2 plays well at 20 with Square, Initial D at 50 with Cubic.",
     NULL, "input",
     {{"5", "5%"}, {"10", "10%"}, {"15", "15%"}, {"20", "20%"}, {"25", "25%"}, {"30", "30%"}, {"35", "35%"}, {"40", "40%"}, {"45", "45%"}, {"50", "50%"}, {NULL, NULL}},
     "25"},
    {"lindbergh_steering_effect", "Steering Effect", NULL,
     "With logarithmic steering: how the rest of the turn rises towards full lock. Cubic is strong, Square weak.",
     NULL, "input",
     {{"cubic", "Cubic (Strong)"}, {"square", "Square (Weak)"}, {NULL, NULL}},
     "cubic"},
    {"lindbergh_audio_thread", "Audio Thread (Restart)", NULL,
     "Hands the sound to RetroArch's audio thread as the loader mixes it, which keeps its pace where a game drops frames. Off by default: OutRun 2 SP's announcer is lost with it.",
     NULL, "audio",
     {{"disabled", "Disabled"}, {"enabled", "Enabled"}, {NULL, NULL}},
     "disabled"},
    {"lindbergh_test_mode", "Test Mode (Restart)", NULL,
     "Starts the game in test mode, as the loader's -t does.",
     NULL, "system",
     {{"disabled", "Disabled"}, {"enabled", "Enabled"}, {NULL, NULL}},
     "disabled"},
    {"lindbergh_freeplay", "Free Play (Restart)", NULL,
     "Default leaves the game's own setting. Enabled or Disabled writes it to the game's EEPROM at start, as the linuxloader.ini's FREEPLAY does.",
     NULL, "system",
     {{"default", "Default"}, {"enabled", "Enabled"}, {"disabled", "Disabled"}, {NULL, NULL}},
     "default"},
    {NULL, NULL, NULL, NULL, NULL, NULL, {{NULL, NULL}}, NULL},
};

/* Steering Inner Zone and Effect only with the curve on: shown or hidden as
 * the option changes in the menu, not only at its next opening (NNshi) */
static bool update_option_display(void)
{
    static int shown = -1;
    const bool log = !strcmp(get_option("lindbergh_steering", "linear"), "logarithmic");
    if (shown == (int)log)
        return false;
    shown = log;
    static const char *const curve_options[] = {"lindbergh_steering_inner", "lindbergh_steering_effect"};
    for (unsigned i = 0; i < 2; i++)
    {
        struct retro_core_option_display display = {curve_options[i], log};
        environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &display);
    }
    return true;
}

static void set_core_options(retro_environment_t cb)
{
    unsigned version = 0;
    if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2)
    {
        struct retro_core_options_v2 options = {option_categories, option_definitions};
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options);
        struct retro_core_options_update_display_callback display = {update_option_display};
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &display);
        return;
    }
    /* "Label; default|other|..." for each, the default first */
    enum { COUNT = sizeof(option_definitions) / sizeof(option_definitions[0]) };
    static struct retro_variable vars[COUNT];
    static char text[COUNT][512];
    for (unsigned i = 0; option_definitions[i].key; i++)
    {
        const struct retro_core_option_v2_definition *d = &option_definitions[i];
        int n = snprintf(text[i], sizeof(text[i]), "%s; %s", d->desc, d->default_value);
        for (unsigned v = 0; d->values[v].value && n > 0 && n < (int)sizeof(text[i]); v++)
            if (strcmp(d->values[v].value, d->default_value))
                n += snprintf(text[i] + n, sizeof(text[i]) - n, "|%s", d->values[v].value);
        vars[i].key = d->key;
        vars[i].value = text[i];
    }
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, vars);
}

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;
    set_core_options(cb);
    bool no_game = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    struct retro_log_callback logging;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        log_cb = logging.log;
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void)
{
    audio_out = malloc(LR_AUDIO_FRAMES * 2 * sizeof(int16_t));
}

void retro_deinit(void)
{
    free(audio_out);
    audio_out = NULL;
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "Lindbergh";
    info->library_version = "0.1";
    info->valid_extensions = "elf|lindbergh";
    info->need_fullpath = true;
    info->block_extract = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width = frame_w;
    info->geometry.base_height = frame_h;
    info->geometry.max_width = LR_MAX_WIDTH;
    info->geometry.max_height = LR_MAX_HEIGHT;
    info->geometry.aspect_ratio = (float)frame_w / (float)frame_h;
    info->timing.fps = 60.0;
    info->timing.sample_rate = sample_rate;
}

void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }

void retro_reset(void) {}

/* A .lindbergh file holds the ELF's path, relative to the file's folder */
/* The last folder separator of a path, or NULL; Windows has both kinds */
static char *last_separator(char *path)
{
    char *slash = strrchr(path, '/');
#ifdef _WIN32
    char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
#endif
    return slash;
}

static bool is_absolute(const char *path)
{
#ifdef _WIN32
    if (path[0] && path[1] == ':')
        return true;
    if (path[0] == '\\')
        return true;
#endif
    return path[0] == '/';
}

static bool game_path(const char *content, char *out, size_t size)
{
    size_t n = strlen(content);
    if (n > 10 && !strcasecmp(content + n - 10, ".lindbergh"))
    {
        FILE *f = fopen(content, "r");
        char line[PATH_MAX] = "";
        if (!f || !fgets(line, sizeof(line), f))
        {
            if (f)
                fclose(f);
            return false;
        }
        fclose(f);
        line[strcspn(line, "\r\n")] = 0;
        if (is_absolute(line))
            snprintf(out, size, "%s", line);
        else
        {
            char dir[PATH_MAX];
            snprintf(dir, sizeof(dir), "%s", content);
            char *slash = last_separator(dir);
            if (slash)
                *slash = 0;
            snprintf(out, size, "%s/%s", slash ? dir : ".", line);
        }
        return true;
    }
    snprintf(out, size, "%s", content);
    return true;
}

static void audio_release_shm(void);
static void audio_callback(void);
static void audio_set_state(bool enabled);

static void close_shm(void)
{
    audio_release_shm();
#ifdef _WIN32
    if (shm)
        UnmapViewOfFile(shm);
    if (shm_mapping)
        CloseHandle(shm_mapping);
    if (run_event)
        CloseHandle(run_event);
    if (frame_event)
        CloseHandle(frame_event);
    shm_mapping = run_event = frame_event = NULL;
    shm = NULL;
    shm_name[0] = 0;
#else
    if (shm)
    {
        munmap(shm, sizeof(LrShm));
        shm = NULL;
    }
    if (shm_name[0])
    {
        shm_unlink(shm_name);
        shm_name[0] = 0;
    }
#endif
}

/* The shared memory, zeroed but for its header; on Windows with the two
 * events beside it */
static bool create_shm(void)
{
#ifdef _WIN32
    snprintf(shm_name, sizeof(shm_name), "Local\\lindbergh-libretro-%lu", GetCurrentProcessId());
    char event_name[96];
    snprintf(event_name, sizeof(event_name), "%s" LR_EVENT_RUN, shm_name);
    run_event = CreateEventA(NULL, FALSE, FALSE, event_name);
    snprintf(event_name, sizeof(event_name), "%s" LR_EVENT_FRAME, shm_name);
    frame_event = CreateEventA(NULL, FALSE, FALSE, event_name);
    shm_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(LrShm), shm_name);
    if (!run_event || !frame_event || !shm_mapping)
    {
        lr_log(RETRO_LOG_ERROR, "cannot create the shared memory: error %lu\n", GetLastError());
        close_shm();
        return false;
    }
    shm = MapViewOfFile(shm_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LrShm));
    if (!shm)
    {
        close_shm();
        return false;
    }
#else
    snprintf(shm_name, sizeof(shm_name), "/lindbergh-libretro-%d", (int)getpid());
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0 || ftruncate(fd, sizeof(LrShm)) != 0)
    {
        lr_log(RETRO_LOG_ERROR, "cannot create the shared memory: %s\n", strerror(errno));
        if (fd >= 0)
            close(fd);
        close_shm();
        return false;
    }
    shm = mmap(NULL, sizeof(LrShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (shm == MAP_FAILED)
    {
        shm = NULL;
        close_shm();
        return false;
    }
#endif
    memset(shm, 0, sizeof(LrShm) - sizeof(shm->pixels));
    __atomic_store_n(&audio_shm, shm, __ATOMIC_RELEASE);
    shm->magic = LR_SHM_MAGIC;
    shm->version = LR_SHM_VERSION;
    return true;
}

/* Whether the loader is gone (and then forgotten) */
static bool loader_exited(void)
{
#ifdef _WIN32
    if (loader && WaitForSingleObject(loader, 0) == WAIT_OBJECT_0)
    {
        CloseHandle(loader);
        loader = NULL;
        return true;
    }
#else
    if (loader > 0 && waitpid(loader, NULL, WNOHANG) == loader)
    {
        loader = 0;
        return true;
    }
#endif
    return false;
}

static void stop_loader(void)
{
    if (shm)
    {
        shm->quit = 1;
        shm->run_seq++;
        lrWake(run_event, &shm->run_seq);
    }
#ifdef _WIN32
    if (loader)
    {
        /* the game exits at its next frame; else it is ended */
        if (WaitForSingleObject(loader, 1000) != WAIT_OBJECT_0)
        {
            TerminateProcess(loader, 1);
            WaitForSingleObject(loader, 1000);
        }
        CloseHandle(loader);
        loader = NULL;
    }
#else
    if (loader > 0)
    {
        /* the game exits at its next frame; then a signal to its group */
        for (int i = 0; i < 50 && !loader_exited(); i++)
            usleep(20000);
        if (loader > 0)
        {
            kill(-loader, SIGTERM);
            usleep(200000);
            kill(-loader, SIGKILL);
            waitpid(loader, NULL, 0);
            loader = 0;
        }
    }
#endif
    close_shm();
}

/* The SDL_Scancode of a RetroArch key, for the keys the loader's controls
 * use (its defaults: 5 and 6 coins, 1 and 2 start, T test, S service, the
 * arrows); 0 for the rest. Escape is RetroArch's own. */
static uint32_t sdl_scancode(unsigned key)
{
    if (key >= RETROK_a && key <= RETROK_z)
        return 4 + (key - RETROK_a);
    if (key >= RETROK_1 && key <= RETROK_9)
        return 30 + (key - RETROK_1);
    if (key >= RETROK_F1 && key <= RETROK_F12)
        return 58 + (key - RETROK_F1);
    switch (key)
    {
    case RETROK_0: return 39;
    case RETROK_RETURN: return 40;
    case RETROK_BACKSPACE: return 42;
    case RETROK_TAB: return 43;
    case RETROK_SPACE: return 44;
    case RETROK_MINUS: return 45;
    case RETROK_EQUALS: return 46;
    case RETROK_RIGHT: return 79;
    case RETROK_LEFT: return 80;
    case RETROK_DOWN: return 81;
    case RETROK_UP: return 82;
    case RETROK_LCTRL: return 224;
    case RETROK_LSHIFT: return 225;
    case RETROK_LALT: return 226;
    case RETROK_RCTRL: return 228;
    case RETROK_RSHIFT: return 229;
    case RETROK_RALT: return 230;
    default: return 0;
    }
}

/* RetroArch's keyboard into the loader's key queue, which it turns into
 * SDL key events (libretroBridge.c) */
static void keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers)
{
    (void)character; (void)key_modifiers;
    uint32_t scancode = sdl_scancode(keycode);
    if (!shm || !scancode)
        return;
    uint32_t head = shm->key_head;
    if (head - shm->key_tail >= LR_KEY_QUEUE)
        return;
    shm->keys[head % LR_KEY_QUEUE] = (LrKey){scancode, down};
    __atomic_store_n(&shm->key_head, head + 1, __ATOMIC_RELEASE);
}

/* Every directory of a path down to its last separator */
static void make_dirs(char *path)
{
    for (char *p = path + 1; *p; p++)
    {
        if (*p != '/' && *p != '\\')
            continue;
        char c = *p;
        *p = 0;
#ifdef _WIN32
        CreateDirectoryA(path, NULL);
#else
        mkdir(path, 0755);
#endif
        *p = c;
    }
}

/* The loader this core carries (the build's system/lindbergh, as a zip)
 * written to root when what is there is from another build or missing, as
 * its build file says: one file to install, the core (NNshi). Without one
 * carried, root holds the loader as it was put there. */
static bool install_loader(const char *root)
{
#ifndef LR_EMBEDDED_SYSTEM
    (void)root;
    return true;
#else
    char stamp_path[PATH_MAX], have[64] = "";
    snprintf(stamp_path, sizeof(stamp_path), "%s/build", root);
    FILE *f = fopen(stamp_path, "rb");
    if (f)
    {
        if (!fgets(have, sizeof(have), f))
            have[0] = 0;
        fclose(f);
        have[strcspn(have, "\r\n")] = 0;
    }
    if (!strcmp(have, LR_BUILD_STAMP))
        return true;

    lr_log(RETRO_LOG_INFO, "writing the loader of build %s to %s (there: %s)\n", LR_BUILD_STAMP, root,
           have[0] ? have : "none");
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, lr_system_zip, (size_t)(lr_system_zip_end - lr_system_zip), 0))
    {
        lr_log(RETRO_LOG_ERROR, "the loader the core carries cannot be read\n");
        return false;
    }
    bool ok = true;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count && ok; i++)
    {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
        {
            ok = false;
            break;
        }
        char path[PATH_MAX * 2];
        snprintf(path, sizeof(path), "%s/%s", root, st.m_filename);
        if (!strcmp(st.m_filename, "build"))
            continue; /* written last, once all the rest is there */
        make_dirs(path);
        if (st.m_is_directory)
            continue;
#ifndef _WIN32
        const mode_t mode = (mode_t)(st.m_external_attr >> 16);
        if (S_ISLNK(mode))
        {
            char target[PATH_MAX];
            const size_t n = (size_t)st.m_uncomp_size < sizeof(target) - 1 ? (size_t)st.m_uncomp_size : sizeof(target) - 1;
            ok = mz_zip_reader_extract_to_mem(&zip, i, target, n, 0);
            target[n] = 0;
            unlink(path);
            ok = ok && symlink(target, path) == 0;
            continue;
        }
        unlink(path);
#else
        DeleteFileA(path);
#endif
        ok = mz_zip_reader_extract_to_file(&zip, i, path, 0);
#ifndef _WIN32
        if (ok && (mode & 0777))
            chmod(path, mode & 0777);
#endif
        if (!ok)
            lr_log(RETRO_LOG_ERROR, "cannot write %s\n", path);
    }
    mz_zip_reader_end(&zip);
    if (!ok)
        return false;
    f = fopen(stamp_path, "wb");
    if (f)
    {
        fprintf(f, "%s\n", LR_BUILD_STAMP);
        fclose(f);
    }
    return true;
#endif
}

/* The loader started on the game of the content */
static bool start_loader(void)
{
    const char *elf = s_elf, *dir = s_dir, *root = s_root;
#ifdef _WIN32
    char exe[PATH_MAX];
    snprintf(exe, sizeof(exe), "%s\\linuxloader.exe", root);
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES)
    {
        lr_log(RETRO_LOG_ERROR, "the loader is not in %s (linuxloader.exe missing)\n", root);
        struct retro_message msg = {"Lindbergh: the loader is missing from system/lindbergh", 600};
        environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
        return false;
    }
    if (!create_shm())
        return false;
    last_frame = 0;

    /* The loader's console output goes to system/lindbergh/loader.log */
    char log_path[PATH_MAX];
    snprintf(log_path, sizeof(log_path), "%s\\loader.log", root);
    SECURITY_ATTRIBUTES inherit = {sizeof(inherit), NULL, TRUE};
    HANDLE log_file = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, NULL);

    char cmdline[PATH_MAX * 2 + 16];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" \"%s\"%s", exe, elf, test_mode ? " -t" : "");
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    if (log_file != INVALID_HANDLE_VALUE)
    {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = si.hStdError = log_file;
    }
    /* the loader inherits these from the frontend's environment, which
     * gets them back right after */
    SetEnvironmentVariableA("LINDBERGH_LIBRETRO_SHM", shm_name);
    /* the mix goes to the core (libretroBridge.c), not to a sound card */
    SetEnvironmentVariableA("SDL_AUDIO_DRIVER", "dummy");
    /* the Resolution option, over linuxloader.ini's (init.c) */
    SetEnvironmentVariableA("LINDBERGH_LIBRETRO_RESOLUTION", strcmp(resolution, "native") ? resolution : NULL);
    /* the Free Play option, over linuxloader.ini's (init.c) */
    SetEnvironmentVariableA("LINDBERGH_LIBRETRO_FREEPLAY", freeplay);
    SetEnvironmentVariableA("LINDBERGH_LIBRETRO_SAVES", s_saves[0] ? s_saves : NULL);
    BOOL started = CreateProcessA(exe, cmdline, NULL, NULL, log_file != INVALID_HANDLE_VALUE,
                                  CREATE_NO_WINDOW, NULL, dir, &si, &pi);
    DWORD error = GetLastError();
    SetEnvironmentVariableA("LINDBERGH_LIBRETRO_SHM", NULL);
    SetEnvironmentVariableA("SDL_AUDIO_DRIVER", NULL);
    SetEnvironmentVariableA("LINDBERGH_LIBRETRO_RESOLUTION", NULL);
    SetEnvironmentVariableA("LINDBERGH_LIBRETRO_FREEPLAY", NULL);
    SetEnvironmentVariableA("LINDBERGH_LIBRETRO_SAVES", NULL);
    if (log_file != INVALID_HANDLE_VALUE)
        CloseHandle(log_file);
    if (!started)
    {
        lr_log(RETRO_LOG_ERROR, "cannot start the loader: error %lu\n", error);
        close_shm();
        return false;
    }
    CloseHandle(pi.hThread);
    loader = pi.hProcess;
    lr_log(RETRO_LOG_INFO, "started %s (pid %lu), its output in %s\n", cmdline, pi.dwProcessId, log_path);
    return true;
#else
    char apprun[PATH_MAX];
    snprintf(apprun, sizeof(apprun), "%s/AppRun", root);
#if defined(__aarch64__)
    /* On ARM the loader and the game are x86 programs as everywhere: run
     * through box64 (built with BOX32, for 32-bit x86), which comes in the
     * same folder */
    char box64[PATH_MAX], launcher[PATH_MAX];
    snprintf(box64, sizeof(box64), "%s/box64", root);
    snprintf(launcher, sizeof(launcher), "%s/usr/bin/linuxloader", root);
    if (access(box64, X_OK) != 0 || access(launcher, R_OK) != 0)
    {
        lr_log(RETRO_LOG_ERROR, "box64 or the loader is not in %s\n", root);
        struct retro_message msg = {"Lindbergh: box64 or the loader is missing from system/lindbergh", 600};
        environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
        return false;
    }
#endif
    if (access(apprun, X_OK) != 0)
    {
        lr_log(RETRO_LOG_ERROR, "the loader is not in %s (AppRun missing)\n", root);
        struct retro_message msg = {"Lindbergh: the loader is missing from system/lindbergh", 600};
        environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
        return false;
    }
    if (!create_shm())
        return false;
    last_frame = 0;

    loader = fork();
    if (loader == 0)
    {
        setpgid(0, 0);
        if (chdir(dir) != 0)
            _exit(127);
        setenv("LINDBERGH_LIBRETRO_SHM", shm_name, 1);
        /* the mix goes to the core (libretroBridge.c), not to a sound card */
        setenv("SDL_AUDIO_DRIVER", "dummy", 1);
        /* the Resolution option, over linuxloader.ini's (init.c) */
        if (strcmp(resolution, "native"))
            setenv("LINDBERGH_LIBRETRO_RESOLUTION", resolution, 1);
        /* the Free Play option, over linuxloader.ini's (init.c) */
        if (freeplay)
            setenv("LINDBERGH_LIBRETRO_FREEPLAY", freeplay, 1);
        if (s_saves[0])
            setenv("LINDBERGH_LIBRETRO_SAVES", s_saves, 1);
#if defined(__aarch64__)
        /* what AppRun sets, for the x86 libraries box64 loads */
        char libs[PATH_MAX * 2];
        snprintf(libs, sizeof(libs), "%s/usr/lib32:%s/usr/lib", root, root);
        setenv("APP_IMG_ROOT", root, 1);
        setenv("LD_LIBRARY_PATH", libs, 1);
        setenv("BOX64_LD_LIBRARY_PATH", libs, 1);
        setenv("BOX64_LOG", "1", 0);
        if (test_mode)
            execl(box64, box64, launcher, elf, "-t", (char *)NULL);
        else
            execl(box64, box64, launcher, elf, (char *)NULL);
#else
        if (test_mode)
            execl(apprun, apprun, elf, "-t", (char *)NULL);
        else
            execl(apprun, apprun, elf, (char *)NULL);
#endif
        _exit(127);
    }
    if (loader < 0)
    {
        lr_log(RETRO_LOG_ERROR, "cannot start the loader: %s\n", strerror(errno));
        loader = 0;
        close_shm();
        return false;
    }
    lr_log(RETRO_LOG_INFO, "started %s %s (pid %d)\n", apprun, elf, (int)loader);
    return true;
#endif
}

bool retro_load_game(const struct retro_game_info *game)
{
    if (!game || !game->path)
        return false;
    read_options();

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
        return false;
    struct retro_keyboard_callback keyboard = {keyboard_event};
    environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard);
    if (!environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble))
        memset(&rumble, 0, sizeof(rumble));
    rumble_seq = 0;
    rumble_frames = 0;
    struct retro_audio_callback audio = {audio_callback, audio_set_state};
    audio_async = audio_thread && environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, &audio);
    lr_log(RETRO_LOG_INFO, "audio: %s\n", audio_async ? "on RetroArch's audio thread" : "a frame's worth each run");

    const char *system = NULL;
    environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system);
    const char *saves = NULL;
    s_saves[0] = 0;
    if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &saves) && saves && *saves)
    {
#ifdef _WIN32
        snprintf(s_saves, sizeof(s_saves), "%s\\Lindbergh\\", saves);
#else
        snprintf(s_saves, sizeof(s_saves), "%s/Lindbergh/", saves);
#endif
        make_dirs(s_saves);
        s_saves[strlen(s_saves) - 1] = 0;
    }
    char root[PATH_MAX];
    snprintf(root, sizeof(root), "%s/lindbergh", system ? system : ".");
    if (!install_loader(root))
    {
        struct retro_message msg = {"Lindbergh: the loader could not be written to system/lindbergh (see the log)", 600};
        environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
        return false;
    }

    char elf[PATH_MAX];
    if (!game_path(game->path, elf, sizeof(elf)) || access(elf, R_OK) != 0)
    {
        lr_log(RETRO_LOG_ERROR, "cannot read the game %s\n", game->path);
        return false;
    }
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", elf);
    char *slash = last_separator(dir);
    if (slash)
        *slash = 0;

snprintf(s_elf, sizeof(s_elf), "%s", elf);
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    snprintf(s_root, sizeof(s_root), "%s", root);
    return start_loader();
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
    (void)type; (void)info; (void)num;
    return false;
}

void retro_unload_game(void)
{
    if (rumble.set_rumble_state)
    {
        rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, 0);
        rumble.set_rumble_state(0, RETRO_RUMBLE_WEAK, 0);
    }
    stop_loader();
}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

/* Steering Response's logarithmic curve, as the Sega System 32 racers' dip
 * switch has it (NNshi, after FBNeo's F1 Super Lap): the Inner Zone's share
 * of the turn around the centre, steeper towards the ends, so a stick steers
 * less twitchily; i x + (1 - i) x^n with n 3 (Cubic, strong) or 2 (Square,
 * weak), full turn at full tilt */
static int16_t steering_curve(int16_t v)
{
    const float x = v / 32767.0f;
    const float ax = x < 0 ? -x : x;
    const float outer = steering_power == 2 ? x * ax : x * x * x;
    float y = steering_inner * x + (1.0f - steering_inner) * outer;
    y = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;
    return (int16_t)(y * 32767.0f);
}

static int16_t axis(unsigned port, unsigned index, unsigned id)
{
    return input_state_cb(port, RETRO_DEVICE_ANALOG, index, id);
}

static int16_t trigger(unsigned port, unsigned id)
{
    int16_t v = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, id);
    if (v <= 0)
        v = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, id) ? 0x7fff : 0;
    return v;
}

static void write_input(void)
{
    static const struct { unsigned retro, lr; } map[] = {
        {RETRO_DEVICE_ID_JOYPAD_B, LR_PAD_A}, {RETRO_DEVICE_ID_JOYPAD_A, LR_PAD_B},
        {RETRO_DEVICE_ID_JOYPAD_Y, LR_PAD_X}, {RETRO_DEVICE_ID_JOYPAD_X, LR_PAD_Y},
        {RETRO_DEVICE_ID_JOYPAD_SELECT, LR_PAD_BACK}, {RETRO_DEVICE_ID_JOYPAD_START, LR_PAD_START},
        {RETRO_DEVICE_ID_JOYPAD_L3, LR_PAD_LSTICK}, {RETRO_DEVICE_ID_JOYPAD_R3, LR_PAD_RSTICK},
        {RETRO_DEVICE_ID_JOYPAD_L, LR_PAD_LSHOULDER}, {RETRO_DEVICE_ID_JOYPAD_R, LR_PAD_RSHOULDER},
        {RETRO_DEVICE_ID_JOYPAD_UP, LR_PAD_UP}, {RETRO_DEVICE_ID_JOYPAD_DOWN, LR_PAD_DOWN},
        {RETRO_DEVICE_ID_JOYPAD_LEFT, LR_PAD_LEFT}, {RETRO_DEVICE_ID_JOYPAD_RIGHT, LR_PAD_RIGHT},
    };
    input_poll_cb();
    for (unsigned p = 0; p < LR_PLAYERS; p++)
    {
        uint32_t buttons = 0;
        for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
            if (input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, map[i].retro))
                buttons |= 1u << map[i].lr;
        LrPad pad = {buttons, {0}};
        pad.axes[LR_AXIS_LX] = axis(p, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        if (steering_log)
            pad.axes[LR_AXIS_LX] = steering_curve(pad.axes[LR_AXIS_LX]);
        pad.axes[LR_AXIS_LY] = axis(p, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        pad.axes[LR_AXIS_RX] = axis(p, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
        pad.axes[LR_AXIS_RY] = axis(p, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
        pad.axes[LR_AXIS_LT] = trigger(p, RETRO_DEVICE_ID_JOYPAD_L2);
        pad.axes[LR_AXIS_RT] = trigger(p, RETRO_DEVICE_ID_JOYPAD_R2);
        shm->pads[p] = pad;
    }

    /* the pointer over the picture: RetroArch's light gun (which follows
     * the mouse) or its pointer, -0x7fff..0x7fff across the content */
    int16_t x, y;
    uint32_t b = 0;
    if (pointer_lightgun)
    {
        x = input_state_cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
        y = input_state_cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
        if (input_state_cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER)) b |= 1;
        if (input_state_cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD)) b |= 2;
        if (input_state_cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_AUX_A)) b |= 4;
    }
    else
    {
        x = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
        y = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
    }
    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT)) b |= 1;
    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT)) b |= 2;
    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE)) b |= 4;
    shm->pointer_x = (uint32_t)((int32_t)x + 0x8000);
    shm->pointer_y = (uint32_t)((int32_t)y + 0x8000);
    shm->pointer_buttons = b;
    shm->pointer_valid = 1;
}

/* The ring keeps about this much in hand: SDL's device in the loader mixes
 * in blocks at its own pace (libretroBridge.c), so a frame's worth is not
 * always there at a run's end */
#define AUDIO_CUSHION_FRAMES 2048

/* The rate SDL's device mixes at, once the loader has seen it */
static void check_audio_rate(void)
{
    if (shm->audio_rate && shm->audio_rate != (uint32_t)sample_rate)
    {
        sample_rate = shm->audio_rate;
        struct retro_system_av_info av;
        retro_get_system_av_info(&av);
        environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
        lr_log(RETRO_LOG_INFO, "audio at %u Hz\n", shm->audio_rate);
    }
}

#define AUDIO_CALLBACK_FRAMES 1024
static int16_t audio_cb_out[AUDIO_CALLBACK_FRAMES * 2];

/* RetroArch's audio thread asks for audio (SET_AUDIO_CALLBACK): what the
 * loader has mixed goes as it arrives, so the sound follows the loader's
 * clock, as SDL's own thread has it in the loader alone, and not the
 * frames, which come slower where a game drops frames (NNshi, Initial D 4's
 * car select at 40 fps) */
static void audio_callback(void)
{
    __atomic_add_fetch(&audio_busy, 1, __ATOMIC_ACQUIRE);
    LrShm *s = __atomic_load_n(&audio_shm, __ATOMIC_ACQUIRE);
    uint32_t take = 0;
    if (s)
    {
        uint32_t write = s->audio_write, read = s->audio_read;
        uint32_t avail = write - read;
        /* a backlog (the frontend's device held back) is cut back rather
         * than played late */
        if (avail > 3 * AUDIO_CUSHION_FRAMES)
        {
            read = write - AUDIO_CUSHION_FRAMES;
            avail = AUDIO_CUSHION_FRAMES;
        }
        take = avail < AUDIO_CALLBACK_FRAMES ? avail : AUDIO_CALLBACK_FRAMES;
        for (uint32_t i = 0; i < take; i++)
        {
            const int16_t *in = &s->audio[((read + i) % LR_AUDIO_FRAMES) * 2];
            audio_cb_out[i * 2] = in[0];
            audio_cb_out[i * 2 + 1] = in[1];
        }
        s->audio_read = read + take;
    }
    __atomic_sub_fetch(&audio_busy, 1, __ATOMIC_RELEASE);
    if (take && audio_batch_cb)
        audio_batch_cb(audio_cb_out, take);
    else
    {
        /* nothing mixed yet: RetroArch calls again at once */
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }
}

static void audio_set_state(bool enabled)
{
    (void)enabled;
}

static void audio_release_shm(void)
{
    __atomic_store_n(&audio_shm, NULL, __ATOMIC_RELEASE);
    while (__atomic_load_n(&audio_busy, __ATOMIC_ACQUIRE))
    {
#ifdef _WIN32
        Sleep(0);
#else
        usleep(100);
#endif
    }
}

/* Exactly one frame's worth of audio a run, at the rate SDL mixes at: out
 * of the ring once it holds the cushion, silence while it fills (again).
 * Handing over what happened to be there gave RetroArch nothing on one run
 * and two blocks on the next: underruns, scratchy sound (NNshi). */
static void send_audio(void)
{
    static double owed;
    static bool primed;

    owed += sample_rate / 60.0;
    uint32_t frames = (uint32_t)owed;
    owed -= frames;

    uint32_t write = shm->audio_write, read = shm->audio_read;
    uint32_t avail = write - read;
    if (!primed && avail >= AUDIO_CUSHION_FRAMES + frames)
        primed = true;
    /* a backlog (the frontend held back, or its clock runs slow) is cut
     * back to the cushion rather than played late */
    if (avail > 3 * AUDIO_CUSHION_FRAMES)
    {
        read = write - AUDIO_CUSHION_FRAMES - frames;
        avail = AUDIO_CUSHION_FRAMES + frames;
    }
    uint32_t take = primed ? (avail < frames ? avail : frames) : 0;
    for (uint32_t i = 0; i < take; i++)
    {
        const int16_t *in = &shm->audio[((read + i) % LR_AUDIO_FRAMES) * 2];
        audio_out[i * 2] = in[0];
        audio_out[i * 2 + 1] = in[1];
    }
    memset(&audio_out[take * 2], 0, (frames - take) * 2 * sizeof(int16_t));
    if (primed && take < frames)
        primed = false; /* ran dry: fill the cushion again */
    shm->audio_read = read + take;
    if (frames && audio_batch_cb)
        audio_batch_cb(audio_out, frames);
}

/* The drive board's force feedback (forceFeedback.c) on player 1's pad, for
 * as long as the loader asked, then stopped */
static void update_rumble(void)
{
    if (!rumble.set_rumble_state)
        return;
    if (!ffb_rumble)
    {
        /* off: what was running stops, the rest is not taken */
        if (rumble_frames)
        {
            rumble_frames = 0;
            rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, 0);
            rumble.set_rumble_state(0, RETRO_RUMBLE_WEAK, 0);
        }
        rumble_seq = __atomic_load_n(&shm->rumble_seq, __ATOMIC_ACQUIRE);
        return;
    }
    uint32_t seq = __atomic_load_n(&shm->rumble_seq, __ATOMIC_ACQUIRE);
    if (seq != rumble_seq)
    {
        rumble_seq = seq;
        rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, (uint16_t)shm->rumble_strong);
        rumble.set_rumble_state(0, RETRO_RUMBLE_WEAK, (uint16_t)shm->rumble_weak);
        rumble_frames = (shm->rumble_ms * 60 + 999) / 1000;
        if (!rumble_frames)
            rumble_frames = 1;
    }
    else if (rumble_frames && !--rumble_frames)
    {
        rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, 0);
        rumble.set_rumble_state(0, RETRO_RUMBLE_WEAK, 0);
    }
}

void retro_run(void)
{
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
        read_options();

    if (!shm)
    {
        video_cb(NULL, frame_w, frame_h, 0);
        return;
    }
    write_input();
    shm->run_seq++;
    lrWake(run_event, &shm->run_seq);

    /* the game's next frame, or the last one again while it loads */
    for (int waited = 0; shm->frame_seq == last_frame && waited < 100; waited += 10)
        lrWait(frame_event, &shm->frame_seq, last_frame, 10);

    if (loader_exited())
    {
        lr_log(RETRO_LOG_ERROR, "the game exited\n");
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }

    if (shm->frame_seq != last_frame && shm->width && shm->height)
    {
        last_frame = shm->frame_seq;
        if (shm->width != frame_w || shm->height != frame_h)
        {
            frame_w = shm->width;
            frame_h = shm->height;
            struct retro_game_geometry geometry = {frame_w, frame_h, LR_MAX_WIDTH, LR_MAX_HEIGHT,
                                                   (float)frame_w / (float)frame_h};
            environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
        }
        video_cb(shm->pixels, frame_w, frame_h, frame_w * 4);
    }
    else
        video_cb(NULL, frame_w, frame_h, 0);
    check_audio_rate();
    if (!audio_async)
        send_audio();
    update_rumble();
}

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
