#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <optional>

namespace wers::ui {

struct FactoryPreset {
    const char* name;
    std::vector<std::pair<const char*, float>> values; // what differs from the neutral default
};

// The presets and the two comparison slots of one plugin. Presets are XML files in the user's folder,
// so they can be copied by hand. Anything a preset does not mention takes its neutral default.
class PresetStore {
public:
    PresetStore(juce::AudioProcessorValueTreeState& state, juce::File folder, std::vector<FactoryPreset> factory, juce::StringArray excludedIds);

    // Factory presets first, then the user's own by name. Damaged and foreign files are left out.
    juce::StringArray names() const;
    bool isFactory(const juce::String& name) const;
    bool load(const juce::String& name);
    bool save(const juce::String& name, juce::String& error);
    bool remove(const juce::String& name);
    juce::String currentName() const;

    void storeSlot(int index);
    void recallSlot(int index);
    void copySlot(int from, int to);
    int activeSlot() const;
    void setActiveSlot(int index);

    // The slots and the chosen preset travel in the project.
    juce::ValueTree toTree() const;
    void fromTree(const juce::ValueTree& tree);
    static const char* treeType() noexcept { return "PRESETS"; }
private:
    using Values = std::map<juce::String, float>;
    struct Slot {
        Values values;
        juce::String name;
    };
    Values currentValues() const;
    void apply(const Values& values);
    std::optional<Values> readFile(const juce::File& file) const;
    juce::File fileFor(const juce::String& name) const;
    juce::String pluginId() const;

    juce::AudioProcessorValueTreeState& state_;
    juce::File folder_;
    std::vector<FactoryPreset> factory_;
    juce::StringArray excluded_;
    juce::CriticalSection lock_; // the host may ask for the project state from another thread
    std::array<std::optional<Slot>, 2> slots_{};
    int activeSlot_ = 0;
    juce::String current_;
};

} // namespace wers::ui
