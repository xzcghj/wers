#include "Favorites.h"

namespace refren {

Favorites::Favorites(juce::File folder) : file_(folder.getChildFile("ulubione.txt")) { reload(); }

void Favorites::reload() {
    if (!file_.existsAsFile()) return;
    names_.clear();
    file_.readLines(names_);
    names_.trim();
    names_.removeEmptyStrings();
    names_.removeDuplicates(false);
}

bool Favorites::toggle(const juce::String& name) {
    const bool wanted = !names_.contains(name);
    reload();
    names_.removeString(name);
    if (wanted) names_.add(name);
    return file_.getParentDirectory().createDirectory().wasOk() && file_.replaceWithText(names_.joinIntoString("\n"), false, false, "\n");
}

} // namespace refren
