#pragma once
// Everything the sound of Refren depends on. Shares are 0 to 1, tilt is -1 to 1.
#include "CharacterShaper.h"

namespace refren {

using wers::Character;

enum class Source : int { Voice = 0, Whisper = 1, Sub = 2, Robot = 3 };
enum class Motion : int { None = 0, Autopan = 1, Tremolo = 2, Flanger = 3 };
enum class EchoTime : int { Sixteenth = 0, Eighth = 1, DottedEighth = 2, Quarter = 3, DottedQuarter = 4, Half = 5, Slap = 6 };

struct LayerParams {
    bool on = false;
    Source source = Source::Voice;
    int voices = 2;
    int semis = 0;
    float formant = 0.0f;
    float levelDb = -6.0f;
    float width = 0.5f;
    float timeMs = 12.0f;
    float detuneCents = 8.0f;
    float lowHz = 20.0f;
    float highHz = 20000.0f;
    int note = 48; // MIDI, from 36 to 72
};

struct RefrenParams {
    RefrenParams() { layer[0].on = true; }
    float amount = 1.0f, widthScale = 1.0f, timeScale = 1.0f, tilt = 0.0f;
    float grainDb = 0.0f;
    Character grainChar = Character::Lamp;
    Motion motion = Motion::None;
    float motionRate = 1.0f, motionDepth = 0.0f;
    float echoMix = 0.0f;
    EchoTime echoTime = EchoTime::Eighth;
    float echoFeedback = 0.25f;
    bool echoPing = false;
    float echoToneHz = 6000.0f;
    float verbMix = 0.0f, verbSize = 0.5f, verbDecay = 1.5f, verbToneHz = 6000.0f, verbPreMs = 10.0f, verbShimmer = 0.0f;
    float duck = 0.5f, sendDry = 0.0f, consonants = 1.0f;
    float dryDb = 0.0f, trimDb = 0.0f, outputDb = 0.0f;
    bool delta = false;
    std::array<LayerParams, 4> layer{};
};

} // namespace refren
