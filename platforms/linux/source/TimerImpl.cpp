#include "TimerImpl.h"
#include "PlatformGlue.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
// One-shot timer thread standing in for the Teensy IntervalTimer used by
// the MIDI clock scheduler.
std::mutex g_mtx;
std::condition_variable g_cv;
std::thread g_thread;
bool g_running = false;
bool g_armed = false;
uint32_t g_generation = 0;
std::chrono::steady_clock::time_point g_deadline;
void (*g_callback)() = nullptr;

void timerThread()
{
    std::unique_lock<std::mutex> lk(g_mtx);
    while (g_running)
    {
        if (!g_armed)
        {
            g_cv.wait(lk);
            continue;
        }
        uint32_t gen = g_generation;
        if (g_cv.wait_until(lk, g_deadline) == std::cv_status::timeout && g_armed &&
            gen == g_generation)
        {
            void (*cb)() = g_callback;
            g_callback = nullptr;
            g_armed = false;
            lk.unlock();
            if (cb)
                cb();
            lk.lock();
        }
    }
}
} // namespace

void TimerImpl::start()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_running)
    {
        g_running = true;
        g_thread = std::thread(timerThread);
    }
}

void TimerImpl::stop()
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_running = false;
        g_armed = false;
    }
    g_cv.notify_all();
    if (g_thread.joinable())
        g_thread.join();
}

uint32_t TimerImpl::millis()
{
    return linuxNowMs();
}

uint32_t TimerImpl::micros()
{
    return linuxNowUs();
}

void TimerImpl::delay(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

uint32_t TimerImpl::clockCycles()
{
    return cpuCycles();
}

uint32_t TimerImpl::clockCyclesPerUs()
{
    return 1000;
}

void TimerImpl::scheduleMidiClockUs(uint32_t delayUs, void (*callback)())
{
    void (*prev)() = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_armed)
        {
            prev = g_callback;
            g_callback = nullptr;
            g_armed = false;
            g_generation++;
        }
    }
    if (prev)
        prev(); // same as the device: a pending clock fires before rescheduling

    if (delayUs <= 2U)
    {
        if (callback)
            callback();
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_callback = callback;
        g_deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(delayUs);
        g_armed = true;
        g_generation++;
    }
    g_cv.notify_all();
}

void TimerImpl::cancelMidiClock()
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_armed = false;
        g_callback = nullptr;
        g_generation++;
    }
    g_cv.notify_all();
}
