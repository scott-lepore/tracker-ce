#ifndef LINUX_CONFIG_H_
#define LINUX_CONFIG_H_
#include <stdint.h>

// Mirrors platforms/mk66/config/config.h for the values the core cares about.
static constexpr int kAudioBlockSamples = 128;
static constexpr float kAudioSampleRateHz = 44117.6470588f; // Original Tracker I2S clock

static constexpr uint32_t START_STATE_EEPROM_ADRESS = 0;
static constexpr uint32_t CONFIG_EEPROM_ADRESS = 10;
static constexpr int kEepromSize = 4096;

static constexpr int kDefaultDevPort = 9170;

#endif
