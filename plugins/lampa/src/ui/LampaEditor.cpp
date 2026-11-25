#include "LampaEditor.h"
#include "plugin/LampaProcessor.h"

namespace lampa {

using namespace ui;

class LampaEditor::Root final : public juce::Component {
public:
    explicit Root(LampaProcessor& p)
        : processor(p), presetBar(p.presets()),
          squeeze(p.state(), ids::squeeze, pl("Ściskanie"), warm::amber, true),
          gain(p.state(), ids::gain, pl("Wzmocnienie"), warm::amber, true),
          color(p.state(), ids::color, pl("Kolor"), warm::ember),
          mix(p.state(), ids::mix, pl("Mix"), warm::amber),
          link(p.state(), ids::link, pl("Link stereo"), warm::amber),
          sidechain(p.state(), ids::sidechain, pl("Bas nie pompuje"), warm::amber),
          output(p.state(), ids::output, pl("Wyjście"), warm::amber, false, true) {
        for (auto* c : std::initializer_list<juce::Component*>{ &presetBar, &bypass, &squeeze, &gain, &meter, &meterMode, &character, &limit,
                                                                &color, &mix, &link, &sidechain, &output })
            addAndMakeVisible(c);

        squeeze.setTip(pl("Jak mocno ściskać. Kręć, aż igła w trybie REDUKCJA zejdzie tyle, ile chcesz: 2–4 dB to wyrównanie, 6–10 dB to wyraźna kompresja."));
        gain.setTip(pl("Podgłośnienie za komórką, prosto w lampę. Oddaje to, co zabrało Ściskanie; im więcej, tym bardziej lampa się grzeje."));
        color.setTip(pl("Ile koloru dokłada lampa i transformatory: harmoniczne, trochę wagi w dole i miękka góra. Na zero prawie czysto."));
        mix.setTip(pl("Proporcja przetworzonego do oryginału. Poniżej 100% to kompresja równoległa: gęstość bez utraty ataku."));
        link.setTip(pl("Na 100% oba kanały ścisza ta sama komórka, obraz stereo stoi w miejscu. Na 0% każdy kanał pracuje osobno."));
        sidechain.setTip(pl("Filtr górnoprzepustowy przed detektorem: Lampa przestaje reagować na najniższy bas, więc stopa i bas nie pompują całością. Sam dźwięk się nie zmienia."));
        output.setTip(pl("Głośność na samym końcu, za lampą."));

        character.setTextSize(14.0f);
        character.setItems({ pl("Opto"), pl("Lampa") }, { 108, 108 });
        character.setItemTooltip(0, pl("Komórka optyczna: łagodny atak i dwuetapowe zwolnienie, które pamięta, jak długo pracowało. Parzyste harmoniczne. Klasyka na wokal i bas."));
        character.setItemTooltip(1, pl("Lampa o zmiennym nachyleniu: szybsza, z bardzo miękkim kolanem i gładkim zwolnieniem. Nieparzyste harmoniczne, mocniejsze przy większej redukcji. Klej na szyny i sumę."));
        characterLink = std::make_unique<juce::ParameterAttachment>(*p.state().getParameter(ids::mode), [this](float v) { character.setSelected(juce::roundToInt(v)); }, nullptr);
        character.onPick = [this](int i) { characterLink->setValueAsCompleteGesture(static_cast<float>(i)); };
        characterLink->sendInitialUpdate();

        limit.setClickingTogglesState(true);
        limit.setOnFill(warm::amber, colour::ink);
        limit.setTextSize(14.0f);
        limit.setTooltip(pl("Wyższy stosunek i szybsza reakcja na szczyty: z wyrównywania robi się ogranicznik."));
        limitLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::limit, limit);

        meterMode.setTextSize(12.5f);
        meterMode.setItems({ pl("Redukcja"), pl("Wyjście") }, { 96, 96 });
        meterMode.setItemTooltip(0, pl("Igła pokazuje, o ile decybeli Lampa ścisza dźwięk."));
        meterMode.setItemTooltip(1, pl("Igła pokazuje poziom na wyjściu, 0 VU to −18 dBFS."));
        meterMode.onPick = [this](int i) { processor.setMeterMode(i); };
        meter.onFlip = [this] { processor.setMeterMode(1 - processor.meterMode()); };

        bypass.setClickingTogglesState(true);
        bypass.setOnFill(colour::weak, colour::ink);
        bypass.setTooltip(pl("Czysty dźwięk, z płynnym przejściem i tym samym opóźnieniem."));
        bypassLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypass);
    }

    void resized() override {
        presetBar.setBounds(250, 14, 520, 36);
        bypass.setBounds(LampaEditor::kWidth - 20 - 88, 14, 88, 36);
        squeeze.setBounds(24, 116, 240, 282);
        gain.setBounds(LampaEditor::kWidth - 24 - 240, 116, 240, 282);
        meter.setBounds(284, 108, 472, 276);
        meterMode.setBounds(284 + (472 - meterMode.preferredWidth()) / 2, 394, meterMode.preferredWidth(), 30);
        character.setBounds(44, 488, character.preferredWidth(), 38);
        limit.setBounds(44, 552, character.preferredWidth(), 38);
        const int x0 = 330, w = 134;
        Knob* small[] = { &color, &mix, &link, &sidechain, &output };
        for (int i = 0; i < 5; ++i) small[i]->setBounds(x0 + i * w, 462, w - 8, 150);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::bg);
        // A faint warm light from behind the meter.
        juce::ColourGradient warmth(warm::amber.withAlpha(0.07f), 520.0f, 250.0f, juce::Colours::transparentBlack, 520.0f, -120.0f, true);
        g.setGradientFill(warmth);
        g.fillRect(0, 64, LampaEditor::kWidth, 380);

        g.setColour(colour::surface);
        g.fillRect(0, 0, LampaEditor::kWidth, 64);
        g.setColour(colour::hairline);
        g.fillRect(0, 64, LampaEditor::kWidth, 1);
        g.setFont(displayFont(800, 26.0f));
        g.setColour(colour::text);
        g.drawText("WERS", 22, 14, 80, 36, juce::Justification::centredLeft);
        const int wersW = juce::GlyphArrangement::getStringWidthInt(displayFont(800, 26.0f), "WERS");
        g.setFont(displayFont(400, 26.0f));
        g.setColour(warm::amber);
        g.drawText("Lampa", 22 + wersW + 5, 14, 140, 36, juce::Justification::centredLeft);

        // What the chosen preset is for.
        g.setFont(uiFont(400, 13.5f));
        g.setColour(colour::dim);
        g.drawFittedText(shownDescription, juce::Rectangle<int>(250, 70, 540, 24), juce::Justification::centred, 1, 0.85f);

        // The bottom panel.
        const auto card = juce::Rectangle<float>(16.0f, 446.0f, static_cast<float>(LampaEditor::kWidth) - 32.0f, 178.0f);
        g.setColour(warm::panel);
        g.fillRoundedRectangle(card, 14.0f);
        g.setColour(warm::panelEdge);
        g.drawRoundedRectangle(card.reduced(0.5f), 14.0f, 1.0f);
        g.setColour(colour::hairline);
        g.fillRect(306, 462, 1, 146);
        g.setFont(uiFont(600, 12.0f));
        g.setColour(colour::dim);
        g.drawText(pl("CHARAKTER"), juce::Rectangle<int>(46, 464, 200, 18), juce::Justification::centredLeft);
        g.drawText(pl("TRYB"), juce::Rectangle<int>(46, 532, 200, 16), juce::Justification::centredLeft);
    }

    void tick(float dt) {
        const auto& e = processor.engine();
        const auto mode = processor.meterMode() == 0 ? VuMeter::Mode::Reduction : VuMeter::Mode::Output;
        meter.setMode(mode);
        meterMode.setSelected(static_cast<int>(mode));
        meter.update(e.meterReductionDb(), e.meterOutput(), e.meterHeat(), e.meterMemoryDb(), dt);
        for (auto* k : { &squeeze, &gain, &color, &mix, &link, &sidechain, &output }) k->refresh();
        limit.setButtonText(pl("Limit"));
        presetBar.refresh();
        const auto d = describe();
        if (d != shownDescription) {
            shownDescription = d;
            repaint(250, 66, 540, 30);
        }
    }

    juce::String describe() const {
        const auto name = processor.presets().plainCurrentName();
        if (const auto* info = processor.presets().factoryInfo(name))
            if (info->description != nullptr) return pl(info->description);
        return name.isEmpty() ? pl("Wybierz preset albo kręć Ściskaniem, aż igła zacznie pracować.") : juce::String();
    }

    LampaProcessor& processor;
    LampaPresetBar presetBar;
    Knob squeeze, gain, color, mix, link, sidechain, output;
    VuMeter meter;
    wers::ui::SegmentRow character, meterMode;
    wers::ui::FlatButton bypass{ "Bypass" }, limit{ "Limit" };
    std::unique_ptr<juce::ParameterAttachment> characterLink;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassLink, limitLink;
    juce::String shownDescription;
    juce::TooltipWindow tooltips{ this, 600 };
};

LampaEditor::LampaEditor(LampaProcessor& owner) : juce::AudioProcessorEditor(owner), processor_(owner) {
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
    lastTick_ = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

LampaEditor::~LampaEditor() {
    stopTimer();
    root_.reset();
    setLookAndFeel(nullptr);
}

void LampaEditor::paint(juce::Graphics& g) { g.fillAll(colour::bg); }

void LampaEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(kWidth);
    root_->setTransform(juce::AffineTransform::scale(scale));
    processor_.setUiScale(scale);
}

void LampaEditor::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = static_cast<float>(juce::jlimit(0.0, 0.25, (now - lastTick_) * 0.001));
    lastTick_ = now;
    updateNow(dt);
}

void LampaEditor::updateNow(float dt) { root_->tick(dt); }
ui::VuMeter& LampaEditor::meter() { return root_->meter; }
ui::LampaPresetBar& LampaEditor::presetBar() { return root_->presetBar; }
ui::Knob& LampaEditor::squeezeKnob() { return root_->squeeze; }
wers::ui::SegmentRow& LampaEditor::characterRow() { return root_->character; }
wers::ui::SegmentRow& LampaEditor::meterRow() { return root_->meterMode; }
wers::ui::FlatButton& LampaEditor::limitButton() { return root_->limit; }
juce::String LampaEditor::description() const { return root_->shownDescription; }

} // namespace lampa
