#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__
#include <glad/gl.h>

#include "phFboFeedback.h"

/**
 * Primeval Hunt renders into a 2048x2048 texture and, during its glow and haze
 * passes, samples that same texture while it is still the bound FBO's
 * GL_COLOR_ATTACHMENT0. GL leaves that feedback loop undefined which the original driver tolerated. 
 * Modern drivers return garbage tiles, which is the flickering block corruption plus the grey wash over the
 * scene. 
 *
 * We hook to 3 glBindTexture in patch.c and redirect them here.
 * So here we copy the attachment into a shadow texture and hand the game that to sample instead.
 * Reading the framebuffer is not a feedback loop, and the game's own shadow map
 * capture already proves glCopyTexSubImage2D reads this FBO correctly.
 */

static GLuint shadowTex = 0;
static GLsizei shadowWidth = 0;
static GLsizei shadowHeight = 0;

void phBindFeedbackTexture(GLenum target, GLuint texture)
{
    if (!glad_glBindTexture)
        return;

    if (shadowTex == 0)
    {
        GLint width = 0;
        GLint height = 0;
        GLint format = 0;
        GLint minFilter = GL_LINEAR;
        GLint magFilter = GL_LINEAR;
        GLint wrapS = GL_REPEAT;
        GLint wrapT = GL_REPEAT;

        glad_glBindTexture(target, texture);
        glad_glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &width);
        glad_glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &height);
        glad_glGetTexLevelParameteriv(target, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        glad_glGetTexParameteriv(target, GL_TEXTURE_MIN_FILTER, &minFilter);
        glad_glGetTexParameteriv(target, GL_TEXTURE_MAG_FILTER, &magFilter);
        glad_glGetTexParameteriv(target, GL_TEXTURE_WRAP_S, &wrapS);
        glad_glGetTexParameteriv(target, GL_TEXTURE_WRAP_T, &wrapT);

        if (width <= 0 || height <= 0)
            return; // Leaves the game sampling the attachment, as it did before.

        glad_glGenTextures(1, &shadowTex);
        glad_glBindTexture(target, shadowTex);
        glad_glCopyTexImage2D(target, 0, format, 0, 0, width, height, 0);
        // Sampler state lives in the texture object, so the copy has to carry
        // the attachment's filtering and wrapping or it samples differently.
        glad_glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
        glad_glTexParameteri(target, GL_TEXTURE_MAG_FILTER, magFilter);
        glad_glTexParameteri(target, GL_TEXTURE_WRAP_S, wrapS);
        glad_glTexParameteri(target, GL_TEXTURE_WRAP_T, wrapT);
        glad_glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);

        shadowWidth = width;
        shadowHeight = height;
        return;
    }

    glad_glBindTexture(target, shadowTex);
    glad_glCopyTexSubImage2D(target, 0, 0, 0, 0, 0, shadowWidth, shadowHeight);
}
