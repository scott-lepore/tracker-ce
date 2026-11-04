#include "AudioThread.h"
#include "PlatformGlue.h"
#include "Tracker.h"
#include <chrono>
#include <cstring>

void AudioThread::start(Tracker* t, BlockSink s, void* user, bool realtimePacing)
{
    tracker = t;
    sink = s;
    sinkUser = user;
    pacing = realtimePacing;
    memset(input, 0, sizeof(input));
    running.store(true);
    thread = std::thread(&AudioThread::run, this);
}

void AudioThread::stop()
{
    running.store(false);
    if (thread.joinable())
        thread.join();
}

void AudioThread::run()
{
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::nanoseconds(
      (int64_t)(1e9 * kAudioBlockSamples / kAudioSampleRateHz + 0.5));
    auto next = clock::now();
    while (running.load())
    {
        if (linuxAudioInterruptEnabled())
        {
            linuxAudioCallbackBegin();
            tracker->updateInputAudioBuffers(input, nullptr, kAudioBlockSamples);
            tracker->updateAudioBuffers(left, right, kAudioBlockSamples);
            linuxAudioCallbackEnd();
        }
        else
        {
            memset(left, 0, sizeof(left));
            memset(right, 0, sizeof(right));
        }
        if (sink)
            sink(left, right, kAudioBlockSamples, sinkUser);
        blocks.fetch_add(1);

        next += period;
        if (pacing)
        {
            auto now = clock::now();
            if (now > next + period * 4)
            {
                late.fetch_add(1);
                next = now; // fell far behind: resync instead of bursting
            }
            else
            {
                std::this_thread::sleep_until(next);
            }
        }
    }
}
