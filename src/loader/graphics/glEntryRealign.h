#pragma once

#ifdef __linux__
/**
 *   Returns a trampoline that calls fn with a 16-byte aligned stack. Meant for
 *   the GL entry points the games resolve through glXGetProcAddressARB and call
 *   straight out of their own frames, which are not ABI-aligned. Returns fn
 *   unchanged if a trampoline could not be built.
 */
void *glRealignWrap(void *fn);
#endif
