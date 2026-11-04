#include "AlsaSink.h"
#include <cstdio>
#include <cstring>

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>

bool AlsaSink::open(const std::string& device, int latencyMs, uint32_t sampleRate)
{
    snd_pcm_t* h = nullptr;
    int err = snd_pcm_open(&h, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0)
    {
        fprintf(stderr, "alsa: cannot open '%s': %s\n", device.c_str(), snd_strerror(err));
        return false;
    }
    unsigned int latencyUs = (unsigned int)(latencyMs > 0 ? latencyMs : 20) * 1000u;
    err = snd_pcm_set_params(h, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                             sampleRate, 1 /* allow resample */, latencyUs);
    if (err < 0)
    {
        fprintf(stderr, "alsa: set_params failed on '%s': %s\n", device.c_str(), snd_strerror(err));
        snd_pcm_close(h);
        return false;
    }
    snd_pcm_uframes_t bufSize = 0, periodSize = 0;
    snd_pcm_get_params(h, &bufSize, &periodSize);
    pcm = h;
    name = device;
    rate = sampleRate;
    bufFrames = (uint32_t)bufSize;
    fprintf(stderr, "alsa: playback on '%s' %u Hz, buffer %lu frames (%.1f ms), period %lu\n",
            device.c_str(), sampleRate, (unsigned long)bufSize, 1000.0 * bufSize / sampleRate,
            (unsigned long)periodSize);
    return true;
}

void AlsaSink::close()
{
    if (pcm)
    {
        snd_pcm_drain((snd_pcm_t*)pcm);
        snd_pcm_close((snd_pcm_t*)pcm);
        pcm = nullptr;
    }
}

void AlsaSink::write(const int16_t* left, const int16_t* right, int frames, bool muted, float gain)
{
    if (!pcm || frames <= 0 || frames > 1024)
        return;
    if (muted)
    {
        memset(interleaved, 0, (size_t)frames * 2 * sizeof(int16_t));
    }
    else if (gain >= 0.999f)
    {
        for (int i = 0; i < frames; i++)
        {
            interleaved[2 * i] = left[i];
            interleaved[2 * i + 1] = right[i];
        }
    }
    else
    {
        for (int i = 0; i < frames; i++)
        {
            interleaved[2 * i] = (int16_t)(left[i] * gain);
            interleaved[2 * i + 1] = (int16_t)(right[i] * gain);
        }
    }
    const int16_t* p = interleaved;
    int remaining = frames;
    while (remaining > 0)
    {
        snd_pcm_sframes_t n = snd_pcm_writei((snd_pcm_t*)pcm, p, (snd_pcm_uframes_t)remaining);
        if (n == -EPIPE)
        {
            xruns.fetch_add(1);
            snd_pcm_prepare((snd_pcm_t*)pcm);
            continue;
        }
        if (n == -ESTRPIPE)
        {
            while (snd_pcm_resume((snd_pcm_t*)pcm) == -EAGAIN)
                snd_pcm_wait((snd_pcm_t*)pcm, 10);
            snd_pcm_prepare((snd_pcm_t*)pcm);
            continue;
        }
        if (n < 0)
        {
            snd_pcm_recover((snd_pcm_t*)pcm, (int)n, 1);
            continue;
        }
        p += n * 2;
        remaining -= (int)n;
    }
}

#else // !HAVE_ALSA

bool AlsaSink::open(const std::string& device, int, uint32_t)
{
    fprintf(stderr, "alsa: built without ALSA support, cannot open '%s'\n", device.c_str());
    return false;
}
void AlsaSink::close() {}
void AlsaSink::write(const int16_t*, const int16_t*, int, bool, float) {}

#endif
