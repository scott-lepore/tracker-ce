#ifndef PLATFORM_IMPL_H_
#define PLATFORM_IMPL_H_
#include "ConfigManagerImpl.h"
#include "FileSystemImpl.h"
#include "MidiImpl.h"
#include "EveDisplayDriver.h"
#include "Platform.h"
#include "Stubs.h"
#include "SwitchGroupImpl.h"
#include "TimerImpl.h"
#include <string>

class PlatformImpl final : public Platform
{
  public:
    struct Options
    {
        std::string sdRoot;
        std::string stateDir;
    };

    void init(const Options& options);
    void update();
    void shutdown();

    void setPowerMode(bool mode) override;
    void resetMCU() override;
    void runBootloader(char* fileName) override;
    void setPowerButtonCallback(PowerButtonCallback cb) override { powerButtonCallback = cb; }
    bool getPowerButtonState() override { return false; }
    bool readForceStartUp() override { return true; }

    Sgtl5000* getCodecAudio() override { return &codec; }
    Radio* getRadio() override { return &radio; }
    SwitchGroup* getButtons() override { return &buttons; }
    SwitchGroup* getPads() override { return &pads; }
    Leds* getLeds() override { return &leds; }
    Battery* getBattery() override { return &battery; }
    Timer* getTimer() override { return &timer; }
    Encoder* getEncoder() override { return &encoder; }
    DisplayDriver* getDisplayDriver() override { return &display; }
    UsbAudio* getUsbAudio() override { return &usbAudio; }
    UartMidi* getUartMidi() override { return &uartMidi; }
    UsbMidi* getUsbMidi() override { return &usbMidi; }
    ConfigManager* getConfigManager() override { return &configManager; }
    FileSystem* getFileSystem() override { return &fileSystem; }
    CrcCalculator* getCrcCalculator() override { return &crc; }
    SystemInfo* getSystemInfo() override { return &systemInfo; }
    Usb* getUsb() override { return &usb; }

    SwitchGroupImpl buttons;
    SwitchGroupImpl pads;
    TimerImpl timer;
    FileSystemImpl fileSystem;
    ConfigManagerImpl configManager;
    EveDisplayDriver display;
    UsbMidiImpl usbMidi;
    UartMidiImpl uartMidi;
    Sgtl5000Impl codec;
    RadioImpl radio;
    LedsImpl leds;
    BatteryImpl battery;
    EncoderImpl encoder;
    UsbAudioImpl usbAudio;
    UsbImpl usb;
    CrcCalculatorImpl crc;
    PowerButtonCallback powerButtonCallback = nullptr;
    static SystemInfo systemInfo;
};

#endif
