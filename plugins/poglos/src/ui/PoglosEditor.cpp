#include "PoglosEditor.h"
#include "plugin/PoglosProcessor.h"

namespace pog {

using namespace ui;

namespace {

// A group of knobs under a heading, drawn by the root.
struct Card {
    juce::String title;
    juce::Rectangle<int> bounds;
    juce::Colour tint;
};

constexpr int kCardKnobW = 92, kCardKnobH = 100;

} // namespace

class PoglosEditor::Root final : public juce::Component {
public:
    explicit Root(PoglosProcessor& p)
        : processor(p), presetBar(p.presets()), space(p), decayView(p), algos(p.state()), eras(p.state()),
          mix(p.state(), ids::mix, pl("Miks"), colour::accent), predelay(p.state(), ids::predelay, pl("Predelay"), hue::space),
          note(p.state(), ids::preNote, pl("Predelay"), hue::space), decay(p.state(), ids::decay, pl("Czas"), hue::mid),
          size(p.state(), ids::size, pl("Rozmiar"), hue::space), attack(p.state(), ids::attack, pl("Narastanie"), hue::space),
          balance(p.state(), ids::balance, pl("Wczesne / późne"), hue::space), diffEarly(p.state(), ids::diffEarly, pl("Dyfuzja wczesna"), hue::space),
          diffLate(p.state(), ids::diffLate, pl("Dyfuzja późna"), hue::space), bassMult(p.state(), ids::bassMult, pl("Mnożnik basu"), hue::low),
          bassHz(p.state(), ids::bassHz, pl("Podział basu"), hue::low), damp(p.state(), ids::damp, pl("Tłumienie góry"), hue::high),
          lowCut(p.state(), ids::lowCut, pl("Obcięcie dołu"), hue::low), highCut(p.state(), ids::highCut, pl("Obcięcie góry"), hue::high),
          modRate(p.state(), ids::modRate, pl("Modulacja"), hue::mid), modDepth(p.state(), ids::modDepth, pl("Głębokość"), hue::mid),
          width(p.state(), ids::width, pl("Szerokość"), hue::mid), duck(p.state(), ids::duck, pl("Ducking"), hue::mid),
          shimmer(p.state(), ids::shimmer, pl("Błysk"), hue::high) {
        for (auto* c : std::initializer_list<juce::Component*>{ &presetBar, &space, &decayView, &algos, &eras, &mix, &predelay, &note, &decay, &size, &attack,
                                                                 &balance, &diffEarly, &diffLate, &bassMult, &bassHz, &damp, &lowCut, &highCut, &modRate,
                                                                 &modDepth, &width, &duck, &shimmer, &freeze, &bypass, &sync, &interval })
            addAndMakeVisible(c);

        mix.setTip(pl("Ile pogłosu w stosunku do suchego dźwięku. Na wysyłce (aux) ustaw 100%."));
        predelay.setTip(pl("Przerwa między dźwiękiem a pogłosem. Odkleja pogłos od źródła, więc wokal zostaje czytelny."));
        note.setTip(pl("Predelay w wartościach nut, w tempie utworu."));
        note.valueOverride = [this] {
            const double ms = juce::jmin(static_cast<double>(kMaxPredelayMs), noteQuarters(processor.reader().note()) * 60000.0 / juce::jlimit(20.0, 400.0, processor.hostBpm()));
            return noteNames()[processor.reader().note()] + juce::String::fromUTF8(" · ") + juce::String(juce::roundToInt(ms)) + " ms";
        };
        decay.setTip(pl("RT60: po ilu sekundach ogon cichnie o 60 dB w środku pasma. Dół i góra liczą się od niego mnożnikiem basu i tłumieniem góry."));
        size.setTip(pl("Wielkość przestrzeni: odstępy odbić i długości linii. Czas pogłosu się nie zmienia."));
        attack.setTip(pl("Jak powoli pogłos narasta. W algorytmie Nieliniowy to kształt bramki: opadający, płaski albo odwrócony."));
        balance.setTip(pl("Proporcja wczesnych odbić do późnego ogona. W lewo bliżej ścian, w prawo sam ogon."));
        diffEarly.setTip(pl("Jak bardzo rozmyty jest początek. Mniej: słychać pojedyncze odbicia. Więcej: gładki zastrzyk."));
        diffLate.setTip(pl("Gęstość ogona. Mniej: ziarnisty, z echem. Więcej: gładki jak jedwab."));
        bassMult.setTip(pl("Czas pogłosu basu względem środka: 2× to dwa razy dłużej, 0,5× to o połowę krócej."));
        bassHz.setTip(pl("Poniżej tej częstotliwości działa mnożnik basu."));
        damp.setTip(pl("Powyżej tej częstotliwości ogon gaśnie szybciej, aż do czterech razy na samej górze. Jak powietrze i zasłony."));
        lowCut.setTip(pl("Obcina dół pogłosu, żeby nie zamulał miksu. Suchy dźwięk zostaje nietknięty."));
        highCut.setTip(pl("Obcina górę pogłosu: ciemniej i dalej. Suchy dźwięk zostaje nietknięty."));
        modRate.setTip(pl("Tempo ruchu linii opóźniających. Wolno: łagodny chorus. Szybko: drżenie."));
        modDepth.setTip(pl("Jak mocno linie pływają. Ruch rozmywa rezonanse, więc ogon nie dzwoni; dużo daje chorus."));
        width.setTip(pl("Szerokość pogłosu: 0% mono, 100% naturalnie, 150% szerzej niż natura."));
        duck.setTip(pl("Ścisza pogłos, kiedy gra źródło, i oddaje go w przerwach. Do wokalu i szybkich partii."));
        shimmer.setTip(pl("Ile podniesionego ogona wraca do pogłosu. Działa w algorytmie Błysk."));

        freeze.setClickingTogglesState(true);
        freeze.setOnFill(hue::ice, colour::ink);
        freeze.setTooltip(pl("Zatrzymuje ogon na zawsze i przestaje przyjmować nowy dźwięk. Nie zapisuje się w presetach."));
        freezeLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::freeze, freeze);
        bypass.setClickingTogglesState(true);
        bypass.setOnFill(colour::weak, colour::ink);
        bypass.setTooltip(pl("Czysty dźwięk, z płynnym przejściem."));
        bypassLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypass);
        sync.setClickingTogglesState(true);
        sync.setOnFill(hue::space, colour::ink);
        sync.setTextSize(12.0f);
        sync.setTooltip(pl("Predelay w nutach, w tempie utworu."));
        syncLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::preSync, sync);
        interval.setTextSize(12.0f);
        interval.setTooltip(pl("Interwał Błysku. Klik: następny."));
        intervalLink = std::make_unique<juce::ParameterAttachment>(*p.state().getParameter(ids::shimPitch), [this](float v) {
            interval.setButtonText(pitchNames()[juce::jlimit(0, 4, juce::roundToInt(v))]);
        }, nullptr);
        intervalLink->sendInitialUpdate();
        interval.onClick = [this] {
            const int now = juce::roundToInt(processor.state().getRawParameterValue(ids::shimPitch)->load());
            intervalLink->setValueAsCompleteGesture(static_cast<float>((now + 1) % 5));
        };
    }

    void placeKnobs(const Card& card, std::initializer_list<Knob*> knobs) {
        const int n = static_cast<int>(knobs.size());
        const auto area = card.bounds.withTrimmedTop(28).reduced(10, 0);
        const int w = juce::jmin(kCardKnobW + 22, area.getWidth() / n);
        int x = area.getX() + (area.getWidth() - w * n) / 2;
        for (auto* k : knobs) {
            k->setBounds(x + (w - kCardKnobW) / 2, area.getY(), kCardKnobW, kCardKnobH);
            x += w;
        }
    }

    void resized() override {
        presetBar.setBounds(250, 14, 470, 36);
        bypass.setBounds(PoglosEditor::kWidth - 16 - 84, 14, 84, 36);
        freeze.setBounds(bypass.getX() - 12 - 104, 14, 104, 36);
        space.setBounds(16, 106, 360, 244);
        decayView.setBounds(388, 106, 716, 244);
        algos.setBounds(16, 362, 616, 92);
        eras.setBounds(644, 362, 460, 92);
        cards[0] = { pl("PRZESTRZEŃ"), { 16, 468, 536, 136 }, hue::space };
        cards[1] = { pl("CHARAKTER"), { 568, 468, 536, 136 }, hue::space };
        cards[2] = { pl("BARWA OGONA"), { 16, 614, 536, 128 }, hue::high };
        cards[3] = { pl("RUCH I MIEJSCE"), { 568, 614, 536, 128 }, hue::mid };
        placeKnobs(cards[0], { &mix, &predelay, &decay, &size });
        note.setBounds(predelay.getBounds());
        placeKnobs(cards[1], { &attack, &balance, &diffEarly, &diffLate });
        placeKnobs(cards[2], { &bassMult, &bassHz, &damp, &lowCut, &highCut });
        placeKnobs(cards[3], { &modRate, &modDepth, &width, &duck, &shimmer });
        sync.setBounds(cards[0].bounds.getRight() - 12 - 70, cards[0].bounds.getY() + 6, 70, 22);
        interval.setBounds(cards[3].bounds.getRight() - 12 - 150, cards[3].bounds.getY() + 6, 150, 22);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::bg);
        g.setColour(colour::surface);
        g.fillRect(0, 0, PoglosEditor::kWidth, 64);
        g.setColour(colour::hairline);
        g.fillRect(0, 64, PoglosEditor::kWidth, 1);
        g.setFont(displayFont(800, 26.0f));
        g.setColour(colour::text);
        g.drawText("WERS", 22, 14, 80, 36, juce::Justification::centredLeft);
        const int wersW = juce::GlyphArrangement::getStringWidthInt(displayFont(800, 26.0f), "WERS");
        g.setFont(displayFont(400, 26.0f));
        g.setColour(hue::space);
        g.drawText(pl("Pogłos"), 22 + wersW + 5, 14, 160, 36, juce::Justification::centredLeft);

        // The preset's group and what it is for.
        g.setColour(colour::bg.brighter(0.04f));
        g.fillRect(0, 65, PoglosEditor::kWidth, 28);
        g.setColour(colour::hairline.withAlpha(0.6f));
        g.fillRect(0, 93, PoglosEditor::kWidth, 1);
        {
            const auto group = description.upToFirstOccurrenceOf("|", false, false);
            const auto text = description.fromFirstOccurrenceOf("|", false, false);
            g.setFont(uiFont(600, 11.5f));
            const int groupW = juce::GlyphArrangement::getStringWidthInt(uiFont(600, 11.5f), group.toUpperCase());
            g.setColour(hue::space);
            g.drawText(group.toUpperCase(), 22, 65, groupW + 4, 28, juce::Justification::centredLeft);
            g.setFont(uiFont(400, 13.0f));
            g.setColour(colour::dim);
            g.drawFittedText(text, 22 + groupW + (group.isEmpty() ? 0 : 14), 65, PoglosEditor::kWidth - 60 - groupW, 28, juce::Justification::centredLeft, 1, 0.85f);
        }

        for (const auto& c : cards) {
            const auto r = c.bounds.toFloat();
            g.setColour(colour::surface);
            g.fillRoundedRectangle(r, 12.0f);
            g.setColour(colour::hairline);
            g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, 1.0f);
            g.setColour(c.tint);
            g.fillRoundedRectangle(r.getX() + 14.0f, r.getY() + 14.0f, 4.0f, 10.0f, 2.0f);
            g.setFont(uiFont(600, 11.5f));
            g.setColour(colour::dim);
            g.drawText(c.title, juce::Rectangle<float>(r.getX() + 24.0f, r.getY() + 10.0f, 200.0f, 18.0f), juce::Justification::centredLeft);
        }
    }

    void tick() {
        space.tick();
        decayView.tick();
        const auto p = processor.currentParams();
        const bool synced = processor.reader().synced();
        predelay.setVisible(!synced);
        note.setVisible(synced);
        const bool gate = p.algo == Algo::Nonlinear, shim = p.algo == Algo::Shimmer;
        decay.setLabel(gate ? pl("Bramka") : pl("Czas"));
        attack.setLabel(gate ? pl("Kształt") : pl("Narastanie"));
        shimmer.setEnabled(shim);
        interval.setVisible(shim);
        for (auto* k : { &mix, &predelay, &note, &decay, &size, &attack, &balance, &diffEarly, &diffLate, &bassMult, &bassHz, &damp, &lowCut, &highCut,
                         &modRate, &modDepth, &width, &duck, &shimmer })
            k->refresh();
        // Ducking shows how much it takes right now.
        const float taken = processor.reverb().duckReductionDb();
        duck.valueOverride = [this, taken] {
            auto t = processor.state().getParameter(ids::duck)->getCurrentValueAsText();
            if (taken > 0.5f) t << juce::String::fromUTF8("  −") << juce::String(juce::roundToInt(taken)) << " dB";
            return t;
        };
        presetBar.refresh();
        juce::String text;
        const auto name = processor.presets().plainCurrentName();
        if (const auto* info = processor.presets().factoryInfo(name))
            text = juce::String::fromUTF8(info->group != nullptr ? info->group : "") + "|" + juce::String::fromUTF8(info->description != nullptr ? info->description : "");
        else if (name.isNotEmpty())
            text = pl("Własny|Twój preset, zapisany w folderze WERS w danych aplikacji.");
        if (text != description) {
            description = text;
            repaint(0, 64, PoglosEditor::kWidth, 30);
        }
    }

    PoglosProcessor& processor;
    ui::PresetBar presetBar;
    juce::String description;
    SpaceView space;
    DecayView decayView;
    AlgoPicker algos;
    EraPicker eras;
    Knob mix, predelay, note, decay, size, attack, balance, diffEarly, diffLate, bassMult, bassHz, damp, lowCut, highCut, modRate, modDepth, width, duck, shimmer;
    wers::ui::FlatButton freeze{ pl("Zamroź") }, bypass{ "Bypass" }, sync{ pl("Z tempem") }, interval{ "" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeLink, bypassLink, syncLink;
    std::unique_ptr<juce::ParameterAttachment> intervalLink;
    std::array<Card, 4> cards;
    juce::TooltipWindow tooltips{ this, 600 };
};

PoglosEditor::PoglosEditor(PoglosProcessor& owner) : juce::AudioProcessorEditor(owner), processor_(owner) {
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

PoglosEditor::~PoglosEditor() {
    stopTimer();
    root_.reset();
    setLookAndFeel(nullptr);
}

void PoglosEditor::paint(juce::Graphics& g) { g.fillAll(colour::bg); }

void PoglosEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(kWidth);
    root_->setTransform(juce::AffineTransform::scale(scale));
    processor_.setUiScale(scale);
}

void PoglosEditor::updateNow() { root_->tick(); }
juce::String PoglosEditor::shownDescription() const { return root_->description; }
ui::AlgoPicker& PoglosEditor::algoPicker() { return root_->algos; }
ui::EraPicker& PoglosEditor::eraPicker() { return root_->eras; }
ui::DecayView& PoglosEditor::decayView() { return root_->decayView; }
ui::PresetBar& PoglosEditor::presetBar() { return root_->presetBar; }
juce::Button& PoglosEditor::freezeButton() { return root_->freeze; }
juce::Button& PoglosEditor::syncButton() { return root_->sync; }
juce::Button& PoglosEditor::intervalButton() { return root_->interval; }
ui::Knob& PoglosEditor::predelayKnob() { return root_->predelay; }
ui::Knob& PoglosEditor::noteKnob() { return root_->note; }
ui::Knob& PoglosEditor::shimmerKnob() { return root_->shimmer; }
ui::Knob& PoglosEditor::decayKnob() { return root_->decay; }

} // namespace pog
