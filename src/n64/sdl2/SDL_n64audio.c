/*
 * SDL_n64audio.c -- SDL audio driver for the Nintendo 64 (libdragon audio).
 *
 * 16-bit stereo output through the AI at the requested rate. SDL normally
 * runs the application's audio callback on its own thread; the console has
 * none, so the driver provides its own "callback thread": every time events
 * are pumped (SDL_PumpEvents, and SDL_Delay) the free hardware buffers are
 * filled by calling the callback, with format conversion through an
 * SDL_AudioStream when the application asked for something else.
 *
 * Registered as DUMMYAUDIO_bootstrap (SDL_AUDIO_DRIVER_DUMMY in SDL_config.h).
 */
#include "SDL_internal.h"
#include "SDL_audio.h"
#include "audio/SDL_audio_c.h"
#include "audio/SDL_sysaudio.h"

#include <libdragon.h>

static SDL_AudioDevice *s_device;

static void fill(SDL_AudioDevice *this, Uint8 *out, int bytes)
{
    SDL_AudioCallback callback = this->callbackspec.callback;
    const int stream_len = this->callbackspec.size;

    if (!SDL_AtomicGet(&this->enabled) || SDL_AtomicGet(&this->paused)) {
        if (this->stream) SDL_AudioStreamClear(this->stream);
        SDL_memset(out, this->spec.silence, bytes);
        return;
    }
    if (!this->stream) {
        if (bytes == stream_len) {
            callback(this->callbackspec.userdata, out, bytes);
        } else {
            callback(this->callbackspec.userdata, this->work_buffer, stream_len);
            SDL_memcpy(out, this->work_buffer, SDL_min(bytes, stream_len));
        }
        return;
    }
    while (SDL_AudioStreamAvailable(this->stream) < bytes) {
        callback(this->callbackspec.userdata, this->work_buffer, stream_len);
        if (SDL_AudioStreamPut(this->stream, this->work_buffer, stream_len) == -1) {
            SDL_AudioStreamClear(this->stream);
            SDL_AtomicSet(&this->enabled, 0);
            break;
        }
    }
    if (SDL_AudioStreamGet(this->stream, out, bytes) != bytes) SDL_memset(out, this->spec.silence, bytes);
}

/* Called from the video driver's event pump and from SDL_Delay. */
void SDL_N64_AudioPump(void)
{
    SDL_AudioDevice *this = s_device;
    if (!this) return;
    while (audio_can_write()) {
        short *buf = audio_write_begin();
        fill(this, (Uint8 *)buf, (int)this->spec.size);
        audio_write_end();
    }
}

static int N64AUDIO_OpenDevice(SDL_AudioDevice *this, const char *devname)
{
    (void)devname;
    if (this->iscapture) return SDL_SetError("n64: no audio capture");
    if (s_device) return SDL_SetError("n64: audio device already open");

    audio_init(this->spec.freq > 0 ? this->spec.freq : 44100, 4);
    this->spec.freq = audio_get_frequency();
    this->spec.format = AUDIO_S16MSB;
    this->spec.channels = 2;
    this->spec.samples = (Uint16)audio_get_buffer_length();
    SDL_CalculateAudioSpec(&this->spec);
    s_device = this;
    return 0;
}

static void N64AUDIO_CloseDevice(SDL_AudioDevice *this)
{
    if (s_device == this) {
        s_device = NULL;
        audio_close();
    }
}

static SDL_bool N64AUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = N64AUDIO_OpenDevice;
    impl->CloseDevice = N64AUDIO_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->ProvidesOwnCallbackThread = SDL_TRUE;
    return SDL_TRUE;
}

AudioBootStrap DUMMYAUDIO_bootstrap = {
    "n64", "Nintendo 64 audio interface", N64AUDIO_Init, SDL_FALSE
};
