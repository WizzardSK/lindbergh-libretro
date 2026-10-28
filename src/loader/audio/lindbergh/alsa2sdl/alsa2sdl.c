#include <SDL3/SDL.h>
#include <stdlib.h>
#include "../../../log/log.h"
#include "../../../config/config.h"
#ifdef COMPILING_LINUXLOADER_ELF
// The launcher executable runs no game: never for the libretro core
#define libretroAttachAudioDevice(dev) ((void)(dev))
#else
#include "../../../libretro/libretroBridge.h"
#endif

#define SND_PCM_STREAM_PLAYBACK 0
#define SND_PCM_STREAM_CAPTURE 1

#define SND_PCM_FORMAT_S8 0
#define SND_PCM_FORMAT_U8 1
#define SND_PCM_FORMAT_S16_LE 2
#define SND_PCM_FORMAT_S16_BE 3
#define SND_PCM_FORMAT_U16_LE 4
#define SND_PCM_FORMAT_U16_BE 5
#define SND_PCM_FORMAT_FLOAT_LE 14

#define SND_PCM_ACCESS_RW_INTERLEAVED 3

#define SND_PCM_STATE_OPEN 0
#define SND_PCM_STATE_SETUP 1
#define SND_PCM_STATE_PREPARED 2
#define SND_PCM_STATE_RUNNING 3
#define SND_PCM_STATE_XRUN 4

typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;

struct _snd_pcm_hw_params
{
    int access;
    int format;
    int channels;
    unsigned int rate;
    unsigned int periods;
    snd_pcm_uframes_t buffer_size;
    snd_pcm_uframes_t period_size;
};

struct _snd_pcm_sw_params
{
    int dummy;
};

struct _snd_pcm
{
    SDL_AudioStream *stream;
    SDL_AudioSpec spec;
    int state;
    char name[64];
    snd_pcm_uframes_t buffer_size;
};

typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_pcm_hw_params snd_pcm_hw_params_t;
typedef struct _snd_pcm_sw_params snd_pcm_sw_params_t;

static SDL_AudioDeviceID g_alsaDevice = 0;
extern uint32_t gId;

static void EnsureAudioInit()
{
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        SDL_Init(SDL_INIT_AUDIO);
    }
    if (g_alsaDevice == 0)
    {
        g_alsaDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
        if (g_alsaDevice == 0)
        {
            log_error("ALSA: Failed to open SDL audio device: %s", SDL_GetError());
        }
        else
        {
            log_info("ALSA: Opened SDL audio device ID %u", g_alsaDevice);
            // Primeval Hunt plays through ALSA alone and played nothing in the
            // libretro core; the games that open ALSA beside FAudio went silent
            // with their ALSA device fed to the core too (NNshi)
            if (gId == PRIMEVAL_HUNT_SBPP)
                libretroAttachAudioDevice(g_alsaDevice);
        }
    }
}

int shared_snd_pcm_open(snd_pcm_t **pcm, const char *name, int stream, int mode)
{
    log_info("snd_pcm_open('%s', stream=%d, mode=%d)", name, stream, mode);

    EnsureAudioInit();

    if (stream != SND_PCM_STREAM_PLAYBACK)
    {
        log_error("ALSA: Only playback streams supported");
        return -22; // -EINVAL
    }

    snd_pcm_t *handle = (snd_pcm_t *)calloc(1, sizeof(snd_pcm_t));
    strncpy(handle->name, name, 63);

    handle->spec.format = SDL_AUDIO_S16LE;
    handle->spec.channels = 2;
    handle->spec.freq = 44100;
    handle->buffer_size = 4096;

    handle->stream = SDL_CreateAudioStream(&handle->spec, NULL);
    if (!handle->stream)
    {
        log_error("ALSA: Failed to create audio stream: %s", SDL_GetError());
        free(handle);
        return -1;
    }

    if (g_alsaDevice)
    {
        SDL_BindAudioStream(g_alsaDevice, handle->stream);
    }

    handle->state = SND_PCM_STATE_OPEN;

    if (pcm)
        *pcm = handle;
    return 0;
}

int shared_snd_pcm_close(snd_pcm_t *pcm)
{
    log_info("snd_pcm_close(%p)", pcm);
    if (pcm)
    {
        if (pcm->stream)
        {
            SDL_DestroyAudioStream(pcm->stream);
        }
        free(pcm);
    }
    return 0;
}

// HW Params
int shared_snd_pcm_hw_params_malloc(snd_pcm_hw_params_t **ptr)
{
    if (ptr)
        *ptr = (snd_pcm_hw_params_t *)calloc(1, sizeof(snd_pcm_hw_params_t));
    return 0;
}

void shared_snd_pcm_hw_params_free(snd_pcm_hw_params_t *obj)
{
    free(obj);
}

int shared_snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{

    if (params)
    {
        params->access = SND_PCM_ACCESS_RW_INTERLEAVED;
        params->format = SND_PCM_FORMAT_S16_LE;
        params->channels = 2;
        params->rate = 44100;
        params->periods = 4;
        params->buffer_size = 4096;
        params->period_size = 1024;
    }
    return 0;
}

int shared_snd_pcm_hw_params_set_access(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int access)
{
    if (params)
        params->access = access;
    return 0;
}

int shared_snd_pcm_hw_params_set_format(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int val)
{
    if (params)
        params->format = val;
    return 0;
}

int shared_snd_pcm_hw_params_set_channels(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int val)
{
    if (params)
        params->channels = val;
    return 0;
}

int shared_snd_pcm_hw_params_set_rate_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir)
{
    if (params && val)
        params->rate = *val;
    return 0;
}

int shared_snd_pcm_hw_params_set_periods(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val, int dir)
{
    if (params)
        params->periods = val;
    return 0;
}

int shared_snd_pcm_hw_params_set_buffer_size(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t val)
{
    if (params)
        params->buffer_size = val;
    return 0;
}

int shared_snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned long *val)
{
    if (params && val)
        params->buffer_size = *val;
    return 0;
}

int shared_snd_pcm_hw_params_set_period_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned long *val, int *dir)
{
    if (params && val)
        params->period_size = *val;
    return 0;
}

int shared_snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
    if (!pcm || !params)
        return -22; // EINVAL

    pcm->spec.freq = params->rate;
    pcm->spec.channels = params->channels;
    pcm->buffer_size = params->buffer_size;

    switch (params->format)
    {
        case SND_PCM_FORMAT_S8:
            pcm->spec.format = SDL_AUDIO_S8;
            break;
        case SND_PCM_FORMAT_U8:
            pcm->spec.format = SDL_AUDIO_U8;
            break;
        case SND_PCM_FORMAT_S16_LE:
            pcm->spec.format = SDL_AUDIO_S16LE;
            break;
        case SND_PCM_FORMAT_S16_BE:
            pcm->spec.format = SDL_AUDIO_S16BE;
            break;
        case SND_PCM_FORMAT_FLOAT_LE:
            pcm->spec.format = SDL_AUDIO_F32LE;
            break;
        default:
            log_warn("ALSA: Unknown format %d, defaulting to S16LE", params->format);
            pcm->spec.format = SDL_AUDIO_S16LE;
            break;
    }

    log_info("ALSA: Configured %s: %d Hz, %d channels, fmt=%d, buf=%lu", pcm->name, pcm->spec.freq, pcm->spec.channels, pcm->spec.format,
             pcm->buffer_size);

    if (pcm->stream)
    {
        SDL_SetAudioStreamFormat(pcm->stream, &pcm->spec, NULL);
    }

    pcm->state = SND_PCM_STATE_PREPARED;
    return 0;
}

// SW Params
int shared_snd_pcm_sw_params_malloc(snd_pcm_sw_params_t **ptr)
{
    if (ptr)
        *ptr = (snd_pcm_sw_params_t *)calloc(1, sizeof(snd_pcm_sw_params_t));
    return 0;
}
void shared_snd_pcm_sw_params_free(snd_pcm_sw_params_t *obj)
{
    free(obj);
}

int shared_snd_pcm_sw_params_current(snd_pcm_t *pcm, snd_pcm_sw_params_t *params)
{
    return 0;
}
int shared_snd_pcm_sw_params_set_avail_min(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val)
{
    return 0;
}
int shared_snd_pcm_sw_params_set_start_threshold(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val)
{
    return 0;
}
int shared_snd_pcm_sw_params_set_xfer_align(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val)
{
    return 0;
}

int shared_snd_pcm_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t *params)
{
    return 0;
}

// Operations
int shared_snd_pcm_prepare(snd_pcm_t *pcm)
{
    if (pcm)
    {
        if (pcm->stream)
            SDL_ClearAudioStream(pcm->stream);
        pcm->state = SND_PCM_STATE_PREPARED;
    }
    return 0;
}

int shared_snd_pcm_start(snd_pcm_t *pcm)
{
    if (pcm)
    {
        if (pcm->stream)
            SDL_ResumeAudioStreamDevice(pcm->stream);
        pcm->state = SND_PCM_STATE_RUNNING;
    }
    return 0;
}

int shared_snd_pcm_drop(snd_pcm_t *pcm)
{
    if (pcm)
    {
        if (pcm->stream)
            SDL_ClearAudioStream(pcm->stream);
        pcm->state = SND_PCM_STATE_SETUP;
    }
    return 0;
}

int shared_snd_pcm_resume(snd_pcm_t *pcm)
{
    return shared_snd_pcm_start(pcm);
}

int shared_snd_pcm_drain(snd_pcm_t *pcm)
{
    if (pcm && pcm->stream)
    {
        SDL_FlushAudioStream(pcm->stream);
    }
    return 0;
}

int shared_snd_pcm_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *delayp)
{
    if (pcm && delayp)
    {

        if (pcm->stream)
        {
            int queued_bytes = SDL_GetAudioStreamQueued(pcm->stream);
            int frame_size = pcm->spec.channels * SDL_AUDIO_BYTESIZE(pcm->spec.format);
            if (frame_size > 0)
            {
                *delayp = queued_bytes / frame_size;
            }
            else
            {
                *delayp = 0;
            }
        }
        else
        {
            *delayp = 0;
        }
    }
    return 0;
}

snd_pcm_sframes_t shared_snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, unsigned long size)
{

    if (!pcm || !pcm->stream)
        return -32; // -EPIPE

    int frame_size = pcm->spec.channels * SDL_AUDIO_BYTESIZE(pcm->spec.format);
    if (frame_size <= 0)
        return -22; // EINVAL

    int total_bytes = size * frame_size;

    if (SDL_PutAudioStreamData(pcm->stream, buffer, total_bytes))
    {
        return size; // Success
    }

    return -32; // Error
}

int shared_snd_pcm_avail_update(snd_pcm_t *pcm)
{
    if (!pcm || !pcm->stream)
        return 4096;

    int frame_size = pcm->spec.channels * SDL_AUDIO_BYTESIZE(pcm->spec.format);
    if (frame_size <= 0)
        return 4096;

    int queued_bytes = SDL_GetAudioStreamQueued(pcm->stream);
    int queued_frames = queued_bytes / frame_size;

    int avail = (int)pcm->buffer_size - queued_frames;
    if (avail < 0)
        avail = 0;

    return avail;
}

int shared_snd_pcm_state(snd_pcm_t *pcm)
{
    if (pcm)
        return pcm->state;
    return SND_PCM_STATE_OPEN;
}

int shared_snd_pcm_format_width(int format)
{
    switch (format)
    {
        case SND_PCM_FORMAT_S8:
            return 8;
        case SND_PCM_FORMAT_U8:
            return 8;
        case SND_PCM_FORMAT_S16_LE:
            return 16;
        case SND_PCM_FORMAT_S16_BE:
            return 16;
        case SND_PCM_FORMAT_FLOAT_LE:
            return 32;
    }
    return 16;
}

const char *shared_snd_strerror(int errnum)
{
    return "ALSA Error";
}
