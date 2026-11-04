#ifndef ALSA_SEQ_H_
#define ALSA_SEQ_H_
#include "Platform.h"
#include <cstdint>
#include <deque>
#include <mutex>

// One ALSA sequencer client with two duplex ports standing in for the
// Tracker's USB MIDI and DIN jack. Incoming events are demultiplexed per
// port; the MidiImpl classes pull from their queue when the core polls.
class AlsaSeq
{
  public:
    enum Port
    {
        kUsb = 0,
        kJack = 1,
        kPads = 2, // controller notes -> Tracker grid pads (host only)
        kPorts = 3
    };
    struct Event
    {
        uint8_t type;    // ALSA snd_seq_event_type
        uint8_t channel; // 0-15
        uint8_t a, b;    // note/velocity, controller/value
        int value;       // pitch bend, song position
    };

    bool open(const char* clientName);
    void close();
    bool isOpen() const { return seq != nullptr; }

    bool pending(Port port);
    bool pop(Port port, Event& out);

    void sendNote(Port port, bool on, uint8_t note, uint8_t velocity, uint8_t channel1);
    void sendController(Port port, uint8_t cc, uint8_t value, uint8_t channel1);
    void sendProgram(Port port, uint8_t program, uint8_t channel1);
    void sendChannelPressure(Port port, uint8_t value, uint8_t channel1);
    void sendKeyPressure(Port port, uint8_t note, uint8_t value, uint8_t channel1);
    void sendPitchBend(Port port, int value, uint8_t channel1);
    void sendRealtime(Port port, uint8_t status);

  private:
    void pump();
    void send(Port port, void* ev); // snd_seq_event_t*
    void* seq = nullptr;            // snd_seq_t*
    int portId[kPorts] = { -1, -1, -1 };
    std::deque<Event> queue[kPorts];
    std::mutex mtx;
};

#endif
