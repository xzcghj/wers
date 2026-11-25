#include "PowiewEditor.h"
#include "plugin/PowiewProcessor.h"

namespace powiew {

using namespace ui;

namespace {
// One line under the character switch, so the choice is never a guess.
juce::String characterText(int c) {
    switch (c) {
    case 1: return pl("Blask: jaśniej i szybciej. Nieparzyste harmoniczne, więcej podbicia, mocniej skacze na atakach.");
    case 2: return pl("Analog: najwięcej harmonicznych, mniej korekcji. Gęsta, ciepła góra jak ze sprzętu.");
    default: return pl("Jedwab: miękko i gładko. Głównie druga harmoniczna, spokojny powrót po sybilantach.");
    }
}
} // namespace

class PowiewEditor::Root final : public juce::Component {
public:
    explicit Root(PowiewProcessor& p)
        : processor(p), presetBar(p.presets()), view(p), presenceCard(p, 0), airCard(p, 1),
          smooth(p.state(), ids::smooth, pl("Gładkość"), colour::weak), mix(p.state(), ids::mix, pl("Mix"), colour::accent),
          trim(p.state(), ids::trim, pl("Wyjście"), colour::accent, true) {
        for (auto* c : std::initializer_list<juce::Component*>{ &presetBar, &view, &presenceCard, &airCard, &smooth, &mix, &trim, &character, &monoSafe, &delta, &bypass })
            addAndMakeVisible(c);

        character.setTextSize(13.0f);
        character.setItems({ pl("Jedwab"), pl("Blask"), pl("Analog") }, { 96, 96, 96 });
        for (int i = 0; i < 3; ++i) character.setItemTooltip(i, characterText(i));
        characterLink = std::make_unique<juce::ParameterAttachment>(*p.state().getParameter(ids::character), [this](float v) {
            character.setSelected(juce::roundToInt(v));
            repaint(characterLine());
        }, nullptr);
        character.onPick = [this](int i) { characterLink->setValueAsCompleteGesture(static_cast<float>(i)); };
        characterLink->sendInitialUpdate();

        smooth.setTip(pl("Jak mocno podbicie cofa się na sybilantach, talerzach i materiale, który już jest jasny. 0%: zawsze tyle samo. Więcej: gładziej."));
        mix.setTip(pl("Ile dodatku trafia do wyjścia. 100%: całość, mniej: delikatniej, równolegle."));
        trim.setTip(pl("Głośność na wyjściu."));

        monoSafe.setClickingTogglesState(true);
        monoSafe.setOnFill(colour::accent, colour::ink);
        monoSafe.setTextSize(12.5f);
        monoSafe.setTooltip(pl("Dodatek tworzony tylko ze środka i dodawany tak samo do obu kanałów: boki zostają nietknięte, a suma mono jest pewna. Na master i podcasty."));
        monoLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::monoSafe, monoSafe);

        delta.setClickingTogglesState(true);
        delta.setOnFill(colour::weak, colour::ink);
        delta.setTextSize(12.5f);
        delta.setTooltip(pl("Słyszysz tylko to, co Powiew dodaje: podbicie i nowe harmoniczne, bez oryginału. Nie zapisuje się w presetach."));
        deltaLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::delta, delta);

        bypass.setClickingTogglesState(true);
        bypass.setOnFill(colour::weak, colour::ink);
        bypass.setTooltip(pl("Czysty dźwięk, z płynnym przejściem i tym samym opóźnieniem."));
        bypassLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypass);
    }

    juce::Rectangle<int> centre() const { return { 356, 382, 328, 262 }; }
    juce::Rectangle<int> characterLine() const { return { centre().getX() + 16, centre().getY() + 68, centre().getWidth() - 32, 34 }; }

    void resized() override {
        presetBar.setBounds(270, 12, 500, 36);
        bypass.setBounds(PowiewEditor::kWidth - 16 - 84, 12, 84, 36);
        view.setBounds(16, 78, PowiewEditor::kWidth - 32, 292);
        presenceCard.setBounds(16, 382, 330, 262);
        airCard.setBounds(694, 382, 330, 262);
        const auto c = centre();
        character.setBounds(c.getX() + (c.getWidth() - character.preferredWidth()) / 2, c.getY() + 32, character.preferredWidth(), 30);
        const int knobW = 96;
        const int x0 = c.getX() + (c.getWidth() - knobW * 3) / 2;
        smooth.setBounds(x0, c.getY() + 106, knobW, 104);
        mix.setBounds(x0 + knobW, c.getY() + 106, knobW, 104);
        trim.setBounds(x0 + knobW * 2, c.getY() + 106, knobW, 104);
        monoSafe.setBounds(c.getX() + 16, c.getBottom() - 42, 144, 30);
        delta.setBounds(c.getRight() - 16 - 144, c.getBottom() - 42, 144, 30);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::bg);
        g.setColour(colour::surface);
        g.fillRect(0, 0, PowiewEditor::kWidth, 64);
        g.setColour(colour::hairline);
        g.fillRect(0, 64, PowiewEditor::kWidth, 1);
        g.setFont(displayFont(800, 26.0f));
        g.setColour(colour::text);
        g.drawText("WERS", 22, 14, 80, 36, juce::Justification::centredLeft);
        const int wersW = juce::GlyphArrangement::getStringWidthInt(displayFont(800, 26.0f), "WERS");
        g.setFont(displayFont(400, 26.0f));
        juce::ColourGradient name(hue::presence, static_cast<float>(22 + wersW + 5), 0.0f, hue::air, static_cast<float>(22 + wersW + 105), 0.0f, false);
        g.setGradientFill(name);
        g.drawText("Powiew", 22 + wersW + 5, 14, 140, 36, juce::Justification::centredLeft);

        const auto c = centre().toFloat();
        g.setColour(colour::surface);
        g.fillRoundedRectangle(c, 12.0f);
        g.setColour(colour::hairline);
        g.drawRoundedRectangle(c.reduced(0.5f), 12.0f, 1.0f);
        g.setFont(uiFont(600, 11.5f));
        g.setColour(colour::faint);
        g.drawText(pl("CHARAKTER"), juce::Rectangle<float>(c.getX() + 16.0f, c.getY() + 8.0f, 200.0f, 16.0f), juce::Justification::centredLeft);
        g.setFont(uiFont(400, 12.0f));
        g.setColour(colour::dim);
        g.drawFittedText(characterText(character.selected()), characterLine(), juce::Justification::centred, 2, 0.9f);
        // A reminder while you are hearing only the addition.
        if (delta.getToggleState()) {
            g.setFont(uiFont(600, 12.0f));
            g.setColour(colour::weak);
            g.drawText(pl("słyszysz sam dodatek"), juce::Rectangle<int>(PowiewEditor::kWidth - 16 - 84 - 200, 12, 186, 36), juce::Justification::centredRight);
        }
    }

    void tick() {
        view.tick();
        presenceCard.tick();
        airCard.tick();
        smooth.refresh();
        mix.refresh();
        trim.refresh();
        if (delta.getToggleState() != shownDelta) {
            shownDelta = delta.getToggleState();
            repaint(0, 0, PowiewEditor::kWidth, 64);
        }
        presetBar.refresh();
    }

    PowiewProcessor& processor;
    wers::ui::PresetBar presetBar;
    AirView view;
    BandCard presenceCard, airCard;
    Knob smooth, mix, trim;
    wers::ui::SegmentRow character;
    wers::ui::FlatButton monoSafe{ pl("Mono-bezpiecznie") }, delta{ pl("Słuchaj dodatku") }, bypass{ "Bypass" };
    std::unique_ptr<juce::ParameterAttachment> characterLink;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoLink, deltaLink, bypassLink;
    bool shownDelta = false;
    juce::TooltipWindow tooltips{ this, 600 };
};

PowiewEditor::PowiewEditor(PowiewProcessor& owner) : juce::AudioProcessorEditor(owner), processor_(owner) {
    setLookAndFeel(&look_);
    root_ = std::make_unique<Root>(owner);
    addAndMakeVisible(*root_);
    root_->setBounds(0, 0, kWidth, kHeight);
    const float scale = juce::jlimit(0.6f, 1.6f, owner.uiScale());
    setResizable(true, true);
    setResizeLimits(juce::roundToInt(kWidth * 0.6f), juce::roundToInt(kHeight * 0.6f), juce::roundToInt(kWidth * 1.6f), juce::roundToInt(kHeight * 1.6f));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(static_cast<double>(kWidth) / kHeight);
    setSize(juce::roundToInt(kWidth * scale), juce::roundToInt(kHeight * scale));
    updateNow();
    startTimerHz(30);
}

PowiewEditor::~PowiewEditor() {
    stopTimer();
    root_.reset();
    setLookAndFeel(nullptr);
}

void PowiewEditor::paint(juce::Graphics& g) { g.fillAll(colour::bg); }

void PowiewEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(kWidth);
    root_->setTransform(juce::AffineTransform::scale(scale));
    processor_.setUiScale(scale);
}

void PowiewEditor::updateNow() { root_->tick(); }
ui::AirView& PowiewEditor::airView() { return root_->view; }
ui::BandCard& PowiewEditor::card(int band) { return band == 0 ? root_->presenceCard : root_->airCard; }
wers::ui::SegmentRow& PowiewEditor::characterRow() { return root_->character; }
wers::ui::FlatButton& PowiewEditor::monoSafeButton() { return root_->monoSafe; }
wers::ui::FlatButton& PowiewEditor::deltaButton() { return root_->delta; }
wers::ui::PresetBar& PowiewEditor::presetBar() { return root_->presetBar; }

} // namespace powiew
