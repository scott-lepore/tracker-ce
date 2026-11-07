// Braids: Mutable Instruments' macro oscillator as a voice. SPDX-License-Identifier: MIT
//
// Proof of concept: one instrument slot of the project (Project Settings -> Braids) plays
// the Braids oscillator instead of its sample. Everything else about the instrument stays
// CE's: volume, amp envelope, filter, panning, tune, fine tune, glide and the track's FX.
// The slot plays with or without a sample loaded.

#ifndef MODS_BRAIDSOSC_H_
#define MODS_BRAIDSOSC_H_

#include "mods.h"

#if MT_HAS_BRAIDS

#include "modsStore.h"

#include <stddef.h>
#include <stdint.h>

namespace mods
{
namespace braidsosc
{

constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::braids) + 1;
constexpr uint8_t kRecordVersion = 1;
constexpr uint8_t kModelCount = 48;
constexpr uint8_t kParameterMax = 127;
constexpr uint8_t kVoiceCount = 8;

struct Settings
{
    uint8_t instrument = 0; // 0 off, 1 to 48 the slot
    uint8_t model = 0;      // MacroOscillatorShape
    uint8_t timbre = 64;    // 0 to 127
    uint8_t color = 64;     // 0 to 127
};

extern Settings settings; // the open project's
extern const char* const kModelNames[kModelCount];

void load(const Store& store);
bool storeIfEdited(Store& store);

// Boot: the oscillators start in a known state.
void init();

// Whether instrument slot instrIdx (0-based) plays Braids in the open project.
bool plays(uint8_t instrIdx);

// Audio engine side, called with audio interrupts off or from the audio interrupt.
// pitchRatio is the playback ratio relative to note 60 (C5 on the Tracker), with
// tune, fine tune and glide applied and without the clock correction.
// render returns false when the voice must stop instead (load guard, below).
void noteOn(int8_t voice);
bool render(int8_t voice, float pitchRatio, int16_t* out, size_t size);

// Load guard: when the last audio block took kGuardPercent or more of its time, the most
// recently started Braids voice stops, so too many voices can't lock the unit up.
constexpr int kGuardPercent = 90;

} // namespace braidsosc
} // namespace mods

#endif // MT_HAS_BRAIDS

#endif /* MODS_BRAIDSOSC_H_ */
