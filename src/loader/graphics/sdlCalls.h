#pragma once

#include <GL/gl.h>
#include <SDL3/SDL_video.h>

int initSDL();
void startSDL();
SDL_Window* getSDLWindow();
SDL_GLContext getSDLContext();
int makeSDLCurrent(SDL_Window *win, SDL_GLContext ctx);
void sdlQuit();
void pollEvents();
void refreshResizedDrawable();
