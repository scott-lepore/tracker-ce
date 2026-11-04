#include "AlsaSource.h"
#include <cstdio>
#include <cstring>

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>

bool AlsaSource::open(const std::string& device, int latencyMs, uint32_t sampleRate)
{
    snd_pcm_t* h = nullptr;
    int err = snd_pcm_open(&h, device.c_str(), SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if (err < 0)
    {
        fprintf(stderr, "alsa in: cannot open '%s': %s\n", device.c_str(), snd_strerror(err));
        return false;
    }
    unsigned int latencyUs = (unsigned int)(latencyMs > 0 ? latencyMs : 50) * 1000u;
    err = snd_pcm_set_params(h, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, sampleRate, 1,
                             latencyUs);
    if (err < 0)
    {
        fprintf(stderr, "alsa in: set_params failed on '%s': %s\n", device.c_str(), snd_strerror(err));
        snd_pcm_close(h);
        return false;
    }
    snd_pcm_start(h);
    pcm = h;
    name = device;
    fprintf(stderr, "alsa in: capture from '%s' %u Hz\n", device.c_str(), sampleRate);
    return true;
}

void AlsaSource::close()
{
    if (pcm)
    {
        snd_pcm_close((snd_pcm_t*)pcm);
        pcm = nullptr;
    }
}

void AlsaSource::read(int16_t* out, int frames)
{
    if (!pcm)
    {
        memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }
    int got = 0;
    while (got < frames)
    {
        snd_pcm_sframes_t n = snd_pcm_readi((snd_pcm_t*)pcm, out + got * 2, (snd_pcm_uframes_t)(frames - got));
        if (n == -EAGAIN || n == 0)
            break; // nothing more yet
        if (n == -EPIPE)
        {
            xruns.fetch_add(1);
            snd_pcm_prepare((snd_pcm_t*)pcm);
            snd_pcm_start((snd_pcm_t*)pcm);
            continue;
        }
        if (n < 0)
        {
            snd_pcm_recover((snd_pcm_t*)pcm, (int)n, 1);
            snd_pcm_start((snd_pcm_t*)pcm);
            break;
        }
        got += (int)n;
    }
    if (got < frames)
        memset(out + got * 2, 0, (size_t)(frames - got) * 2 * sizeof(int16_t));
}

#else
bool AlsaSource::open(const std::string& device, int, uint32_t)
{
    fprintf(stderr, "alsa in: built without ALSA support, cannot open '%s'\n", device.c_str());
    return false;
}
void AlsaSource::close() {}
void AlsaSource::read(int16_t* out, int frames) { memset(out, 0, (size_t)frames * 4); }
#endif
