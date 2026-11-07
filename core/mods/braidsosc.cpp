// SPDX-License-Identifier: MIT
#include "braidsosc.h"

#if MT_HAS_BRAIDS

#include "braids/macro_oscillator.h"

#include "Profiler.h"

#include <cmath>
#include <cr_section_macros.h>
#include <cstring>

namespace mods
{
namespace braidsosc
{

Settings settings;

const char* const kModelNames[kModelCount] = {
    "CSAW",          "MORPH",          "SAW/SQUARE",   "SINE/TRIANGLE",  "BUZZ",
    "SQUARE SUB",    "SAW SUB",        "SQUARE SYNC",  "SAW SYNC",       "TRIPLE SAW",
    "TRIPLE SQUARE", "TRIPLE TRI",     "TRIPLE SINE",  "TRIPLE RING",    "SAW SWARM",
    "SAW COMB",      "TOY",            "ZLPF",         "ZPKF",           "ZBPF",
    "ZHPF",          "VOSIM",          "VOWEL",        "VOWEL FOF",      "HARMONICS",
    "FM",            "FEEDBACK FM",    "CHAOTIC FM",   "PLUCKED",        "BOWED",
    "BLOWN",         "FLUTED",         "BELL",         "DRUM",           "KICK",
    "CYMBAL",        "SNARE",          "WAVETABLES",   "WAVE MAP",       "WAVE LINE",
    "WAVE PARAPHON", "FILTERED NOISE", "TWIN PEAKS",   "CLOCKED NOISE",  "GRANULAR",
    "PARTICLES",     "DIGITAL MOD",    "QUESTION MARK",
};
static_assert(kModelCount == ::braids::MACRO_OSC_SHAPE_LAST);

namespace
{

constexpr size_t kRecordSize = 4;
static_assert(sizeof(Settings) == kRecordSize);
constexpr size_t kChunk = 16; // Braids renders at most 24 samples, an even number, at a time

// The oscillators (about 600 bytes each) in internal RAM, which roughly halves their cost;
// their delay lines (16 KB each, used by the physical models) in external RAM.
::braids::MacroOscillator oscillators[kVoiceCount];
__NOINIT(EXTERNAL_RAM)::braids::DigitalOscillatorDelayLines delayLines[kVoiceCount];

::braids::MacroOscillator& oscillatorOf(int v)
{
    return oscillators[v];
}

// Load guard state: when each voice started and the last block it rendered in.
uint32_t startOrder[kVoiceCount];
uint32_t lastBlock[kVoiceCount];
uint32_t starts;

// Whether voice should stop now: the last block overran the guard and this is the most
// recently started of the voices still sounding.
bool guardStops(int8_t voice, uint32_t block)
{
    if (Profiler::getInstance()->lastAudioLoadPercent() < kGuardPercent)
    {
        return false;
    }
    int youngest = -1;
    for (int v = 0; v < kVoiceCount; v++)
    {
        if (block - lastBlock[v] <= 1 && (youngest < 0 || startOrder[v] > startOrder[youngest]))
        {
            youngest = v;
        }
    }
    return youngest == voice;
}

bool valid(const Settings& s)
{
    return s.instrument <= 48 && s.model < kModelCount && s.timbre <= kParameterMax &&
           s.color <= kParameterMax;
}

Settings fromRecord(const Store& store)
{
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, nullptr, &length);
    Settings s;
    if (data != nullptr && length == kRecordSize)
    {
        Settings stored;
        memcpy(&stored, data, kRecordSize);
        if (valid(stored))
        {
            s = stored;
        }
    }
    return s;
}

int16_t parameter(uint8_t value)
{
    return static_cast<int16_t>(value * 32767 / kParameterMax);
}

} // namespace

void load(const Store& store)
{
    settings = fromRecord(store);
}

bool storeIfEdited(Store& store)
{
    const Settings stored = fromRecord(store);
    if (memcmp(&stored, &settings, kRecordSize) == 0)
    {
        return false;
    }
    Settings copy = settings;
    return store.put(kRecord, kRecordVersion, &copy, kRecordSize);
}

void init()
{
    for (int v = 0; v < kVoiceCount; v++)
    {
        ::braids::MacroOscillator& osc = oscillatorOf(v);
        osc.set_delay_lines(&delayLines[v]);
        osc.Init();
        osc.set_shape(::braids::MACRO_OSC_SHAPE_CSAW);
        osc.set_parameters(parameter(64), parameter(64));
        osc.set_pitch(60 << 7);
    }
}

bool plays(uint8_t instrIdx)
{
    return on(Mod::braids) && settings.instrument != 0 && settings.instrument == instrIdx + 1;
}

void noteOn(int8_t voice)
{
    if (voice < 0 || voice >= kVoiceCount)
    {
        return;
    }
    oscillatorOf(voice).Strike();
    startOrder[voice] = ++starts;
    lastBlock[voice] = Profiler::getInstance()->audioBlockCount(); // sounding from now on
}

bool render(int8_t voice, float pitchRatio, int16_t* out, size_t size)
{
    if (voice < 0 || voice >= kVoiceCount || pitchRatio <= 0.0f)
    {
        memset(out, 0, size * sizeof(int16_t));
        return true;
    }

    const uint32_t block = Profiler::getInstance()->audioBlockCount();
    lastBlock[voice] = block;
    if (guardStops(voice, block))
    {
        lastBlock[voice] = block - 2; // no longer sounding
        return false;
    }

    ::braids::MacroOscillator& osc = oscillatorOf(voice);
    osc.set_shape(static_cast<::braids::MacroOscillatorShape>(
      settings.model < kModelCount ? settings.model : 0));
    osc.set_parameters(parameter(settings.timbre), parameter(settings.color));

    // Braids pitch: MIDI note in 1/128 semitone; 60 << 7 is middle C.
    int32_t pitch = (60 << 7) + static_cast<int32_t>(lrintf(1536.0f * log2f(pitchRatio)));
    if (pitch < 0)
    {
        pitch = 0;
    }
    else if (pitch > (127 << 7))
    {
        pitch = 127 << 7;
    }
    osc.set_pitch(static_cast<int16_t>(pitch));

    static const uint8_t noSync[kChunk] = {};
    for (size_t i = 0; i + kChunk <= size; i += kChunk)
    {
        osc.Render(noSync, out + i, kChunk);
    }
    return true;
}

} // namespace braidsosc
} // namespace mods

#endif // MT_HAS_BRAIDS
