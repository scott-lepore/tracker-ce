#ifndef MIDI_IMPL_H_
#define MIDI_IMPL_H_
#include "AlsaSeq.h"
#include "Platform.h"

// The Tracker's two MIDI ports. Backed by the ALSA sequencer when one is
// attached (setSeq); otherwise sends are dropped (logged with
// TRACKER_MIDI_LOG=1) and nothing is received. Incoming note-on with
// velocity 0 is delivered as note-off, as the Teensy and Arduino MIDI
// libraries do on the device.
class UsbMidiImpl final : public UsbMidi
{
  public:
    void setSeq(AlsaSeq* s) { seq = s; }
    bool isReadAvailable() override;
    void read() override;
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

  private:
    AlsaSeq* seq = nullptr;
};

class UartMidiImpl final : public UartMidi
{
  public:
    void setSeq(AlsaSeq* s) { seq = s; }
    void init() override {}
    bool isReadAvailable() override;
    void read() override;
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

  private:
    AlsaSeq* seq = nullptr;
};

#endif
