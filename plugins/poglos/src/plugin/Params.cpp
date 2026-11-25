#include "Params.h"

namespace pog {

namespace {

juce::String withComma(float value, int decimals) { return juce::String(value, decimals).replaceCharacter('.', ','); }
float numberFrom(const juce::String& text) { return text.replaceCharacter(',', '.').getFloatValue(); }

using Attr = juce::AudioParameterFloatAttributes;

Attr percentText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + "%"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

Attr hertzText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) {
            if (v >= 1000.0f) return withComma(v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
            return juce::String(juce::roundToInt(v)) + " Hz";
        })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t) * (t.containsIgnoreCase("k") ? 1000.0f : 1.0f); });
}

Attr secondsText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) {
            if (v < 1.0f) return juce::String(juce::roundToInt(v * 1000.0f)) + " ms";
            return withComma(v, v < 10.0f ? 2 : 1) + " s";
        })
        .withValueFromStringFunction([](const juce::String& t) {
            const float x = numberFrom(t);
            return t.containsIgnoreCase("ms") ? x / 1000.0f : x;
        });
}

Attr msText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return v < 10.0f ? withComma(v, 1) + " ms" : juce::String(juce::roundToInt(v)) + " ms"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

Attr multText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return withComma(v, 2) + juce::String::fromUTF8(" ×"); })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

Attr rateText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return withComma(v, v < 1.0f ? 2 : 1) + " Hz"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

Attr balanceText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) {
            const int late = juce::roundToInt(v);
            if (late <= 0) return pl("Tylko wczesne");
            if (late >= 100) return pl("Tylko późne");
            return juce::String(100 - late) + " / " + juce::String(late);
        })
        .withValueFromStringFunction([](const juce::String& t) {
            if (t.containsIgnoreCase("wczesne")) return 0.0f;
            if (t.containsIgnoreCase(pl("późne"))) return 100.0f;
            return t.contains("/") ? numberFrom(t.fromFirstOccurrenceOf("/", false, false)) : numberFrom(t);
        });
}

juce::NormalisableRange<float> skewed(float lo, float hi, float centre, float step = 0.0f) {
    juce::NormalisableRange<float> r(lo, hi, step);
    r.setSkewForCentre(centre);
    return r;
}

} // namespace

juce::StringArray algoNames() {
    return { pl("Sala"), pl("Pokój"), pl("Płyta"), pl("Komora"), pl("Ambient"), pl("Katedra"), pl("Błysk"), pl("Nieliniowy") };
}
juce::StringArray eraNames() { return { pl("Dziś"), pl("Lata 80"), pl("Lata 70") }; }
juce::StringArray noteNames() { return { "1/64", "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2" }; }
juce::StringArray pitchNames() { return { pl("+12 oktawa"), pl("+7 kwinta"), pl("+19 kwinta wyżej"), pl("+24 dwie oktawy"), pl("−12 oktawa w dół") }; }

double noteQuarters(int index) {
    static constexpr double q[] = { 1.0 / 16, 1.0 / 8, 1.0 / 6, 1.0 / 4, 3.0 / 8, 1.0 / 3, 1.0 / 2, 3.0 / 4, 2.0 / 3, 1.0, 1.5, 2.0 };
    return q[juce::jlimit(0, 11, index)];
}

float pitchSemis(int index) {
    static constexpr float s[] = { 12.0f, 7.0f, 19.0f, 24.0f, -12.0f };
    return s[juce::jlimit(0, 4, index)];
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto addFloat = [&](const char* id, const char* name, NormalisableRange<float> range, float def, Attr attr) {
        l.add(std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, pl(name), range, def, attr));
    };
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::algo, 1 }, pl("Algorytm"), algoNames(), 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::era, 1 }, pl("Epoka"), eraNames(), 0));
    addFloat(ids::mix, "Miks", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f, percentText());
    addFloat(ids::predelay, "Predelay", skewed(0.0f, 500.0f, 80.0f, 0.1f), 20.0f, msText());
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::preSync, 1 }, pl("Predelay z tempem"), false));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::preNote, 1 }, pl("Predelay: nuta"), noteNames(), 3));
    addFloat(ids::decay, "Czas pogłosu", skewed(kMinDecay, kMaxDecay, 2.0f, 0.001f), 2.5f, secondsText());
    addFloat(ids::size, "Rozmiar", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 60.0f, percentText());
    addFloat(ids::attack, "Narastanie", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentText());
    addFloat(ids::balance, "Wczesne / późne", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, balanceText());
    addFloat(ids::diffEarly, "Dyfuzja wczesna", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 75.0f, percentText());
    addFloat(ids::diffLate, "Dyfuzja późna", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 70.0f, percentText());
    addFloat(ids::bassMult, "Mnożnik basu", skewed(0.25f, 4.0f, 1.0f, 0.01f), 1.2f, multText());
    addFloat(ids::bassHz, "Podział basu", skewed(50.0f, 1000.0f, 220.0f, 1.0f), 250.0f, hertzText());
    addFloat(ids::damp, "Tłumienie góry", skewed(1000.0f, 20000.0f, 4500.0f, 1.0f), 6000.0f, hertzText());
    addFloat(ids::highCut, "Obcięcie góry", skewed(1000.0f, 20000.0f, 6000.0f, 1.0f), 16000.0f, hertzText());
    addFloat(ids::lowCut, "Obcięcie dołu", skewed(20.0f, 1000.0f, 140.0f, 1.0f), 40.0f, hertzText());
    addFloat(ids::modRate, "Modulacja: tempo", skewed(0.05f, 5.0f, 0.6f, 0.001f), 0.6f, rateText());
    addFloat(ids::modDepth, "Modulacja: głębokość", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 40.0f, percentText());
    addFloat(ids::width, "Szerokość", NormalisableRange<float>(0.0f, 150.0f, 0.1f), 100.0f, percentText());
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::freeze, 1 }, pl("Zamrożenie"), false));
    addFloat(ids::duck, "Ducking", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentText());
    addFloat(ids::shimmer, "Błysk: ilość", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 40.0f, percentText());
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::shimPitch, 1 }, pl("Błysk: interwał"), pitchNames(), 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::bypass, 1 }, pl("Bypass"), false));
    return l;
}

namespace {
std::atomic<float>* get(juce::AudioProcessorValueTreeState& s, const char* id) {
    auto* p = s.getRawParameterValue(id);
    jassert(p != nullptr);
    return p;
}
} // namespace

ParamReader::ParamReader(juce::AudioProcessorValueTreeState& s)
    : algo_(get(s, ids::algo)), era_(get(s, ids::era)), mix_(get(s, ids::mix)), predelay_(get(s, ids::predelay)), preSync_(get(s, ids::preSync)),
      preNote_(get(s, ids::preNote)), decay_(get(s, ids::decay)), size_(get(s, ids::size)), attack_(get(s, ids::attack)), balance_(get(s, ids::balance)),
      diffEarly_(get(s, ids::diffEarly)), diffLate_(get(s, ids::diffLate)), bassMult_(get(s, ids::bassMult)), bassHz_(get(s, ids::bassHz)),
      damp_(get(s, ids::damp)), highCut_(get(s, ids::highCut)), lowCut_(get(s, ids::lowCut)), modRate_(get(s, ids::modRate)),
      modDepth_(get(s, ids::modDepth)), width_(get(s, ids::width)), freeze_(get(s, ids::freeze)), duck_(get(s, ids::duck)),
      shimmer_(get(s, ids::shimmer)), shimPitch_(get(s, ids::shimPitch)), bypass_(get(s, ids::bypass)) {}

Params ParamReader::read(double bpm) const noexcept {
    Params p;
    p.algo = static_cast<Algo>(juce::jlimit(0, kAlgoCount - 1, juce::roundToInt(algo_->load())));
    p.era = static_cast<Era>(juce::jlimit(0, kEraCount - 1, juce::roundToInt(era_->load())));
    p.mix = mix_->load() * 0.01f;
    p.predelayMs = predelay_->load();
    if (preSync_->load() > 0.5f) {
        const double tempo = juce::jlimit(20.0, 400.0, bpm > 0.0 ? bpm : 120.0);
        p.predelayMs = static_cast<float>(juce::jmin(static_cast<double>(kMaxPredelayMs), noteQuarters(note()) * 60000.0 / tempo));
    }
    p.decay = decay_->load();
    p.size = size_->load() * 0.01f;
    p.attack = attack_->load() * 0.01f;
    p.balance = balance_->load() * 0.01f;
    p.diffEarly = diffEarly_->load() * 0.01f;
    p.diffLate = diffLate_->load() * 0.01f;
    p.bassMult = bassMult_->load();
    p.bassHz = bassHz_->load();
    p.dampHz = damp_->load();
    p.highCutHz = highCut_->load();
    p.lowCutHz = lowCut_->load();
    p.modRate = modRate_->load();
    p.modDepth = modDepth_->load() * 0.01f;
    p.width = width_->load() * 0.01f;
    p.freeze = freeze_->load() > 0.5f;
    p.duck = duck_->load() * 0.01f;
    p.shimmer = shimmer_->load() * 0.01f;
    p.shimmerSemis = pitchSemis(juce::roundToInt(shimPitch_->load()));
    return p;
}

} // namespace pog
