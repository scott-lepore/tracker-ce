#include "MidiImpl.h"
#include <cstdio>
#include <cstdlib>

namespace
{
bool logEnabled()
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* e = getenv("TRACKER_MIDI_LOG");
        enabled = (e != nullptr && *e != '0') ? 1 : 0;
    }
    return enabled == 1;
}
void log(const char* port, const char* what, int a, int b, int c)
{
    if (logEnabled())
        fprintf(stderr, "midi-out %s %s %d %d %d\n", port, what, a, b, c);
}
} // namespace

#define MIDI_SENDERS(CLS, NAME)                                                                    \
    void CLS::sendRealTime(MessageType m) { log(NAME, "rt", (int)m, 0, 0); }                      \
    void CLS::sendControlChange(uint8_t i, uint8_t v, uint8_t ch) { log(NAME, "cc", ch, i, v); }  \
    void CLS::sendProgramChange(uint8_t p, uint8_t ch) { log(NAME, "pc", ch, p, 0); }             \
    void CLS::sendAfterTouch(uint8_t v, uint8_t ch) { log(NAME, "at", ch, v, 0); }                \
    void CLS::sendPolyPressure(uint8_t n, uint8_t v, uint8_t ch) { log(NAME, "pp", ch, n, v); }   \
    void CLS::sendPitchBend(int v, uint8_t ch) { log(NAME, "pb", ch, v, 0); }                     \
    void CLS::sendNoteOn(uint8_t n, uint8_t v, uint8_t ch) { log(NAME, "on", ch, n, v); }         \
    void CLS::sendNoteOff(uint8_t n, uint8_t v, uint8_t ch) { log(NAME, "off", ch, n, v); }

MIDI_SENDERS(UsbMidiImpl, "usb")
MIDI_SENDERS(UartMidiImpl, "jack")
