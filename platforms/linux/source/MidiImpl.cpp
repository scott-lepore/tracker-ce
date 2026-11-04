#include "MidiImpl.h"
#include <cstdio>
#include <cstdlib>

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#else
// event type values, so the dispatch below compiles without ALSA headers
enum
{
    SND_SEQ_EVENT_NOTEON = 6,
    SND_SEQ_EVENT_NOTEOFF = 7,
    SND_SEQ_EVENT_CONTROLLER = 10,
    SND_SEQ_EVENT_SONGPOS = 20,
    SND_SEQ_EVENT_START = 30,
    SND_SEQ_EVENT_CONTINUE = 31,
    SND_SEQ_EVENT_STOP = 32,
    SND_SEQ_EVENT_CLOCK = 36,
};
#endif

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

// ------------------------------------------------------------------ USB ----
bool UsbMidiImpl::isReadAvailable()
{
    return seq && seq->pending(AlsaSeq::kUsb);
}

void UsbMidiImpl::read()
{
    AlsaSeq::Event e;
    if (!seq || !seq->pop(AlsaSeq::kUsb, e))
        return;
    uint8_t ch = e.channel + 1;
    switch (e.type)
    {
        case SND_SEQ_EVENT_NOTEON:
            if (e.b == 0)
            {
                if (actions.noteOff) actions.noteOff(ch, e.a, 0);
            }
            else if (actions.noteOn) actions.noteOn(ch, e.a, e.b);
            break;
        case SND_SEQ_EVENT_NOTEOFF:
            if (actions.noteOff) actions.noteOff(ch, e.a, e.b);
            break;
        case SND_SEQ_EVENT_CONTROLLER:
            if (actions.controlChange) actions.controlChange(ch, e.a, e.b);
            break;
        case SND_SEQ_EVENT_SONGPOS:
            if (actions.songPosition) actions.songPosition((uint16_t)e.value);
            break;
        case SND_SEQ_EVENT_CLOCK:
            if (actions.realTime) actions.realTime(0xF8);
            break;
        case SND_SEQ_EVENT_START:
            if (actions.realTime) actions.realTime(0xFA);
            break;
        case SND_SEQ_EVENT_CONTINUE:
            if (actions.realTime) actions.realTime(0xFB);
            break;
        case SND_SEQ_EVENT_STOP:
            if (actions.realTime) actions.realTime(0xFC);
            break;
        default: break;
    }
}

// ----------------------------------------------------------------- Jack ----
bool UartMidiImpl::isReadAvailable()
{
    return seq && seq->pending(AlsaSeq::kJack);
}

void UartMidiImpl::read()
{
    AlsaSeq::Event e;
    if (!seq || !seq->pop(AlsaSeq::kJack, e))
        return;
    uint8_t ch = e.channel + 1;
    switch (e.type)
    {
        case SND_SEQ_EVENT_NOTEON:
            if (e.b == 0)
            {
                if (actions.noteOff) actions.noteOff(ch, e.a, 0);
            }
            else if (actions.noteOn) actions.noteOn(ch, e.a, e.b);
            break;
        case SND_SEQ_EVENT_NOTEOFF:
            if (actions.noteOff) actions.noteOff(ch, e.a, e.b);
            break;
        case SND_SEQ_EVENT_CONTROLLER:
            if (actions.controlChange) actions.controlChange(ch, e.a, e.b);
            break;
        case SND_SEQ_EVENT_SONGPOS:
            if (actions.songPosition) actions.songPosition((unsigned)e.value);
            break;
        case SND_SEQ_EVENT_CLOCK:
            if (actions.clock) actions.clock();
            break;
        case SND_SEQ_EVENT_START:
            if (actions.start) actions.start();
            break;
        case SND_SEQ_EVENT_CONTINUE:
            if (actions.continueMessage) actions.continueMessage();
            break;
        case SND_SEQ_EVENT_STOP:
            if (actions.stop) actions.stop();
            break;
        default: break;
    }
}

// --------------------------------------------------------------- senders ---
#define MIDI_SENDERS(CLS, PORT, NAME)                                                              \
    void CLS::sendRealTime(MessageType m)                                                          \
    {                                                                                              \
        log(NAME, "rt", (int)m, 0, 0);                                                             \
        if (seq) seq->sendRealtime(PORT, (uint8_t)m);                                              \
    }                                                                                              \
    void CLS::sendControlChange(uint8_t i, uint8_t v, uint8_t ch)                                  \
    {                                                                                              \
        log(NAME, "cc", ch, i, v);                                                                 \
        if (seq) seq->sendController(PORT, i, v, ch);                                              \
    }                                                                                              \
    void CLS::sendProgramChange(uint8_t p, uint8_t ch)                                             \
    {                                                                                              \
        log(NAME, "pc", ch, p, 0);                                                                 \
        if (seq) seq->sendProgram(PORT, p, ch);                                                    \
    }                                                                                              \
    void CLS::sendAfterTouch(uint8_t v, uint8_t ch)                                                \
    {                                                                                              \
        log(NAME, "at", ch, v, 0);                                                                 \
        if (seq) seq->sendChannelPressure(PORT, v, ch);                                            \
    }                                                                                              \
    void CLS::sendPolyPressure(uint8_t n, uint8_t v, uint8_t ch)                                   \
    {                                                                                              \
        log(NAME, "pp", ch, n, v);                                                                 \
        if (seq) seq->sendKeyPressure(PORT, n, v, ch);                                             \
    }                                                                                              \
    void CLS::sendPitchBend(int v, uint8_t ch)                                                     \
    {                                                                                              \
        log(NAME, "pb", ch, v, 0);                                                                 \
        if (seq) seq->sendPitchBend(PORT, v, ch);                                                  \
    }                                                                                              \
    void CLS::sendNoteOn(uint8_t n, uint8_t v, uint8_t ch)                                         \
    {                                                                                              \
        log(NAME, "on", ch, n, v);                                                                 \
        if (seq) seq->sendNote(PORT, true, n, v, ch);                                              \
    }                                                                                              \
    void CLS::sendNoteOff(uint8_t n, uint8_t v, uint8_t ch)                                        \
    {                                                                                              \
        log(NAME, "off", ch, n, v);                                                                \
        if (seq) seq->sendNote(PORT, false, n, v, ch);                                             \
    }

MIDI_SENDERS(UsbMidiImpl, AlsaSeq::kUsb, "usb")
MIDI_SENDERS(UartMidiImpl, AlsaSeq::kJack, "jack")
