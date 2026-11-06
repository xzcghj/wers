#include "PresetBrowser.h"
#include "Strings.h"
#include "plugin/FactoryPresets.h"

namespace refren {

using namespace wers::ui;

namespace {

constexpr int kGroupsW = 148, kGroupRowH = 32, kCardH = 58, kGap = 6, kColumns = 3, kStarArea = 32, kScrollW = 8;

void drawStar(juce::Graphics& g, juce::Rectangle<float> area, bool filled, juce::Colour colourNow) {
    juce::Path star;
    star.addStar(area.getCentre(), 5, area.getWidth() * 0.22f, area.getWidth() * 0.5f, 0.0f);
    g.setColour(colourNow);
    if (filled) g.fillPath(star);
    else g.strokePath(star, juce::PathStrokeType(1.2f));
}

juce::String withoutStar(juce::String name) { return name.endsWithChar('*') ? name.dropLastCharacters(1) : name; }

} // namespace

class PresetBrowser::GroupRow final : public juce::Button {
public:
    GroupRow(const juce::String& name, bool isFavourites) : juce::Button(name), star_(isFavourites) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTitle(name);
    }
    void paintButton(juce::Graphics& g, bool over, bool) override {
        const auto bounds = getLocalBounds().toFloat();
        const bool on = getToggleState();
        if (on || over) {
            g.setColour(colour::raised);
            g.fillRoundedRectangle(bounds, 6.0f);
        }
        if (hasKeyboardFocus(false)) {
            g.setColour(colour::accent);
            g.drawRoundedRectangle(bounds.reduced(1.0f), 6.0f, 1.5f);
        }
        const juce::Colour shown = on ? colour::text : colour::dim;
        int textX = 10;
        if (star_) {
            drawStar(g, juce::Rectangle<float>(10.0f, bounds.getCentreY() - 7.0f, 14.0f, 14.0f), true, on ? colour::accent : colour::dim);
            textX = 30;
        }
        g.setFont(uiFont(on ? 600 : 500, 14.0f));
        g.setColour(shown);
        g.drawText(getButtonText(), textX, 0, getWidth() - textX - 6, getHeight(), juce::Justification::centredLeft, true);
    }
private:
    const bool star_;
};

class PresetBrowser::Card final : public juce::Button {
public:
    Card(PresetBrowser& owner, const juce::String& name, const juce::String& description) : juce::Button(name), owner_(owner) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTitle(name);
        setTooltip(description);
    }
    void setCurrent(bool current) {
        if (current != current_) {
            current_ = current;
            repaint();
        }
    }
    void paintButton(juce::Graphics& g, bool over, bool) override {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(current_ || over ? colour::raised : colour::surface);
        g.fillRoundedRectangle(bounds, 8.0f);
        if (current_ || hasKeyboardFocus(false)) {
            g.setColour(colour::accent);
            g.drawRoundedRectangle(bounds.reduced(0.75f), 8.0f, 1.5f);
        }
        g.setFont(uiFont(current_ ? 600 : 500, 14.0f));
        g.setColour(colour::text);
        g.drawFittedText(getButtonText(), 12, 0, getWidth() - 12 - kStarArea, getHeight(), juce::Justification::centredLeft, 2, 0.9f);
        const bool star = owner_.hasStar(getButtonText());
        const auto place = juce::Rectangle<float>(bounds.getRight() - 25.0f, bounds.getCentreY() - 8.0f, 16.0f, 16.0f);
        drawStar(g, place, star, star ? colour::accent : (over ? colour::dim : colour::faint));
    }
    void clicked() override {
        if (getMouseXYRelative().x >= getWidth() - kStarArea) owner_.toggleStar(getButtonText());
        else owner_.choose(getButtonText());
    }
private:
    PresetBrowser& owner_;
    bool current_ = false;
};

PresetBrowser::PresetBrowser(PresetStore& store, PresetBar& bar, const juce::File& folder) : store_(store), bar_(bar), favorites_(folder) {
    for (const auto& name : groupNames()) {
        auto* row = rows_.add(new GroupRow(name, name == pl(text::favorites)));
        row->onClick = [this, name] { showGroup(name); };
        addAndMakeVisible(row);
    }
    viewport_.setViewedComponent(&holder_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(kScrollW);
    addAndMakeVisible(viewport_);
    const auto current = withoutStar(store_.currentName());
    lastCurrent_ = store_.currentName();
    showGroup(groupOf(current));
}

PresetBrowser::~PresetBrowser() = default;

juce::StringArray PresetBrowser::groupNames() const {
    juce::StringArray all{ pl(text::favorites), pl(text::mine) };
    for (const auto& group : kGroups) all.add(pl(group.name));
    return all;
}

juce::String PresetBrowser::groupOf(const juce::String& preset) const {
    if (const auto* info = store_.factoryInfo(preset))
        if (info->group != nullptr) return pl(info->group);
    if (store_.userNames().contains(preset)) return pl(text::mine);
    return pl(kGroups[0].name);
}

juce::StringArray PresetBrowser::namesIn(const juce::String& group) const {
    if (group == pl(text::mine)) return store_.userNames();
    const auto all = store_.names();
    juce::StringArray found;
    if (group == pl(text::favorites)) {
        for (const auto& name : favorites_.names())
            if (all.contains(name)) found.add(name);
        return found;
    }
    for (const auto& name : all) {
        const auto* info = store_.factoryInfo(name);
        if (info != nullptr && info->group != nullptr && pl(info->group) == group) found.add(name);
    }
    return found;
}

void PresetBrowser::showGroup(const juce::String& name) {
    group_ = name;
    for (auto* row : rows_) row->setToggleState(row->getButtonText() == name, juce::dontSendNotification);
    buildCards();
}

void PresetBrowser::buildCards() {
    shown_ = namesIn(group_);
    bar_.setWalkList(shown_);
    cards_.clear();
    const auto current = withoutStar(store_.currentName());
    for (const auto& name : shown_) {
        const auto* info = store_.factoryInfo(name);
        auto* card = cards_.add(new Card(*this, name, info != nullptr && info->description != nullptr ? pl(info->description) : juce::String()));
        card->setCurrent(name == current);
        holder_.addAndMakeVisible(card);
    }
    emptyText_ = !shown_.isEmpty() ? juce::String() : group_ == pl(text::favorites) ? pl(text::emptyFavorites) : group_ == pl(text::mine) ? pl(text::emptyMine) : juce::String();
    placeCards();
    repaint();
}

void PresetBrowser::placeCards() {
    const int width = viewport_.getWidth() - kScrollW - 2;
    if (width <= 0) return;
    const int cardW = (width - (kColumns - 1) * kGap) / kColumns;
    const int rows = (cards_.size() + kColumns - 1) / kColumns;
    holder_.setSize(width, juce::jmax(viewport_.getHeight(), rows * (kCardH + kGap)));
    for (int i = 0; i < cards_.size(); ++i) cards_[i]->setBounds((i % kColumns) * (cardW + kGap), (i / kColumns) * (kCardH + kGap), cardW, kCardH);
}

void PresetBrowser::choose(const juce::String& preset) {
    store_.load(preset);
    bar_.refresh();
    refresh();
}

void PresetBrowser::toggleStar(const juce::String& preset) {
    favorites_.toggle(preset);
    if (group_ == pl(text::favorites)) buildCards();
    else
        for (auto* card : cards_)
            if (card->getButtonText() == preset) card->repaint();
}

void PresetBrowser::refresh() {
    const auto current = store_.currentName();
    if (current == lastCurrent_) return;
    lastCurrent_ = current;
    // A preset saved or deleted just now changes this list.
    if (group_ == pl(text::mine)) {
        buildCards();
        return;
    }
    const auto plain = withoutStar(current);
    for (auto* card : cards_) card->setCurrent(card->getButtonText() == plain);
}

void PresetBrowser::resized() {
    auto area = getLocalBounds();
    auto left = area.removeFromLeft(kGroupsW);
    for (auto* row : rows_) {
        row->setBounds(left.removeFromTop(kGroupRowH));
        left.removeFromTop(2);
    }
    area.removeFromLeft(8);
    viewport_.setBounds(area);
    placeCards();
}

void PresetBrowser::paint(juce::Graphics& g) {
    if (emptyText_.isEmpty()) return;
    g.setFont(uiFont(400, 14.0f));
    g.setColour(colour::dim);
    g.drawFittedText(emptyText_, viewport_.getBounds().withHeight(60).reduced(12, 0), juce::Justification::centredLeft, 3, 1.0f);
}

} // namespace refren
