// Headless Linux host for the Polyend Tracker core (Original Tracker build).
//
//   tracker-linux --sd ~/TrackerSD [--port 9170] [--state ~/.local/share/tracker-linux]
//
// Talk to it with tools/trackerdev.py --linux (pyserial socket:// transport).

#include "AlsaSeq.h"
#include "AlsaSink.h"
#include "AudioThread.h"
#include "DevMonitor.h"
#include "DevTcpPort.h"
#include "LinuxConfig.h"
#include "PlatformGlue.h"
#include "PlatformImpl.h"
#include "Tracker.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
PlatformImpl g_platform;
Tracker* g_tracker = nullptr;
DevTcpPort g_devPort;
AudioThread g_audio;
AlsaSink g_sink;
AlsaSeq g_seq;
volatile sig_atomic_t g_stop = 0;

void onSignal(int)
{
    g_stop = 1;
}

void devButtonHook(uint8_t idx, uint8_t action)
{
    g_platform.buttons.simulateKey(idx, action);
}

void devPadHook(uint8_t idx, uint8_t action)
{
    g_platform.pads.simulateKey(idx, action);
}

FILE* g_capture = nullptr; // --capture: raw s16le stereo of what the sink receives

void captureBlock(const int16_t* l, const int16_t* r, int frames, bool muted, float gain)
{
    static uint32_t blockCount = 0;
    static bool debug = getenv("TRACKER_DEBUG_AUDIO") != nullptr;
    if (debug && (blockCount++ % 345) == 0) // about once a second
    {
        int peak = 0;
        for (int i = 0; i < frames; i++)
        {
            int a = l[i] < 0 ? -l[i] : l[i];
            int b = r[i] < 0 ? -r[i] : r[i];
            peak = a > peak ? a : peak;
            peak = b > peak ? b : peak;
        }
        fprintf(stderr, "audio: muted=%d gain=%.2f raw-peak=%d\n", muted ? 1 : 0, gain, peak);
    }
    if (!g_capture)
        return;
    int16_t buf[2 * 1024];
    for (int i = 0; i < frames && i < 1024; i++)
    {
        float g = muted ? 0.0f : gain;
        buf[2 * i] = (int16_t)(l[i] * g);
        buf[2 * i + 1] = (int16_t)(r[i] * g);
    }
    fwrite(buf, sizeof(int16_t) * 2, (size_t)frames, g_capture);
}

float outputGain()
{
    return g_platform.codec.headphoneMuted ? 0.0f : g_platform.codec.masterVolume;
}

void nullSink(const int16_t* l, const int16_t* r, int frames, void*)
{
    captureBlock(l, r, frames, isAudioOutMuted() != 0, outputGain());
}

// Output follows the device: silent while the startup mute key is engaged,
// scaled by the codec master volume the UI sets.
void alsaSink(const int16_t* l, const int16_t* r, int frames, void*)
{
    bool muted = isAudioOutMuted() != 0;
    g_sink.write(l, r, frames, muted, outputGain());
    captureBlock(l, r, frames, muted, outputGain());
}

void usage()
{
    fprintf(stderr,
            "usage: tracker-linux --sd DIR [--audio alsa[:DEV]|none] [--latency MS] [--midi alsa|none]\n"
            "                     [--port N] [--state DIR] [--no-pacing] [--quit-after SEC]\n"
            "  --audio alsa[:DEV] play through ALSA device DEV (default 'default' = PipeWire/Pulse)\n"
            "  --audio none       discard audio, pace by the clock\n"
            "  --latency MS       ALSA buffer length (default 20)\n"
            "  --capture FILE     also write the output as raw s16le stereo 44118 Hz\n"
            "  --midi alsa|none   ALSA sequencer ports 'USB MIDI' and 'MIDI Jack' (default alsa)\n"
            "  --sd DIR        directory mirroring the SD card (Projects/, Samples/, ...)\n"
            "  --port N        dev monitor TCP port on 127.0.0.1 (default %d)\n"
            "  --state DIR     where eeprom.bin lives (default ~/.local/share/tracker-linux)\n"
            "  --no-pacing     run the audio engine as fast as possible (tests)\n"
            "  --quit-after S  exit after S seconds (smoke tests)\n",
            kDefaultDevPort);
}
} // namespace

int main(int argc, char** argv)
{
    PlatformImpl::Options opt;
    int port = kDefaultDevPort;
    bool pacing = true;
    double quitAfter = 0;
    std::string audio = "alsa";
    std::string midi = "alsa";
    int latencyMs = 20;
    std::string capture;
    const char* home = getenv("HOME");
    opt.stateDir = std::string(home ? home : ".") + "/.local/share/tracker-linux";

    for (int i = 1; i < argc; i++)
    {
        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "%s needs a value\n", flag);
                exit(1);
            }
            return argv[++i];
        };
        if (!strcmp(argv[i], "--sd")) opt.sdRoot = next("--sd");
        else if (!strcmp(argv[i], "--port")) port = atoi(next("--port"));
        else if (!strcmp(argv[i], "--state")) opt.stateDir = next("--state");
        else if (!strcmp(argv[i], "--no-pacing")) pacing = false;
        else if (!strcmp(argv[i], "--audio")) audio = next("--audio");
        else if (!strcmp(argv[i], "--midi")) midi = next("--midi");
        else if (!strcmp(argv[i], "--latency")) latencyMs = atoi(next("--latency"));
        else if (!strcmp(argv[i], "--capture")) capture = next("--capture");
        else if (!strcmp(argv[i], "--quit-after")) quitAfter = atof(next("--quit-after"));
        else
        {
            usage();
            return 1;
        }
    }
    if (opt.sdRoot.empty())
    {
        usage();
        return 1;
    }

    // ~/.local/share may not exist yet
    std::string parent = opt.stateDir.substr(0, opt.stateDir.find_last_of('/'));
    mkdir(parent.c_str(), 0775);

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    g_platform.init(opt);
    if (!g_platform.fileSystem.isSdCardInserted())
    {
        fprintf(stderr, "SD root '%s' is not a directory\n", opt.sdRoot.c_str());
        return 1;
    }
    if (!g_devPort.listen(port))
    {
        fprintf(stderr, "cannot listen on 127.0.0.1:%d\n", port);
        return 1;
    }

    g_tracker = Tracker::getInstance();

    if (!capture.empty())
    {
        g_capture = fopen(capture.c_str(), "wb");
        if (!g_capture)
            perror("--capture");
    }
    AudioThread::BlockSink sink = nullSink;
    if (audio.rfind("alsa", 0) == 0)
    {
        // 44118 Hz is the nearest integer rate to the Original's 44117.647 Hz;
        // ALSA (or the PipeWire/Pulse plug-in) resamples, so pitch matches the device.
        // Without an explicit device try the usual suspects: the 32-bit build
        // often lacks the pipewire ALSA plug-in behind "default" but has "pulse".
        const char* chain[] = { "default", "pulse", "plughw:0,0" };
        bool opened = false;
        if (audio.size() > 5 && audio[4] == ':')
        {
            opened = g_sink.open(audio.substr(5), latencyMs, 44118);
        }
        else
        {
            for (const char* dev : chain)
            {
                if ((opened = g_sink.open(dev, latencyMs, 44118)))
                    break;
            }
        }
        if (opened)
        {
            sink = alsaSink;
            pacing = false; // blocking writes pace the audio thread
        }
        else
        {
            fprintf(stderr, "audio: no ALSA device could be opened, output is silent\n");
        }
    }
    if (midi == "alsa" && g_seq.open("Tracker CE"))
    {
        g_platform.usbMidi.setSeq(&g_seq);
        g_platform.uartMidi.setSeq(&g_seq);
    }

    // Same order as platforms/mk66/source/TrackerMain.cpp setup()
    void (*startDisplay)(Platform*) = g_tracker->getStartDisplayFunc();
    if (startDisplay)
        startDisplay(&g_platform);
    g_audio.start(g_tracker, sink, nullptr, pacing);
    g_tracker->init(&g_platform);

    DevMonitorHooks hooks;
    hooks.button = devButtonHook;
    hooks.pad = devPadHook;
    devMonitorInit(&g_devPort, &g_platform, hooks);

    fprintf(stderr, "tracker-linux: core %s build %s, sd=%s, dev port 127.0.0.1:%d\n",
            Tracker::getVersion(), g_platform.getSystemInfo()->buildId, opt.sdRoot.c_str(), port);

    uint32_t t0 = linuxNowMs();
    while (!g_stop)
    {
        g_platform.update();
        g_tracker->update();
        devMonitorUpdate();
        if (quitAfter > 0 && (linuxNowMs() - t0) > (uint32_t)(quitAfter * 1000))
            break;
        usleep(500);
    }

    fprintf(stderr, "tracker-linux: stopping (%llu audio blocks, %u late, %u underruns)\n",
            (unsigned long long)g_audio.blocksRendered(), g_audio.lateBlocks(), g_sink.underruns());
    g_audio.stop();
    g_sink.close();
    g_seq.close();
    if (g_capture)
        fclose(g_capture);
    g_devPort.close();
    g_platform.shutdown();
    return 0;
}
