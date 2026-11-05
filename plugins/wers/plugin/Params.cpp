#include "Params.h"

namespace wers {

static juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const auto percent = [](const char* id, const String& name, float lo, float def) {
        auto attrs = AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction([](float v, int) { return String(roundToInt(v)) + "%"; });
        return std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, name, NormalisableRange<float>(lo, 100.0f, 0.1f), def, attrs);
    };
    const auto choiceAttrs = AudioParameterChoiceAttributes().withAutomatable(false);
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::role, 1 }, pl("Rola"), StringArray{ pl("Wokal"), pl("Bit"), pl("Master") }, 0, choiceAttrs));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::bypass, 1 }, pl("Bypass"), false));
    l.add(percent(ids::density, pl("Samogłoski: gęstość"), 0.0f, 0.0f));
    l.add(percent(ids::clarity, pl("Syczące: czystość"), 0.0f, 0.0f));
    l.add(percent(ids::attack, pl("Zwarte: atak"), -100.0f, 0.0f));
    l.add(percent(ids::breath, pl("Oddechy: ciszej"), 0.0f, 0.0f));
    l.add(percent(ids::place, pl("Miejsce: głębokość"), 0.0f, 40.0f));
    l.add(percent(ids::space, pl("Przestrzeń: ilość"), 0.0f, 0.0f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::charX, 1 }, pl("Charakter: intymny–agresywny"), NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ ids::charY, 1 }, pl("Charakter: suchy–szeroki"), NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ ids::fixes, 1 }, pl("Naprawy słów"), true));
    const StringArray devices{ pl("Telefon"), pl("Pchełki"), pl("Auto"), pl("Laptop"), pl("Studio") };
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::viewDevice, 1 }, pl("Sprawdzam na"), devices, 0, choiceAttrs));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::monDevice, 1 }, pl("Odsłuch na"), devices, 4, choiceAttrs));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::monNorm, 1 }, pl("Normalizacja"), StringArray{ pl("Wyłączona"), pl("Spotify -14"), pl("Spotify -11"), pl("Spotify -19") }, 0, choiceAttrs));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ ids::group, 1 }, pl("Para"), StringArray{ "1", "2", "3", "4", "5", "6", "7", "8" }, 0, choiceAttrs));
    return l;
}

} // namespace wers
