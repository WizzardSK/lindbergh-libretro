/*
 * The core and the loader take turns on run_seq and frame_seq
 * (libretro_shm.h): one side bumps a counter and wakes the other, which
 * waits while the counter still has the value it last saw. On Linux the
 * counters are futexes; on Windows each has a named auto-reset event
 * beside the shared memory (LR_EVENT_RUN, LR_EVENT_FRAME appended to its
 * name), as a futex does not reach another process there. The waits are
 * short and in a loop on the counter, so a wake-up that comes early or
 * twice is harmless.
 */

#ifndef LINDBERGH_LIBRETRO_IPC_H
#define LINDBERGH_LIBRETRO_IPC_H

#include <stdint.h>

#define LR_EVENT_RUN "-run"
#define LR_EVENT_FRAME "-frame"

#ifdef _WIN32
#include <windows.h>

typedef HANDLE LrEvent;

static inline void lrWait(LrEvent event, volatile uint32_t *word, uint32_t value, int ms)
{
    if (*word == value)
        WaitForSingleObject(event, ms < 0 ? INFINITE : (DWORD)ms);
}

static inline void lrWake(LrEvent event, volatile uint32_t *word)
{
    (void)word;
    SetEvent(event);
}
#else
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* No handle: the futex is the word itself */
typedef int LrEvent;

static inline void lrWait(LrEvent event, volatile uint32_t *word, uint32_t value, int ms)
{
    (void)event;
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    syscall(SYS_futex, word, FUTEX_WAIT, value, ms >= 0 ? &ts : NULL, NULL, 0);
}

static inline void lrWake(LrEvent event, volatile uint32_t *word)
{
    (void)event;
    syscall(SYS_futex, word, FUTEX_WAKE, 0x7fffffff, NULL, NULL, 0);
}
#endif

#endif
