#include "AlsaSeq.h"
#include <cstdio>

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>

bool AlsaSeq::open(const char* clientName)
{
    snd_seq_t* h = nullptr;
    int err = snd_seq_open(&h, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (err < 0)
    {
        fprintf(stderr, "alsa seq: open failed: %s\n", snd_strerror(err));
        return false;
    }
    snd_seq_set_client_name(h, clientName);
    const char* names[kPorts] = { "USB MIDI", "MIDI Jack" };
    unsigned caps = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_WRITE |
                    SND_SEQ_PORT_CAP_SUBS_WRITE;
    unsigned type = SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION |
                    SND_SEQ_PORT_TYPE_SOFTWARE;
    for (int p = 0; p < kPorts; p++)
    {
        portId[p] = snd_seq_create_simple_port(h, names[p], caps, type);
        if (portId[p] < 0)
        {
            fprintf(stderr, "alsa seq: port '%s' failed: %s\n", names[p], snd_strerror(portId[p]));
            snd_seq_close(h);
            return false;
        }
    }
    seq = h;
    fprintf(stderr, "alsa seq: client %d '%s' ports %d (USB) %d (Jack)\n", snd_seq_client_id(h),
            clientName, portId[kUsb], portId[kJack]);
    return true;
}

void AlsaSeq::close()
{
    if (seq)
    {
        snd_seq_close((snd_seq_t*)seq);
        seq = nullptr;
    }
}

void AlsaSeq::pump()
{
    snd_seq_event_t* ev = nullptr;
    while (snd_seq_event_input((snd_seq_t*)seq, &ev) >= 0 && ev)
    {
        int p = -1;
        for (int i = 0; i < kPorts; i++)
            if (ev->dest.port == portId[i])
                p = i;
        if (p < 0)
            continue;
        Event e{};
        e.type = (uint8_t)ev->type;
        switch (ev->type)
        {
            case SND_SEQ_EVENT_NOTEON:
            case SND_SEQ_EVENT_NOTEOFF:
            case SND_SEQ_EVENT_KEYPRESS:
                e.channel = ev->data.note.channel;
                e.a = ev->data.note.note;
                e.b = ev->data.note.velocity;
                break;
            case SND_SEQ_EVENT_CONTROLLER:
            case SND_SEQ_EVENT_PGMCHANGE:
            case SND_SEQ_EVENT_CHANPRESS:
            case SND_SEQ_EVENT_PITCHBEND:
            case SND_SEQ_EVENT_SONGPOS:
                e.channel = ev->data.control.channel;
                e.a = (uint8_t)ev->data.control.param;
                e.value = ev->data.control.value;
                e.b = (uint8_t)(ev->data.control.value & 0x7F);
                break;
            case SND_SEQ_EVENT_CLOCK:
            case SND_SEQ_EVENT_START:
            case SND_SEQ_EVENT_STOP:
            case SND_SEQ_EVENT_CONTINUE:
                break;
            default:
                continue; // sysex, port events, etc.
        }
        queue[p].push_back(e);
    }
}

bool AlsaSeq::pending(Port port)
{
    if (!seq)
        return false;
    std::lock_guard<std::mutex> lk(mtx);
    if (queue[port].empty())
        pump();
    return !queue[port].empty();
}

bool AlsaSeq::pop(Port port, Event& out)
{
    if (!seq)
        return false;
    std::lock_guard<std::mutex> lk(mtx);
    if (queue[port].empty())
        pump();
    if (queue[port].empty())
        return false;
    out = queue[port].front();
    queue[port].pop_front();
    return true;
}

void AlsaSeq::send(Port port, void* evp)
{
    if (!seq)
        return;
    snd_seq_event_t* ev = (snd_seq_event_t*)evp;
    snd_seq_ev_set_source(ev, (unsigned char)portId[port]);
    snd_seq_ev_set_subs(ev);
    snd_seq_ev_set_direct(ev);
    std::lock_guard<std::mutex> lk(mtx);
    snd_seq_event_output_direct((snd_seq_t*)seq, ev);
}

#define EV_BEGIN snd_seq_event_t ev; snd_seq_ev_clear(&ev)

void AlsaSeq::sendNote(Port port, bool on, uint8_t note, uint8_t velocity, uint8_t ch1)
{
    EV_BEGIN;
    if (on)
        snd_seq_ev_set_noteon(&ev, (ch1 - 1) & 0x0F, note, velocity);
    else
        snd_seq_ev_set_noteoff(&ev, (ch1 - 1) & 0x0F, note, velocity);
    send(port, &ev);
}

void AlsaSeq::sendController(Port port, uint8_t cc, uint8_t value, uint8_t ch1)
{
    EV_BEGIN;
    snd_seq_ev_set_controller(&ev, (ch1 - 1) & 0x0F, cc, value);
    send(port, &ev);
}

void AlsaSeq::sendProgram(Port port, uint8_t program, uint8_t ch1)
{
    EV_BEGIN;
    snd_seq_ev_set_pgmchange(&ev, (ch1 - 1) & 0x0F, program);
    send(port, &ev);
}

void AlsaSeq::sendChannelPressure(Port port, uint8_t value, uint8_t ch1)
{
    EV_BEGIN;
    snd_seq_ev_set_chanpress(&ev, (ch1 - 1) & 0x0F, value);
    send(port, &ev);
}

void AlsaSeq::sendKeyPressure(Port port, uint8_t note, uint8_t value, uint8_t ch1)
{
    EV_BEGIN;
    snd_seq_ev_set_keypress(&ev, (ch1 - 1) & 0x0F, note, value);
    send(port, &ev);
}

void AlsaSeq::sendPitchBend(Port port, int value, uint8_t ch1)
{
    EV_BEGIN;
    if (value < -8192) value = -8192;
    if (value > 8191) value = 8191;
    snd_seq_ev_set_pitchbend(&ev, (ch1 - 1) & 0x0F, value);
    send(port, &ev);
}

void AlsaSeq::sendRealtime(Port port, uint8_t status)
{
    EV_BEGIN;
    switch (status)
    {
        case 0xF8: ev.type = SND_SEQ_EVENT_CLOCK; break;
        case 0xFA: ev.type = SND_SEQ_EVENT_START; break;
        case 0xFB: ev.type = SND_SEQ_EVENT_CONTINUE; break;
        case 0xFC: ev.type = SND_SEQ_EVENT_STOP; break;
        case 0xFE: ev.type = SND_SEQ_EVENT_SENSING; break;
        case 0xFF: ev.type = SND_SEQ_EVENT_RESET; break;
        case 0xF6: ev.type = SND_SEQ_EVENT_TUNE_REQUEST; break;
        default: return;
    }
    send(port, &ev);
}

#else // !HAVE_ALSA

bool AlsaSeq::open(const char*) { return false; }
void AlsaSeq::close() {}
void AlsaSeq::pump() {}
bool AlsaSeq::pending(Port) { return false; }
bool AlsaSeq::pop(Port, Event&) { return false; }
void AlsaSeq::send(Port, void*) {}
void AlsaSeq::sendNote(Port, bool, uint8_t, uint8_t, uint8_t) {}
void AlsaSeq::sendController(Port, uint8_t, uint8_t, uint8_t) {}
void AlsaSeq::sendProgram(Port, uint8_t, uint8_t) {}
void AlsaSeq::sendChannelPressure(Port, uint8_t, uint8_t) {}
void AlsaSeq::sendKeyPressure(Port, uint8_t, uint8_t, uint8_t) {}
void AlsaSeq::sendPitchBend(Port, int, uint8_t) {}
void AlsaSeq::sendRealtime(Port, uint8_t) {}

#endif
