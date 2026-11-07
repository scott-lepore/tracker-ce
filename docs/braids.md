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
- The 8 oscillators (about 17 KB each) live in external RAM.

## Cost on the Original

| | Before | With Braids |
|---|---|---|
| Flash | 918,816 B (87.6 %) | 1,002,612 B (95.6 %) |
| Internal RAM | 139,812 B | 140,872 B |
| External RAM | 15,606,432 B | 15,742,240 B |

CPU per voice is not measured yet: it has to be read on the device (`trackerdev.py stat`,
audio load per block) with several voices sounding. The port's figures don't predict it.

## Known limits of the proof of concept

- One Braids slot per project, with one model and knob setting.
- No Braids screen in the instrument editor; no per-step control of Timbre and Color.
- Some models' fixed internal timings (drum pulse delays, wavetable band limits) keep
  their 96 kHz design, so they sound slightly different from the module.
