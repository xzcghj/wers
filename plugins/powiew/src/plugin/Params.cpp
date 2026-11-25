#include "Params.h"

namespace powiew {

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
        .withStringFromValueFunction([](float v, int) { return withComma(v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"; })
        .withValueFromStringFunction([](const juce::String& t) {
            const float v = numberFrom(t);
            return t.containsIgnoreCase("k") || v < 100.0f ? v * 1000.0f : v;
        });
}

Attr decibelText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return (v > 0.04f ? "+" : "") + withComma(v, 1) + " dB"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

juce::NormalisableRange<float> hertzRange(float lo, float hi) {
    juce::NormalisableRange<float> r(lo, hi, 10.0f);
    r.setSkewForCentre(std::sqrt(lo * hi)); // the geometric middle: the knob turns like an octave scale
    return r;
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::presence, 1 }, pl("Obecność"), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::air, 1 }, pl("Powietrze"), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::presenceHz, 1 }, pl("Obecność: częstotliwość"), hertzRange(3000.0f, 10000.0f), 6000.0f, hertzText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::airHz, 1 }, pl("Powietrze: częstotliwość"), hertzRange(8000.0f, 20000.0f), 12000.0f, hertzText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::smooth, 1 }, pl("Gładkość"), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, percentText()));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::character, 1 }, pl("Charakter"), StringArray{ pl("Jedwab"), pl("Blask"), pl("Analog") }, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::mix, 1 }, pl("Mix"), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::trim, 1 }, pl("Wyjście"), NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f, decibelText()));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::monoSafe, 1 }, pl("Mono-bezpiecznie"), false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::delta, 1 }, pl("Słuchaj dodatku"), false));
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
    : presence_(get(s, ids::presence)), air_(get(s, ids::air)), presenceHz_(get(s, ids::presenceHz)), airHz_(get(s, ids::airHz)), smooth_(get(s, ids::smooth)),
      character_(get(s, ids::character)), mix_(get(s, ids::mix)), trim_(get(s, ids::trim)), monoSafe_(get(s, ids::monoSafe)), delta_(get(s, ids::delta)),
      bypass_(get(s, ids::bypass)) {}

Params ParamReader::read() const noexcept {
    Params p;
    p.presence = presence_->load() * 0.01f;
    p.air = air_->load() * 0.01f;
    p.presenceHz = presenceHz_->load();
    p.airHz = airHz_->load();
    p.smooth = smooth_->load() * 0.01f;
    p.character = static_cast<Character>(juce::jlimit(0, 2, juce::roundToInt(character_->load())));
    p.mix = mix_->load() * 0.01f;
    p.outputGain = juce::Decibels::decibelsToGain(trim_->load(), -100.0f);
    p.monoSafe = monoSafe_->load() > 0.5f;
    p.delta = delta_->load() > 0.5f;
    p.bypass = bypass_->load() > 0.5f;
    return p;
}

} // namespace powiew
