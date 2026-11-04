#include "AudioThread.h"
#include "PlatformGlue.h"
#include "Tracker.h"
#include <chrono>
#include <cstring>
#include <pthread.h>
#include <sched.h>

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
    // Best effort real-time priority (works when the user has rtprio, e.g.
    // via PipeWire's rtkit limits); silently ignored otherwise.
    sched_param sp{};
    sp.sched_priority = 70;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

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
