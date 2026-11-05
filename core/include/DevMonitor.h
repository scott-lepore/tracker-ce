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
// Input event tap (called by the core's button/pad/encoder handlers on the
// main loop). Emits "ev <b|p|j> <idx> <value>" lines while `events 1` is on,
// so a host can mirror the physical controls onto another instance.
// Returns true when the event was consumed (controller mode): the caller must
// then NOT pass it to the UI. Controller mode (`controller 1`) turns the
// device into a dumb control surface for a host and ends automatically when
// the host disconnects.
bool devMonitorInputEvent(char kind, uint8_t idx, int16_t value);
bool devMonitorControllerMode();
// MIDI input log (`midilog 1`): one line per incoming note, CC and transport
// message plus what the firmware did with it. Main loop only.
bool devMonitorMidiLogOn();
void devMonitorMidiLog(const char* fmt, ...);
void devMonitorMidiClock(char src); // counted, reported once per 96 clocks
#define DEVMIDILOG(...)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if (devMonitorMidiLogOn())                                                                 \
            devMonitorMidiLog(__VA_ARGS__);                                                        \
    } while (0)
// Call from the main loop (never from the audio interrupt).
void devMonitorUpdate();

#endif /* DEVMONITOR_H_ */
