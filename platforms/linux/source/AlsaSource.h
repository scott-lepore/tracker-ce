#ifndef ALSA_SOURCE_H_
#define ALSA_SOURCE_H_
#include <atomic>
#include <cstdint>
#include <string>

// ALSA capture feeding the core's audio input (line in / mic on the device),
// so the Sample Recorder and input monitoring work on the host. Runs in
// non-blocking mode: the audio thread pulls whatever has arrived and pads
// with silence, so capture never stalls playback pacing.
class AlsaSource
{
  public:
    bool open(const std::string& device, int latencyMs, uint32_t sampleRate);
    void close();
    bool isOpen() const { return pcm != nullptr; }
    // Fills `frames` interleaved stereo int16 frames; missing data = silence.
    void read(int16_t* interleaved, int frames);
    uint32_t overruns() const { return xruns.load(); }
    const std::string& deviceName() const { return name; }

  private:
    void* pcm = nullptr;
    std::string name;
    std::atomic<uint32_t> xruns{ 0 };
};

#endif
