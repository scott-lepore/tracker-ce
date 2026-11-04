# Developer Serial Monitor & USB Firmware Upload (Original Tracker)

The Original Tracker's USB device already exposes a CDC serial interface next to USB MIDI.
Community Edition builds from this branch run a small line-oriented command console on it,
polled from the main loop. It lets you inspect state, inject MIDI and key events, upload
files to the SD card and trigger a firmware update without touching the card.

Host tool: [`tools/trackerdev.py`](../tools/trackerdev.py) (needs `pyserial`). The same protocol serves the
headless Linux build over TCP (`tools/trackerdev.py --linux`, see [linux_port.md](linux_port.md)).
Linux setup: install [`tools/70-polyend-tracker.rules`](../tools/70-polyend-tracker.rules)
so the port is user-accessible, ignored by ModemManager and available as `/dev/tracker`.

## Commands

| Command | Reply | Notes |
|---|---|---|
| `ping` | `pong tracker-ce <ver> build <hash> product <original/plus/mini>` | |
| `stat` | `stat t= play= rec= bpm= pat= pos= notesin= voicemode= cpu=min/avg/max/last ram= stack=` | cpu = audio load % per block (Profiler), ram = free heap bytes, stack = peak main stack |
| `mon <ms>` | periodic `mon ...` lines | `mon 0` stops. Only emitted while the host holds DTR |
| `tracks` | 8 x `track n ch= pos= open= inst= note= last= held= mute=` | `ch` = per-track MIDI-in channel, `last` = last instrument on the track, `held` = note held by MIDI in |
| `midi <ch> <note> <vel> [j]` | | Injects into the USB (or `j` = jack) note-in path exactly like a real message. vel 0 = note off |
| `key <idx> <p\|r\|h>` | | Button index (see `tools/trackerdev.py` for names), press / release / hold |
| `pad <0-47> <p\|r\|h>` | | Grid pad |
| `jog <delta>` | | Jog wheel steps |
| `exists <path>` / `rm <path>` | | SD card paths, e.g. `/Firmware/x.ptf` |
| `put <path> <size>` | `go`, then `done <path> size= crc=` | Raw bytes follow; serviced incrementally so the UI stays alive. 5 s inactivity aborts |
| `get <path>` | `data <size>` then raw bytes | Download a file from the card (UI pauses while it streams) |
| `eeprom <offset> <len>` | `<offset> <hex>` lines | Dump configuration memory; `trackerdev.py eeprom-pull` clones it into the Linux port |
| `shot <path>` | | Linux build only: save the emulated screen as PPM (`trackerdev.py shot x.png` converts) |
| `flash <file>` | `flashing, device will reboot` | File must exist in `/Firmware/`, name 31 chars or fewer. Calls the normal bootloader path |
| `cfg notesin <0-3>` / `cfg trackch <track> <ch>` / `cfg save` | | Sets Config > MIDI "Notes in" (0 off, 1 USB, 2 jack, 3 both) and per-track in channels in RAM, as the menu would; `save` persists to EEPROM. For scripted tests |
| `reboot` | | |

Every command ends with `ok` or `err <reason>`.

## Typical loop

```bash
make original -j8
tools/trackerdev.py flash build/PolyendTracker_CE_1.0.0.ptf   # upload, reboot, wait, ping
tools/trackerdev.py run "key config t; stat; tracks"
tools/trackerdev.py midi 3 60 100 ; tools/trackerdev.py tracks ; tools/trackerdev.py midi 3 60 0
tools/trackerdev.py mon 1000
```

## Rules

* Never print from the audio interrupt. The monitor runs only from the main loop.
* Uploads share the main loop with the UI and SD sample streaming; expect streamed
  samples to stutter while a file is transferring.
* If a build hangs before the main loop runs, the monitor is gone with it. Recovery is the
  normal one: known-good `.ptf` in `/Firmware/`, pinhole reset.
