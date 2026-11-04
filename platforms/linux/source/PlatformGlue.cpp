#include "PlatformGlue.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------- IRQ lock --
// On the device DISABLE_GLOBAL_INTERRUPTS() keeps the audio ISR out of a
// critical section. Here the ISR is a thread, so the same macro takes a
// recursive mutex. An unlock without a matching lock on this thread is
// ignored (the firmware has a few early-return paths that would otherwise
// over-release).
namespace
{
std::recursive_mutex g_irqMutex;
thread_local int t_irqDepth = 0;
} // namespace

extern "C" void linuxIrqLock(void)
{
    g_irqMutex.lock();
    t_irqDepth++;
}

extern "C" void linuxIrqUnlock(void)
{
    if (t_irqDepth > 0)
    {
        t_irqDepth--;
        g_irqMutex.unlock();
    }
}

// ------------------------------------------------------ audio interrupt --
namespace
{
std::atomic<bool> g_audioIrqEnabled{ true };
std::mutex g_audioCallbackMutex;
std::atomic<std::thread::id> g_audioThreadId{};
} // namespace

void linuxAudioCallbackBegin()
{
    g_audioCallbackMutex.lock();
    g_audioThreadId.store(std::this_thread::get_id());
}

void linuxAudioCallbackEnd()
{
    g_audioThreadId.store(std::thread::id());
    g_audioCallbackMutex.unlock();
}

void disableAudioInterrupt()
{
    g_audioIrqEnabled.store(false);
    if (g_audioThreadId.load() == std::this_thread::get_id())
    {
        return; // called from inside the block callback: nothing to wait for
    }
    // NVIC_DisableIRQ lets a running ISR finish; mirror that by waiting for
    // the current block callback (if any) to return.
    g_audioCallbackMutex.lock();
    g_audioCallbackMutex.unlock();
}

void enableAudioInterrupt()
{
    g_audioIrqEnabled.store(true);
}

bool linuxAudioInterruptEnabled()
{
    return g_audioIrqEnabled.load();
}

// -------------------------------------------------------------- mute key --
namespace
{
std::atomic<bool> g_outputMuted{ true }; // the device boots muted
}

void muteAudioOutByKey()
{
    g_outputMuted.store(true);
}

void unmuteAudioOutByKey()
{
    g_outputMuted.store(false);
}

uint8_t isAudioOutMuted()
{
    return g_outputMuted.load() ? 1 : 0;
}

// ------------------------------------------------------------------ time --
namespace
{
const std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();
}

uint32_t linuxNowUs()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now() - g_t0)
      .count();
}

uint32_t linuxNowMs()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - g_t0)
      .count();
}

// "clock cycles" are nanoseconds here; TimerImpl reports 1000 cycles per us.
uint32_t cpuCycles()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now() - g_t0)
      .count();
}

// ------------------------------------------------------------- sin_q15 ----
// Same algorithm as CMSIS arm_sin_q15: 512-entry table, linear interpolation.
namespace
{
struct SinTable
{
    int16_t v[513];
    SinTable()
    {
        for (int i = 0; i <= 512; i++)
        {
            v[i] = (int16_t)lrint(sin(2.0 * M_PI * i / 512.0) * 32767.0);
        }
    }
};
const SinTable g_sinTable;
} // namespace

int16_t sin_q15(int16_t x)
{
    uint16_t ux = (uint16_t)x;
    int32_t index = ux >> 6;               // 10 bits of index
    int32_t fract = (ux & 0x3F) << 9;      // Q15 fraction
    int32_t a = g_sinTable.v[index];
    int32_t b = g_sinTable.v[index + 1];
    int32_t sinVal = ((0x8000 - fract) * a) >> 16;
    sinVal += (fract * b) >> 16;
    return (int16_t)(sinVal << 1);
}

// ------------------------------------------------------------- logging ----
void _debuglog(const char* msg)
{
    fputs(msg, stderr);
    fputc('\n', stderr);
}

void _debuglogf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

extern "C" void assert_func_play(const char* file, int line)
{
    fprintf(stderr, "ASSERT %s:%d\n", file, line);
    abort();
}

extern "C" void restartMCU()
{
    fprintf(stderr, "restartMCU() requested, exiting\n");
    exit(2);
}
