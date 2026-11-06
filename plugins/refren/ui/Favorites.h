#pragma once
// The presets with a star. A plain text file next to the user's presets, one name a line, so it survives anything.
#include <juce_core/juce_core.h>

namespace refren {

class Favorites {
public:
    explicit Favorites(juce::File folder);
    bool contains(const juce::String& name) const { return names_.contains(name); }
    void toggle(const juce::String& name);
    juce::StringArray names() const { return names_; }
private:
    juce::File file_;
    juce::StringArray names_;
};

} // namespace refren
