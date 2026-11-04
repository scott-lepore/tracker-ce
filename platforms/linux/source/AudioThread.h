#ifndef AUDIO_THREAD_H_
#define AUDIO_THREAD_H_
#include "LinuxConfig.h"
#include <atomic>
#include <cstdint>
#include <thread>

class Tracker;

// Stands in for the I2S DMA interrupts: every 128 samples it asks the core
// for the next block (and feeds silent input). Paced by the steady clock to
// the Original Tracker's 44117.6 Hz. A sink callback receives each rendered
// block; stage 1 ships a null sink (nothing is played).
class AudioThread
{
  public:
    typedef void (*BlockSink)(const int16_t* left, const int16_t* right, int frames, void* user);

    void start(Tracker* tracker, BlockSink sink, void* user, bool realtimePacing);
    void stop();
    uint64_t blocksRendered() const { return blocks.load(); }
    uint32_t lateBlocks() const { return late.load(); }

  private:
    void run();
    Tracker* tracker = nullptr;
    BlockSink sink = nullptr;
    void* sinkUser = nullptr;
    bool pacing = true;
    std::thread thread;
    std::atomic<bool> running{ false };
    std::atomic<uint64_t> blocks{ 0 };
    std::atomic<uint32_t> late{ 0 };
    int16_t left[kAudioBlockSamples];
    int16_t right[kAudioBlockSamples];
    int16_t input[kAudioBlockSamples * 2];
};

#endif
