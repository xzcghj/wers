#include "LampaPresetBar.h"
#include "SharedText.h"

namespace lampa::ui {

using namespace wers::ui;

namespace {
constexpr juce::uint32 kErrorMs = 4000;
}

LampaPresetBar::LampaPresetBar(PresetStore& store) : store_(store) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{ &previous_, &name_, &next_, &save_, &slotA_, &slotB_, &copy_ }) addAndMakeVisible(c);
    addChildComponent(naming_);
    previous_.setTooltip(pl(text::presetPreviousTip));
    next_.setTooltip(pl(text::presetNextTip));
    name_.setTooltip(pl(text::presetListTip));
    save_.setButtonText(pl(text::presetSave));
    save_.setTooltip(pl(text::presetSaveTip));
    slotA_.setTooltip(pl(text::presetSlotTip));
    slotB_.setTooltip(pl(text::presetSlotTip));
    copy_.setTooltip(pl(text::presetCopyTip));
    previous_.onClick = [this] { step(-1); };
    next_.onClick = [this] { step(1); };
    name_.onClick = [this] { openMenu(); };
    save_.onClick = [this] { startNaming(); };
    slotA_.onClick = [this] { pickSlot(0); };
    slotB_.onClick = [this] { pickSlot(1); };
    copy_.onClick = [this] { copyToOtherSlot(); };
    naming_.setFont(uiFont(400, 14.0f));
    naming_.setTextToShowWhenEmpty(pl(text::presetName), colour::dim);
    naming_.setIndents(8, 7);
    naming_.addListener(this);
    refresh();
}

void LampaPresetBar::setWalkList(const juce::StringArray& names) { walkList_ = names; }

void LampaPresetBar::step(int direction) {
    const auto names = walkList_.has_value() ? *walkList_ : store_.names();
    const int size = names.size();
    if (size == 0) return;
    // A star after the name means the sound has moved on from this preset, which is still where the walk is.
    const int now = names.indexOf(store_.plainCurrentName());
    int next = now >= 0 ? now + direction : (direction > 0 ? 0 : size - 1);
    // A file can vanish or get damaged after the list was read. The arrows still get past it.
    for (int tried = 0; tried < size; ++tried, next += direction) {
        if (store_.load(names[(next % size + size) % size])) {
            error_.clear();
            refresh();
            return;
        }
    }
    showError(pl(text::presetNoRead));
    refresh();
}

void LampaPresetBar::loadNamed(const juce::String& name) {
    if (store_.load(name)) error_.clear();
    else showError(pl(text::presetNoRead));
    refresh();
}

void LampaPresetBar::pickSlot(int index) {
    if (index != store_.activeSlot()) {
        store_.storeSlot(store_.activeSlot());
        store_.setActiveSlot(index);
        store_.recallSlot(index);
    }
    refresh();
}

void LampaPresetBar::copyToOtherSlot() {
    const int active = store_.activeSlot();
    store_.storeSlot(active);
    store_.copySlot(active, 1 - active);
}

void LampaPresetBar::saveAs(const juce::String& name) {
    juce::String error;
    if (store_.save(name, error)) error_.clear();
    else showError(error);
    stopNaming();
    refresh();
}

void LampaPresetBar::showError(const juce::String& message) {
    error_ = message;
    errorUntil_ = juce::Time::getMillisecondCounter() + kErrorMs;
}

void LampaPresetBar::refresh() {
    const auto now = juce::Time::getMillisecondCounter();
    juce::String shown = error_.isNotEmpty() && now < errorUntil_ ? error_ : store_.currentName();
    if (shown.isEmpty()) shown = pl(text::presetChoose);
    if (name_.getButtonText() != shown) name_.setButtonText(shown);
    const int active = store_.activeSlot();
    slotA_.setToggleState(active == 0, juce::dontSendNotification);
    slotB_.setToggleState(active == 1, juce::dontSendNotification);
    const juce::String copyText = active == 0 ? "A > B" : "B > A";
    if (copy_.getButtonText() != copyText) copy_.setButtonText(copyText);
}

void LampaPresetBar::openMenu() {
    const auto names = store_.names();
    const auto current = store_.plainCurrentName();
    juce::PopupMenu menu;
    // Factory presets under their group's heading, then the user's own.
    juce::String group;
    bool mine = false;
    for (int i = 0; i < names.size(); ++i) {
        if (const auto* info = store_.factoryInfo(names[i])) {
            const auto g = info->group != nullptr ? pl(info->group) : juce::String();
            if (g != group) {
                group = g;
                if (i > 0) menu.addSeparator();
                menu.addSectionHeader(g.toUpperCase());
            }
        } else if (!mine) {
            mine = true;
            menu.addSeparator();
            menu.addSectionHeader(pl("MOJE"));
        }
        menu.addItem(i + 1, names[i], true, names[i] == current);
    }
    constexpr int kDeleteId = 100000;
    const auto deletable = nameToDelete();
    if (deletable.isNotEmpty()) {
        menu.addSeparator();
        menu.addItem(kDeleteId, pl(text::presetDelete) + " " + deletable);
    }
    juce::Component::SafePointer<LampaPresetBar> self(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&name_), [self, names, deletable](int picked) {
        if (self == nullptr || picked == 0) return;
        if (picked != kDeleteId) {
            self->loadNamed(names[picked - 1]);
            return;
        }
        if (!self->store_.remove(deletable)) self->showError(pl(text::presetNoDelete));
        self->refresh();
    });
}

juce::String LampaPresetBar::nameToOffer() const { return nameToDelete(); }

juce::String LampaPresetBar::nameToDelete() const {
    const auto current = store_.plainCurrentName();
    return store_.isFactory(current) ? juce::String() : current;
}

void LampaPresetBar::startNaming() {
    naming_.setText(nameToOffer(), false);
    naming_.setBounds(name_.getBounds());
    name_.setVisible(false);
    naming_.setVisible(true);
    naming_.grabKeyboardFocus();
    naming_.selectAll();
}

void LampaPresetBar::stopNaming() {
    naming_.setVisible(false);
    name_.setVisible(true);
}

void LampaPresetBar::textEditorReturnKeyPressed(juce::TextEditor& editor) { saveAs(editor.getText()); }

void LampaPresetBar::textEditorEscapeKeyPressed(juce::TextEditor&) { stopNaming(); }

void LampaPresetBar::textEditorFocusLost(juce::TextEditor&) { stopNaming(); }

void LampaPresetBar::resized() {
    auto area = getLocalBounds();
    copy_.setBounds(area.removeFromRight(52));
    area.removeFromRight(2);
    slotB_.setBounds(area.removeFromRight(32));
    area.removeFromRight(2);
    slotA_.setBounds(area.removeFromRight(32));
    area.removeFromRight(6);
    save_.setBounds(area.removeFromRight(64));
    area.removeFromRight(6);
    previous_.setBounds(area.removeFromLeft(30));
    area.removeFromLeft(2);
    next_.setBounds(area.removeFromRight(30));
    area.removeFromRight(2);
    name_.setBounds(area);
    naming_.setBounds(area);
}

} // namespace lampa::ui
