#include "Params.h"
#include "ui/Strings.h"

namespace refren {

namespace {

juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

juce::String withComma(float value, int decimals) { return juce::String(value, decimals).replaceCharacter('.', ','); }

float numberFrom(const juce::String& text) { return text.replaceCharacter(',', '.').getFloatValue(); }

// The text carries the unit, because some hosts would print a separate label after it again.
juce::AudioParameterFloatAttributes textWith(const char* unit, int decimals, bool automatable = true) {
    const juce::String suffix(unit);
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([suffix, decimals](float v, int) { return (decimals == 0 ? juce::String(juce::roundToInt(v)) : withComma(v, decimals)) + suffix; })
        .withValueFromStringFunction([](const juce::String& t) { return numberFrom(t); })
        .withAutomatable(automatable);
}

juce::AudioParameterFloatAttributes percentText(bool automatable = true) { return textWith("%", 0, automatable); }
juce::AudioParameterFloatAttributes decibelText(bool automatable = true) { return textWith(" dB", 1, automatable); }

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

juce::NormalisableRange<float> skewedRange(float low, float high, float step) {
    juce::NormalisableRange<float> range(low, high, step);
    range.setSkewForCentre(std::sqrt(low * high));
    return range;
}

juce::NormalisableRange<float> linearRange(float low, float high, float step = 0.1f) { return { low, high, step }; }

juce::AudioParameterBoolAttributes switchText(bool automatable) {
    return juce::AudioParameterBoolAttributes()
        .withStringFromValueFunction([](bool on, int) { return pl(on ? text::switchOn : text::switchOff); })
        .withValueFromStringFunction([](const juce::String& t) { return t.trim() == pl(text::switchOn) || t.getFloatValue() > 0.5f; })
        .withAutomatable(automatable);
}

juce::String noteName(int note) {
    static const juce::StringArray names{ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[note % 12] + juce::String(note / 12 - 1);
}

int noteFromName(const juce::String& text) {
    for (int note = 36; note <= 72; ++note)
        if (noteName(note).equalsIgnoreCase(text.trim())) return note;
    return text.getIntValue();
}

juce::AudioParameterIntAttributes signedText() {
    return juce::AudioParameterIntAttributes()
        .withStringFromValueFunction([](int v, int) { return (v > 0 ? "+" : "") + juce::String(v); })
        .withValueFromStringFunction([](const juce::String& t) { return t.getIntValue(); })
        .withAutomatable(false);
}

} // namespace

juce::String layerId(int layer, const char* suffix) { return "l" + juce::String(layer + 1) + suffix; }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const auto fixedChoice = AudioParameterChoiceAttributes().withAutomatable(false);
    const auto add = [&l](auto parameter) { l.add(std::move(parameter)); };
    const auto number = [&](const char* id, const char* name, NormalisableRange<float> range, float initial, AudioParameterFloatAttributes attributes) {
        add(std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, pl(name), range, initial, attributes));
    };
    const auto choice = [&](const char* id, const char* name, const std::vector<const char*>& items, int initial) {
        StringArray names;
        for (const char* item : items) names.add(pl(item));
        add(std::make_unique<AudioParameterChoice>(ParameterID{ id, 1 }, pl(name), names, initial, fixedChoice));
    };

    number(ids::amount, text::amount, linearRange(0.0f, 100.0f), 100.0f, percentText());
    number(ids::width, text::width, linearRange(0.0f, 200.0f), 100.0f, percentText());
    number(ids::time, text::time, linearRange(0.0f, 200.0f), 100.0f, percentText());
    number(ids::tilt, text::tilt, linearRange(-100.0f, 100.0f), 0.0f, percentText());
    number(ids::grain, text::grain, linearRange(0.0f, 36.0f), 0.0f, decibelText());
    choice(ids::grainChar, text::grainChar, { text::lamp, text::tape, text::transformer, text::transistor }, 0);
    choice(ids::motion, text::motion, { text::motionNone, text::motionAutopan, text::motionTremolo, text::motionFlanger }, 0);
    number(ids::motionRate, text::motionRate, skewedRange(0.05f, 10.0f, 0.01f), 1.0f, textWith(" Hz", 2));
    number(ids::motionDepth, text::motionDepth, linearRange(0.0f, 100.0f), 0.0f, percentText());
    number(ids::echoMix, text::echoMix, linearRange(0.0f, 100.0f), 0.0f, percentText());
    choice(ids::echoTime, text::echoTime, { text::echoSixteenth, text::echoEighth, text::echoDottedEighth, text::echoQuarter, text::echoDottedQuarter, text::echoHalf, text::echoSlap }, 1);
    number(ids::echoFeedback, text::echoFeedback, linearRange(0.0f, 90.0f), 25.0f, percentText());
    add(std::make_unique<AudioParameterBool>(ParameterID{ ids::echoPing, 1 }, pl(text::echoPing), false, switchText(false)));
    number(ids::echoTone, text::echoTone, skewedRange(500.0f, 12000.0f, 1.0f), 6000.0f, frequencyText());
    number(ids::verbMix, text::verbMix, linearRange(0.0f, 100.0f), 0.0f, percentText());
    number(ids::verbSize, text::verbSize, linearRange(0.0f, 100.0f), 50.0f, percentText(false));
    number(ids::verbDecay, text::verbDecay, skewedRange(0.2f, 8.0f, 0.01f), 1.5f, textWith(" s", 2));
    number(ids::verbTone, text::verbTone, skewedRange(1000.0f, 16000.0f, 1.0f), 6000.0f, frequencyText());
    number(ids::verbPre, text::verbPre, linearRange(0.0f, 80.0f), 10.0f, textWith(" ms", 0));
    number(ids::verbShimmer, text::verbShimmer, linearRange(0.0f, 100.0f), 0.0f, percentText());
    number(ids::duck, text::duck, linearRange(0.0f, 100.0f), 50.0f, percentText());
    number(ids::sendDry, text::sendDry, linearRange(0.0f, 100.0f), 0.0f, percentText());
    number(ids::consonants, text::consonants, linearRange(0.0f, 100.0f), 100.0f, percentText());
    number(ids::dry, text::dry, linearRange(-48.0f, 0.0f), 0.0f, decibelText());
    number(ids::trim, text::trim, linearRange(-24.0f, 12.0f), 0.0f, decibelText(false));
    number(ids::output, text::output, linearRange(-24.0f, 24.0f), 0.0f, decibelText());
    add(std::make_unique<AudioParameterBool>(ParameterID{ ids::delta, 1 }, pl(text::delta), false, switchText(false)));
    add(std::make_unique<AudioParameterBool>(ParameterID{ ids::bypass, 1 }, pl(text::bypass), false, switchText(true)));

    for (int k = 0; k < kLayers; ++k) {
        const String name = pl(text::layer) + " " + String(k + 1) + ": ";
        const auto id = [k](const char* suffix) { return ParameterID{ layerId(k, suffix), 1 }; };
        const auto layerNumber = [&](const char* suffix, const char* title, NormalisableRange<float> range, float initial, AudioParameterFloatAttributes attributes) {
            add(std::make_unique<AudioParameterFloat>(id(suffix), name + pl(title), range, initial, attributes));
        };
        StringArray sources;
        for (const char* source : { text::sourceVoice, text::sourceWhisper, text::sourceSub, text::sourceRobot }) sources.add(pl(source));
        add(std::make_unique<AudioParameterBool>(id(ids::on), name + pl(text::layerOn), k == 0, switchText(false)));
        add(std::make_unique<AudioParameterChoice>(id(ids::source), name + pl(text::layerSource), sources, 0, fixedChoice));
        add(std::make_unique<AudioParameterInt>(id(ids::voices), name + pl(text::layerVoices), 1, 16, 2, AudioParameterIntAttributes().withAutomatable(false)));
        add(std::make_unique<AudioParameterInt>(id(ids::semis), name + pl(text::layerSemis), -24, 24, 0, signedText()));
        layerNumber(ids::formant, text::layerFormant, linearRange(-12.0f, 12.0f), 0.0f, textWith("", 1, false));
        layerNumber(ids::level, text::layerLevel, linearRange(-48.0f, 6.0f), -6.0f, decibelText());
        layerNumber(ids::layerWidth, text::layerWidth, linearRange(0.0f, 100.0f), 50.0f, percentText());
        layerNumber(ids::layerTime, text::layerTime, linearRange(0.0f, 40.0f), 12.0f, textWith(" ms", 1));
        layerNumber(ids::detune, text::layerDetune, linearRange(0.0f, 50.0f), 8.0f, textWith(" ct", 1));
        layerNumber(ids::low, text::layerLow, skewedRange(20.0f, 2000.0f, 1.0f), 20.0f, frequencyText());
        layerNumber(ids::high, text::layerHigh, skewedRange(1000.0f, 20000.0f, 1.0f), 20000.0f, frequencyText());
        add(std::make_unique<AudioParameterInt>(id(ids::note), name + pl(text::layerNote), 36, 72, 48,
                                                AudioParameterIntAttributes()
                                                    .withStringFromValueFunction([](int v, int) { return noteName(v); })
                                                    .withValueFromStringFunction([](const String& t) { return noteFromName(t); })
                                                    .withAutomatable(false)));
    }
    return l;
}

} // namespace refren
