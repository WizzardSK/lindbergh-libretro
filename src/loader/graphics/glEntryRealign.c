#if defined(__linux__) && defined(__i386__)

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "../log/log.h"

/*
 * Realigning trampolines for the GL entry points the games call directly.
 *
 * The Lindbergh binaries predate the i386 SysV ABI's 16-byte stack guarantee
 * and do not keep %esp aligned across their own calls, so any host function
 * they enter can land on a misaligned stack. Modern Mesa stores xmm registers
 * to its own frames with vmovaps/vmovdqa -- yyparse and
 * st_update_framebuffer_state are both known offenders -- and an aligned SSE
 * store to a misaligned address raises #GP, which the kernel delivers as
 * SIGSEGV. glxStackAlign.c fixes this for the GLX and GLU entry points, but the
 * GL entry points are not imported: the games resolve them through
 * glXGetProcAddressARB and call the pointers straight out of their own frames,
 * so they cannot be interposed as symbols and every one of them is a potential
 * fault site depending on how the game's stack happens to line up that call.
 *
 * glRealignWrap() returns a per-function trampoline that realigns the stack and
 * forwards the call.
 *
 * Only cdecl is supported, which is what every Linux GL entry point uses, and
 * the argument window is 16 dwords. The widest GL entry point is a dozen
 * arguments, so nothing is truncated; anything narrower simply has harmless
 * values in the unused slots.
 */

/*
 * Forwarder. The trampoline loads the real entry point into %eax and jumps
 * here with the caller's arguments still exactly where the caller put them.
 *
 * regparm(1) makes the compiler take fn from %eax, so a0..a15 are read straight
 * from the caller's argument area. force_align_arg_pointer realigns before the
 * copy, so the real entry point is always called with an ABI-aligned stack.
 *
 * The uint64_t return type carries %eax:%edx through untouched, which covers
 * every GL return type. Nothing in GL returns float or double, whose result
 * would come back in %st0.
 */
typedef uint64_t (*GLEntry16)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

__attribute__((force_align_arg_pointer, regparm(1))) static uint64_t
glRealignForward(GLEntry16 fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                 uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13,
                 uint32_t a14, uint32_t a15)
{
    return fn(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15);
}

/*
 * Trampolines are 15 bytes, padded to 16:
 *   b8 <entry point>     movl $entry, %eax
 *   ff 25 <slot address> jmp *slot
 *   <slot: 4 bytes>      the address of glRealignForward
 *
 * The absolute slot keeps the stub position-independent: it does not have to
 * be patched relative to wherever the arena happens to be mapped.
 */
#define STUB_SIZE 16
#define ARENA_SIZE (64 * 1024)
/* Sega Race TV alone looks up over 1100 distinct entry points, so the cache
 * is sized well past that. Power of two for the open-addressing mask. */
#define CACHE_SIZE 8192

static void *arena = NULL;
static size_t arenaUsed = 0;

static pthread_mutex_t wrapLock = PTHREAD_MUTEX_INITIALIZER;
static void *wrappedEntry[CACHE_SIZE];
static void *wrappedStub[CACHE_SIZE];
static int wrappedCount = 0;

static void *allocStub(void *entry)
{
    if (arena == NULL || arenaUsed + STUB_SIZE > ARENA_SIZE)
    {
        /* RWX because the arena stays writable for later trampolines while
         * earlier ones are already live. The loader maps and patches game code
         * the same way. */
        void *fresh = mmap(NULL, ARENA_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (fresh == MAP_FAILED)
        {
            log_error("Stack alignment shim: could not map trampoline memory.\n");
            return NULL;
        }
        arena = fresh;
        arenaUsed = 0;
    }

    unsigned char *stub = (unsigned char *)arena + arenaUsed;
    arenaUsed += STUB_SIZE;

    uint32_t slot = (uint32_t)(uintptr_t)(stub + 11); /* where the forwarder address lives */
    uint32_t forward = (uint32_t)(uintptr_t)glRealignForward;

    stub[0] = 0xb8; /* movl $entry, %eax */
    memcpy(stub + 1, &entry, 4);
    stub[5] = 0xff; /* jmp *slot */
    stub[6] = 0x25;
    memcpy(stub + 7, &slot, 4);
    memcpy(stub + 11, &forward, 4);

    return stub;
}

/**
 *   Returns a trampoline that calls fn with a 16-byte aligned stack, or fn
 *   itself if no trampoline could be built. Trampolines are cached per entry
 *   point, so repeated glXGetProcAddressARB calls for the same name are cheap.
 */
void *glRealignWrap(void *fn)
{
    if (fn == NULL)
        return NULL;

    pthread_mutex_lock(&wrapLock);

    /* Open addressing on the entry point address; the low bits are mostly
     * alignment, so they are shifted out first. */
    size_t slot = ((uintptr_t)fn >> 4) & (CACHE_SIZE - 1);
    while (wrappedEntry[slot] != NULL && wrappedEntry[slot] != fn)
        slot = (slot + 1) & (CACHE_SIZE - 1);

    if (wrappedEntry[slot] == fn)
    {
        void *cached = wrappedStub[slot];
        pthread_mutex_unlock(&wrapLock);
        return cached;
    }

    void *stub = allocStub(fn);

    /* Keep one slot free so the probe above always terminates. Past that the
     * stub is still handed out, just not cached: an unwrapped entry point is
     * exactly the crash this file exists to prevent. */
    if (stub != NULL && wrappedCount < CACHE_SIZE - 1)
    {
        wrappedEntry[slot] = fn;
        wrappedStub[slot] = stub;
        wrappedCount++;
    }

    pthread_mutex_unlock(&wrapLock);

    return stub != NULL ? stub : fn;
}

#endif /* __linux__ && __i386__ */
