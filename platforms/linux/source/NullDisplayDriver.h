#ifndef NULL_DISPLAY_DRIVER_H_
#define NULL_DISPLAY_DRIVER_H_
#include "DisplayDriver.h"

// Headless FT812: swallows every co-processor command, reports the
// co-processor as always idle. Keeps a few counters so a renderer can be
// slotted in later without touching the interface.
class NullDisplayDriver final : public DisplayDriver
{
  public:
    void setPowerState(bool state) override { powered = state; }
    void setBrightness(int value) override { brightness = value; }
    void setRotation(int value) override { rotation = value; }

    void getImgProps(uint32_t& addr, uint32_t& width, uint32_t& height) override
    {
        addr = 0;
        width = 0;
        height = 0;
    }
    uint32_t getDisplayListOffset() override { return 0; }
    void IncCMDOffset(uint16_t value) override { cmdOffset = (uint16_t)((cmdOffset + value) & 0xFFF); }
    void WriteData32(uint32_t) override { words++; }

    void beginCoProList() override { lists++; }
    void beginCoProListNoCheck() override { lists++; }
    void endCoProList() override {}
    void awaitCoProEmpty() override {}
    uint8_t isCoProEmpty() override { return 1; }
    void writeDataRAMG(const uint8_t*, uint32_t, uint32_t) override {}
    void writeFontRAMG(const uint8_t*, uint32_t, uint32_t) override {}
    void readDataRAMG(uint8_t* imgData, uint32_t dataSize, uint32_t) override
    {
        for (uint32_t i = 0; i < dataSize; i++)
            imgData[i] = 0;
    }
    void readBackDataRAMG(uint8_t* imgData, uint32_t dataSize, uint32_t src) override
    {
        readDataRAMG(imgData, dataSize, src);
    }
    uint8_t sendString(const char* string, uint32_t length) override;
    void writeDataToCMD(const uint8_t*, uint32_t) override {}

    void initStreamDma(uint16_t, uint16_t, uint16_t, uint8_t*, uint32_t) override {}
    bool updateStreamDma(bool) override { return true; }
    void stopStreamDma() override {}

    bool powered = false;
    int brightness = 0;
    int rotation = 0;
    uint16_t cmdOffset = 0;
    uint32_t words = 0;
    uint32_t lists = 0;
};

#endif
