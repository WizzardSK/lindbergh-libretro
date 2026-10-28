#include <glad/gl.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "sdlCalls.h"
#include "../log/log.h"

#include "pbufferEmu.h"

/* There is no GL/glx.h on Windows, so the handful of tokens the pbuffer calls
   speak in are spelled out here. Same values as the GLX headers. */
#ifndef GLX_PRESERVED_CONTENTS
#define GLX_PRESERVED_CONTENTS 0x801B
#endif
#ifndef GLX_LARGEST_PBUFFER
#define GLX_LARGEST_PBUFFER 0x801C
#endif
#ifndef GLX_WIDTH
#define GLX_WIDTH 0x801D
#endif
#ifndef GLX_HEIGHT
#define GLX_HEIGHT 0x801E
#endif
#ifndef GLX_FBCONFIG_ID
#define GLX_FBCONFIG_ID 0x8013
#endif
#ifndef GLX_PBUFFER_HEIGHT
#define GLX_PBUFFER_HEIGHT 0x8040
#endif
#ifndef GLX_PBUFFER_WIDTH
#define GLX_PBUFFER_WIDTH 0x8041
#endif

#define MAX_EMULATED_PBUFFERS 8

// ==========================================================================
//   Pbuffer emulation
// ==========================================================================

/**
 * Windows has no GLX at all, so there is no real pbuffer to hand the games: the
 * FBConfig they are holding is one of ours, and a real pbuffer could not be
 * created from it either. A framebuffer object stands in for each pbuffer, and
 * its geometry is remembered here so that the calls which interrogate the
 * drawable can be answered instead of reaching a driver that has never heard of
 * the handle.
 */
typedef struct
{
    unsigned int fbo;
    unsigned int colour;
    unsigned int depth;
    int width;
    int height;
    int largest;
    int preserved;
    bool inUse;
} EmulatedPbuffer;

static EmulatedPbuffer emulatedPbuffers[MAX_EMULATED_PBUFFERS];

/* Which pbuffer the game is drawing into, or NULL for the window. Binding the
   FBO does not change the current SDL window, so this is what glXGetCurrentDrawable
   has to be answered from. */
static EmulatedPbuffer *currentPbuffer = NULL;

static EmulatedPbuffer *findPbuffer(GLXDrawable drawable)
{
    if (drawable == 0)
        return NULL;

    for (int i = 0; i < MAX_EMULATED_PBUFFERS; i++)
    {
        if (emulatedPbuffers[i].inUse && drawable == (GLXDrawable)(uintptr_t)&emulatedPbuffers[i])
            return &emulatedPbuffers[i];
    }
    return NULL;
}

static bool framebufferObjectsAvailable(void)
{
    return glad_glGenFramebuffers != NULL && glad_glBindFramebuffer != NULL && glad_glFramebufferTexture2D != NULL &&
           glad_glCheckFramebufferStatus != NULL && glad_glDeleteFramebuffers != NULL && glad_glGenRenderbuffers != NULL &&
           glad_glBindRenderbuffer != NULL && glad_glRenderbufferStorage != NULL && glad_glFramebufferRenderbuffer != NULL &&
           glad_glDeleteRenderbuffers != NULL;
}

/**
 * A pbuffer is an off-screen surface the game renders into and then reads back.
 * A second window cannot serve that: it needs its own context, and the game's
 * textures, programs and buffers then live in the wrong one, so its geometry
 * draws nothing even though a clear works. A framebuffer object is the honest
 * equivalent -- same context, same objects, real off-screen storage.
 *
 * Built on first bind, because it needs a current context.
 */
static bool ensurePbufferStorage(EmulatedPbuffer *pbuffer)
{
    if (pbuffer->fbo != 0)
        return true;

    if (!framebufferObjectsAvailable())
    {
        log_error("glXCreatePbuffer: no framebuffer object support, cannot emulate a pbuffer");
        return false;
    }

    /* This runs lazily inside glXMakeCurrent, so every binding it touches has to
       go back the way it was or the game's next call lands on our objects. */
    unsigned int previousFbo = 0;
    unsigned int previousTexture = 0;
    unsigned int previousRenderbuffer = 0;
    glad_glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *)&previousFbo);
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, (GLint *)&previousTexture);
    glad_glGetIntegerv(GL_RENDERBUFFER_BINDING, (GLint *)&previousRenderbuffer);

    glad_glGenFramebuffers(1, &pbuffer->fbo);
    glad_glBindFramebuffer(GL_FRAMEBUFFER, pbuffer->fbo);

    glad_glGenTextures(1, &pbuffer->colour);
    glad_glBindTexture(GL_TEXTURE_2D, pbuffer->colour);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pbuffer->width, pbuffer->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glad_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pbuffer->colour, 0);

    glad_glGenRenderbuffers(1, &pbuffer->depth);
    glad_glBindRenderbuffer(GL_RENDERBUFFER, pbuffer->depth);
    glad_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, pbuffer->width, pbuffer->height);
    glad_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, pbuffer->depth);

    GLenum status = glad_glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glad_glBindRenderbuffer(GL_RENDERBUFFER, previousRenderbuffer);
    glad_glBindTexture(GL_TEXTURE_2D, previousTexture);
    glad_glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        log_error("glXCreatePbuffer: framebuffer incomplete (0x%x) for %dx%d", status, pbuffer->width, pbuffer->height);
        return false;
    }

    return true;
}

bool pbufferIsPbuffer(GLXDrawable drawable)
{
    return findPbuffer(drawable) != NULL;
}

GLXDrawable pbufferGetCurrentDrawable(void)
{
    return (GLXDrawable)(uintptr_t)currentPbuffer;
}

/**
 * Binding a pbuffer keeps the same window and context current and swaps the
 * framebuffer object underneath, so everything the game created -- textures,
 * programs, buffers -- stays reachable while it renders off-screen.
 */
void pbufferBindDrawable(GLXDrawable drawable)
{
    EmulatedPbuffer *bound = findPbuffer(drawable);

    currentPbuffer = bound;

    /* Point rendering at the pbuffer's framebuffer object, or back at the
       window when the game is done with it. */
    if (!framebufferObjectsAvailable())
        return;

    if (bound != NULL)
    {
        if (ensurePbufferStorage(bound))
            glad_glBindFramebuffer(GL_FRAMEBUFFER, bound->fbo);
    }
    else
    {
        glad_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

GLXPbuffer bridgeGlxCreatePbuffer(Display *dpy, GLXFBConfig config, const int *attrib_list)
{
    int width = 0;
    int height = 0;
    int largest = 0;
    int preserved = 0;

    if (attrib_list)
    {
        for (int i = 0; attrib_list[i] != 0; i += 2)
        {
            switch (attrib_list[i])
            {
                case GLX_PBUFFER_WIDTH:
                    width = attrib_list[i + 1];
                    break;
                case GLX_PBUFFER_HEIGHT:
                    height = attrib_list[i + 1];
                    break;
                case GLX_LARGEST_PBUFFER:
                    largest = attrib_list[i + 1];
                    break;
                case GLX_PRESERVED_CONTENTS:
                    preserved = attrib_list[i + 1];
                    break;
                default:
                    break;
            }
        }
    }

    if (width == 0)
        width = 512;
    if (height == 0)
        height = 512;

    EmulatedPbuffer *slot = NULL;
    for (int i = 0; i < MAX_EMULATED_PBUFFERS; i++)
    {
        if (!emulatedPbuffers[i].inUse)
        {
            slot = &emulatedPbuffers[i];
            break;
        }
    }

    if (slot == NULL)
    {
        log_error("Out of emulated pbuffer slots, the game asked for more than %d", MAX_EMULATED_PBUFFERS);
        return 0;
    }

    memset(slot, 0, sizeof(*slot));
    slot->width = width;
    slot->height = height;
    slot->largest = largest;
    slot->preserved = preserved;
    slot->inUse = true;

    log_debug("Creating pbuffer %dx%d\n", width, height);

    /* The FBO itself is built on the first bind, when a context is current. */
    return (GLXPbuffer)(uintptr_t)slot;
}

void bridgeGlxDestroyPbuffer(Display *dpy, GLXPbuffer pbuf)
{
    EmulatedPbuffer *pbuffer = findPbuffer(pbuf);
    if (pbuffer == NULL)
    {
        log_debug("glXDestroyPbuffer on an unknown drawable %p", (void *)(uintptr_t)pbuf);
        return;
    }

    if (pbuffer->fbo != 0 && framebufferObjectsAvailable())
    {
        glad_glDeleteFramebuffers(1, &pbuffer->fbo);
        glad_glDeleteTextures(1, &pbuffer->colour);
        glad_glDeleteRenderbuffers(1, &pbuffer->depth);
    }

    if (currentPbuffer == pbuffer)
        currentPbuffer = NULL;

    memset(pbuffer, 0, sizeof(*pbuffer));
}

/**
 * Answers GLX_WIDTH / GLX_HEIGHT and friends out of the registry. Without this
 * the fake drawable would be handed to the real libGL, which has never seen it.
 */
void bridgeGlxQueryDrawable(Display *dpy, GLXDrawable draw, int attribute, unsigned int *value)
{
    if (value == NULL)
        return;

    EmulatedPbuffer *pbuffer = findPbuffer(draw);
    int width = 0;
    int height = 0;

    if (pbuffer != NULL)
    {
        width = pbuffer->width;
        height = pbuffer->height;
    }
    else if (getSDLWindow() != NULL)
    {
        SDL_GetWindowSize(getSDLWindow(), &width, &height);
    }

    switch (attribute)
    {
        case GLX_WIDTH:
        case GLX_PBUFFER_WIDTH:
            *value = (unsigned int)width;
            break;
        case GLX_HEIGHT:
        case GLX_PBUFFER_HEIGHT:
            *value = (unsigned int)height;
            break;
        case GLX_PRESERVED_CONTENTS:
            *value = pbuffer ? (unsigned int)pbuffer->preserved : 1u;
            break;
        case GLX_LARGEST_PBUFFER:
            *value = pbuffer ? (unsigned int)pbuffer->largest : 0u;
            break;
        case GLX_FBCONFIG_ID:
            *value = 1u;
            break;
        default:
            *value = 0u;
            break;
    }
}

void bridgeGlxDestroyGLXPbufferSGIX(Display *dpy, GLXPbuffer pbuf)
{
    bridgeGlxDestroyPbuffer(dpy, pbuf);
}

GLXPbuffer bridgeGlxCreateGLXPbufferSGIX(Display *dpy, GLXFBConfig config, int width, int height, int *attrib_list)
{
    int pbufferAttribs[] = {
        GLX_PBUFFER_WIDTH, width, GLX_PBUFFER_HEIGHT, height, GLX_PRESERVED_CONTENTS, true, GLX_LARGEST_PBUFFER, true, 0};
    return bridgeGlxCreatePbuffer(dpy, config, pbufferAttribs);
}

#endif
