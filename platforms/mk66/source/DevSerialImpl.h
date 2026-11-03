#ifndef SOURCE_DEVSERIALIMPL_H_
#define SOURCE_DEVSERIALIMPL_H_

#include "DevMonitor.h"

// USB CDC serial port (the "Serial" interface of the Tracker's composite USB device).
class DevSerialImpl : public DevSerialPort
{
  public:
    virtual int available() override;
    virtual int read(uint8_t* buf, size_t len) override;
    virtual size_t write(const uint8_t* buf, size_t len) override;
    virtual bool connected() override;
};

class PlatformImpl;
void devSerialStart(PlatformImpl* platform);
void devSerialUpdate();

#endif /* SOURCE_DEVSERIALIMPL_H_ */
