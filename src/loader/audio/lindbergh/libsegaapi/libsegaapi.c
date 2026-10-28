#include "libsegaapi.h"

#include <FAudio.h>
#include <FAudioFX.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdint.h>

// The games do not pass these by pointer: each one is an R_386_COPY relocation, so ld.so copies
// these sixteen bytes into the game's own .bss at load time. Zero-filled definitions leave every
// game unable to tell one effect or FX slot from another. Values read from libsegaapi.so 2.08.0000.
GUID EAX_NULL_GUID                   = {0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
GUID EAX_REVERB_EFFECT               = {0x0cf95c8f, 0xa3cc, 0x4849, {0xb0, 0xb6, 0x83, 0x2e, 0xcc, 0x18, 0x22, 0xdf}};
GUID EAX_ECHO_EFFECT                 = {0x0e9f1bc0, 0xac82, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_CHORUS_EFFECT               = {0xde6d6fe0, 0xac79, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_FLANGER_EFFECT              = {0xa70007c0, 0x07d2, 0x11d3, {0x9b, 0x1e, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_DISTORTION_EFFECT           = {0x975a4ce0, 0xac7e, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_EQUALIZER_EFFECT            = {0x65f94ce0, 0x9793, 0x11d3, {0x93, 0x9d, 0x00, 0xc0, 0xf0, 0x2d, 0xd6, 0xf0}};
GUID EAX_AUTOWAH_EFFECT              = {0xec3130c0, 0xac7a, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_PITCHSHIFTER_EFFECT         = {0xe7905100, 0xafb2, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_FREQUENCYSHIFTER_EFFECT     = {0xdc3e1880, 0x9212, 0x11d3, {0x93, 0x9d, 0x00, 0xc0, 0xf0, 0x2d, 0xd6, 0xf0}};
GUID EAX_RINGMODULATOR_EFFECT        = {0x0b89fe60, 0xafb5, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_VOCALMORPHER_EFFECT         = {0xe41cf10c, 0x3383, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_AGCCOMPRESSOR_EFFECT        = {0xbfb7a01e, 0x7825, 0x4039, {0x92, 0x7f, 0x03, 0xaa, 0xbd, 0xa0, 0xc5, 0x60}};
GUID EAXPROPERTYID_EAX40_FXSlot0     = {0xc4d79f1e, 0xf1ac, 0x436b, {0xa8, 0x1d, 0xa7, 0x38, 0xe7, 0x04, 0x54, 0x69}};
GUID EAXPROPERTYID_EAX40_FXSlot1     = {0x08c00e96, 0x74be, 0x4491, {0x93, 0xaa, 0xe8, 0xad, 0x35, 0xa4, 0x91, 0x17}};
GUID EAXPROPERTYID_EAX40_FXSlot2     = {0x1d433b88, 0xf0f6, 0x4637, {0x91, 0x9f, 0x60, 0xe7, 0xe0, 0x6b, 0x5e, 0xdd}};
GUID EAXPROPERTYID_EAX40_FXSlot3     = {0xefff08ea, 0xc7d8, 0x44ab, {0x93, 0xad, 0x6d, 0xbd, 0x5f, 0x91, 0x00, 0x64}};
GUID EAXPROPERTYID_EAX40_Context     = {0x1d4870ad, 0x0def, 0x43c0, {0xa4, 0x0c, 0x52, 0x36, 0x32, 0x29, 0x63, 0x42}};
GUID EAXPROPERTYID_EAX40_Source      = {0x1b86b823, 0x22df, 0x4eae, {0x8b, 0x3c, 0x12, 0x78, 0xce, 0x54, 0x42, 0x27}};
GUID EAX_PrimaryFXSlotID             = {0xf317866d, 0x924c, 0x450c, {0x86, 0x1b, 0xe6, 0xda, 0xa2, 0x5e, 0x7c, 0x20}};
const GUID EAXPROPERTYID_EAX40_SEGA_Custom = {0xa7feec3f, 0x2bfd, 0x4a40,
                                              {0x89, 0x1f, 0x74, 0x23, 0xe3, 0x8b, 0xac, 0x1f}};

#define HANDLE_CHECK(handle, returnValue)                                                                              \
    if (handle == NULL)                                                                                                \
    {                                                                                                                  \
        debug(1, "Error: Failed a handle check\n");                                                                    \
        return returnValue;                                                                                            \
    }

// Every entry point routes its handle through lookupBuffer(), so a stale or bogus handle returns
// SEGA_ERROR_BAD_HANDLE instead of dereferencing freed memory. The registry doubles as the buffer
// list that SEGAAPI_Reset and SEGAAPI_Exit walk.
#define BUFFER_CHECK(handle, returnValue)                                                                              \
    Buffer *buffer = lookupBuffer(handle);                                                                             \
    if (buffer == NULL)                                                                                                \
    {                                                                                                                  \
        debug(1, "Error: invalid buffer handle %p\n", (handle));                                                       \
        setLastStatus(SEGA_ERROR_BAD_HANDLE);                                                                          \
        return returnValue;                                                                                            \
    }

#define DEBUG_LEVEL -1

#define OUTPUT_CHANNELS 6 // FL, FR, BL, BR, C, SW
#define FX_SLOTS 4        // EAX 4.0 exposes four global effect slots
#define HARDWARE_SENDS 7  // the board offers 7 sends per channel; MAX_SENDS only sizes the arrays
#define MAX_CHANNELS 6
#define MAX_SENDS 13

#define MIN_SAMPLE_RATE 8000
#define MAX_SAMPLE_RATE 48000

FAudio *fAudio;
pthread_mutex_t fAudioMutex;
FAudioMasteringVoice *fAudioMasteringVoice;
FAudioSubmixVoice *fAudioSubmixVoices[MAX_SENDS];

// The EAX effect slots. Ports 10-13 route here; each is a mono submix with a reverb in its effect
// chain. Kept separate from fAudioSubmixVoices because FXSLOT3_PORT is 13, one past that array.
FAudioSubmixVoice *fAudioFxVoices[FX_SLOTS];
FAPO *fAudioFxReverbs[FX_SLOTS];
static int fxSlotHasEffect[FX_SLOTS];
static int fxSlotEffectEnabled[FX_SLOTS];
static float fxSlotVolume[FX_SLOTS];
static int fxSlotConfiguredByGame[FX_SLOTS];
static int fxSlotSendWarned[FX_SLOTS];

unsigned int lastStatus;
unsigned int maxInputChannels;

static unsigned int deviceSampleRate;

typedef struct FAudioVoiceCallbackWithBuffer FAudioVoiceCallbackWithBuffer;

#define BUFFER_MAGIC 0xADADFEFEu
// CRI allocates a large voice pool up front. The original had no fixed ceiling, so this must be
// generous or buffers fail to create and their sounds simply never play.
#define MAX_BUFFERS 1024

typedef struct Buffer
{
    // Must stay first: it is what tells a live handle from a stale or bogus one.
    unsigned int magic;

    FAudioWaveFormatEx fAudioFormat;
    FAudioBuffer fAudioBuffer;
    FAudioSourceVoice *fAudioSourceVoice;
    FAudioVoiceCallbackWithBuffer *fAudioVoiceCallback;

    uint8_t *data;
    size_t size;
    int ownsData;

    void *userData;
    BufferCallback callback;
    int bDoContinuousLooping;
    PlaybackStatus playbackStatus;

    unsigned int startLoop;
    unsigned int endLoop;
    unsigned int endOffset;

    OutputFormat outputFormat;
    int updateOutputFormat;
    int releaseState;
    unsigned int playPosition;

    // SamplesPlayed at the moment of the last submit. FAudio's src.totalSamples is a free-running
    // per-voice lifetime counter it resets only on FAUDIO_END_OF_STREAM, a flag we never set, so it
    // carries over when a pooled buffer is reused and cannot be used as a position on its own.
    uint64_t playOrigin;
    int synthCapable;
    int synthWarned;

    // Tracks whether the voice has been started, so a seek or a rate change can put it back the way
    // it found it, and whether anything is queued at all.
    int started;
    int hasSubmitted;

    // UpdateBuffer is a per-tick no-op on hardware - the card DMAs straight out of the game's
    // memory - so a resubmit that changes nothing must not disturb a stream that is already playing.
    const void *lastData;
    unsigned int lastPlayBegin;
    unsigned int lastPlayLength;
    unsigned int lastLoopBegin;
    unsigned int lastLoopLength;
    int lastLoopInfinite;

    // Set by SEGAAPI_Play to override the no-op cache below: Play restarts a voice, it never
    // resumes one, so the cache must not be allowed to skip its resubmit.
    int forceResubmit;

    // PlayWithSetup applies 7 routings then 7 levels. Recomputing the send graph after each one
    // rebuilds the routing 14 times per voice start, for one final result.
    int deferSends;

    // Only rebuild the send list when destinations change: SetOutputVoices resets every matrix to unity.
    FAudioSubmixVoice *lastVoices[MAX_CHANNELS * MAX_SENDS];
    int lastVoiceCount;

    Routing routing[MAX_CHANNELS][MAX_SENDS];
    unsigned int sendLevels[MAX_CHANNELS][MAX_SENDS];
    unsigned int channelVolumes[MAX_CHANNELS];

    SynthParams synthParams[MOD_ENV_TO_FILTER_CUTOFF + 1];

    int lastStatus;

} Buffer;

void debug(int level, const char *format, ...);
int SEGAAPI_DestroyBuffer(void *hHandle);
static int eaxLogEnabled(void);
static void updateFxSlotGain(int slot);

typedef struct FAudioVoiceCallbackWithBuffer
{
    OnBufferEndFunc OnBufferEnd;
    OnBufferStartFunc OnBufferStart;
    OnLoopEndFunc OnLoopEnd;
    OnStreamEndFunc OnStreamEnd;
    OnVoiceErrorFunc OnVoiceError;
    OnVoiceProcessingPassEndFunc OnVoiceProcessingPassEnd;
    OnVoiceProcessingPassStartFunc OnVoiceProcessingPassStart;
    Buffer *buffer;
} FAudioVoiceCallbackWithBuffer;

// SetSampleRate used to defer the rate change until the queue drained, which starved the voice and
// stalled the position the game uses as its clock. It now stops, flushes, sets and requeues in one go.
void StaticOnBufferEnd(FAudioVoiceCallback *callback, void *pBufferContext)
{
}

void StaticOnVoiceProcessingPassStart(FAudioVoiceCallback *callback, uint32_t bytesRequired)
{
}
void StaticOnVoiceProcessingPassEnd(FAudioVoiceCallback *callback)
{
}
void StaticOnStreamEnd(FAudioVoiceCallback *callback)
{
}
void StaticOnBufferStart(FAudioVoiceCallback *callback, void *pBufferContext)
{
}
void StaticOnLoopEnd(FAudioVoiceCallback *callback, void *pBufferContext)
{
}
void StaticOnVoiceError(FAudioVoiceCallback *callback, void *pBufferContext, uint32_t error)
{
}

void debug(int level, const char *format, ...)
{
    if (DEBUG_LEVEL < level)
        return;

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    fflush(stdout);
    va_end(args);
}

static Buffer *bufferRegistry[MAX_BUFFERS];
static pthread_mutex_t registryMutex = PTHREAD_MUTEX_INITIALIZER;

// Physical ports are sparse (0-5, 10-11, 20-21), so map them onto a compact table.
static unsigned int ioVolumes[10] = {VOL_MAX, VOL_MAX, VOL_MAX, VOL_MAX, VOL_MAX,
                                     VOL_MAX, VOL_MAX, VOL_MAX, 0x99980000, 0x99980000};

static unsigned int *ioVolumeSlot(SoundBoardIO dwPhysIO)
{
    switch (dwPhysIO)
    {
    case OUT_FRONT_LEFT:
    case OUT_FRONT_RIGHT:
    case OUT_FRONT_CENTER:
    case OUT_LFE_PORT:
    case OUT_REAR_LEFT:
    case OUT_REAR_RIGHT:
        return &ioVolumes[dwPhysIO];
    case OUT_OPTICAL_LEFT:
        return &ioVolumes[6];
    case OUT_OPTICAL_RIGHT:
        return &ioVolumes[7];
    case IN_LINEIN_LEFT:
        return &ioVolumes[8];
    case IN_LINEIN_RIGHT:
        return &ioVolumes[9];
    default:
        return NULL;
    }
}

// The original sets g_LastStatus on every entry point, success and failure alike. Ours only ever
// held whatever SEGAAPI_SetLastStatus was handed, so GetLastStatus after a failure returned stale
// data.
static void setLastStatus(int status)
{
    lastStatus = (unsigned int)status;
}

static Buffer *lookupBuffer(void *hHandle)
{
    if (hHandle == NULL)
        return NULL;

    Buffer *candidate = (Buffer *)hHandle;

    pthread_mutex_lock(&registryMutex);
    for (int i = 0; i < MAX_BUFFERS; i++)
    {
        if (bufferRegistry[i] == candidate)
        {
            pthread_mutex_unlock(&registryMutex);
            return candidate->magic == BUFFER_MAGIC ? candidate : NULL;
        }
    }
    pthread_mutex_unlock(&registryMutex);

    return NULL;
}

static int registerBuffer(Buffer *buffer)
{
    pthread_mutex_lock(&registryMutex);
    for (int i = 0; i < MAX_BUFFERS; i++)
    {
        if (bufferRegistry[i] == NULL)
        {
            bufferRegistry[i] = buffer;
            pthread_mutex_unlock(&registryMutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&registryMutex);

    fprintf(stderr, "[segaapi] CreateBuffer rejected: buffer registry full (%d live buffers)\n", MAX_BUFFERS);
    fflush(stderr);
    return 0;
}

static void unregisterBuffer(Buffer *buffer)
{
    pthread_mutex_lock(&registryMutex);
    for (int i = 0; i < MAX_BUFFERS; i++)
    {
        if (bufferRegistry[i] == buffer)
        {
            bufferRegistry[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&registryMutex);
}

// Snapshot first, then destroy outside the lock: SEGAAPI_DestroyBuffer takes it again.
static void destroyAllBuffers(void)
{
    Buffer *live[MAX_BUFFERS];
    int count = 0;

    pthread_mutex_lock(&registryMutex);
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (bufferRegistry[i] != NULL)
            live[count++] = bufferRegistry[i];
    pthread_mutex_unlock(&registryMutex);

    for (int i = 0; i < count; i++)
        SEGAAPI_DestroyBuffer(live[i]);
}

// SetSynthParam, SetReleaseState and PlayWithSetup all require a mono, synth-capable buffer on
// hardware and return INVALID_CHANNEL otherwise. Rejecting outright risks silencing a game that has
// been relying on our laxer behaviour, so for now report the violation once per buffer and proceed.
static void warnIfNotSynthVoice(Buffer *buffer, void *hHandle, const char *what)
{
    if (buffer->outputFormat.byNumChans == 1 && buffer->synthCapable)
        return;

    if (buffer->synthWarned)
        return;

    buffer->synthWarned = 1;
    fprintf(stderr, "[segaapi] %s on buffer %p which the hardware would reject: %u channels, synth flag %s\n", what,
            hHandle, buffer->outputFormat.byNumChans, buffer->synthCapable ? "set" : "clear");
    fflush(stderr);
}

// Ports 0-5 are the physical outputs; 10-13 are the EAX effect slots. Anything else is unroutable.
// The original only ever looked at ports 0-5, so every wet send was dropped before it reached the
// mixer and no effect could be heard however it was configured.
static FAudioSubmixVoice *submixForPort(Routing port)
{
    if ((int)port >= 0 && (int)port < OUTPUT_CHANNELS)
        return fAudioSubmixVoices[(int)port];

    if ((int)port >= (int)FXSLOT0_PORT && (int)port <= (int)FXSLOT0_PORT + FX_SLOTS - 1)
        return fAudioFxVoices[(int)port - (int)FXSLOT0_PORT];

    return NULL;
}

static unsigned int bytesToSamples(Buffer *buffer, int value)
{
    switch (buffer->outputFormat.dwSampleFormat)
    {
    case SIGNED_16PCM:
        return value / (buffer->outputFormat.byNumChans * 2);
        break;

    case UNSIGNED_8PCM:
    default:
        return value / buffer->outputFormat.byNumChans;
        break;
    }
}

static unsigned int samplesToBytes(Buffer *buffer, int value)
{
    switch (buffer->outputFormat.dwSampleFormat)
    {
    case SIGNED_16PCM:
        return value * buffer->outputFormat.byNumChans * 2;
        break;

    case UNSIGNED_8PCM:
    default:
        return value * buffer->outputFormat.byNumChans;
        break;
    }
}

char *getRoutingString(Routing dwDest)
{

    char *dest = "Unknown";
    switch (dwDest)
    {
    case UNUSED_PORT:
        dest = "UNUSED_PORT";
        break;
    case FRONT_LEFT_PORT:
        dest = "FRONT_LEFT_PORT";
        break;
    case FRONT_RIGHT_PORT:
        dest = "FRONT_RIGHT_PORT";
        break;
    case FRONT_CENTER_PORT:
        dest = "FRONT_CENTER_PORT";
        break;
    case LFE_PORT:
        dest = "LFE_PORT";
        break;
    case REAR_LEFT_PORT:
        dest = "REAR_LEFT_PORT";
        break;
    case REAR_RIGHT_PORT:
        dest = "REAR_RIGHT_PORT";
        break;
    case FXSLOT0_PORT:
        dest = "FXSLOT0_PORT";
        break;
    case FXSLOT1_PORT:
        dest = "FXSLOT1_PORT";
        break;
    case FXSLOT2_PORT:
        dest = "FXSLOT2_PORT";
        break;
    case FXSLOT3_PORT:
        dest = "FXSLOT3_PORT";
        break;
    default:
        break;
    }
    return dest;
}

// buffer->started is our own flag and games never clear it, so it reads "playing" for a voice FAudio
// drained long ago. pCurrentBufferContext is non-NULL only while a buffer is genuinely being consumed.
static int voiceIsLive(Buffer *buffer)
{
    FAudioVoiceState state;

    FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &state, 0);

    return state.pCurrentBufferContext != NULL;
}

static void processSends(Buffer *buffer)
{
    if (buffer->deferSends)
        return;

    // One entry per distinct destination: several sends can land on the same port and the hardware sums
    // them, but SetOutputMatrix resolves to the FIRST match, so duplicates would overwrite each other.
    // Sized byNumChans * MAX_SENDS - the first dimension was MAX_SENDS and overran.
    FAudioSubmixVoice *voices[MAX_CHANNELS * MAX_SENDS];
    Routing routes[MAX_CHANNELS * MAX_SENDS];
    float matrix[MAX_CHANNELS * MAX_SENDS][MAX_CHANNELS];

    int voiceIndex = 0;

    memset(matrix, 0, sizeof(matrix));

    for (uint32_t i = 0; i < buffer->outputFormat.byNumChans && i < MAX_CHANNELS; i++)
    {
        for (uint32_t j = 0; j < MAX_SENDS; j++)
        {
            Routing port = buffer->routing[i][j];

            debug(1, "Entry channel %d send %d to %s with level %X and channel level %X\n", i, j,
                  getRoutingString(port), buffer->sendLevels[i][j], buffer->channelVolumes[i]);

            if (port == UNUSED_PORT)
                continue;

            FAudioSubmixVoice *destination = submixForPort(port);

            if (destination == NULL)
                continue;

            if ((int)port >= (int)FXSLOT0_PORT)
            {
                int fxSlot = (int)port - (int)FXSLOT0_PORT;

                // Slots 0 and 1 come up with reverb already loaded, inferred from EAX 4.0 defaults
                // and the original only nulling slots 2 and 3. If a game routes into one it never
                // configured, it is getting reverb it did not ask for and that default is wrong.
                if (!fxSlotSendWarned[fxSlot])
                {
                    fxSlotSendWarned[fxSlot] = 1;
                    fprintf(stderr, "[segaapi] send routed to FX slot %d (effect %s, %s by the game)\n", fxSlot,
                            fxSlotHasEffect[fxSlot] ? "loaded" : "none",
                            fxSlotConfiguredByGame[fxSlot] ? "configured" : "NEVER configured");
                    fflush(stderr);
                }
            }

            int slot = -1;
            for (int k = 0; k < voiceIndex; k++)
            {
                if (voices[k] == destination)
                {
                    slot = k;
                    break;
                }
            }

            if (slot < 0)
            {
                slot = voiceIndex++;
                voices[slot] = destination;
                routes[slot] = port;
            }

            matrix[slot][i] +=
                (buffer->sendLevels[i][j] / (float)VOL_MAX) * (buffer->channelVolumes[i] / (float)VOL_MAX);
        }
    }

    // Summed sends can exceed unity where the hardware mixer would have headroom before its DAC.
    // Clamp so a buffer routing several full-level sends to one port does not clip on output.
    for (int i = 0; i < voiceIndex; i++)
        for (uint32_t c = 0; c < MAX_CHANNELS; c++)
            if (matrix[i][c] > 1.0f)
                matrix[i][c] = 1.0f;

    debug(1, "-- matrix --\n");
    for (uint32_t j = 0; j < buffer->outputFormat.byNumChans; j++)
    {
        debug(1, "CHANNEL_%d ", j + 1);
    }
    debug(1, "\n");
    for (int i = 0; i < voiceIndex; i++)
    {
        debug(1, "%s ", getRoutingString(routes[i]));
        for (uint32_t j = 0; j < buffer->outputFormat.byNumChans; j++)
        {
            debug(1, "%f ", matrix[i][j]);
        }
        debug(1, "\n");
    }
    debug(1, "--\n");

    if (voiceIndex == 0)
        return;

    // Rebuilding the send list resets every matrix to unity, so only do it when the destinations
    // genuinely changed. A send-level or channel-volume ramp reaches here on every tick with the
    // same destinations in the same order, and those only need the matrices re-sent.
    int sameDestinations = buffer->lastVoiceCount == voiceIndex;

    for (int i = 0; sameDestinations && i < voiceIndex; i++)
        if (buffer->lastVoices[i] != voices[i])
            sameDestinations = 0;

    if (!sameDestinations)
    {
        FAudioSendDescriptor fAudioSendDescriptor[MAX_CHANNELS * MAX_SENDS];
        for (int i = 0; i < voiceIndex; i++)
        {
            fAudioSendDescriptor[i].Flags = 0;
            fAudioSendDescriptor[i].pOutputVoice = voices[i];
        }

        FAudioVoiceSends fAudioVoiceSends = {.SendCount = voiceIndex, .pSends = fAudioSendDescriptor};
        FAudioVoice_SetOutputVoices(buffer->fAudioSourceVoice, &fAudioVoiceSends);


        for (int i = 0; i < voiceIndex; i++)
            buffer->lastVoices[i] = voices[i];

        buffer->lastVoiceCount = voiceIndex;
    }

    for (int i = 0; i < voiceIndex; i++)
        FAudioVoice_SetOutputMatrix(buffer->fAudioSourceVoice, voices[i], buffer->outputFormat.byNumChans, 1,
                                    &matrix[i][0], FAUDIO_COMMIT_NOW);
}


/* The games keep the stack only 4-byte aligned, and these entry points run FAudio - and through it
 * SDL - on it: SEGAAPI_Init reaches SDL via FAudioCreate. Both assume 16-byte i386 alignment and
 * fault on movaps, so each entry point corrects the stack before going in. */
__attribute__((force_align_arg_pointer)) int SEGAAPI_Play(void *hHandle)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    processSends(buffer);

    // Play restarts a voice, it never resumes one, so the no-op cache must not skip its resubmit.
    buffer->forceResubmit = 1;

    SEGAAPI_UpdateBuffer(buffer, -1, -1);

    buffer->playbackStatus = PLAYBACK_STATUS_ACTIVE;
    buffer->started = 1;

    FAudioSourceVoice_Start(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_Pause(void *hHandle)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    buffer->playbackStatus = PLAYBACK_STATUS_PAUSE;
    buffer->started = 0;

    FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_Stop(void *hHandle)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    buffer->playbackStatus = PLAYBACK_STATUS_STOP;
    buffer->started = 0;

    FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);
    FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice);
    buffer->hasSubmitted = 0;
    buffer->lastData = NULL;

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_PlayWithSetup(void *hHandle, unsigned int dwNumSendRouteParams, SendRouteParamSet *pSendRouteParams,
                          unsigned int dwNumSendLevelParams, SendLevelParamSet *pSendLevelParams,
                          unsigned int dwNumVoiceParams, VoiceParamSet *pVoiceParams, unsigned int dwNumSynthParams,
                          SynthParamSet *pSynthParams)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    warnIfNotSynthVoice(buffer, hHandle, "PlayWithSetup");

    // Batch the whole setup and rebuild the send graph once at the end.
    buffer->deferSends = 1;

    for (uint32_t i = 0; i < dwNumSendRouteParams; i++)
        SEGAAPI_SetSendRouting(hHandle, pSendRouteParams[i].dwChannel, pSendRouteParams[i].dwSend,
                               pSendRouteParams[i].dwDest);

    for (uint32_t i = 0; i < dwNumSendLevelParams; i++)
        SEGAAPI_SetSendLevel(hHandle, pSendLevelParams[i].dwChannel, pSendLevelParams[i].dwSend,
                             pSendLevelParams[i].dwLevel);

    for (uint32_t i = 0; i < dwNumVoiceParams; i++)
    {
        switch (pVoiceParams[i].VoiceIoctl)
        {
        case SET_START_LOOP_OFFSET:
            SEGAAPI_SetStartLoopOffset(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_END_LOOP_OFFSET:
            SEGAAPI_SetEndLoopOffset(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_END_OFFSET:
            SEGAAPI_SetEndOffset(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_PLAY_POSITION:
            SEGAAPI_SetPlaybackPosition(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_LOOP_STATE:
            SEGAAPI_SetLoopState(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_NOTIFICATION_POINT:
            SEGAAPI_SetNotificationPoint(hHandle, pVoiceParams[i].dwParam1);
            break;
        case CLEAR_NOTIFICATION_POINT:
            SEGAAPI_ClearNotificationPoint(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_NOTIFICATION_FREQUENCY:
            SEGAAPI_SetNotificationFrequency(hHandle, pVoiceParams[i].dwParam1);
            break;
        default:
            break;
        }
    }

    for (uint32_t i = 0; i < dwNumSynthParams; i++)
        SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);

    buffer->deferSends = 0;
    processSends(buffer);

    return SEGAAPI_Play(hHandle);
}

__attribute__((force_align_arg_pointer)) PlaybackStatus SEGAAPI_GetPlaybackStatus(void *hHandle)
{
    BUFFER_CHECK(hHandle, PLAYBACK_STATUS_INVALID);

    // Demote ACTIVE to STOP only once the voice has genuinely run out of data; never demote PAUSE.
    // The old test used cumulative SamplesPlayed, which nothing resets, so voices latched STOP.
    if (buffer->playbackStatus == PLAYBACK_STATUS_ACTIVE)
    {
        FAudioVoiceState fAudioVoiceState;
        FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &fAudioVoiceState, 0);

        if (fAudioVoiceState.BuffersQueued == 0)
            buffer->playbackStatus = PLAYBACK_STATUS_STOP;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->playbackStatus;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetFormat(void *hHandle, OutputFormat *pFormat)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    debug(1, "Warning: This game used SetFormat\n");

    buffer->outputFormat.byNumChans = pFormat->byNumChans;
    buffer->outputFormat.dwSampleFormat = pFormat->dwSampleFormat;
    buffer->outputFormat.dwSampleRate = pFormat->dwSampleRate;

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_GetFormat(void *hHandle, OutputFormat *pFormat)
{
    BUFFER_CHECK(hHandle, 0);

    debug(1, "Warning: This game used GetFormat\n");

    pFormat->byNumChans = buffer->outputFormat.byNumChans;
    pFormat->dwSampleFormat = buffer->outputFormat.dwSampleFormat;
    pFormat->dwSampleRate = buffer->outputFormat.dwSampleRate;

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetSampleRate(void *hHandle, unsigned int dwSampleRate)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    if (dwSampleRate < MIN_SAMPLE_RATE || dwSampleRate > MAX_SAMPLE_RATE)
    {
        setLastStatus(SEGA_ERROR_BAD_SAMPLERATE);
        return SEGA_ERROR_BAD_SAMPLERATE;
    }

    buffer->outputFormat.dwSampleRate = dwSampleRate;
    buffer->fAudioFormat.nSamplesPerSec = dwSampleRate;
    buffer->fAudioFormat.nAvgBytesPerSec = samplesToBytes(buffer, dwSampleRate);

    // FAudio refuses SetSourceSampleRate while a buffer is queued, so stop, flush, set and requeue in
    // one go. Resume only what was genuinely playing - buffer->started is stale, and trusting it
    // restarted drained voices mid-setup, making a neighbouring sound in the shared buffer audible.
    int wasStarted = voiceIsLive(buffer);
    int requeue = buffer->hasSubmitted;

    if (requeue)
    {
        FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);
        FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice);
    }

    // FAudio refuses this outright while anything sits in src.bufferList, and swallowing that left
    // the voice running at the previous sample's rate - the whole of the next sample then plays at
    // the wrong speed, which is garbled from its first frame to its last.
    uint32_t rateResult = FAudioSourceVoice_SetSourceSampleRate(buffer->fAudioSourceVoice, dwSampleRate);

    if (rateResult != 0)
    {
        static int reported;

        if (reported < 8)
        {
            reported++;
            fprintf(stderr, "[segaapi] SetSourceSampleRate REFUSED for buffer %p: %u Hz not applied "
                            "(hasSubmitted=%d started=%d) - sample will play at the old rate\n",
                    hHandle, dwSampleRate, buffer->hasSubmitted, buffer->started);
            fflush(stderr);
        }
    }

    if (requeue)
    {
        FAudioSourceVoice_SubmitSourceBuffer(buffer->fAudioSourceVoice, &buffer->fAudioBuffer, NULL);

        if (wasStarted)
            FAudioSourceVoice_Start(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);
    }

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) unsigned int SEGAAPI_GetSampleRate(void *hHandle)
{
    BUFFER_CHECK(hHandle, 0);

    return buffer->outputFormat.dwSampleRate;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetPriority(void *hHandle, unsigned int dwPriority)
{
    // We have an unlimited number of buffers, so we can ignore this function.
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) unsigned int SEGAAPI_GetPriority(void *hHandle)
{
    // Assume everything has maximum priority and won't get cut.
    return P_MAXIMUM;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetUserData(void *hHandle, void *hUserData)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    buffer->userData = hUserData;

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) void *SEGAAPI_GetUserData(void *hHandle)
{
    BUFFER_CHECK(hHandle, NULL);

    return buffer->userData;
}

// Valid destinations are the six physical outputs, the four EAX slots, and "unused". The original
// rejects everything else, including the 6-9 gap between them.
static int isValidRouting(Routing dwDest)
{
    if (dwDest == UNUSED_PORT)
        return 1;

    if ((int)dwDest >= 0 && (int)dwDest < OUTPUT_CHANNELS)
        return 1;

    return (int)dwDest >= (int)FXSLOT0_PORT && (int)dwDest <= (int)FXSLOT0_PORT + FX_SLOTS - 1;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetSendRouting(void *hHandle, unsigned int dwChannel, unsigned int dwSend, Routing dwDest)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    // The original caps dwSend at 6 - seven sends per channel, not thirteen - validates dwChannel
    // against the buffer's own channel count, and rejects any destination outside {0-5, 10-13}.
    if (!isValidRouting(dwDest) || dwSend >= HARDWARE_SENDS || dwChannel >= buffer->outputFormat.byNumChans ||
        dwChannel >= MAX_CHANNELS)
    {
        // Rejecting a send means that path is silent, so say so - these checks are new and a game
        // that trips one would otherwise just lose a sound with no explanation.
        static int reported;

        if (reported < 8)
        {
            reported++;
            fprintf(stderr, "[segaapi] SetSendRouting rejected: channel %u send %u dest %d (buffer has %u channels)\n",
                    dwChannel, dwSend, (int)dwDest, buffer->outputFormat.byNumChans);
            fflush(stderr);
        }

        setLastStatus(!isValidRouting(dwDest)    ? SEGA_ERROR_BAD_PARAM
                      : dwSend >= HARDWARE_SENDS ? SEGA_ERROR_INVALID_SEND
                                                 : SEGA_ERROR_INVALID_CHANNEL);
        return lastStatus;
    }

    debug(1, "Info: Set send routing called for buffer %p channel %d send %d to destination %s\n", hHandle, dwChannel,
          dwSend, getRoutingString(dwDest));

    // Only the effect sends are interesting: a game drives a reverb by raising the wet send, so if
    // nothing is ever routed to ports 10-13 the effect can never be heard however it is configured.
    // PlayWithSetup re-applies the whole routing table on every voice start, so only report changes.
    if (eaxLogEnabled() && buffer->routing[dwChannel][dwSend] != dwDest && (int)dwDest >= (int)FXSLOT0_PORT &&
        (int)dwDest <= (int)FXSLOT0_PORT + FX_SLOTS - 1)
        fprintf(stderr, "[segaapi-eax] buffer %p channel %u send %u -> FX slot %d (level %08x)\n", hHandle, dwChannel,
                dwSend, (int)dwDest - (int)FXSLOT0_PORT, buffer->sendLevels[dwChannel][dwSend]);

    buffer->routing[dwChannel][dwSend] = dwDest;

    processSends(buffer);

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) Routing SEGAAPI_GetSendRouting(void *hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    BUFFER_CHECK(hHandle, UNUSED_PORT);

    // Must not leak an error code through a Routing return; report it via last-status instead.
    if (dwChannel >= MAX_CHANNELS || dwSend >= HARDWARE_SENDS)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return UNUSED_PORT;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->routing[dwChannel][dwSend];
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetSendLevel(void *hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwLevel)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    if (dwSend >= HARDWARE_SENDS)
    {
        setLastStatus(SEGA_ERROR_INVALID_SEND);
        return SEGA_ERROR_INVALID_SEND;
    }

    if (dwChannel >= buffer->outputFormat.byNumChans || dwChannel >= MAX_CHANNELS)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return SEGA_ERROR_INVALID_CHANNEL;
    }

    if (eaxLogEnabled() && buffer->sendLevels[dwChannel][dwSend] != dwLevel)
    {
        Routing dest = buffer->routing[dwChannel][dwSend];

        if ((int)dest >= (int)FXSLOT0_PORT && (int)dest <= (int)FXSLOT0_PORT + FX_SLOTS - 1)
            fprintf(stderr, "[segaapi-eax] buffer %p channel %u send %u FX slot %d level %08x -> %08x\n", hHandle,
                    dwChannel, dwSend, (int)dest - (int)FXSLOT0_PORT, buffer->sendLevels[dwChannel][dwSend], dwLevel);
    }

    // Plain linear fractions of VOL_MAX. ha10kx.ko stores these as an 8-bit mini-float, but that round
    // trip preserves the linear value - applying the decode squashes it ~24 dB, audible in OutRun 2.
    buffer->sendLevels[dwChannel][dwSend] = dwLevel;

    processSends(buffer);

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) unsigned int SEGAAPI_GetSendLevel(void *hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    BUFFER_CHECK(hHandle, 0);

    if (dwChannel >= MAX_CHANNELS || dwSend >= HARDWARE_SENDS)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return 0;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->sendLevels[dwChannel][dwSend];
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetChannelVolume(void *hHandle, unsigned int dwChannel, unsigned int dwVolume)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    if (dwChannel >= MAX_CHANNELS)
        return SEGA_ERROR_INVALID_CHANNEL;

    buffer->channelVolumes[dwChannel] = dwVolume;

    processSends(buffer);

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) unsigned int SEGAAPI_GetChannelVolume(void *hHandle, unsigned int dwChannel)
{
    BUFFER_CHECK(hHandle, 0);

    if (dwChannel >= MAX_CHANNELS)
        return 0;

    return buffer->channelVolumes[dwChannel];
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetPlaybackPosition(void *hHandle, unsigned int dwPlaybackPos)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    if (dwPlaybackPos > buffer->size)
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return SEGA_ERROR_BAD_PARAM;
    }

    // Re-submit with a new PlayBegin rather than recreating the voice, which loses the send matrix, volumes
    // and synth parameters. An explicit seek restarts even at an unchanged offset: CRI replays pooled
    // voices from the same offset expecting them to start over.
    int wasStarted = voiceIsLive(buffer);

    FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);
    FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice);

    buffer->playPosition = dwPlaybackPos;

    if (buffer->hasSubmitted)
    {
        unsigned int frame = bytesToSamples(buffer, (int)dwPlaybackPos);

        buffer->fAudioBuffer.PlayBegin = frame;

        if (buffer->fAudioBuffer.PlayLength > 0 && frame > buffer->lastPlayBegin)
        {
            unsigned int consumed = frame - buffer->lastPlayBegin;
            buffer->fAudioBuffer.PlayLength =
                consumed < buffer->lastPlayLength ? buffer->lastPlayLength - consumed : 0;
        }

        FAudioSourceVoice_SubmitSourceBuffer(buffer->fAudioSourceVoice, &buffer->fAudioBuffer, NULL);

        if (wasStarted)
        {
            FAudioSourceVoice_Start(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);
            buffer->started = 1;
        }
    }

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) unsigned int SEGAAPI_GetPlaybackPosition(void *hHandle)
{
    BUFFER_CHECK(hHandle, 0);

    FAudioVoiceState fAudioVoiceState;
    FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &fAudioVoiceState, 0);

    // Count from the last submit: SamplesPlayed is a per-voice lifetime counter nothing here resets.
    uint64_t elapsed = fAudioVoiceState.SamplesPlayed >= buffer->playOrigin
                           ? fAudioVoiceState.SamplesPlayed - buffer->playOrigin
                           : 0;

    unsigned int frame = buffer->lastPlayBegin;

    if (buffer->lastLoopInfinite && buffer->lastLoopLength > 0)
    {
        unsigned int untilLoop = buffer->lastLoopBegin > buffer->lastPlayBegin
                                     ? buffer->lastLoopBegin - buffer->lastPlayBegin
                                     : 0;

        if (elapsed < untilLoop)
            frame = buffer->lastPlayBegin + (unsigned int)elapsed;
        else
            frame = buffer->lastLoopBegin + (unsigned int)((elapsed - untilLoop) % buffer->lastLoopLength);
    }
    else if (buffer->lastPlayLength > 0)
    {
        frame = buffer->lastPlayBegin + (unsigned int)(elapsed < buffer->lastPlayLength ? elapsed
                                                                                        : buffer->lastPlayLength);
    }

    unsigned int position = samplesToBytes(buffer, (int)frame);

    if (position >= buffer->size)
        position = buffer->size ? position % buffer->size : 0;


    return position;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetNotificationFrequency(void *hHandle, unsigned int dwFrameCount)
{
    debug(1, "Warning: This game is trying to use the notification frequency API\n");
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetNotificationPoint(void *hHandle, unsigned int dwBufferOffset)
{
    debug(1, "Warning: This game is trying to use the notification frequency API\n");
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_ClearNotificationPoint(void *hHandle, unsigned int dwBufferOffset)
{
    debug(1, "Warning: This game is trying to use the notification frequency API\n");
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetStartLoopOffset(void *hHandle, unsigned int dwOffset)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    // Playback normally begins at the loop start; a seek overrides it until the next loop change.
    buffer->playPosition = dwOffset;

    buffer->startLoop = dwOffset;

    SEGAAPI_UpdateBuffer(hHandle, -1, -1);

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) unsigned int SEGAAPI_GetStartLoopOffset(void *hHandle)
{
    BUFFER_CHECK(hHandle, 0);

    return buffer->startLoop;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetEndLoopOffset(void *hHandle, unsigned int dwOffset)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    buffer->endLoop = dwOffset;

    SEGAAPI_UpdateBuffer(hHandle, -1, -1);

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) unsigned int SEGAAPI_GetEndLoopOffset(void *hHandle)
{
    BUFFER_CHECK(hHandle, 0);

    return buffer->endLoop;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetEndOffset(void *hHandle, unsigned int dwOffset)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    buffer->endOffset = dwOffset;

    SEGAAPI_UpdateBuffer(hHandle, -1, -1);

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) unsigned int SEGAAPI_GetEndOffset(void *hHandle)
{
    BUFFER_CHECK(hHandle, 0);

    return buffer->endOffset;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetLoopState(void *hHandle, int bDoContinuousLooping)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    buffer->bDoContinuousLooping = bDoContinuousLooping;

    SEGAAPI_UpdateBuffer(hHandle, -1, -1);

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_GetLoopState(void *hHandle)
{
    BUFFER_CHECK(hHandle, 0);

    return buffer->bDoContinuousLooping;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_UpdateBuffer(void *hHandle, unsigned int dwStartOffset, unsigned int dwLength)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    // Ignore the call if we're waiting to update the output format
    if (buffer->updateOutputFormat)
        return SEGA_SUCCESS;

    // A seek overrides the loop start until the next loop change.
    unsigned int playBegin = buffer->playPosition < buffer->endOffset ? buffer->playPosition : buffer->startLoop;

    // FAudio needs the loop region inside the play region; on the board they are independent registers.
    unsigned int playEnd = buffer->endOffset;

    if (buffer->bDoContinuousLooping && buffer->endLoop > buffer->startLoop && buffer->endLoop > playEnd)
        playEnd = buffer->endLoop;

    if (playEnd > buffer->size)
        playEnd = buffer->size;

    if (playEnd < playBegin)
        playEnd = playBegin;

    unsigned int newPlayBegin = bytesToSamples(buffer, (int)playBegin);
    unsigned int newPlayLength = bytesToSamples(buffer, (int)(playEnd - playBegin));
    unsigned int newLoopBegin = bytesToSamples(buffer, (int)buffer->startLoop);
    unsigned int newLoopLength = bytesToSamples(buffer, (int)(buffer->endLoop - buffer->startLoop));

    // On hardware this is only a handle check - the card DMAs from the game's memory - so the game calls
    // it every tick. Only touch the voice when something changed, or streamed music is destroyed.
    int unchanged = buffer->hasSubmitted && buffer->data == buffer->lastData &&
                    newPlayBegin == buffer->lastPlayBegin && newPlayLength == buffer->lastPlayLength &&
                    newLoopBegin == buffer->lastLoopBegin && newLoopLength == buffer->lastLoopLength &&
                    buffer->bDoContinuousLooping == buffer->lastLoopInfinite;


    if (unchanged && !buffer->forceResubmit)
    {
        setLastStatus(SEGA_SUCCESS);
        return SEGA_SUCCESS;
    }

    // buffer->started is never cleared by this game, so ask FAudio what is actually playing.
    int reallyPlaying = voiceIsLive(buffer);

    // On the board these offsets are registers; writing one mid-voice does not restart anything. We
    // cannot rewrite a queued buffer's bounds in FAudio, so the honest equivalent is to leave a live
    // voice alone and let the change land at the next Play, which always forces a resubmit.
    if (reallyPlaying && !buffer->forceResubmit)
    {
        setLastStatus(SEGA_SUCCESS);
        return SEGA_SUCCESS;
    }

    // Play wanted a restart and the region is byte-for-byte the region we last submitted: this is
    // precisely the recycled-voice case that used to resume mid-sample.
    buffer->forceResubmit = 0;

    buffer->lastData = buffer->data;
    buffer->lastPlayBegin = newPlayBegin;
    buffer->lastPlayLength = newPlayLength;
    buffer->lastLoopBegin = newLoopBegin;
    buffer->lastLoopLength = newLoopLength;
    buffer->lastLoopInfinite = buffer->bDoContinuousLooping;

    // Flush keeps the playing buffer unless the voice is stopped first, which left two buffers queued.
    if (reallyPlaying)
        FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);

    // Stop leaves the queue alone - it only clears src.active - so this reads the same depth either
    // side of it. Past the cache above, so this is not a per-tick cost.
    FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice);

    memset(&buffer->fAudioBuffer, 0, sizeof(FAudioBuffer));

    buffer->fAudioBuffer.Flags = 0;
    buffer->fAudioBuffer.AudioBytes = buffer->size;
    buffer->fAudioBuffer.pAudioData = buffer->data;
    buffer->fAudioBuffer.PlayBegin = newPlayBegin;
    buffer->fAudioBuffer.PlayLength = newPlayLength;
    buffer->fAudioBuffer.LoopBegin = 0;
    buffer->fAudioBuffer.LoopLength = 0;
    buffer->fAudioBuffer.LoopCount = 0;
    buffer->fAudioBuffer.pContext = buffer;

    if (buffer->bDoContinuousLooping && newLoopLength > 0)
    {
        buffer->fAudioBuffer.LoopBegin = newLoopBegin;
        buffer->fAudioBuffer.LoopLength = newLoopLength;
        buffer->fAudioBuffer.LoopCount = FAUDIO_LOOP_INFINITE;
    }

    // FAudio rejects out-of-range play or loop bounds; a swallowed failure leaves the voice empty.
    buffer->fAudioBuffer.pContext = buffer;

    uint32_t submitted = FAudioSourceVoice_SubmitSourceBuffer(buffer->fAudioSourceVoice, &buffer->fAudioBuffer, NULL);

    if (submitted != 0)
    {
        static int reported;

        if (reported < 8)
        {
            reported++;
            fprintf(stderr, "[segaapi] SubmitSourceBuffer rejected for buffer %p: play %u+%u loop %u+%u (size %zu, "
                            "%u ch) - voice left empty\n",
                    (void *)buffer, newPlayBegin, newPlayLength, newLoopBegin, newLoopLength, buffer->size,
                    buffer->outputFormat.byNumChans);
            fflush(stderr);
        }

        buffer->lastData = NULL;
        buffer->hasSubmitted = 0;

        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return SEGA_ERROR_BAD_PARAM;
    }

    buffer->hasSubmitted = 1;

    // Latch the origin so GetPlaybackPosition can report a position relative to THIS submit.
    {
        FAudioVoiceState originState;
        FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &originState, 0);
        buffer->playOrigin = originState.SamplesPlayed;
    }

    // Resume only a voice we actually interrupted. Starting a drained one here would begin the next
    // sample at whichever setup call happened to change an offset, before the game called Play.
    if (reallyPlaying)
        FAudioSourceVoice_Start(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetSynthParam(void *hHandle, SynthParams param, int lPARWValue)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    warnIfNotSynthVoice(buffer, hHandle, "SetSynthParam");

    buffer->synthParams[param] = lPARWValue;

    switch (param)
    {
    // Confirmed against ha10kx.ko: EMU10KXTransApp::TransCbToInitialAtten takes centibels, so the
    // value here is 1/10 dB of attenuation and this conversion is right after all.
    case ATTENUATION:
    {
        float volumeInDecibels = 0.0f - lPARWValue / 10.0f;
        float volumeInGain = volumeInDecibels > -100.f ? powf(10.0f, volumeInDecibels * 0.05f) : 0;
        FAudioVoice_SetVolume(buffer->fAudioSourceVoice, volumeInGain, FAUDIO_COMMIT_NOW);
    }
    break;

    case PITCH:
    {
        float semiTones = lPARWValue / 100.0f;
        float freqRatio = powf(2.0f, semiTones / 12.0f);
        FAudioSourceVoice_SetFrequencyRatio(buffer->fAudioSourceVoice, freqRatio, FAUDIO_COMMIT_NOW);
    }
    break;

    default:
        break;
    }

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_GetSynthParam(void *hHandle, SynthParams param)
{
    BUFFER_CHECK(hHandle, 0);

    return buffer->synthParams[param];
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetSynthParamMultiple(void *hHandle, unsigned int dwNumParams, SynthParamSet *pSynthParams)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);
    (void)buffer;

    for (uint32_t i = 0; i < dwNumParams; i++)
    {
        SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);
    }

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_GetSynthParamMultiple(void *hHandle, unsigned int dwNumParams, SynthParamSet *pSynthParams)
{

    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    for (uint32_t i = 0; i < dwNumParams; i++)
    {
        pSynthParams[i].lPARWValue = buffer->synthParams[pSynthParams[i].param];
    }

    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetReleaseState(void *hHandle, int bSet)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    warnIfNotSynthVoice(buffer, hHandle, "SetReleaseState");

    // Note-off. CRI never sets VOL_ENV_RELEASE, so the board has no release time to honour and the
    // voice ends promptly. ExitLoop instead played out the rest of the region - up to 13 s at full
    // scale per voice, and OutRun releases ~25 of them at the end of a race.
    if (bSet)
    {
        FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);

        // The queue is left intact deliberately - only the status is ended, so the game's
        // wait-for-STOP completes instead of spinning on a voice that still has data queued.
        buffer->started = 0;
        buffer->playbackStatus = PLAYBACK_STATUS_STOP;
    }

    buffer->releaseState = bSet;

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

// The board reports only SIGNED_16PCM as supported, so UNSIGNED_8PCM is rejected outright.
#define SUPPORTED_FORMATS SIGNED_16PCM

__attribute__((force_align_arg_pointer)) int SEGAAPI_CreateBuffer(BufferConfig *pConfig, BufferCallback pCallback, unsigned int dwFlags, void **phHandle)
{
    HANDLE_CHECK(pConfig, SEGA_ERROR_BAD_CONFIG);
    HANDLE_CHECK(phHandle, SEGA_ERROR_BAD_HANDLE);

    if (fAudio == NULL)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return SEGA_ERROR_INIT_FAILED;
    }

    // The original validates the whole config against the board's capabilities before allocating
    // anything; this accepted everything and returned success.
    if ((pConfig->dwSampleFormat & SUPPORTED_FORMATS) == 0)
    {
        fprintf(stderr, "[segaapi] CreateBuffer rejected: sample format 0x%x unsupported (only SIGNED_16PCM)\n",
                pConfig->dwSampleFormat);
        setLastStatus(SEGA_ERROR_BAD_CONFIG);
        return SEGA_ERROR_BAD_CONFIG;
    }

    if (pConfig->dwSampleRate < MIN_SAMPLE_RATE || pConfig->dwSampleRate > MAX_SAMPLE_RATE)
    {
        fprintf(stderr, "[segaapi] CreateBuffer rejected: sample rate %u outside %u-%u\n", pConfig->dwSampleRate,
                MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);
        setLastStatus(SEGA_ERROR_BAD_SAMPLERATE);
        return SEGA_ERROR_BAD_SAMPLERATE;
    }

    if (pConfig->byNumChans == 0 || pConfig->byNumChans > 2)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return SEGA_ERROR_INVALID_CHANNEL;
    }

    if (pConfig->mapData.dwSize < pConfig->mapData.dwOffset || pConfig->mapData.dwSize == 0)
    {
        setLastStatus(SEGA_ERROR_BAD_CONFIG);
        return SEGA_ERROR_BAD_CONFIG;
    }

    // Caller-supplied and mapped memory are mutually exclusive, and a synth buffer must be mono.
    if ((dwFlags & (ALLOC_USER_MEM | USE_MAPPED_MEM)) == (ALLOC_USER_MEM | USE_MAPPED_MEM) ||
        ((dwFlags & 1) != 0 && pConfig->byNumChans != 1))
    {
        fprintf(stderr, "[segaapi] CreateBuffer rejected: flags 0x%x with %u channels\n", dwFlags,
                pConfig->byNumChans);
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return SEGA_ERROR_BAD_PARAM;
    }

    // calloc, not malloc: not every field is assigned below - synthParams among them - and a
    // recycled heap block made behaviour differ from one run to the next.
    Buffer *buffer = (Buffer *)calloc(1, sizeof(Buffer));

    if (buffer == NULL)
    {
        setLastStatus(SEGA_ERROR_OUT_OF_MEMORY);
        return SEGA_ERROR_OUT_OF_MEMORY;
    }

    buffer->magic = BUFFER_MAGIC;
    buffer->synthCapable = (dwFlags & 1) != 0;
    buffer->playbackStatus = PLAYBACK_STATUS_STOP;
    buffer->callback = pCallback;

    buffer->outputFormat.dwSampleRate = pConfig->dwSampleRate;
    buffer->outputFormat.dwSampleFormat = pConfig->dwSampleFormat;
    buffer->outputFormat.byNumChans = pConfig->byNumChans;
    buffer->updateOutputFormat = 0;

    buffer->userData = pConfig->hUserData;
    buffer->size = pConfig->mapData.dwSize;
    pConfig->mapData.dwOffset = 0;

    debug(1, "Info: Buffer created with %d channels\n", pConfig->byNumChans);

    buffer->fAudioVoiceCallback =
        (FAudioVoiceCallbackWithBuffer *)calloc(1, sizeof(FAudioVoiceCallbackWithBuffer));
    buffer->fAudioVoiceCallback->OnBufferStart = StaticOnBufferStart;
    buffer->fAudioVoiceCallback->OnBufferEnd = StaticOnBufferEnd;
    buffer->fAudioVoiceCallback->OnBufferStart = StaticOnBufferStart;
    buffer->fAudioVoiceCallback->OnLoopEnd = StaticOnLoopEnd;
    buffer->fAudioVoiceCallback->OnStreamEnd = StaticOnStreamEnd;
    buffer->fAudioVoiceCallback->OnVoiceError = StaticOnVoiceError;
    buffer->fAudioVoiceCallback->OnVoiceProcessingPassEnd = StaticOnVoiceProcessingPassEnd;
    buffer->fAudioVoiceCallback->OnVoiceProcessingPassStart = StaticOnVoiceProcessingPassStart;
    buffer->fAudioVoiceCallback->buffer = buffer;

    // Use buffer supplied by caller
    if (dwFlags & ALLOC_USER_MEM)
    {
        buffer->data = (uint8_t *)pConfig->mapData.hBufferHdr;
    }
    // Reuse buffer
    else if (dwFlags & USE_MAPPED_MEM)
    {
        buffer->data = (uint8_t *)pConfig->mapData.hBufferHdr;
    }
    // Allocate new buffer (caller will fill it later)
    else
    {
        buffer->data = (uint8_t *)calloc(1, buffer->size);
        buffer->ownsData = 1;
    }

    pConfig->mapData.hBufferHdr = buffer->data;

    buffer->fAudioFormat.wFormatTag = 1;
    buffer->fAudioFormat.nChannels = buffer->outputFormat.byNumChans;
    buffer->fAudioFormat.nSamplesPerSec = buffer->outputFormat.dwSampleRate;
    buffer->fAudioFormat.nAvgBytesPerSec = samplesToBytes(buffer, buffer->outputFormat.dwSampleRate);
    buffer->fAudioFormat.nBlockAlign = samplesToBytes(buffer, 1);
    buffer->fAudioFormat.wBitsPerSample = pConfig->dwSampleFormat == SIGNED_16PCM ? 16 : 8;
    // cbSize counts the extra format bytes that follow the struct, not the struct itself. FAudio
    // copies sizeof(FAudioWaveFormatEx) + cbSize bytes out of it, so claiming 18 here read 18 bytes
    // past the field and into the rest of the Buffer. PCM has no trailing block.
    buffer->fAudioFormat.cbSize = 0;

    pthread_mutex_lock(&fAudioMutex);
    FAudio_CreateSourceVoice(fAudio, &buffer->fAudioSourceVoice, &buffer->fAudioFormat, 0, FAUDIO_MAX_FREQ_RATIO,
                             (FAudioVoiceCallback *)buffer->fAudioVoiceCallback, NULL, NULL);
    pthread_mutex_unlock(&fAudioMutex);

    memset(&buffer->fAudioBuffer, 0, sizeof(FAudioBuffer));

    buffer->startLoop = 0;
    buffer->endOffset = buffer->size;
    buffer->endLoop = buffer->size;
    buffer->bDoContinuousLooping = 0;

    for (int channel = 0; channel < MAX_CHANNELS; channel++)
    {
        buffer->channelVolumes[channel] = VOL_MAX;
        for (int send = 0; send < MAX_SENDS; send++)
        {
            buffer->sendLevels[channel][send] = 0;
            buffer->routing[channel][send] = UNUSED_PORT;
        }
    }

    switch (pConfig->byNumChans)
    {
    case 1:
        buffer->routing[0][0] = FRONT_LEFT_PORT;
        buffer->routing[0][1] = FRONT_RIGHT_PORT;
        break;

    case 2:
        buffer->routing[0][0] = FRONT_LEFT_PORT;
        buffer->routing[1][1] = FRONT_RIGHT_PORT;
        break;

    default:
        break;
    }

    if (!registerBuffer(buffer))
    {
        FAudioVoice_DestroyVoice(buffer->fAudioSourceVoice);
        free(buffer->fAudioVoiceCallback);
        if (buffer->ownsData)
            free(buffer->data);
        free(buffer);
        setLastStatus(SEGA_ERROR_NO_RESOURCES);
        return SEGA_ERROR_NO_RESOURCES;
    }


    *phHandle = buffer;

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_DestroyBuffer(void *hHandle)
{
    BUFFER_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    debug(1, "SEGAAPI_DestroyBuffer called\n");

    if (buffer->playbackStatus != PLAYBACK_STATUS_STOP)
        SEGAAPI_Stop(hHandle);

    // Drop from the registry and clear the magic before freeing, so a handle used after this fails
    // validation instead of touching freed memory. The buffer, its data and its callback used to leak.
    unregisterBuffer(buffer);
    buffer->magic = 0;

    FAudioVoice_DestroyVoice(buffer->fAudioSourceVoice);

    free(buffer->fAudioVoiceCallback);

    if (buffer->ownsData)
        free(buffer->data);

    free(buffer);

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

// FAudio ships an I3DL2 preset for the first 23 EAX environments; DRUGGED, DIZZY and PSYCHOTIC have
// no counterpart and fall back to the plate. EAX Reverb's core block is I3DL2 field for field.
static const FAudioFXReverbI3DL2Parameters eaxEnvironmentPresets[] = {
    {100, -1000, -100,0.0f, 1.49f,0.83f, -2602,0.007f, 200,0.011f,100.0f,100.0f,5000.0f}, /* GENERIC */
    {100, -1000,-6000,0.0f, 0.17f,0.10f, -1204,0.001f, 207,0.002f,100.0f,100.0f,5000.0f}, /* PADDEDCELL */
    {100, -1000, -454,0.0f, 0.40f,0.83f, -1646,0.002f, 53,0.003f,100.0f,100.0f,5000.0f}, /* ROOM */
    {100, -1000,-1200,0.0f, 1.49f,0.54f, -370,0.007f, 1030,0.011f,100.0f, 60.0f,5000.0f}, /* BATHROOM */
    {100, -1000,-6000,0.0f, 0.50f,0.10f, -1376,0.003f, -1104,0.004f,100.0f,100.0f,5000.0f}, /* LIVINGROOM */
    {100, -1000, -300,0.0f, 2.31f,0.64f, -711,0.012f, 83,0.017f,100.0f,100.0f,5000.0f}, /* STONEROOM */
    {100, -1000, -476,0.0f, 4.32f,0.59f, -789,0.020f, -289,0.030f,100.0f,100.0f,5000.0f}, /* AUDITORIUM */
    {100, -1000, -500,0.0f, 3.92f,0.70f, -1230,0.020f, -2,0.029f,100.0f,100.0f,5000.0f}, /* CONCERTHALL */
    {100, -1000, 0,0.0f, 2.91f,1.30f, -602,0.015f, -302,0.022f,100.0f,100.0f,5000.0f}, /* CAVE */
    {100, -1000, -698,0.0f, 7.24f,0.33f, -1166,0.020f, 16,0.030f,100.0f,100.0f,5000.0f}, /* ARENA */
    {100, -1000,-1000,0.0f,10.05f,0.23f, -602,0.020f, 198,0.030f,100.0f,100.0f,5000.0f}, /* HANGAR */
    {100, -1000,-4000,0.0f, 0.30f,0.10f, -1831,0.002f, -1630,0.030f,100.0f,100.0f,5000.0f}, /* CARPETEDHALLWAY */
    {100, -1000, -300,0.0f, 1.49f,0.59f, -1219,0.007f, 441,0.011f,100.0f,100.0f,5000.0f}, /* HALLWAY */
    {100, -1000, -237,0.0f, 2.70f,0.79f, -1214,0.013f, 395,0.020f,100.0f,100.0f,5000.0f}, /* STONECORRIDOR */
    {100, -1000, -270,0.0f, 1.49f,0.86f, -1204,0.007f, -4,0.011f,100.0f,100.0f,5000.0f}, /* ALLEY */
    {100, -1000,-3300,0.0f, 1.49f,0.54f, -2560,0.162f, -613,0.088f, 79.0f,100.0f,5000.0f}, /* FOREST */
    {100, -1000, -800,0.0f, 1.49f,0.67f, -2273,0.007f, -2217,0.011f, 50.0f,100.0f,5000.0f}, /* CITY */
    {100, -1000,-2500,0.0f, 1.49f,0.21f, -2780,0.300f, -2014,0.100f, 27.0f,100.0f,5000.0f}, /* MOUNTAINS */
    {100, -1000,-1000,0.0f, 1.49f,0.83f,-10000,0.061f, 500,0.025f,100.0f,100.0f,5000.0f}, /* QUARRY */
    {100, -1000,-2000,0.0f, 1.49f,0.50f, -2466,0.179f, -2514,0.100f, 21.0f,100.0f,5000.0f}, /* PLAIN */
    {100, -1000, 0,0.0f, 1.65f,1.50f, -1363,0.008f, -1153,0.012f,100.0f,100.0f,5000.0f}, /* PARKINGLOT */
    {100, -1000,-1000,0.0f, 2.81f,0.14f, 429,0.014f, 648,0.021f, 80.0f, 60.0f,5000.0f}, /* SEWERPIPE */
    {100, -1000,-4000,0.0f, 1.49f,0.10f, -449,0.007f, 1700,0.011f,100.0f,100.0f,5000.0f}, /* UNDERWATER */
    {100, -1000, -200,0.0f, 1.30f,0.90f, 0,0.002f, 0,0.010f,100.0f, 75.0f,5000.0f}, /* DRUGGED -> PLATE */
    {100, -1000, -200,0.0f, 1.30f,0.90f, 0,0.002f, 0,0.010f,100.0f, 75.0f,5000.0f}, /* DIZZY -> PLATE */
    {100, -1000, -200,0.0f, 1.30f,0.90f, 0,0.002f, 0,0.010f,100.0f, 75.0f,5000.0f}, /* PSYCHOTIC -> PLATE */
};

static const char *const eaxEnvironmentNames[] = {
    "GENERIC",  "PADDEDCELL", "ROOM",   "BATHROOM",        "LIVINGROOM", "STONEROOM", "AUDITORIUM",
    "CONCERTHALL", "CAVE",    "ARENA",  "HANGAR",          "CARPETEDHALLWAY", "HALLWAY",
    "STONECORRIDOR", "ALLEY", "FOREST", "CITY",            "MOUNTAINS",  "QUARRY",    "PLAIN",
    "PARKINGLOT", "SEWERPIPE", "UNDERWATER", "DRUGGED",    "DIZZY",      "PSYCHOTIC",
};

// EAX 4.0 EAXREVERBPROPERTIES, laid out exactly as the games pass it. Its size is what lets us
// recognise an ALLPARAMETERS write without having to trust a property id.
typedef struct EAXVECTOR
{
    float x, y, z;
} EAXVECTOR;

typedef struct EAXREVERBPROPERTIES
{
    unsigned long ulEnvironment;
    float flEnvironmentSize;
    float flEnvironmentDiffusion;
    long lRoom;
    long lRoomHF;
    long lRoomLF;
    float flDecayTime;
    float flDecayHFRatio;
    float flDecayLFRatio;
    long lReflections;
    float flReflectionsDelay;
    EAXVECTOR vReflectionsPan;
    long lReverb;
    float flReverbDelay;
    EAXVECTOR vReverbPan;
    float flEchoTime;
    float flEchoDepth;
    float flModulationTime;
    float flModulationDepth;
    float flAirAbsorptionHF;
    float flHFReference;
    float flLFReference;
    float flRoomRolloffFactor;
    unsigned long ulFlags;
} EAXREVERBPROPERTIES;

static void applyI3DL2(int slot, const FAudioFXReverbI3DL2Parameters *i3dl2)
{
    if (slot < 0 || slot >= FX_SLOTS || fAudioFxVoices[slot] == NULL)
        return;

    FAudioFXReverbParameters native;

    ReverbConvertI3DL2ToNative(i3dl2, &native);
    FAudioVoice_SetEffectParameters(fAudioFxVoices[slot], 0, &native, sizeof(native), FAUDIO_COMMIT_NOW);
}

static void applyEaxEnvironment(int slot, unsigned long environment)
{
    const unsigned long count = sizeof(eaxEnvironmentPresets) / sizeof(eaxEnvironmentPresets[0]);
    unsigned long index = environment < count ? environment : 0;

    applyI3DL2(slot, &eaxEnvironmentPresets[index]);

    if (eaxLogEnabled())
        fprintf(stderr, "[segaapi-eax] FX slot %d environment %lu (%s)\n", slot, environment,
                eaxEnvironmentNames[index]);
}

// The EAX 4/5 extras - RoomLF, DecayLFRatio, the pans, echo, modulation, air absorption,
// LFReference, flags - have no counterpart in FAudio's reverb and are dropped.
static void applyEaxReverb(int slot, const EAXREVERBPROPERTIES *eax)
{
    FAudioFXReverbI3DL2Parameters i3dl2;

    i3dl2.WetDryMix = 100.0f; // this submix carries only the wet path; balance is done by send level
    i3dl2.Room = (int32_t)eax->lRoom;
    i3dl2.RoomHF = (int32_t)eax->lRoomHF;
    i3dl2.RoomRolloffFactor = eax->flRoomRolloffFactor;
    i3dl2.DecayTime = eax->flDecayTime;
    i3dl2.DecayHFRatio = eax->flDecayHFRatio;
    i3dl2.Reflections = (int32_t)eax->lReflections;
    i3dl2.ReflectionsDelay = eax->flReflectionsDelay;
    i3dl2.Reverb = (int32_t)eax->lReverb;
    i3dl2.ReverbDelay = eax->flReverbDelay;
    i3dl2.Diffusion = eax->flEnvironmentDiffusion * 100.0f; // EAX is 0..1, I3DL2 is a percentage
    i3dl2.Density = 100.0f;
    i3dl2.HFReference = eax->flHFReference;

    applyI3DL2(slot, &i3dl2);

    if (eaxLogEnabled())
        fprintf(stderr, "[segaapi-eax] FX slot %d reverb applied: decay %.2fs room %ldmB reflections %ldmB "
                        "reverb %ldmB diffusion %.2f\n",
                slot, eax->flDecayTime, eax->lRoom, eax->lReflections, eax->lReverb, eax->flEnvironmentDiffusion);
}

// An EAX slot with no effect loaded produces no output at all. Without this the bus would pass the
// send through unprocessed, which sounds like "something changed" but is just a dry duplicate.
static void updateFxSlotGain(int slot)
{
    if (slot < 0 || slot >= FX_SLOTS || fAudioFxVoices[slot] == NULL)
        return;

    if (fxSlotHasEffect[slot] && !fxSlotEffectEnabled[slot])
        FAudioVoice_EnableEffect(fAudioFxVoices[slot], 0, FAUDIO_COMMIT_NOW);
    else if (!fxSlotHasEffect[slot] && fxSlotEffectEnabled[slot])
        FAudioVoice_DisableEffect(fAudioFxVoices[slot], 0, FAUDIO_COMMIT_NOW);

    fxSlotEffectEnabled[slot] = fxSlotHasEffect[slot];

    FAudioVoice_SetVolume(fAudioFxVoices[slot], fxSlotHasEffect[slot] ? fxSlotVolume[slot] : 0.0f,
                          FAUDIO_COMMIT_NOW);
}



// Bit 0x10000 set means a property of the FX slot itself, clear means one of the effect loaded into it.
// Confirmed against ID5: 0x10002 is slot LOADEFFECT, a bare 0x2 is EAXREVERB_ENVIRONMENT.
#define EAX_SLOT_PROPERTY 0x10000u
#define EAX_PROPERTY_ID(p) ((unsigned long)(p) & 0xFFFFu)
#define EAX_IS_SLOT_PROPERTY(p) (((unsigned long)(p) & EAX_SLOT_PROPERTY) != 0)
#define EAXFXSLOT_LOADEFFECT 2
#define EAXFXSLOT_VOLUME 3
#define EAXREVERB_ALLPARAMETERS 1
#define EAXREVERB_ENVIRONMENT 2

static int fxSlotForGuid(const GUID *guid)
{
    static const GUID *const slots[FX_SLOTS] = {&EAXPROPERTYID_EAX40_FXSlot0, &EAXPROPERTYID_EAX40_FXSlot1,
                                                &EAXPROPERTYID_EAX40_FXSlot2, &EAXPROPERTYID_EAX40_FXSlot3};

    for (int i = 0; i < FX_SLOTS; i++)
        if (memcmp(guid, slots[i], sizeof(GUID)) == 0)
            return i;

    return -1;
}

// DEBUG_LEVEL is a compile-time -1, so debug() is dead in shipped builds. Gate this on the
// environment instead, so a trace can be captured without rebuilding: SEGAAPI_EAX_LOG=1
static int eaxLogEnabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
    {
        const char *value = getenv("SEGAAPI_EAX_LOG");
        enabled = (value != NULL && value[0] != '\0' && value[0] != '0') ? 1 : 0;
    }

    return enabled;
}

static void logEaxProperty(const char *what, const GUID *guid, unsigned long ulProperty, void *pData,
                           unsigned long ulDataSize)
{
    if (!eaxLogEnabled())
        return;

    fprintf(stderr, "[segaapi-eax] %s guid=%08lx-%04x-%04x property=%lu (raw 0x%lx) size=%lu data=", what,
            guid->Data1, guid->Data2, guid->Data3, EAX_PROPERTY_ID(ulProperty), ulProperty, ulDataSize);

    for (unsigned long i = 0; pData != NULL && i < ulDataSize && i < 160; i++)
        fprintf(stderr, "%02x", ((unsigned char *)pData)[i]);

    fprintf(stderr, "\n");
    fflush(stderr);
}

// The original handles EAXPROPERTYID_EAX40_SEGA_Custom locally and forwards the rest to the driver's
// AL context. This returned success without doing anything, so no effect was ever loaded.
__attribute__((force_align_arg_pointer)) int SEGAAPI_SetGlobalEAXProperty(GUID *guid, unsigned long ulProperty, void *pData, unsigned long ulDataSize)
{
    if (guid == NULL)
    {
        setLastStatus(SEGA_ERROR_BAD_POINTER);
        return SEGA_ERROR_BAD_POINTER;
    }

    static int announced = 0;
    if (!announced)
    {
        announced = 1;
        fprintf(stderr, "[segaapi] first SEGAAPI_SetGlobalEAXProperty call (EAX trace %s)\n",
                eaxLogEnabled() ? "on" : "off - set SEGAAPI_EAX_LOG=1 for detail");
        fflush(stderr);
    }

    logEaxProperty("set", guid, ulProperty, pData, ulDataSize);

    int slot = fxSlotForGuid(guid);

    if (slot >= 0)
        fxSlotConfiguredByGame[slot] = 1;

    if (slot < 0 || fAudioFxVoices[slot] == NULL)
    {
        setLastStatus(SEGA_SUCCESS);
        return SEGA_SUCCESS;
    }

    // ---- properties of the slot itself ----
    if (EAX_IS_SLOT_PROPERTY(ulProperty) && EAX_PROPERTY_ID(ulProperty) == EAXFXSLOT_LOADEFFECT && pData != NULL &&
        ulDataSize >= sizeof(GUID))
    {
        int isReverb = memcmp(pData, &EAX_REVERB_EFFECT, sizeof(GUID)) == 0;

        fxSlotHasEffect[slot] = isReverb;
        updateFxSlotGain(slot);

        if (eaxLogEnabled())
            fprintf(stderr, "[segaapi-eax] FX slot %d %s\n", slot,
                    isReverb ? "loaded reverb" : "cleared (not reverb / EAX_NULL_GUID)");

        setLastStatus(SEGA_SUCCESS);
        return SEGA_SUCCESS;
    }

    if (EAX_IS_SLOT_PROPERTY(ulProperty) && EAX_PROPERTY_ID(ulProperty) == EAXFXSLOT_VOLUME && pData != NULL &&
        ulDataSize >= sizeof(long))
    {
        // EAX slot volume is millibels of attenuation.
        fxSlotVolume[slot] = powf(10.0f, (float)(*(long *)pData) / 2000.0f);
        updateFxSlotGain(slot);
        setLastStatus(SEGA_SUCCESS);
        return SEGA_SUCCESS;
    }

    // ---- properties of the reverb loaded into the slot ----
    if (!EAX_IS_SLOT_PROPERTY(ulProperty) && pData != NULL && fxSlotHasEffect[slot])
    {
        if (EAX_PROPERTY_ID(ulProperty) == EAXREVERB_ENVIRONMENT && ulDataSize >= sizeof(unsigned long))
        {
            applyEaxEnvironment(slot, *(const unsigned long *)pData);
            setLastStatus(SEGA_SUCCESS);
            return SEGA_SUCCESS;
        }

        if (EAX_PROPERTY_ID(ulProperty) == EAXREVERB_ALLPARAMETERS && ulDataSize >= sizeof(EAXREVERBPROPERTIES))
        {
            applyEaxReverb(slot, (const EAXREVERBPROPERTIES *)pData);
            setLastStatus(SEGA_SUCCESS);
            return SEGA_SUCCESS;
        }

        if (eaxLogEnabled())
            fprintf(stderr, "[segaapi-eax] FX slot %d: unhandled reverb property %lu (size %lu)\n", slot,
                    EAX_PROPERTY_ID(ulProperty), ulDataSize);
    }

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_GetGlobalEAXProperty(GUID *guid, unsigned long ulProperty, void *pData, unsigned long ulDataSize)
{
    if (guid == NULL || pData == NULL)
    {
        setLastStatus(SEGA_ERROR_BAD_POINTER);
        return SEGA_ERROR_BAD_POINTER;
    }

    logEaxProperty("get", guid, ulProperty, pData, ulDataSize);

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetSPDIFOutChannelStatus(unsigned int dwChannelStatus, unsigned int dwExtChannelStatus)
{
    debug(1, "Warning: This game tried to use SEGAAPI_SetSPDIFOutChannelStatus\n");
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_GetSPDIFOutChannelStatus(unsigned int *pdwChannelStatus, unsigned int *pdwExtChannelStatus)
{
    debug(1, "Warning: This game tried to use SEGAAPI_GetSPDIFOutChannelStatus\n");
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetSPDIFOutSampleRate(SPDIFOutputSampleRate dwSamplingRate)
{
    debug(1, "Warning: This game tried to use SEGAAPI_SetSPDIFOutSampleRate\n");
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) SPDIFOutputSampleRate SEGAAPI_GetSPDIFOutSampleRate(void)
{
    debug(1, "Warning: This game tried to use SEGAAPI_GetSPDIFOutSampleRate\n");
    return SPDIFOUT_44_1KHZ;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_SetSPDIFOutChannelRouting(unsigned int dwChannel, Routing dwSource)
{
    debug(1, "Warning: This game tried to use SEGAAPI_SetSPDIFOutChannelRouting\n");
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) Routing SEGAAPI_GetSPDIFOutChannelRouting(unsigned int dwChannel)
{
    debug(1, "Warning: This game tried to use SEGAAPI_GetSPDIFOutChannelRouting\n");
    return UNUSED_PORT;
}

// _ChangeBMixerVolume maps each SoundBoardIO to its own board-mixer control, using the top 16 bits.
// This used to set the master volume whichever port was named. SPDIF and line-in have no counterpart.
__attribute__((force_align_arg_pointer)) int SEGAAPI_SetIOVolume(SoundBoardIO dwPhysIO, unsigned int dwVolume)
{
    unsigned int *slot = ioVolumeSlot(dwPhysIO);

    if (slot == NULL)
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return SEGA_ERROR_BAD_PARAM;
    }

    *slot = dwVolume;

    if (dwPhysIO < OUTPUT_CHANNELS && fAudioSubmixVoices[dwPhysIO] != NULL)
        FAudioVoice_SetVolume(fAudioSubmixVoices[dwPhysIO], (dwVolume >> 16) / 65535.0f, FAUDIO_COMMIT_NOW);

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) unsigned int SEGAAPI_GetIOVolume(SoundBoardIO dwPhysIO)
{
    unsigned int *slot = ioVolumeSlot(dwPhysIO);

    if (slot == NULL)
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return 0;
    }

    setLastStatus(SEGA_SUCCESS);
    return *slot;
}

__attribute__((force_align_arg_pointer)) void SEGAAPI_SetLastStatus(int LastStatus)
{
    setLastStatus(LastStatus);
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_GetLastStatus(void)
{
    return lastStatus;
}

// Matches the original: the defaults SEGAAPI_Init establishes and SEGAAPI_Reset re-applies.
static void applyDefaultState(void)
{
    SEGAAPI_SetSPDIFOutChannelRouting(0, FRONT_LEFT_PORT);
    SEGAAPI_SetSPDIFOutChannelRouting(1, FRONT_RIGHT_PORT);
    SEGAAPI_SetSPDIFOutSampleRate(SPDIFOUT_48KHZ);

    static const SoundBoardIO ports[] = {OUT_FRONT_LEFT,   OUT_FRONT_RIGHT,   OUT_FRONT_CENTER,
                                         OUT_LFE_PORT,     OUT_REAR_LEFT,     OUT_REAR_RIGHT,
                                         OUT_OPTICAL_LEFT, OUT_OPTICAL_RIGHT, IN_LINEIN_LEFT,
                                         IN_LINEIN_RIGHT};

    for (unsigned int i = 0; i < sizeof(ports) / sizeof(ports[0]); i++)
        SEGAAPI_SetIOVolume(ports[i], (ports[i] == IN_LINEIN_LEFT || ports[i] == IN_LINEIN_RIGHT) ? 0x99980000
                                                                                                 : VOL_MAX);
}

// The original destroys every live buffer, resets the driver, then re-applies the full Init default
// set. This returned SEGA_SUCCESS, so a game resetting between modes kept every buffer it had,
// still playing.
__attribute__((force_align_arg_pointer)) int SEGAAPI_Reset(void)
{
    destroyAllBuffers();
    applyDefaultState();

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

__attribute__((force_align_arg_pointer)) int SEGAAPI_Init(void)
{
    pthread_mutex_init(&fAudioMutex, NULL);
    pthread_mutex_lock(&fAudioMutex);

    FAudioCreate(&fAudio, 0, FAUDIO_DEFAULT_PROCESSOR);

    FAudio_CreateMasteringVoice(fAudio, &fAudioMasteringVoice, FAUDIO_DEFAULT_CHANNELS, FAUDIO_DEFAULT_SAMPLERATE, 0, 0,
                                NULL);

    FAudioVoiceDetails fAudioVoiceDetails;
    FAudioVoice_GetVoiceDetails(fAudioMasteringVoice, &fAudioVoiceDetails);

    maxInputChannels = fAudioVoiceDetails.InputChannels;
    deviceSampleRate = fAudioVoiceDetails.InputSampleRate;

    for (int i = 0; i < OUTPUT_CHANNELS; i++)
    {
        FAudio_CreateSubmixVoice(fAudio, &fAudioSubmixVoices[i], 1, fAudioVoiceDetails.InputSampleRate, 0, 0, NULL,
                                 NULL);
        float matrix[OUTPUT_CHANNELS] = {0};
        if (fAudioVoiceDetails.InputChannels == 6)
        {
            matrix[i] = 1.0f;
        }
        else if (fAudioVoiceDetails.InputChannels == 2)
        {
            // Downmix, L, R, C, S, BL, BR
            if (i < 2)
            {
                matrix[i] = 1.0f;
            }
            else if (i == 2)
            {
                matrix[0] = 1.0f;
                matrix[1] = 1.0f;
            }
            else if (i == 3)
            {
                matrix[0] = 0.5f;
                matrix[1] = 0.5f;
            }
            else
            {
                matrix[i - 4] = 0.5f;
            }
        }
        FAudioVoice_SetOutputMatrix(fAudioSubmixVoices[i], fAudioMasteringVoice, 1, fAudioVoiceDetails.InputChannels,
                                    matrix, FAUDIO_COMMIT_NOW);
    }

    // The four EAX effect slots, ports 10-13. Each is a mono submix with a reverb in its effect
    // chain, folded to the front pair on the way out. FAudio ships the reverb we need, and EAX
    // Reverb's core block is I3DL2 field for field, so no effect had to be written.
    for (int i = 0; i < FX_SLOTS; i++)
    {
        if (FAudioCreateReverb(&fAudioFxReverbs[i], 0) != 0)
            continue;

        FAudioEffectDescriptor descriptor = {fAudioFxReverbs[i], 0, 1};
        FAudioEffectChain chain = {1, &descriptor};

        if (FAudio_CreateSubmixVoice(fAudio, &fAudioFxVoices[i], 1, fAudioVoiceDetails.InputSampleRate, 0, 0, NULL,
                                     &chain) != 0)
        {
            fAudioFxVoices[i] = NULL;
            continue;
        }

        float matrix[OUTPUT_CHANNELS] = {0};
        matrix[0] = 1.0f;
        if (fAudioVoiceDetails.InputChannels > 1)
            matrix[1] = 1.0f;

        FAudioVoice_SetOutputMatrix(fAudioFxVoices[i], fAudioMasteringVoice, 1, fAudioVoiceDetails.InputChannels,
                                    matrix, FAUDIO_COMMIT_NOW);
        FAudioVoice_SetVolume(fAudioFxVoices[i], 0.0f, FAUDIO_COMMIT_NOW);
    }

    pthread_mutex_unlock(&fAudioMutex);

    // EAX 4.0 brings up slots 0 and 1 with reverb already loaded, which is why the original library
    // explicitly nulls only slots 2 and 3 under property 0x10002. Initial D relies on it and never
    // calls LOADEFFECT itself.
    for (int i = 0; i < FX_SLOTS; i++)
    {
        fxSlotHasEffect[i] = (i < 2);
        fxSlotEffectEnabled[i] = 0;
        fxSlotVolume[i] = 1.0f;
        fxSlotConfiguredByGame[i] = 0;
        fxSlotSendWarned[i] = 0;

        updateFxSlotGain(i);

        if (fxSlotHasEffect[i])
            applyI3DL2(i, &eaxEnvironmentPresets[0]);
    }

    applyDefaultState();

    int fxReady = 0;
    for (int i = 0; i < FX_SLOTS; i++)
        if (fAudioFxVoices[i] != NULL)
            fxReady++;

    fprintf(stderr, "[segaapi] init: FAudio backend, %u ch @ %u Hz, %d output buses, %d/%d FX slots ready "
                    "(reverb loaded in %d)\n",
            maxInputChannels, deviceSampleRate, OUTPUT_CHANNELS, fxReady, FX_SLOTS,
            (fxSlotHasEffect[0] ? 1 : 0) + (fxSlotHasEffect[1] ? 1 : 0));
    fflush(stderr);

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}

// The original loops GetFirstHandleEntry / DestroyBuffer until the table is empty, destroys the
// handle table, then unwinds the driver and COM. This logged a line and returned, leaving FAudio,
// the mastering voice and every submix voice alive.
__attribute__((force_align_arg_pointer)) int SEGAAPI_Exit(void)
{
    debug(1, "SEGAAPI_Exit called\n");

    destroyAllBuffers();

    for (int i = 0; i < FX_SLOTS; i++)
    {
        if (fAudioFxVoices[i] != NULL)
        {
            FAudioVoice_DestroyVoice(fAudioFxVoices[i]);
            fAudioFxVoices[i] = NULL;
        }
    }

    for (int i = 0; i < OUTPUT_CHANNELS; i++)
    {
        if (fAudioSubmixVoices[i] != NULL)
        {
            FAudioVoice_DestroyVoice(fAudioSubmixVoices[i]);
            fAudioSubmixVoices[i] = NULL;
        }
    }

    if (fAudioMasteringVoice != NULL)
    {
        FAudioVoice_DestroyVoice(fAudioMasteringVoice);
        fAudioMasteringVoice = NULL;
    }

    if (fAudio != NULL)
    {
        FAudio_StopEngine(fAudio);
        FAudio_Release(fAudio);
        fAudio = NULL;
    }

    setLastStatus(SEGA_SUCCESS);
    return SEGA_SUCCESS;
}
