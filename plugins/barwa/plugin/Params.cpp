#include "Params.h"
#include "ui/Strings.h"

namespace barwa {

namespace {

juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

juce::String withComma(float value, int decimals) { return juce::String(value, decimals).replaceCharacter('.', ','); }

float numberFrom(const juce::String& text) { return text.replaceCharacter(',', '.').getFloatValue(); }

// The text carries the unit, because some hosts would print a separate label after it again.
juce::AudioParameterFloatAttributes percentText() {
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + "%"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

juce::AudioParameterFloatAttributes decibelText() {
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return withComma(v, 1) + " dB"; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); });
}

juce::AudioParameterFloatAttributes frequencyText() {
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) {
            return v < 1000.0f ? juce::String(juce::roundToInt(v)) + " Hz" : withComma(v / 1000.0f, 2) + " kHz";
        })
        .withValueFromStringFunction([](const juce::String& t) {
            const bool kilo = t.containsIgnoreCase("k");
            return numberFrom(t) * (kilo ? 1000.0f : 1.0f);
        });
}

juce::NormalisableRange<float> frequencyRange(float low, float high) {
    juce::NormalisableRange<float> range(low, high, 1.0f);
    range.setSkewForCentre(std::sqrt(low * high));
    return range;
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const auto fixedChoice = AudioParameterChoiceAttributes().withAutomatable(false);
    const auto fixedSwitch = AudioParameterBoolAttributes().withAutomatable(false);
    const StringArray characters{ pl(text::lamp), pl(text::tape), pl(text::transformer), pl(text::transistor) };

    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::bands, 1 }, pl(text::bands), StringArray{ "1", "2", "3" }, 0, fixedChoice));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::xover1, 1 }, pl(text::split1), frequencyRange(60.0f, 1000.0f), 200.0f, frequencyText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::xover2, 1 }, pl(text::split2), frequencyRange(1000.0f, 10000.0f), 3000.0f, frequencyText()));

    const std::array<const char*, 3> charIds{ ids::char1, ids::char2, ids::char3 };
    const std::array<const char*, 3> driveIds{ ids::drive1, ids::drive2, ids::drive3 };
    const std::array<const char*, 3> mixIds{ ids::bandMix1, ids::bandMix2, ids::bandMix3 };
    const std::array<const char*, 3> soloIds{ ids::solo1, ids::solo2, ids::solo3 };
    for (size_t i = 0; i < 3; ++i) {
        const String name = pl(text::band) + " " + String(static_cast<int>(i) + 1) + ": ";
        l.add(std::make_unique<AudioParameterChoice>(ParameterID{ charIds[i], 1 }, name + pl(text::character), characters, 0, fixedChoice));
        l.add(std::make_unique<AudioParameterFloat>(ParameterID{ driveIds[i], 1 }, name + pl(text::drive), NormalisableRange<float>(0.0f, 36.0f, 0.1f), 0.0f, decibelText()));
        l.add(std::make_unique<AudioParameterFloat>(ParameterID{ mixIds[i], 1 }, name + pl(text::mix), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentText()));
        l.add(std::make_unique<AudioParameterBool>(ParameterID{ soloIds[i], 1 }, name + pl(text::solo), false, fixedSwitch));
    }

    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::mask, 1 }, pl(text::mask), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::tone, 1 }, pl(text::tone), NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f, percentText()));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::mix, 1 }, pl(text::mix), NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentText()));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::autoLevel, 1 }, pl(text::autoLevel), true, fixedSwitch));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::output, 1 }, pl(text::output), NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, decibelText()));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::delta, 1 }, pl(text::delta), false, fixedSwitch));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::quality, 1 }, pl(text::quality), StringArray{ "2x", "4x", "8x" }, 1, fixedChoice));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::bypass, 1 }, pl(text::bypass), false));
    return l;
}

} // namespace barwa
