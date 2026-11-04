#ifndef ALSA_SINK_H_
#define ALSA_SINK_H_
#include <atomic>
#include <cstdint>
#include <string>

// Plays the core's stereo blocks through ALSA (which on this machine is the
// PipeWire/Pulse plugin). Blocking writes pace the audio thread, replacing the
// steady-clock pacing used by the null sink. Honours the device's output
// mute key and the codec master volume so levels follow the Tracker's UI.
class AlsaSink
{
  public:
    bool open(const std::string& device, int latencyMs, uint32_t sampleRate);
    void close();
    bool isOpen() const { return pcm != nullptr; }
    // frames = 128 per block; left/right are int16
    void write(const int16_t* left, const int16_t* right, int frames, bool muted, float gain);
    uint32_t underruns() const { return xruns.load(); }
    uint32_t actualRate() const { return rate; }
    uint32_t bufferFrames() const { return bufFrames; }
    const std::string& deviceName() const { return name; }

  private:
    void* pcm = nullptr; // snd_pcm_t*
    std::string name;
    uint32_t rate = 0;
    uint32_t bufFrames = 0;
    std::atomic<uint32_t> xruns{ 0 };
    int16_t interleaved[2 * 1024];
};

#endif
