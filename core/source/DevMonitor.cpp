// Developer monitor: line-oriented command console over the USB serial port.
// See docs/dev_serial_monitor.md and tools/trackerdev.py.

#include "DevMonitor.h"

#include "Platform.h"
#include "Profiler.h"
#include "Tracker.h"
#include "config.h"
#include "mtMidi.h"
#include "mtSequencer.h"
#include "mtStructs.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void onPotChange(uint8_t n, int16_t value);

namespace
{
DevSerialPort* port = nullptr;
Platform* platform = nullptr;
DevMonitorHooks hooks;

constexpr size_t kLineMax = 160;
char line[kLineMax];
size_t lineLen = 0;

char out[256];

uint32_t monIntervalMs = 0;
uint32_t monLastMs = 0;

// Upload state (put command), serviced incrementally from update().
struct Upload
{
    bool active = false;
    FileSystem::FileHandle file = nullptr;
    uint32_t remaining = 0;
    uint32_t total = 0;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t lastRxMs = 0;
    char path[96];
} upload;

constexpr uint32_t kUploadTimeoutMs = 5000;
constexpr size_t kUploadChunk = 4096;
uint8_t chunk[kUploadChunk];

uint32_t nowMs()
{
    return platform->getTimer()->millis();
}

void send(const char* s)
{
    port->write(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

void sendf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(out, sizeof(out), fmt, args);
    va_end(args);
    send(out);
}

void ok()
{
    send("ok\n");
}

void err(const char* why)
{
    sendf("err %s\n", why);
}

uint32_t crc32Update(uint32_t crc, const uint8_t* p, size_t n)
{
    while (n--)
    {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

const char* productName()
{
#if defined(TRACKER_MINI)
    return "mini";
#elif defined(PLATFORM_IMX)
    return "plus";
#else
    return "original";
#endif
}

void cmdPing()
{
    sendf("pong tracker-ce %s build %s product %s\n",
          Tracker::getVersion(),
          platform->getSystemInfo()->buildId,
          productName());
    ok();
}

void sendStat(const char* tag)
{
    Profiler::AudioProcessorUsage u;
    Profiler::getInstance()->getAudioProcessingUsagePercent(u, true);

    int bpm10 = (int)(sequencer.getCurrentTempo() * 10.0f + 0.5f);

    sendf("%s t=%lu play=%d rec=%d bpm=%d.%d pat=%d pos=%d cpu=%d/%d/%d/%d ram=%u stack=%u\n",
          tag,
          (unsigned long)nowMs(),
          sequencer.isPlay() ? 1 : 0,
          sequencer.isRec() ? 1 : 0,
          bpm10 / 10,
          bpm10 % 10,
          mtProject.values.actualPattern,
          sequencer.getActualPos(),
          u.minimum,
          u.average,
          u.maximum,
          u.last,
          (unsigned)Profiler::getInstance()->freeRamBytes(),
          (unsigned)Profiler::getInstance()->maxStackBytes());
}

void cmdStat()
{
    sendStat("stat");
    ok();
}

void cmdTracks()
{
    for (uint8_t tr = 0; tr < kSampleTracks; tr++)
    {
        Sequencer::TrackDevInfo info;
        sequencer.getTrackDevInfo(tr, info);
        sendf("track %d ch=%d pos=%d open=%d inst=%d note=%d last=%d held=%d mute=%d\n",
              tr + 1,
              mtConfig.midi.trackNotesInChannel[tr],
              info.pos,
              info.noteOpen ? 1 : 0,
              info.instrument,
              info.note,
              info.lastInstrument,
              midiDevGetTrackHeldNote(tr),
              info.muted ? 1 : 0);
    }
    ok();
}

void cmdMidi(int argc, char** argv)
{
    if (argc < 4)
    {
        err("usage: midi <ch 1-16> <note 0-127> <vel 0-127> [j]");
        return;
    }
    int ch = atoi(argv[1]);
    int note = atoi(argv[2]);
    int vel = atoi(argv[3]);
    bool jack = (argc >= 5 && argv[4][0] == 'j');

    if (ch < 1 || ch > 16 || note < 0 || note > 127 || vel < 0 || vel > 127)
    {
        err("range");
        return;
    }

    if (jack)
    {
        handleJackNoteOn(ch, note, vel); // vel 0 is routed to note-off inside
    }
    else
    {
        handleUsbNoteOn(ch, note, vel);
    }
    ok();
}

uint8_t parseAction(const char* s, bool* valid)
{
    *valid = true;
    switch (s[0])
    {
        case 'p': return 1;
        case 'r': return 0;
        case 'h': return 2;
        default: *valid = false; return 0;
    }
}

void cmdKey(int argc, char** argv, bool isPad)
{
    if (argc < 3)
    {
        err(isPad ? "usage: pad <0-47> <p|r|h>" : "usage: key <idx> <p|r|h>");
        return;
    }
    void (*hook)(uint8_t, uint8_t) = isPad ? hooks.pad : hooks.button;
    if (hook == nullptr)
    {
        err("not supported on this platform");
        return;
    }
    bool valid;
    uint8_t action = parseAction(argv[2], &valid);
    if (!valid)
    {
        err("action must be p, r or h");
        return;
    }
    hook((uint8_t)atoi(argv[1]), action);
    ok();
}

void cmdJog(int argc, char** argv)
{
    if (argc < 2)
    {
        err("usage: jog <delta>");
        return;
    }
    onPotChange(0, (int16_t)atoi(argv[1]));
    ok();
}

void cmdMon(int argc, char** argv)
{
    monIntervalMs = (argc >= 2) ? (uint32_t)atoi(argv[1]) : 0;
    monLastMs = nowMs();
    ok();
}

void cmdExists(int argc, char** argv)
{
    if (argc < 2)
    {
        err("usage: exists <path>");
        return;
    }
    FileSystem* fs = platform->getFileSystem();
    if (fs->exists(argv[1]))
    {
        sendf("exists %s size=%u\n", argv[1], (unsigned)fs->getFileSize(argv[1]));
    }
    else
    {
        sendf("missing %s\n", argv[1]);
    }
    ok();
}

void cmdRm(int argc, char** argv)
{
    if (argc < 2)
    {
        err("usage: rm <path>");
        return;
    }
    if (platform->getFileSystem()->remove(argv[1]))
        ok();
    else
        err("remove failed");
}

void abortUpload(const char* why)
{
    FileSystem* fs = platform->getFileSystem();
    if (upload.file != nullptr)
    {
        fs->closeFile(upload.file);
        upload.file = nullptr;
    }
    fs->remove(upload.path);
    upload.active = false;
    err(why);
}

void cmdPut(int argc, char** argv)
{
    if (argc < 3)
    {
        err("usage: put <path> <size>");
        return;
    }
    if (upload.active)
    {
        err("upload in progress");
        return;
    }
    long size = atol(argv[2]);
    if (size <= 0)
    {
        err("size");
        return;
    }
    if (strlen(argv[1]) >= sizeof(upload.path))
    {
        err("path too long");
        return;
    }

    FileSystem* fs = platform->getFileSystem();
    if (!fs->isSdCardInserted())
    {
        err("no sd card");
        return;
    }
    strcpy(upload.path, argv[1]);
    if (fs->exists(upload.path))
    {
        fs->remove(upload.path);
    }
    auto opened = fs->openFile(upload.path, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!std::get<0>(opened))
    {
        err("open failed");
        return;
    }
    upload.file = std::get<1>(opened);
    upload.total = (uint32_t)size;
    upload.remaining = (uint32_t)size;
    upload.crc = 0xFFFFFFFFu;
    upload.lastRxMs = nowMs();
    upload.active = true;
    send("go\n");
}

void serviceUpload()
{
    if (upload.remaining > 0)
    {
        size_t want = upload.remaining < kUploadChunk ? upload.remaining : kUploadChunk;
        int n = port->read(chunk, want);
        if (n > 0)
        {
            size_t written = platform->getFileSystem()->writeFile(chunk, (size_t)n, upload.file);
            if (written != (size_t)n)
            {
                abortUpload("write failed");
                return;
            }
            upload.crc = crc32Update(upload.crc, chunk, (size_t)n);
            upload.remaining -= (uint32_t)n;
            upload.lastRxMs = nowMs();
        }
        else if (nowMs() - upload.lastRxMs > kUploadTimeoutMs)
        {
            abortUpload("timeout");
            return;
        }
    }

    if (upload.remaining == 0)
    {
        platform->getFileSystem()->closeFile(upload.file);
        upload.file = nullptr;
        upload.active = false;
        sendf("done %s size=%lu crc=%08lx\n",
              upload.path,
              (unsigned long)upload.total,
              (unsigned long)(upload.crc ^ 0xFFFFFFFFu));
        ok();
    }
}

void cmdFlash(int argc, char** argv)
{
    if (argc < 2)
    {
        err("usage: flash <file in /Firmware/>");
        return;
    }
    static char path[128];
    snprintf(path, sizeof(path), "/Firmware/%s", argv[1]);
    if (!platform->getFileSystem()->exists(path))
    {
        err("file not found in /Firmware/");
        return;
    }
    if (strlen(argv[1]) > 31)
    {
        err("filename longer than 31 chars");
        return;
    }
    send("flashing, device will reboot\n");
    ok();
    platform->getTimer()->delay(200); // let the reply leave the USB buffer
    platform->runBootloader(argv[1]);  // does not return
}

void cmdReboot()
{
    ok();
    platform->getTimer()->delay(200);
    platform->resetMCU();
}

void cmdHelp()
{
    send("commands: ping stat mon <ms> tracks midi <ch> <note> <vel> [j] key <idx> <p|r|h> "
         "pad <0-47> <p|r|h> jog <delta> exists <path> rm <path> put <path> <size> "
         "flash <file> reboot help\n");
    ok();
}

void execute(char* text)
{
    char* argv[6];
    int argc = 0;
    char* save = nullptr;
    for (char* tok = strtok_r(text, " ", &save); tok && argc < 6; tok = strtok_r(nullptr, " ", &save))
    {
        argv[argc++] = tok;
    }
    if (argc == 0)
        return;

    const char* c = argv[0];
    if (!strcmp(c, "ping")) cmdPing();
    else if (!strcmp(c, "stat")) cmdStat();
    else if (!strcmp(c, "tracks")) cmdTracks();
    else if (!strcmp(c, "midi")) cmdMidi(argc, argv);
    else if (!strcmp(c, "key")) cmdKey(argc, argv, false);
    else if (!strcmp(c, "pad")) cmdKey(argc, argv, true);
    else if (!strcmp(c, "jog")) cmdJog(argc, argv);
    else if (!strcmp(c, "mon")) cmdMon(argc, argv);
    else if (!strcmp(c, "exists")) cmdExists(argc, argv);
    else if (!strcmp(c, "rm")) cmdRm(argc, argv);
    else if (!strcmp(c, "put")) cmdPut(argc, argv);
    else if (!strcmp(c, "flash")) cmdFlash(argc, argv);
    else if (!strcmp(c, "reboot")) cmdReboot();
    else if (!strcmp(c, "help")) cmdHelp();
    else err("unknown command, try help");
}
} // namespace

void devMonitorInit(DevSerialPort* p, Platform* pl, const DevMonitorHooks& h)
{
    port = p;
    platform = pl;
    hooks = h;
}

void devMonitorUpdate()
{
    if (port == nullptr || platform == nullptr)
        return;

    if (upload.active)
    {
        serviceUpload();
        return;
    }

    // Read whatever is pending, execute complete lines.
    uint8_t buf[64];
    int n;
    while ((n = port->read(buf, sizeof(buf))) > 0)
    {
        for (int i = 0; i < n; i++)
        {
            char ch = (char)buf[i];
            if (ch == '\r')
                continue;
            if (ch == '\n')
            {
                line[lineLen] = '\0';
                lineLen = 0;
                execute(line);
                if (upload.active)
                    return; // remaining bytes belong to the upload stream
                continue;
            }
            if (lineLen < kLineMax - 1)
                line[lineLen++] = ch;
            else
                lineLen = 0; // overflow: drop the line
        }
    }

    if (monIntervalMs > 0 && port->connected() && (nowMs() - monLastMs) >= monIntervalMs)
    {
        monLastMs = nowMs();
        sendStat("mon");
    }
}
