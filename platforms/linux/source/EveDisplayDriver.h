#ifndef EVE_DISPLAY_DRIVER_H_
#define EVE_DISPLAY_DRIVER_H_
#include "DisplayDriver.h"
#include "eve/EveEmu.h"

// DisplayDriver on top of the FT812 emulator. The co-processor runs
// synchronously at endCoProList(), so it is always idle when asked.
class EveDisplayDriver final : public DisplayDriver
{
  public:
    EveEmu emu;

    void setPowerState(bool state) override { powered = state; }
    void setBrightness(int value) override { brightness = value; }
    void setRotation(int value) override { rotation = value; }

    void getImgProps(uint32_t& addr, uint32_t& width, uint32_t& height) override
    {
        emu.getProps(addr, width, height);
    }
    uint32_t getDisplayListOffset() override { return emu.dlOffset(); }
    void IncCMDOffset(uint16_t) override {}
    void WriteData32(uint32_t data) override { emu.fifoWrite32(data); }

    void beginCoProList() override { emu.execute(); }
    void beginCoProListNoCheck() override {}
    void endCoProList() override { emu.execute(); }
    void awaitCoProEmpty() override { emu.execute(); }
    uint8_t isCoProEmpty() override { emu.execute(); return 1; }
    void writeDataRAMG(const uint8_t* d, uint32_t n, uint32_t addr) override { emu.memWrite(addr, d, n); }
    void writeFontRAMG(const uint8_t* d, uint32_t n, uint32_t addr) override { emu.memWrite(addr, d, n); }
    void readDataRAMG(uint8_t* d, uint32_t n, uint32_t addr) override { emu.memRead(addr, d, n); }
    void readBackDataRAMG(uint8_t* d, uint32_t n, uint32_t addr) override { emu.memRead(addr, d, n); }
    uint8_t sendString(const char* string, uint32_t length) override;
    void writeDataToCMD(const uint8_t* d, uint32_t n) override { emu.fifoWriteBytes(d, n); emu.execute(); }

    void initStreamDma(uint16_t byteResolution, uint16_t w, uint16_t h, uint8_t* buf,
                       uint32_t dest) override
    {
        streamBytes = (uint32_t)byteResolution * w * h;
        streamBuf = buf;
        streamDest = dest;
    }
    bool updateStreamDma(bool) override
    {
        if (streamBuf) emu.memWrite(streamDest, streamBuf, streamBytes);
        return true;
    }
    void stopStreamDma() override { streamBuf = nullptr; }

    bool powered = false;
    int brightness = 0;
    int rotation = 0;

  private:
    uint8_t* streamBuf = nullptr;
    uint32_t streamBytes = 0;
    uint32_t streamDest = 0;
};

#endif
