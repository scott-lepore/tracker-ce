// Headless Linux host for the Polyend Tracker core (Original Tracker build).
//
//   tracker-linux --sd ~/TrackerSD [--port 9170] [--state ~/.local/share/tracker-linux]
//
// Talk to it with tools/trackerdev.py --linux (pyserial socket:// transport).

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

void nullSink(const int16_t*, const int16_t*, int, void*) {}

void usage()
{
    fprintf(stderr,
            "usage: tracker-linux --sd DIR [--port N] [--state DIR] [--no-pacing] [--quit-after SEC]\n"
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

    // Same order as platforms/mk66/source/TrackerMain.cpp setup()
    void (*startDisplay)(Platform*) = g_tracker->getStartDisplayFunc();
    if (startDisplay)
        startDisplay(&g_platform);
    g_audio.start(g_tracker, nullSink, nullptr, pacing);
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

    fprintf(stderr, "tracker-linux: stopping (%llu audio blocks, %u late)\n",
            (unsigned long long)g_audio.blocksRendered(), g_audio.lateBlocks());
    g_audio.stop();
    g_devPort.close();
    g_platform.shutdown();
    return 0;
}
