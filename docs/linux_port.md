# Linux host port (headless)

`platforms/linux` builds the Original Tracker firmware core as a native Linux
program. Stages 1 and 2 are done: the complete application (sequencer, audio
engine, file manager, UI state machine, config) runs against a directory that
stands in for the SD card, plays through ALSA/PipeWire, exposes its two MIDI
ports on the ALSA sequencer, and is driven over the same developer monitor
protocol as the hardware (`docs/dev_serial_monitor.md`), on TCP instead of
USB serial. There is no screen yet (stage 3).

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
| FT812 display | `NullDisplayDriver`: swallows command lists, co-processor always idle |
| TCA8418 key matrix / pads | `SwitchGroupImpl`: same hold-repeat timing, fed by `key`/`pad` dev commands |
| SD card (FatFs) | `FileSystemImpl`: POSIX over a directory; listings sorted case-insensitively; "hidden" = dot-files and `System Volume Information` |
| FatFs fast-seek + raw sector DMA (sample streaming) | fake contiguous cluster chain per open file; `readSectorsAsync` is a synchronous `pread` |
| EEPROM | 4 KiB `eeprom.bin` |
| USB serial dev monitor | TCP 127.0.0.1:9170 |
| USB MIDI and DIN jack | `AlsaSeq`: ALSA sequencer client with one duplex port per device port; note-on velocity 0 delivered as note-off like the device libraries |
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

## Known quirk

Keys sent during the first ~3 s after start are swallowed by the boot
sequence (same as on the hardware); wait before scripting input.

## Roadmap

1. headless core + scripted control: done
2. ALSA audio output and sequencer MIDI: done (no audio input yet)
3. FT812 display-list renderer and keyboard/MIDI-controller input mapping
