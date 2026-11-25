#include "Params.h"

namespace lampa {

namespace {

juce::String withComma(float value, int decimals) { return juce::String(value, decimals).replaceCharacter('.', ','); }
float numberFrom(const juce::String& text) { return text.replaceCharacter(',', '.').getFloatValue(); }

using Attr = juce::AudioParameterFloatAttributes;

Attr percentText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + "%"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

// Ściskanie is a 0..100 scale like on the old leveling amplifiers: no unit, just how far it goes.
Attr squeezeText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)); })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

Attr decibelText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return (v > 0.04f ? "+" : "") + withComma(v, 1) + " dB"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

Attr sidechainText() {
    return Attr()
        .withStringFromValueFunction([](float v, int) { return v <= 20.5f ? pl("wył.") : juce::String(juce::roundToInt(v)) + " Hz"; })
        .withValueFromStringFunction([](const juce::String& t) { return t.containsIgnoreCase("w") ? 20.0f : numberFrom(t); });
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::squeeze, 1 }, pl("Ściskanie"), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f, squeezeText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::gain, 1 }, pl("Wzmocnienie"), NormalisableRange<float>(0.0f, 30.0f, 0.1f), 3.0f, decibelText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::color, 1 }, pl("Kolor"), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 25.0f, percentText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::mix, 1 }, pl("Mix"), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentText()));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::mode, 1 }, pl("Charakter"), StringArray{ pl("Opto"), pl("Lampa") }, 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::limit, 1 }, pl("Limit"), false));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::link, 1 }, pl("Link stereo"), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentText()));
    NormalisableRange<float> sc(20.0f, 500.0f, 1.0f);
    sc.setSkewForCentre(100.0f);
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::sidechain, 1 }, pl("Filtr detektora"), sc, 20.0f, sidechainText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::output, 1 }, pl("Wyjście"), NormalisableRange<float>(-18.0f, 12.0f, 0.1f), 0.0f, decibelText()));
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
    : squeeze_(get(s, ids::squeeze)), gain_(get(s, ids::gain)), color_(get(s, ids::color)), mix_(get(s, ids::mix)), mode_(get(s, ids::mode)),
      limit_(get(s, ids::limit)), link_(get(s, ids::link)), sidechain_(get(s, ids::sidechain)), output_(get(s, ids::output)), bypass_(get(s, ids::bypass)) {}

Params ParamReader::read() const noexcept {
    Params p;
    p.squeeze = squeeze_->load();
    p.gainDb = gain_->load();
    p.color = color_->load() * 0.01f;
    p.mix = mix_->load() * 0.01f;
    p.character = juce::roundToInt(mode_->load()) == 1 ? Character::Tube : Character::Opto;
    p.limit = limit_->load() > 0.5f;
    p.link = link_->load() * 0.01f;
    p.sidechainHz = sidechain_->load();
    p.outputGain = juce::Decibels::decibelsToGain(output_->load(), -100.0f);
    p.bypass = bypass_->load() > 0.5f;
    return p;
}

} // namespace lampa
