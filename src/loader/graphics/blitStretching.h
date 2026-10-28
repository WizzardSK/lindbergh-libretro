#pragma once
#include <stdbool.h>

typedef struct
{
    int W;
    int H;
    int X;
    int Y;
} Dest;

extern Dest dest;
extern int drawableW;
extern int drawableH;
extern int blitWidth;
extern int blitHeight;
extern int renderWidth;
extern int renderHeight;
extern int fboInitialized;
extern unsigned int fboId;
extern int gameIsOutrunChihiroMode;

void initBlitting();
void blitSetWidthandHeightSize();
int blitInitializeFbo();
void blitStretch();

/* For the libretro core on Windows, at a resolution larger than the screen:
 * the game's framebuffer 0 is a framebuffer object of the full resolution
 * instead of the hidden window's, which the driver cuts to the screen's size
 * (NNshi: 2160p on a 1440p monitor showed a corner). The game's binds of 0 and its GL_BACK and
 * GL_FRONT go to it (glHooks.cpp); 0 elsewhere. */
extern unsigned int lrGameFbo;
/* Made and bound once the game's context is current */
void lrOffscreenBind(void);
/* A framebuffer the game binds, 0 turned into lrGameFbo */
unsigned int lrGameFramebuffer(unsigned int framebuffer);
/* A colour buffer the game names for drawing or reading, GL_BACK and the
 * like turned into lrGameFbo's attachment while it is bound */
unsigned int lrGameColorBuffer(unsigned int mode, bool draw);
void blitWindowToRender(float windowX, float windowY, float *renderX, float *renderY);
void blitRenderToWindow(float renderX, float renderY, float *windowX, float *windowY);
