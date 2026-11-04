#ifndef TIMER_IMPL_H_
#define TIMER_IMPL_H_
#include "Platform.h"

class TimerImpl final : public Timer
{
  public:
    uint32_t millis() override;
    uint32_t micros() override;
    void delay(int ms) override;
    uint32_t clockCycles() override;
    uint32_t clockCyclesPerUs() override;
    void scheduleMidiClockUs(uint32_t delayUs, void (*callback)()) override;
    void cancelMidiClock() override;
    void start();
    void stop();
};

#endif
