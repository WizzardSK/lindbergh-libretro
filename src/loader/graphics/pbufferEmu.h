#pragma once

#include "glxBridge.h"

#include <stdbool.h>

/**
 * Pbuffer emulation, backed by framebuffer objects. Windows only: on Linux the
 * game reaches the real driver's pbuffers through glxStackAlign.c.
 *
 * The GLX entry points the games call -- glXCreatePbuffer, glXDestroyPbuffer,
 * glXQueryDrawable and their SGIX spellings -- are implemented in pbufferEmu.c
 * and declared with the rest of the GLX surface in glxBridge.h. What follows is
 * only what the GLX bridge itself needs to keep the rest of the emulation
 * honest: which drawable is a pbuffer, which one is bound, and how to bind it.
 */
#ifdef _WIN32

#ifdef __cplusplus
extern "C"
{
#endif
    /* True when the drawable is one of ours rather than the SDL window. */
    bool pbufferIsPbuffer(GLXDrawable drawable);

    /* Points rendering at the drawable: the pbuffer's framebuffer object, or the
       window's framebuffer when the drawable is not a pbuffer. Needs a current
       context, so it belongs after the SDL_GL_MakeCurrent that accepted it. */
    void pbufferBindDrawable(GLXDrawable drawable);

    /* The pbuffer the game is drawing into, or 0 when it is drawing to the
       window. Binding the FBO does not change the current SDL window, so this is
       what glXGetCurrentDrawable has to be answered from. */
    GLXDrawable pbufferGetCurrentDrawable(void);
#ifdef __cplusplus
}
#endif

#endif
