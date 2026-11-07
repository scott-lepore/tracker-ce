# Braids voices (proof of concept)

Mutable Instruments' Braids macro oscillator as a voice on the Original Tracker and its
Linux port. Branch `braids-poc`. Code: `core/mods/braidsosc.*`, Braids itself in
`core/libs/mutable/` (MIT, Emilie Gillet).

## Using it

1. Config > Project Settings > Mods: turn **Braids** on.
2. Config > Project Settings > Braids: pick the **Instrument** slot (1 to 48), the
   **Model** (all 48 Braids models) and the **Timbre** and **Color** knobs (0 to 127).
3. Play that instrument from the pattern, the pads or MIDI.

The slot plays Braids instead of its sample, with or without a sample loaded. Everything
else stays the Tracker's: volume, amp envelope, filter, panning, tune, fine tune, glide,
the track's FX and sends. Every track can play it at once: 8 voices, each with its own
oscillator. The settings are saved with the project (`mods.bin`).

Tracker note 60 (shown as C5) is MIDI middle C, 261.6 Hz.

## How it is built

- Braids runs at the Tracker's own rate. Its nine lookup tables that depend on the sample
  rate (oscillator increments and delays, resonator, SVF, bowing and blowing envelopes)
  were regenerated for 44117.647 Hz. The generator reproduces Mutable's shipped 96 kHz
  tables exactly before producing these. Tuning measures 440.00 Hz for A4.
- A voice renders 16 samples at a time into the slot's `AudioPlayMemory` block, which
  then goes through the voice's envelope, filter and mixer as a sample would.
- Each oscillator (596 bytes) is in internal RAM; its delay lines (16 KB, used by the
  physical models) are in external RAM, held through a pointer added to Braids'
  `DigitalOscillator`. Internal RAM made a voice a third to a half cheaper than having it
  all in external RAM.
- **Load guard:** when an audio block takes 90 % or more of its time, the most recently
  started Braids voice stops, one per block, until the load is back under. Without it,
  8 voices of a heavy model locked the unit up.

## Cost on the Original

| | Before | With Braids |
|---|---|---|
| Flash | 918,816 B (87.6 %) | 1,003,060 B (95.7 %) |
| Internal RAM (static) | 139,812 B | 145,704 B |
| External RAM | 15,606,432 B | 15,737,504 B |

To make room, the Original build (flag `MT_TRIM_ORIGINAL`, also the Linux port) no longer
has the factory test mode or .it/.mod import and .it export (24 KB), and uses a plain C++
terminate handler instead of the verbose one (32 KB). Flash is then 946,824 B (90.3 %).

Audio load, measured on the device (`prof reset`, then `stat` over 1.2 s per step), all
tracks stopped: 46 % idle. Each Braids voice adds 1.5 % (Clocked Noise) to 8 % (Triple
Square). A sample voice adds about 0.3 %. Voices that fit under 80 % load:

| Voices | Models |
|---|---|
| 8 | 27 models, among them CSAW, Buzz, Saw Sync, the FM models, Vosim, Vowel, Toy, Bowed, Blown, Kick, Wavetables, Granular and all noise models |
| 7 | Sine/Triangle, Saw Swarm, ZLPF, ZPKF |
| 6 | Morph, Saw/Square, Square Sub, Saw Sub, Square Sync, Triple Saw, Vowel FOF, Bell, Drum, Snare, Wave Map, Wave Paraphonic |
| 5 | Harmonics, Plucked, Fluted, Cymbal |
| 4 | Triple Square |

## Known limits of the proof of concept

- One Braids slot per project, with one model and knob setting.
- No Braids screen in the instrument editor; no per-step control of Timbre and Color.
- Some models' fixed internal timings (drum pulse delays, wavetable band limits) keep
  their 96 kHz design, so they sound slightly different from the module.
