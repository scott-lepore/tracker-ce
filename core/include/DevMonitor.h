#ifndef DEVMONITOR_H_
#define DEVMONITOR_H_

#include <stddef.h>
#include <stdint.h>

class Platform;

// Byte stream used by the developer monitor (USB CDC serial on hardware).
class DevSerialPort
{
  public:
    virtual int available() = 0;
    // Non-blocking: reads up to len bytes, returns number read (0 if none).
    virtual int read(uint8_t* buf, size_t len) = 0;
    virtual size_t write(const uint8_t* buf, size_t len) = 0;
    // True while a host has the port open (DTR asserted).
    virtual bool connected() = 0;
};

// Optional platform hooks for input injection.
struct DevMonitorHooks
{
    // action: 0 = release, 1 = press, 2 = hold
    void (*button)(uint8_t idx, uint8_t action) = nullptr;
    void (*pad)(uint8_t idx, uint8_t action) = nullptr;
    // Host builds: save the current screen to a file. nullptr = unsupported.
    bool (*screenshot)(const char* path) = nullptr;
};

void devMonitorInit(DevSerialPort* port, Platform* platform, const DevMonitorHooks& hooks);
// Call from the main loop (never from the audio interrupt).
void devMonitorUpdate();

#endif /* DEVMONITOR_H_ */
