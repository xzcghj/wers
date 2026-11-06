#include "Favorites.h"

namespace refren {

Favorites::Favorites(juce::File folder) : file_(folder.getChildFile("ulubione.txt")) {
    if (!file_.existsAsFile()) return;
    file_.readLines(names_);
    names_.trim();
    names_.removeEmptyStrings();
    names_.removeDuplicates(false);
}

void Favorites::toggle(const juce::String& name) {
    if (names_.contains(name)) names_.removeString(name);
    else names_.add(name);
    // When this cannot be written the star lasts until the window closes, which is better than a message about it.
    if (file_.getParentDirectory().createDirectory().wasOk()) file_.replaceWithText(names_.joinIntoString("\n"), false, false, "\n");
}

} // namespace refren
