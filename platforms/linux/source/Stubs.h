#ifndef STUBS_H_
#define STUBS_H_
#include "Platform.h"
#include <cstring>

// Peripherals that have no host counterpart (yet). All no-ops with sane
// return values so the core behaves as on a healthy, mains-powered device.

class Sgtl5000Impl final : public Sgtl5000
{
  public:
    void init() override {}
    void setInputType(InputType) override {}
    void setHeadphoneSource(HeadphoneSource) override {}
    void setLineInLevel(int) override {}
    void setMicGain(int) override {}
    void setHeadphoneMuted(bool muted) override { headphoneMuted = muted; }
    void setMasterVolume(float volume) override { masterVolume = volume; }
    void setAutoVolumeEnabled(bool) override {}
    void configureAutoVolume(AutoVolumeParams*) override {}
    void setEnhanceBassEnabled(bool) override {}
    void configureEnhanceBass(EnhanceBassParams*) override {}
    void setEqualizerType(EqualizerType) override {}
    void configureFiveBandEqualizer(float*) override {}
    void enableSurroundSound(bool) override {}
    void configureSurroundSound(SurroundSoundParams*) override {}
    void enableHardwareProcessor(bool) override {}
    bool headphoneMuted = false;
    float masterVolume = 1.0f;
};

class RadioImpl final : public Radio
{
  public:
    void enableLineInput(bool) override {}
    void enable(bool) override {}
    bool setFrequency(float f) override { freq = f; return true; }
    void setSeekCallback(void (*)(void)) override {}
    void seek(SeekDirection) override {}
    void updateSeek() override {}
    bool updateRDS() override { return false; }
    float getFrequency() override { return freq; }
    float getBaseFrequency() override { return 87.5f; }
    float getSpacing() override { return 0.1f; }
    bool isinitialized() override { return false; }
    void clearRDS() override {}
    void setRegion(Region) override {}
    bool isRegionChanged() override { return false; }
    char* getStationName() override { return stationName; }
    float freq = 87.5f;
    char stationName[9] = "";
};

class LedsImpl final : public Leds
{
  public:
    void setLed(int idx, bool enabled, int brightness) override
    {
        if (idx >= 0 && idx < 48)
            level[idx] = enabled ? (uint8_t)brightness : 0;
    }
    uint8_t level[48] = { 0 };
};

class BatteryImpl final : public Battery
{
  public:
    void enableMonitoring() override {}
    int getLevel() override { return 100; }
    float getVoltage() override { return 4.2f; }
    bool getCharging() override { return false; }
    int getTimeTo() override { return 0; }
    bool getState() override { return true; }
    bool isEmpty() override { return false; }
};

class EncoderImpl final : public Encoder
{
  public:
    void setAcceleration(int a) override { acceleration = a; }
    void setResolution(int r) override { resolution = r; }
    int acceleration = 2;
    int resolution = 24;
};

class UsbAudioImpl final : public UsbAudio
{
  public:
    void beginFillData() override {}
    void fillChannelData(void*, int, int) override {}
    void finishFillData() override {}
};

class UsbImpl final : public Usb
{
  public:
    void init(void) override {}
    void reset(void) override {}
    void update(void) override {}
    void setClassState(ClassType type, bool enable) override { state[(int)type] = enable; }
    bool getClassState(ClassType type) override { return state[(int)type]; }
    bool isAttached() override { return true; }
    bool state[5] = { true, false, false, false, true };
};

class CrcCalculatorImpl final : public CrcCalculator
{
  public:
    // CRC-32 (poly 0x04C11DB7 reflected, init/xorout 0xFFFFFFFF), same as FastCRC32::crc32
    uint32_t calculate32bitCrc(const uint8_t* data, const uint16_t datalen) override
    {
        uint32_t crc = 0xFFFFFFFFu;
        for (uint16_t i = 0; i < datalen; i++)
        {
            crc ^= data[i];
            for (int b = 0; b < 8; b++)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }
};

#endif
