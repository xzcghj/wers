#include "Params.h"

namespace doc {

namespace {

juce::String withComma(float value, int decimals) { return juce::String(value, decimals).replaceCharacter('.', ','); }
float numberFrom(const juce::String& text) { return text.replaceCharacter(',', '.').retainCharacters("+-.0123456789").getFloatValue(); }

using Attr = juce::AudioParameterFloatAttributes;
using Range = juce::NormalisableRange<float>;

constexpr float kExpandShare = 0.08f, kRatioCurve = 1.15f;

Attr decibels(int decimals = 1) {
    return Attr()
        .withStringFromValueFunction([decimals](float v, int) { return (v > 0.04f ? "+" : "") + withComma(v, decimals) + " dB"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

Attr milliseconds() {
    return Attr()
        .withStringFromValueFunction([](float v, int) {
            if (v >= 1000.0f) return withComma(v / 1000.0f, 2) + " s";
            if (v >= 100.0f) return juce::String(juce::roundToInt(v)) + " ms";
            if (v >= 10.0f) return withComma(v, 1) + " ms";
            return withComma(v, 2) + " ms";
        })
        .withValueFromStringFunction([](const juce::String& t) {
            const float v = numberFrom(t);
            return t.containsIgnoreCase("ms") || !t.containsIgnoreCase("s") ? v : v * 1000.0f;
        });
}

Attr percent() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + "%"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

Range skewed(float lo, float hi, float centre) {
    Range r(lo, hi);
    r.setSkewForCentre(centre);
    return r;
}

} // namespace

float ratioFromNormalised(float n) noexcept {
    n = juce::jlimit(0.0f, 1.0f, n);
    if (n < kExpandShare) return kRatioMin + (1.0f - kRatioMin) * n / kExpandShare;
    const float t = (n - kExpandShare) / (1.0f - kExpandShare);
    return std::pow(kRatioInfinite, std::pow(t, kRatioCurve));
}

float normalisedFromRatio(float r) noexcept {
    r = juce::jlimit(kRatioMin, kRatioInfinite, r);
    if (r < 1.0f) return kExpandShare * (r - kRatioMin) / (1.0f - kRatioMin);
    const float t = std::pow(std::log(r) / std::log(kRatioInfinite), 1.0f / kRatioCurve);
    return kExpandShare + (1.0f - kExpandShare) * t;
}

juce::String ratioText(float r) {
    if (r >= kRatioInfinite - 0.5f) return juce::String::fromUTF8("∞:1");
    if (r < 1.0f) return "1:" + withComma(1.0f / r, 2); // expansion reads the other way round
    return withComma(r, r < 10.0f ? 1 : 0) + ":1";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto addFloat = [&](const char* id, const char* name, Range range, float def, Attr attr) {
        l.add(std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, pl(name), range, def, attr));
    };
    addFloat(ids::threshold, "Próg", Range(-60.0f, 0.0f, 0.1f), -18.0f, decibels());
    Range ratioRange(kRatioMin, kRatioInfinite,
                     [](float, float, float n) { return ratioFromNormalised(n); },
                     [](float, float, float v) { return normalisedFromRatio(v); },
                     [](float, float, float v) { return juce::jlimit(kRatioMin, kRatioInfinite, v); });
    addFloat(ids::ratio, "Ratio", ratioRange, 4.0f,
             Attr()
                 .withStringFromValueFunction([](float v, int) { return ratioText(v); })
                 .withValueFromStringFunction([](const juce::String& t) {
                     if (t.containsIgnoreCase("inf") || t.contains(juce::String::fromUTF8("∞"))) return kRatioInfinite;
                     if (t.startsWith("1:")) return 1.0f / juce::jmax(0.01f, numberFrom(t.fromFirstOccurrenceOf(":", false, false)));
                     return numberFrom(t.upToFirstOccurrenceOf(":", false, false));
                 }));
    addFloat(ids::knee, "Kolano", Range(0.0f, 24.0f, 0.1f), 6.0f, decibels());
    addFloat(ids::attack, "Atak", skewed(0.05f, 250.0f, 10.0f), 10.0f, milliseconds());
    addFloat(ids::release, "Powrót", skewed(5.0f, 2500.0f, 150.0f), 120.0f, milliseconds());
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::autoRelease, 1 }, pl("Powrót automatyczny"), false));
    addFloat(ids::range, "Zakres", Range(1.0f, kRangeUnlimited, 0.1f), kRangeUnlimited,
             Attr()
                 .withStringFromValueFunction([](float v, int) { return v >= kRangeUnlimited - 0.05f ? pl("bez limitu") : withComma(v, 1) + " dB"; })
                 .withValueFromStringFunction([](const juce::String& t) { return t.containsIgnoreCase("bez") ? kRangeUnlimited : numberFrom(t); }));
    addFloat(ids::makeup, "Zysk", Range(-12.0f, 24.0f, 0.1f), 0.0f, decibels());
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::autoMakeup, 1 }, pl("Zysk automatyczny"), false));
    addFloat(ids::mix, "Miks", Range(0.0f, 100.0f, 0.1f), 100.0f, percent());
    addFloat(ids::output, "Wyjście", Range(-24.0f, 12.0f, 0.1f), 0.0f, decibels());
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::character, 1 }, pl("Charakter"), StringArray{ pl("Czysty"), pl("Punch"), pl("Klej") }, 0));
    addFloat(ids::detector, "Detektor", Range(0.0f, 100.0f, 0.1f), 0.0f,
             Attr()
                 .withStringFromValueFunction([](float v, int) {
                     const int r = juce::roundToInt(v);
                     if (r <= 0) return pl("Szczyt");
                     if (r >= 100) return pl("RMS");
                     return juce::String(r) + "% RMS";
                 })
                 .withValueFromStringFunction([](const juce::String& t) {
                     if (t.containsIgnoreCase("szczyt")) return 0.0f;
                     if (t.trim().equalsIgnoreCase("rms")) return 100.0f;
                     return numberFrom(t);
                 }));
    addFloat(ids::link, "Łączenie stereo", Range(0.0f, 100.0f, 0.1f), 100.0f, percent());
    addFloat(ids::lookahead, "Wyprzedzenie", Range(0.0f, kMaxLookaheadMs, 0.01f), 0.0f,
             Attr()
                 .withStringFromValueFunction([](float v, int) { return v < 0.005f ? pl("wył.") : withComma(v, 1) + " ms"; })
                 .withValueFromStringFunction([](const juce::String& t) { return t.containsIgnoreCase("wy") ? 0.0f : numberFrom(t); }));
    addFloat(ids::keyHighPass, "Filtr SC", skewed(20.0f, 500.0f, 100.0f), 20.0f,
             Attr()
                 .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + " Hz"; })
                 .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); }));
    addFloat(ids::keyTilt, "Barwa SC", Range(-6.0f, 6.0f, 0.1f), 0.0f, decibels());
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::external, 1 }, pl("Zewnętrzny sidechain"), false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::listen, 1 }, pl("Odsłuch sidechainu"), false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::clip, 1 }, pl("Miękki clip"), false));
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
    : threshold_(get(s, ids::threshold)), ratio_(get(s, ids::ratio)), knee_(get(s, ids::knee)), attack_(get(s, ids::attack)),
      release_(get(s, ids::release)), autoRelease_(get(s, ids::autoRelease)), range_(get(s, ids::range)), makeup_(get(s, ids::makeup)),
      autoMakeup_(get(s, ids::autoMakeup)), mix_(get(s, ids::mix)), output_(get(s, ids::output)), character_(get(s, ids::character)),
      detector_(get(s, ids::detector)), link_(get(s, ids::link)), lookahead_(get(s, ids::lookahead)), keyHighPass_(get(s, ids::keyHighPass)),
      keyTilt_(get(s, ids::keyTilt)), external_(get(s, ids::external)), listen_(get(s, ids::listen)), clip_(get(s, ids::clip)),
      bypass_(get(s, ids::bypass)) {}

Params ParamReader::read() const noexcept {
    Params p;
    p.thresholdDb = threshold_->load();
    p.ratio = ratio_->load();
    p.kneeDb = knee_->load();
    p.attackMs = attack_->load();
    p.releaseMs = release_->load();
    p.autoRelease = autoRelease_->load() > 0.5f;
    p.rangeDb = range_->load();
    p.makeupDb = makeup_->load();
    p.autoMakeup = autoMakeup_->load() > 0.5f;
    p.mix = mix_->load() * 0.01f;
    p.outputDb = output_->load();
    p.character = static_cast<Character>(juce::jlimit(0, 2, juce::roundToInt(character_->load())));
    p.rms = detector_->load() * 0.01f;
    p.link = link_->load() * 0.01f;
    p.lookaheadMs = lookahead_->load();
    p.keyHighPassHz = keyHighPass_->load();
    p.keyTiltDb = keyTilt_->load();
    p.external = external_->load() > 0.5f;
    p.listen = listen_->load() > 0.5f;
    p.clip = clip_->load() > 0.5f;
    return p;
}

} // namespace doc
