#ifndef SOURCE_PROFILER_H_
#define SOURCE_PROFILER_H_
#include "AudioStream.h"
#include "config.h"
#include "stddef.h"
#include "stdint.h"

class Profiler
{
  public:
    static Profiler* getInstance()
    {
        if (instance == nullptr)
        {
            instance = new Profiler;
        }

        return instance;
    }

    struct AudioProcessorUsage
    {
        int minimum;
        int maximum;
        int tempMaximum;
        int average;
        int last;
    };

    void init();

    size_t freeRamBytes();
    size_t maxStackBytes();
    void getAudioProcessingUsageUs(AudioProcessorUsage& usage, bool resetTempMaximum = false);
    void getAudioProcessingUsagePercent(AudioProcessorUsage& usage, bool resetTempMaximum = false);
    void startMeasuringAudio();
    void stopMeasuringAudio();
    void resetMeasuringAudio();

    // For guards in the audio interrupt: the last block's load in percent, and a count of
    // measured blocks that resetMeasuringAudio leaves alone.
    int lastAudioLoadPercent() const { return (100 * lastSampleUs) / kMaxAudioUpdateTime; }
    uint32_t audioBlockCount() const { return blockCount; }

  private:
    static constexpr uint8_t kStackWatermark =
      0xAA; // this value should match the same value from initial code;
    static constexpr float kSamplePeriodUs = (1.0f / 44100.0f) * 1000000.0f;
    static constexpr int kMaxAudioUpdateTime = AUDIO_BLOCK_SAMPLES * kSamplePeriodUs;
    static Profiler* instance;
    int calculatePercentProcessorUsage(int processorUsageUs);

    uint32_t audioUpdateTimer;
    int lastSampleUs;
    int sumOfSamples;
    int samples;
    int minSampleUs;
    int maxSampleUs;
    int tempMaxSampleUs;
    uint32_t blockCount = 0;
};

#endif /* SOURCE_PROFILER_H_ */
