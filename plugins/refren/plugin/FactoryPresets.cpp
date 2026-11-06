#include "FactoryPresets.h"
#include "Params.h"
#include "RefrenParams.h"
#include <cstring>

namespace refren {

namespace {

// Written out, because a preset keeps pointers to these texts for as long as the program runs.
struct LayerIds {
    const char* on, *source, *voices, *semis, *formant, *level, *width, *time, *detune, *low, *high, *note;
};
constexpr std::array<LayerIds, 4> kLayerIds{ {
    { "l1On", "l1Source", "l1Voices", "l1Semis", "l1Formant", "l1Level", "l1Width", "l1Time", "l1Detune", "l1Low", "l1High", "l1Note" },
    { "l2On", "l2Source", "l2Voices", "l2Semis", "l2Formant", "l2Level", "l2Width", "l2Time", "l2Detune", "l2Low", "l2High", "l2Note" },
    { "l3On", "l3Source", "l3Voices", "l3Semis", "l3Formant", "l3Level", "l3Width", "l3Time", "l3Detune", "l3Low", "l3High", "l3Note" },
    { "l4On", "l4Source", "l4Voices", "l4Semis", "l4Formant", "l4Level", "l4Width", "l4Time", "l4Detune", "l4Low", "l4High", "l4Note" },
} };

// What a preset does not say takes the default of the parameter.
struct Voices {
    Source source = Source::Voice;
    int count = 2, semis = 0;
    float formant = 0.0f, time = 12.0f, width = 50.0f, detune = 8.0f, low = 20.0f, high = 20000.0f, level = -6.0f;
    int note = 48;
};

struct Bus {
    float verb = 0.0f, decay = 1.5f, size = 50.0f, verbTone = 6000.0f, pre = 10.0f, shimmer = 0.0f;
    float echo = 0.0f;
    EchoTime echoTime = EchoTime::Eighth;
    float feedback = 25.0f;
    bool ping = false;
    float echoTone = 6000.0f;
    float grain = 0.0f;
    Character character = Character::Lamp;
    Motion motion = Motion::None;
    float rate = 1.0f, depth = 0.0f;
    float tilt = 0.0f, duck = 50.0f, send = 0.0f, consonants = 100.0f, dry = 0.0f, width = 100.0f, time = 100.0f;
};

using Values = std::vector<std::pair<const char*, float>>;

void add(Values& values, const char* id, float value, float initial) {
    if (std::abs(value - initial) > 1.0e-6f) values.emplace_back(id, value);
}

Values valuesOf(const std::vector<Voices>& layers, const Bus& b) {
    Values v;
    for (size_t k = 0; k < kLayerIds.size(); ++k) {
        const auto& id = kLayerIds[k];
        const bool on = k < layers.size();
        add(v, id.on, on ? 1.0f : 0.0f, k == 0 ? 1.0f : 0.0f);
        if (!on) continue;
        const auto& l = layers[k];
        add(v, id.source, static_cast<float>(l.source), 0.0f);
        add(v, id.voices, static_cast<float>(l.count), 2.0f);
        add(v, id.semis, static_cast<float>(l.semis), 0.0f);
        add(v, id.formant, l.formant, 0.0f);
        add(v, id.level, l.level, -6.0f);
        add(v, id.width, l.width, 50.0f);
        add(v, id.time, l.time, 12.0f);
        add(v, id.detune, l.detune, 8.0f);
        add(v, id.low, l.low, 20.0f);
        add(v, id.high, l.high, 20000.0f);
        add(v, id.note, static_cast<float>(l.note), 48.0f);
    }
    add(v, ids::verbMix, b.verb, 0.0f);
    add(v, ids::verbDecay, b.decay, 1.5f);
    add(v, ids::verbSize, b.size, 50.0f);
    add(v, ids::verbTone, b.verbTone, 6000.0f);
    add(v, ids::verbPre, b.pre, 10.0f);
    add(v, ids::verbShimmer, b.shimmer, 0.0f);
    add(v, ids::echoMix, b.echo, 0.0f);
    add(v, ids::echoTime, static_cast<float>(b.echoTime), static_cast<float>(EchoTime::Eighth));
    add(v, ids::echoFeedback, b.feedback, 25.0f);
    add(v, ids::echoPing, b.ping ? 1.0f : 0.0f, 0.0f);
    add(v, ids::echoTone, b.echoTone, 6000.0f);
    add(v, ids::grain, b.grain, 0.0f);
    add(v, ids::grainChar, static_cast<float>(b.character), 0.0f);
    add(v, ids::motion, static_cast<float>(b.motion), 0.0f);
    add(v, ids::motionRate, b.rate, 1.0f);
    add(v, ids::motionDepth, b.depth, 0.0f);
    add(v, ids::tilt, b.tilt, 0.0f);
    add(v, ids::duck, b.duck, 50.0f);
    add(v, ids::sendDry, b.send, 0.0f);
    add(v, ids::consonants, b.consonants, 100.0f);
    add(v, ids::dry, b.dry, 0.0f);
    add(v, ids::width, b.width, 100.0f);
    add(v, ids::time, b.time, 100.0f);
    return v;
}

struct Trim {
    const char* name;
    float db;
};

// What each preset needs to be as loud as the voice that goes in. Written by refren_trim.
constexpr Trim kTrims[] = {
    { "", 0.0f },
#include "PresetTrims.inc"
};

float trimFor(const char* name) {
    for (const auto& t : kTrims)
        if (std::strcmp(t.name, name) == 0) return t.db;
    return 0.0f;
}

class Builder {
public:
    Builder& group(const char* name) {
        group_ = name;
        return *this;
    }
    Builder& add(const char* name, const char* description, std::vector<Voices> layers, Bus bus = {}) {
        auto values = valuesOf(layers, bus);
        refren::add(values, ids::trim, trimFor(name), 0.0f);
        all_.push_back({ name, std::move(values), group_, description });
        return *this;
    }
    std::vector<wers::ui::FactoryPreset> take() { return std::move(all_); }
private:
    const char* group_ = nullptr;
    std::vector<wers::ui::FactoryPreset> all_;
};

} // namespace

std::vector<wers::ui::FactoryPreset> factoryPresets() {
    Builder b;
    b.group("Dubel")
        .add("Ciasny", "Jeden bliski dubel, prawie na samym głosie.", { { .count = 1, .time = 6.0f, .width = 0.0f, .detune = 3.0f, .level = -8.0f } })
        .add("Luźny", "Dwa dublujące głosy, lekko spóźnione i rozstrojone.", { { .time = 14.0f, .detune = 8.0f, .level = -8.0f } })
        .add("Lewo i prawo", "Po jednym dublu na każdą stronę.", { { .time = 10.0f, .width = 100.0f, .detune = 5.0f, .level = -8.0f } })
        .add("Szeroki dubel", "Cztery głosy rozłożone szeroko po bokach.", { { .count = 4, .time = 16.0f, .width = 100.0f, .detune = 6.0f, .level = -8.0f } })
        .add("Za głosem", "Dubel wyraźnie spóźniony, jak drugie nagranie.", { { .time = 28.0f, .width = 40.0f, .detune = 6.0f, .level = -10.0f } })
        .add("Przed i za", "Jeden głos wchodzi przed tobą, drugi po tobie.", { { .time = 40.0f, .width = 70.0f, .detune = 5.0f, .level = -10.0f } })
        .add("Ciemny dubel", "Dubel bez góry, ukryty pod głosem.", { { .time = 12.0f, .width = 60.0f, .detune = 6.0f, .high = 3000.0f, .level = -5.0f } }, { .dry = -3.0f })
        .add("Jasny dubel", "Dubel bez dołu, sama góra dokoła głosu.", { { .time = 10.0f, .width = 70.0f, .detune = 5.0f, .low = 1000.0f, .level = -6.0f } })
        .add("Cienki dubel", "Wąskie pasmo po bokach, jak ślad w tle.", { { .time = 10.0f, .width = 100.0f, .low = 800.0f, .high = 6000.0f, .level = -14.0f } })
        .add("Gruby dubel", "Ciemniejsza barwa, lekkie ziarno i cichy sub pod spodem.", { { .formant = -1.0f, .time = 12.0f, .width = 70.0f, .detune = 10.0f, .level = -8.0f }, { .source = Source::Sub, .count = 1, .semis = -12, .time = 6.0f, .width = 0.0f, .level = -20.0f } }, { .grain = 6.0f })
        .add("Płynący", "Mocno rozstrojone głosy, które wolno się przelewają.", { { .time = 16.0f, .width = 40.0f, .detune = 30.0f, .level = -9.0f } })
        .add("Ręczny", "Luźno zagrany dubel, jak nagrany drugi raz i rozłożony na boki.", { { .time = 30.0f, .width = 100.0f, .detune = 14.0f, .level = -9.0f } });
    return b.take();
}

juce::StringArray presetExcludedIds() { return { ids::amount, ids::output, ids::delta, ids::bypass }; }

} // namespace refren
