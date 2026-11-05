# Linux host port (headless)

`platforms/linux` builds the Original Tracker firmware core as a native Linux
program: the complete application (sequencer, audio engine, file manager, UI,
config) runs against a directory that stands in for the SD card, plays through
ALSA/PipeWire, exposes its two MIDI ports on the ALSA sequencer, shows the
800x480 panel in a window through a software FT812, takes keyboard input, and
is driven over the same developer monitor protocol as the hardware
(`docs/dev_serial_monitor.md`), on TCP instead of USB serial.

Purpose: regression-test core changes without flashing, script scenarios
(load project, play, inject MIDI, read back track state) and diff the results
against the device. It does **not** model CPU load, RAM or timing of the
MK66; take performance numbers from the device.

## Build

The port is compiled **32-bit** (`-m32 -fshort-enums -funsigned-char
-malign-double`) so that every struct written to disk (`strInstrument`,
patterns, project values, config) has exactly the ARM layout. The core even
asserts `sizeof(strInstrument) == 372`, which a 64-bit build cannot satisfy.

    sudo apt install gcc-multilib g++-multilib   # once
    make linux                                   # -> build/tracker-linux

`make linux M32=0` compiles 64-bit for quick syntax checks only; it fails the
layout asserts by design.

### Prebuilt binary

The release binary is 32-bit and was built on Ubuntu 24.04, so it needs glibc
2.39 or newer and the 32-bit runtime libraries:

    sudo dpkg --add-architecture i386
    sudo apt update
    sudo apt install libc6:i386 libstdc++6:i386 libx11-6:i386 \
                     libasound2t64:i386 libasound2-plugins:i386

On Debian 12 and other older releases the ALSA package is `libasound2:i386`;
build from source there if glibc is older. `libasound2-plugins:i386` provides
the PulseAudio plug-in that carries sound to PipeWire or PulseAudio.

`--sd` must point at a copy of a Tracker card (the Workspace, Projects and
Samples folders). The port writes to it, so use a copy, not the card itself.

## Run

    build/tracker-linux --sd ~/TrackerSD

Options: `--audio alsa[:DEV]` (default `alsa:default`, i.e. PipeWire through
the pulse plug-in; `--audio none` discards audio), `--latency MS` (ALSA
buffer, default 20), `--midi alsa|none` (sequencer client "Tracker CE" with
ports "USB MIDI" and "MIDI Jack"; connect a controller or a DAW with
`aconnect`/qpwgraph), `--port N` (dev monitor port, default 9170),
`--state DIR` (where the emulated EEPROM lives, default
`~/.local/share/tracker-linux`), `--no-pacing` (with `--audio none`: audio
engine runs as fast as the CPU allows; for tests), `--quit-after SEC`.

`--audio-in alsa[:DEV]|none` (default none) captures from ALSA/PipeWire so
the Sample Recorder and input monitoring work; latency 50 ms.

### Using the Tracker itself as the controller

With the Tracker on USB and `tracker-linux` running:

    tools/trackerdev.py bridge

puts the device in *controller mode*: its pads, buttons and jog wheel stop
acting locally and are streamed to the port instead (`controller 1` and the
`ev` lines in `docs/dev_serial_monitor.md`). Holds are regenerated on the
port with the device's timing. `bridge mirror` keeps the Tracker working
normally while forwarding. Ctrl-C restores the device; it also restores itself
if the USB connection drops. A MIDI controller can drive the pads too, through
the ALSA sequencer port "Tracker CE: Pads" (note `--pads-base`, default 36, is
pad 1 on the bottom row).

`--display x11|none` (default x11 when `$DISPLAY` is set) opens the panel
window; `--scale 2` starts it doubled. The window is resizable: the 800x480
frame is scaled to fit (the UI is bitmap text on a fixed pixel grid, not
vector, so it scales rather than re-flows; integer multiples stay crispest). With `--display none` the screen is still
rendered, so `trackerdev.py --linux shot out.png` works headless.

### Keyboard

| Keys | Tracker control |
|---|---|
| `1`..`=`, `q`..`]`, `a`..`\`, `z`..`/` | the 48 pads, four rows of 12, top row first (bottom row has 10 keys plus `<` on ISO layouts) |
| F1..F8 | the eight screen buttons under the display |
| Space / Enter / Shift | Play / Rec / Shift |
| arrows, Insert, Delete or Backspace, Home | arrows, Insert, Delete, Copy |
| Ctrl+P E F G M | Params, Perform, File, Config, Master |
| Ctrl+L Y D R | Sample Loader, Sample Playback, Sample Editor, Sample Recorder |
| Ctrl+S N I V X T | Song, Note, Instrument, Vol/FX2, FX/FX1, Pattern |
| mouse wheel, PageUp/PageDown | jog wheel |

Holding a key repeats like holding the physical button (same hold timing).

`--capture FILE` additionally writes what the sink receives as raw s16le
stereo at 44118 Hz (`sox -r 44118 -e signed -b 16 -c 2 FILE out.wav` to
convert), with `--audio none` as a silent, clock-paced renderer for tests.

Output level follows the Tracker's own master volume (applied as a linear
gain, where the SGTL5000 codec uses dB steps, so absolute level differs
slightly from the device) and the startup mute,
so the port is silent until the core releases the mute exactly as the
device is. Sample rate is requested as 44118 Hz (nearest integer to the
Original's 44117.647 Hz) and resampled by ALSA/PipeWire, so pitch matches.

`~/TrackerSD` should be a copy of the card (`rsync -a /media/.../32GB/
~/TrackerSD/`). Projects, instruments and samples saved by the port can be
copied back to the card unchanged.

Then from another terminal:

    tools/trackerdev.py --linux ping
    tools/trackerdev.py --linux run "key play t; sleep 2; tracks; key play t"

## What is emulated how

| Device part | Host stand-in |
|---|---|
| Audio DMA interrupts | `AudioThread`: one thread calling the core every 128 samples; paced by blocking ALSA writes (`AlsaSink`), or by the steady clock with `--audio none` |
| `CPSID i` critical sections | recursive mutex shared with the audio thread (`linuxIrqLock`) |
| `NVIC_DisableIRQ(audio)` | flag + wait for the in-flight block |
| FT812 display | `eve/EveEmu`: RAM_G, RAM_DL, co-processor FIFO; expands CMD_TEXT/NUMBER/GRADIENT/APPEND/MEMCPY/SETFONT2/SETBITMAP/matrix commands into display-list words like the chip, then rasterizes RECTS/LINES/LINE_STRIP/POINTS/BITMAPS (all bitmap formats, transforms, scissor, blend) with coverage anti-aliasing. `CMD_LOADIMAGE` decodes JPEG/PNG to RGB565 via the vendored public-domain `stb_image` (Config > Info > Reference/Notes/Custom image viewer, `/Images/<name>/N.jpg`). Firmware fonts are the chip's own L4 blocks, so text is pixel-exact |
| TCA8418 key matrix / pads | `SwitchGroupImpl`: same hold-repeat timing, fed by `key`/`pad` dev commands |
| SD card (FatFs) | `FileSystemImpl`: POSIX over a directory; listings sorted case-insensitively; "hidden" = dot-files and `System Volume Information` |
| FatFs fast-seek + raw sector DMA (sample streaming) | fake contiguous cluster chain per open file; `readSectorsAsync` is a synchronous `pread` |
| EEPROM | 4 KiB `eeprom.bin` |
| USB serial dev monitor | TCP 127.0.0.1:9170, plus `shot <path>` to dump the panel |
| USB MIDI and DIN jack | `AlsaSeq`: ALSA sequencer client with one duplex port per device port (plus a host-only "Pads" port); note-on velocity 0 delivered as note-off like the device libraries |
| Audio input (line/mic) | `AlsaSource`: non-blocking ALSA capture, silence when nothing has arrived |
| Radio, battery, LEDs, codec, USB audio | stubs (codec master volume is applied in the sink) |
| Cortex-M4 DSP intrinsics | `core/libs/Audio/utility/dspinst_portable.h`, bit-exact C |
| `.ptfx` effect plug-ins | cannot run (ARM Thumb code); loader is a no-op |

Core changes are all under `#ifdef PLATFORM_LINUX` (`core/config/config.h`,
`cr_section_macros.h`, `dspinst.h`, `Polyverb/util.h`, `ptfx_loader.cpp`,
`Tracker.cpp`), plus two portability fixes that are correct on every target
(`FileBrowser.cpp` missing `assert` include, `textBox.cpp` const `strchr`).

## Known differences

- Directory order is sorted, not FAT physical order, so lists that the
  firmware does not sort itself may appear in a different order than on the
  card.
- Power off / reset exit the process (status 0 / 2).
- Float contraction (FMA) decisions differ between compilers, so float DSP
  paths may differ in the last bit from the device. Fixed-point paths are exact.

## Fidelity

Font glyphs and layout come straight from the firmware. The rasterizer was
calibrated against a device screenshot of the pattern editor: integer vertex
coordinates address pixel centres, rectangles extend by the line width with
corners rounded by that radius, and CMD_TEXT centres with `x - (w-1)/2`.
Transformed (rotated) bitmaps sample texel `floor(u - 0.5)`. Result over nine
device screens (pattern, song, config, sample editor, sample loader,
instrument parameters and effects, track mixer, on-screen keyboard dialog):
0.1 to 0.6 % of pixels differ by more than 30/255,
half of that being the 1-px header separator, which the chip draws softer
than this rasterizer; nothing structural.

To repeat the comparison: `trackerdev.py eeprom-pull` clones the device
configuration (fonts, layout, MIDI settings) into the port's EEPROM; on the
device hold Shift and tap the power button (the firmware writes
`Snapshots/Snapshot_NNN.bmp` to the card); `trackerdev.py get
/Snapshots/Snapshot_001.bmp` fetches it over USB; `trackerdev.py --linux shot
port.png` grabs the port; diff with PIL.

## Known quirk

Keys sent during the first ~3 s after start are swallowed by the boot
sequence (same as on the hardware); wait before scripting input.

## Roadmap

1. headless core + scripted control: done
2. ALSA audio output and sequencer MIDI: done (no audio input yet)
3. FT812 renderer, window and keyboard: done
4. Image loading, audio input, Tracker-as-controller bridge, MIDI pads: done

Firmware fixes found through the port: after a `Snapshots/` screenshot only
visible controls are redrawn (hidden lists held stale text pointers, which
crashed the port and is the likely cause of the device hanging after repeated
Shift+power); the capture now runs in 192 kB strips; `sqrt_uint32(0)` is
defined on hosts where division by zero traps.
