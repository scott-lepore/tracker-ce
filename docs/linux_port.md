# Linux host port (headless)

`platforms/linux` builds the Original Tracker firmware core as a native Linux
program. Stage 1 is headless: no screen, no sound card, no real MIDI ports.
What runs is the complete application (sequencer, audio engine, file manager,
UI state machine, config) against a directory that stands in for the SD card,
driven over the same developer monitor protocol as the hardware
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

## Run

    build/tracker-linux --sd ~/TrackerSD

Options: `--port N` (dev monitor port, default 9170), `--state DIR`
(where the emulated EEPROM lives, default `~/.local/share/tracker-linux`),
`--no-pacing` (audio engine runs as fast as the CPU allows; for tests),
`--quit-after SEC`.

`~/TrackerSD` should be a copy of the card (`rsync -a /media/.../32GB/
~/TrackerSD/`). Projects, instruments and samples saved by the port can be
copied back to the card unchanged.

Then from another terminal:

    tools/trackerdev.py --linux ping
    tools/trackerdev.py --linux run "key play t; sleep 2; tracks; key play t"

## What is emulated how

| Device part | Host stand-in |
|---|---|
| Audio DMA interrupts | `AudioThread`: one thread calling the core every 128 samples, paced to 44117.6 Hz, output discarded (sink hook ready for ALSA/JACK in stage 2) |
| `CPSID i` critical sections | recursive mutex shared with the audio thread (`linuxIrqLock`) |
| `NVIC_DisableIRQ(audio)` | flag + wait for the in-flight block |
| FT812 display | `NullDisplayDriver`: swallows command lists, co-processor always idle |
| TCA8418 key matrix / pads | `SwitchGroupImpl`: same hold-repeat timing, fed by `key`/`pad` dev commands |
| SD card (FatFs) | `FileSystemImpl`: POSIX over a directory; listings sorted case-insensitively; "hidden" = dot-files and `System Volume Information` |
| FatFs fast-seek + raw sector DMA (sample streaming) | fake contiguous cluster chain per open file; `readSectorsAsync` is a synchronous `pread` |
| EEPROM | 4 KiB `eeprom.bin` |
| USB serial dev monitor | TCP 127.0.0.1:9170 |
| MIDI in/out, radio, battery, LEDs, codec, USB audio | stubs (MIDI sends logged with `TRACKER_MIDI_LOG=1`) |
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

## Roadmap

1. headless core + scripted control (this document)
2. real audio output (ALSA/JACK sink on `AudioThread`), ALSA sequencer MIDI
3. FT812 display-list renderer and keyboard/MIDI-controller input mapping
