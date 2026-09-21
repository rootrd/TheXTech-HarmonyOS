#include "../../SDL_internal.h"

#ifndef SDL_ohosaudio_h_
#define SDL_ohosaudio_h_

#include "../SDL_sysaudio.h"

#define _THIS SDL_AudioDevice *_this

struct SDL_PrivateAudioData
{
    void *renderer;
    void *builder;
    Uint8 *mixbuf;
    Uint8 *audioBuf;
    int audioBufSize;
    int audioBufPos;
    SDL_mutex *lock;
    SDL_cond *cond;
};

#endif
