#include "Modules.h"

namespace wers::ui {

CharacterPad::CharacterPad(WersProcessor& p) : proc_(p) {
    x_ = p.state().getParameter(ids::charX);
    y_ = p.state().getParameter(ids::charY);
    for (auto* b : { &gloss_, &rage_, &szept_, &drill_ }) addAndMakeVisible(b);
    gloss_.onClick = [this] { proc_.applyCharacterPreset(0.2f, 0.8f, 50.0f, 60.0f, 45.0f); };
    rage_.onClick = [this] { proc_.applyCharacterPreset(0.85f, 0.8f, 80.0f, 35.0f, 30.0f); };
    szept_.onClick = [this] { proc_.applyCharacterPreset(0.15f, 0.2f, 25.0f, 60.0f, 15.0f); };
    drill_.onClick = [this] { proc_.applyCharacterPreset(0.8f, 0.2f, 65.0f, 50.0f, 10.0f); };
    setTitle(pl("Charakter wokalu"));
}

void CharacterPad::resized() {
    constexpr int w = 60, h = 30, m = 6;
    gloss_.setBounds(m, m, w, h);
    rage_.setBounds(getWidth() - w - m, m, w, h);
    szept_.setBounds(m, getHeight() - h - m, w, h);
    drill_.setBounds(getWidth() - w - m, getHeight() - h - m, w, h);
}

void CharacterPad::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat();
    g.setColour(colour::bg);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(colour::hairline);
    g.fillRect(r.getCentreX() - 0.5f, r.getY() + 1.0f, 1.0f, r.getHeight() - 2.0f);
    g.fillRect(r.getX() + 1.0f, r.getCentreY() - 0.5f, r.getWidth() - 2.0f, 1.0f);
    const float x = x_->getValue(), y = y_->getValue();
    const float px = r.getX() + 10.0f + x * (r.getWidth() - 20.0f);
    const float py = r.getBottom() - 10.0f - y * (r.getHeight() - 20.0f);
    g.setColour(colour::accent.withAlpha(0.18f));
    g.fillEllipse(px - 15.0f, py - 15.0f, 30.0f, 30.0f);
    g.setColour(colour::accent);
    g.fillEllipse(px - 6.0f, py - 6.0f, 12.0f, 12.0f);
}

void CharacterPad::setFromPoint(juce::Point<int> p) {
    const auto r = getLocalBounds().toFloat();
    const float x = juce::jlimit(0.0f, 1.0f, (static_cast<float>(p.x) - 10.0f) / (r.getWidth() - 20.0f));
    const float y = juce::jlimit(0.0f, 1.0f, 1.0f - (static_cast<float>(p.y) - 10.0f) / (r.getHeight() - 20.0f));
    x_->setValueNotifyingHost(x);
    y_->setValueNotifyingHost(y);
    repaint();
}

void CharacterPad::mouseDown(const juce::MouseEvent& e) { x_->beginChangeGesture(); y_->beginChangeGesture(); setFromPoint(e.getPosition()); }
void CharacterPad::mouseDrag(const juce::MouseEvent& e) { setFromPoint(e.getPosition()); }
void CharacterPad::mouseUp(const juce::MouseEvent&) { x_->endChangeGesture(); y_->endChangeGesture(); }

void CharacterPad::tick() {
    const float x = x_->getValue(), y = y_->getValue();
    if (x != lastX_ || y != lastY_) { lastX_ = x; lastY_ = y; repaint(); }
}

ModuleStrip::ModuleStrip(WersProcessor& p) : proc_(p), pad_(p) {
    fonemHead_.set("Fonem", pl("Osobna obróbka dla każdej grupy głosek."));
    placeHead_.set("Miejsce", pl("Ile bit ustępuje słowom. Sub, kick i boki zostają."));
    spaceHead_.set(pl("Przestrzeń"), pl("Pogłos i delay między frazami, pod słowami prawie nic."));
    charHead_.set("Charakter", pl("Od intymnego do agresywnego, od suchego do szerokiego."));
    for (juce::Component* c : std::initializer_list<juce::Component*>{ &fonemHead_, &placeHead_, &spaceHead_, &charHead_, &density_, &clarity_, &attack_, &breath_, &place_, &space_, &pad_ }) addAndMakeVisible(c);
    auto& s = p.state();
    densityA_ = std::make_unique<Attachment>(s, ids::density, density_.slider);
    clarityA_ = std::make_unique<Attachment>(s, ids::clarity, clarity_.slider);
    attackA_ = std::make_unique<Attachment>(s, ids::attack, attack_.slider);
    breathA_ = std::make_unique<Attachment>(s, ids::breath, breath_.slider);
    spaceA_ = std::make_unique<Attachment>(s, ids::space, space_.slider);
    place_.slider.setRange(0.0, 100.0, 0.1);
    place_.slider.onDragEnd = [this] { if (auto* g = proc_.group()) g->requestPlaceDepth(static_cast<float>(place_.slider.getValue()) * 0.01f); };
}

void ModuleStrip::paint(juce::Graphics& g) {
    g.fillAll(colour::surface);
    g.setColour(colour::hairline);
    g.fillRect(0, 0, getWidth(), 1);
    // A line between modules, in the middle of the gaps laid out in resized().
    for (int x : { 438, 672, 906 }) g.fillRect(x, 18, 1, getHeight() - 36);
}

void ModuleStrip::resized() {
    auto b = getLocalBounds().reduced(28, 16);
    auto f = b.removeFromLeft(396); b.removeFromLeft(28);
    auto pl_ = b.removeFromLeft(206); b.removeFromLeft(28);
    auto sp = b.removeFromLeft(206); b.removeFromLeft(28);
    auto ch = b;
    fonemHead_.setBounds(f.removeFromTop(48));
    f.removeFromTop(6);
    auto r1 = f.removeFromTop(50); f.removeFromTop(4);
    auto r2 = f.removeFromTop(50);
    density_.setBounds(r1.removeFromLeft(188)); clarity_.setBounds(r1.removeFromRight(188));
    attack_.setBounds(r2.removeFromLeft(188)); breath_.setBounds(r2.removeFromRight(188));
    placeHead_.setBounds(pl_.removeFromTop(62));
    pl_.removeFromTop(4);
    place_.setBounds(pl_.removeFromTop(50));
    spaceHead_.setBounds(sp.removeFromTop(62));
    sp.removeFromTop(4);
    space_.setBounds(sp.removeFromTop(50));
    charHead_.setBounds(ch.removeFromTop(62));
    ch.removeFromTop(4);
    pad_.setBounds(ch);
}

void ModuleStrip::tick() {
    pad_.tick();
    auto* g = proc_.group();
    const bool linked = g != nullptr && g->beatCount() > 0;
    place_.slider.setEnabled(linked);
    placeHead_.set("Miejsce", linked ? pl("Ile bit ustępuje słowom. Sub, kick i boki zostają.") : pl("Dodaj WERS na ślad bitu i wybierz tam rolę Bit."));
    if (linked && !place_.slider.isMouseButtonDown()) place_.slider.setValue(static_cast<double>(g->placeDepth()) * 100.0, juce::dontSendNotification);
}

} // namespace wers::ui
