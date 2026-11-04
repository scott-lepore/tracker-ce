#ifndef MIDI_IMPL_H_
#define MIDI_IMPL_H_
#include "Platform.h"

// Stage 1: no real MIDI I/O. Receive actions are stored so a later ALSA
// sequencer backend (or the dev monitor) can feed them; sends are optionally
// logged with TRACKER_MIDI_LOG=1.
class UsbMidiImpl final : public UsbMidi
{
  public:
    bool isReadAvailable() override { return false; }
    void read() override {}
    void sendRealTime(MessageType messageType) override;
    void sendControlChange(uint8_t index, uint8_t value, uint8_t channel) override;
    void sendProgramChange(uint8_t programNumber, uint8_t channel) override;
    void sendAfterTouch(uint8_t velocity, uint8_t channel) override;
    void sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel) override;
    void sendPitchBend(int value, uint8_t channel) override;
    void sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) override;
    void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) override;
    void setReceiveActions(MidiReceiveActions* receiveActions) override { actions = *receiveActions; }
    MidiReceiveActions actions = {};
};

class UartMidiImpl final : public UartMidi
{
  public:
    void init() override {}
    bool isReadAvailable() override { return false; }
    void read() override {}
    void sendRealTime(MessageType messageType) override;
    void sendControlChange(uint8_t index, uint8_t value, uint8_t channel) override;
    void sendProgramChange(uint8_t programNumber, uint8_t channel) override;
    void sendAfterTouch(uint8_t velocity, uint8_t channel) override;
    void sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel) override;
    void sendPitchBend(int value, uint8_t channel) override;
    void sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) override;
    void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) override;
    void setReceiveActions(MidiReceiveActions* receiveActions) override { actions = *receiveActions; }
    void setReceiveNoteOnAction(void (*action)(uint8_t, uint8_t, uint8_t)) override
    {
        actions.noteOn = action;
    }
    MidiReceiveActions actions = {};
};

#endif
