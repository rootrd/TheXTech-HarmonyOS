#include "../../SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_OHOS

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_ohosaudio.h"

#include <ohaudio/native_audiostreambuilder.h>
#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostream_base.h>

#include <stdio.h>

static int OHOSAUDIO_WriteData(OH_AudioRenderer *renderer, void *userData, void *buffer, int32_t length);
static int32_t OHOSAUDIO_InterruptEvent(OH_AudioRenderer *renderer, void *userData,
                                         OH_AudioInterrupt_ForceType type, OH_AudioInterrupt_Hint hint);

static int OHOSAUDIO_OpenDevice(_THIS, const char *devname)
{
    fprintf(stderr, "OHOSAUDIO_OpenDevice: freq=%d channels=%d\n", _this->spec.freq, _this->spec.channels);

    SDL_AudioFormat test_format;
    OH_AudioStream_SampleFormat ohFormat = AUDIOSTREAM_SAMPLE_S16LE;

    _this->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*_this->hidden));
    if (!_this->hidden) {
        return SDL_OutOfMemory();
    }

    for (test_format = SDL_FirstAudioFormat(_this->spec.format); test_format; test_format = SDL_NextAudioFormat()) {
        if (test_format == AUDIO_S16LSB) {
            _this->spec.format = test_format;
            ohFormat = AUDIOSTREAM_SAMPLE_S16LE;
            break;
        }
        if (test_format == AUDIO_S32LSB) {
            _this->spec.format = test_format;
            ohFormat = AUDIOSTREAM_SAMPLE_S32LE;
            break;
        }
        if (test_format == AUDIO_F32LSB) {
            _this->spec.format = test_format;
            ohFormat = AUDIOSTREAM_SAMPLE_F32LE;
            break;
        }
    }

    if (!test_format) {
        _this->spec.format = AUDIO_S16LSB;
        ohFormat = AUDIOSTREAM_SAMPLE_S16LE;
    }

    _this->hidden->lock = SDL_CreateMutex();
    _this->hidden->cond = SDL_CreateCond();

    OH_AudioStreamBuilder *builder = NULL;
    OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER);
    OH_AudioStreamBuilder_SetSamplingRate(builder, _this->spec.freq);
    OH_AudioStreamBuilder_SetChannelCount(builder, _this->spec.channels);
    OH_AudioStreamBuilder_SetSampleFormat(builder, ohFormat);
    OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW);
    OH_AudioStreamBuilder_SetRendererInfo(builder, AUDIOSTREAM_USAGE_GAME);
    OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST);

    OH_AudioRenderer_Callbacks callbacks;
    callbacks.OH_AudioRenderer_OnWriteData = OHOSAUDIO_WriteData;
    callbacks.OH_AudioRenderer_OnStreamEvent = NULL;
    callbacks.OH_AudioRenderer_OnInterruptEvent = OHOSAUDIO_InterruptEvent;
    callbacks.OH_AudioRenderer_OnError = NULL;
    OH_AudioStreamBuilder_SetRendererCallback(builder, callbacks, _this);

    OH_AudioRenderer *renderer = NULL;
    OH_AudioStream_Result result = OH_AudioStreamBuilder_GenerateRenderer(builder, &renderer);
    if (result != AUDIOSTREAM_SUCCESS) {
        fprintf(stderr, "OHOSAUDIO_OpenDevice: GenerateRenderer failed: %d\n", result);
        SDL_SetError("OH_AudioStreamBuilder_GenerateRenderer failed: %d", result);
        OH_AudioStreamBuilder_Destroy(builder);
        return -1;
    }

    fprintf(stderr, "OHOSAUDIO_OpenDevice: renderer created OK, starting...\n");

    _this->hidden->builder = builder;
    _this->hidden->renderer = renderer;

    /* Final spec is settled (format may have changed above); allocate the
     * mix buffer GetDeviceBuf returns and the intermediate ring now. */
    SDL_CalculateAudioSpec(&_this->spec);

    _this->hidden->mixbuf = (Uint8 *)SDL_malloc(_this->spec.size);
    if (!_this->hidden->mixbuf) {
        OH_AudioStreamBuilder_Destroy(builder);
        return SDL_OutOfMemory();
    }
    int bufSize = _this->spec.size * 4;
    /* The OHAudio callback asks for large chunks (e.g. 32768 bytes observed
     * in FAST latency mode); keep at least two callback periods buffered so
     * every callback is served real PCM instead of half silence. */
    if (bufSize < 65536) {
        bufSize = 65536;
    }
    _this->hidden->audioBuf = (Uint8 *)SDL_calloc(1, bufSize);
    _this->hidden->audioBufSize = bufSize;
    _this->hidden->audioBufPos = 0;

    OH_AudioStream_Result startRes = OH_AudioRenderer_Start(renderer);
    if (startRes != AUDIOSTREAM_SUCCESS) {
        fprintf(stderr, "OHOSAUDIO_OpenDevice: OH_AudioRenderer_Start FAILED: %d\n", startRes);
        SDL_SetError("OH_AudioRenderer_Start failed: %d", startRes);
        return -1;
    }

    fprintf(stderr, "OHOSAUDIO_OpenDevice: started OK (fmt=%d freq=%d ch=%d size=%d)\n",
            (int)_this->spec.format, _this->spec.freq, _this->spec.channels, _this->spec.size);

    return 0;
}

static int32_t OHOSAUDIO_WriteData(OH_AudioRenderer *renderer, void *userData, void *buffer, int32_t length)
{
    static int cbCount = 0;

    SDL_AudioDevice *_this = (SDL_AudioDevice *)userData;
    if (!_this || !_this->hidden) {
        memset(buffer, 0, length);
        return 0;
    }

    SDL_LockMutex(_this->hidden->lock);

    int available = _this->hidden->audioBufPos;
    int toCopy = (available < length) ? available : length;

    if (toCopy > 0) {
        memcpy(buffer, _this->hidden->audioBuf, toCopy);
        if (available > toCopy) {
            memmove(_this->hidden->audioBuf, _this->hidden->audioBuf + toCopy, available - toCopy);
            _this->hidden->audioBufPos = available - toCopy;
        } else {
            _this->hidden->audioBufPos = 0;
        }
    }

    if (toCopy < length) {
        memset((Uint8 *)buffer + toCopy, 0, length - toCopy);
    }

    cbCount++;
    if (cbCount <= 3 || (cbCount % 2000) == 0) {
        fprintf(stderr, "OHOSAUDIO: WriteData cb#%d len=%d got=%d\n", cbCount, length, toCopy);
    }

    SDL_CondSignal(_this->hidden->cond);
    SDL_UnlockMutex(_this->hidden->lock);

    return 0;
}

static int32_t OHOSAUDIO_InterruptEvent(OH_AudioRenderer *renderer, void *userData,
                                         OH_AudioInterrupt_ForceType type, OH_AudioInterrupt_Hint hint)
{
    return 0;
}

static void OHOSAUDIO_PlayDevice(_THIS)
{
    static int playCount = 0;

    SDL_LockMutex(_this->hidden->lock);

    int freeSpace = _this->hidden->audioBufSize - _this->hidden->audioBufPos;
    while (freeSpace < _this->spec.size) {
        SDL_CondWait(_this->hidden->cond, _this->hidden->lock);
        freeSpace = _this->hidden->audioBufSize - _this->hidden->audioBufPos;
    }

    memcpy(_this->hidden->audioBuf + _this->hidden->audioBufPos, _this->hidden->mixbuf, _this->spec.size);
    _this->hidden->audioBufPos += _this->spec.size;

    playCount++;
    if (playCount <= 3 || (playCount % 2000) == 0) {
        fprintf(stderr, "OHOSAUDIO: PlayDevice #%d size=%d buffered=%d\n",
                playCount, _this->spec.size, _this->hidden->audioBufPos);
    }

    SDL_UnlockMutex(_this->hidden->lock);
}

static Uint8 *OHOSAUDIO_GetDeviceBuf(_THIS)
{
    return _this->hidden->mixbuf;
}

static void OHOSAUDIO_WaitDevice(_THIS)
{
    SDL_LockMutex(_this->hidden->lock);
    int freeSpace = _this->hidden->audioBufSize - _this->hidden->audioBufPos;
    while (freeSpace < _this->spec.size) {
        SDL_CondWait(_this->hidden->cond, _this->hidden->lock);
        freeSpace = _this->hidden->audioBufSize - _this->hidden->audioBufPos;
    }
    SDL_UnlockMutex(_this->hidden->lock);
}

static void OHOSAUDIO_CloseDevice(_THIS)
{
    if (_this->hidden) {
        if (_this->hidden->renderer) {
            OH_AudioRenderer_Stop((OH_AudioRenderer *)_this->hidden->renderer);
            OH_AudioRenderer_Release((OH_AudioRenderer *)_this->hidden->renderer);
        }
        if (_this->hidden->builder) {
            OH_AudioStreamBuilder_Destroy((OH_AudioStreamBuilder *)_this->hidden->builder);
        }
        if (_this->hidden->lock) {
            SDL_DestroyMutex(_this->hidden->lock);
        }
        if (_this->hidden->cond) {
            SDL_DestroyCond(_this->hidden->cond);
        }
        if (_this->hidden->audioBuf) {
            SDL_free(_this->hidden->audioBuf);
        }
        if (_this->hidden->mixbuf) {
            SDL_free(_this->hidden->mixbuf);
        }
        SDL_free(_this->hidden);
        _this->hidden = NULL;
    }
}

static SDL_bool OHOSAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    fprintf(stderr, "OHOSAUDIO_Init: registering OHOS audio driver\n");

    impl->OpenDevice = OHOSAUDIO_OpenDevice;
    impl->PlayDevice = OHOSAUDIO_PlayDevice;
    impl->GetDeviceBuf = OHOSAUDIO_GetDeviceBuf;
    impl->WaitDevice = OHOSAUDIO_WaitDevice;
    impl->CloseDevice = OHOSAUDIO_CloseDevice;

    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    /* SDL must run its own audio thread (SDL_RunAudio) that drives
     * GetDeviceBuf -> PlayDevice -> WaitDevice in push mode; the OHAudio
     * callback only drains the intermediate buffer. Without this, nothing
     * ever fills the buffer and the callback outputs silence. */
    impl->ProvidesOwnCallbackThread = SDL_FALSE;

    return SDL_TRUE;
}

AudioBootStrap OHOSAUDIO_bootstrap = {
    "ohos", "HarmonyOS OHAudio driver", OHOSAUDIO_Init, SDL_FALSE
};

#endif
