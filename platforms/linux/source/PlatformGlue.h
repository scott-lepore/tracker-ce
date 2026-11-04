#ifndef PLATFORM_GLUE_H_
#define PLATFORM_GLUE_H_
#include <stdint.h>

// Free functions the core expects the platform to provide, plus host helpers.
extern "C" void linuxIrqLock(void);
extern "C" void linuxIrqUnlock(void);

void enableAudioInterrupt();
void disableAudioInterrupt();
bool linuxAudioInterruptEnabled();
// Audio thread wraps each block callback in this so disableAudioInterrupt()
// can wait for an in-flight block like NVIC_DisableIRQ does on the device.
void linuxAudioCallbackBegin();
void linuxAudioCallbackEnd();

void muteAudioOutByKey();
void unmuteAudioOutByKey();
uint8_t isAudioOutMuted();

int16_t sin_q15(int16_t value);
uint32_t cpuCycles();
uint32_t linuxNowUs();
uint32_t linuxNowMs();

#endif
