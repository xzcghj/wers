#include "DociskEditor.h"
#include "plugin/DociskProcessor.h"
#include "SharedText.h"

namespace doc {

using namespace ui;

namespace {

constexpr int kPanelY = 432, kPanelH = 180, kKnobW = 72, kPad = 13, kGap = 12;
const float kQuickRatios[] = { 1.5f, 2.0f, 4.0f, 8.0f, kRatioInfinite };

// A titled frame behind a group of knobs. The note on the right says what the group is doing now.
class Panel final : public juce::Component {
public:
    explicit Panel(const juce::String& title) : title_(title) { setInterceptsMouseClicks(false, false); }
    void setNote(const juce::String& note, juce::Colour c) {
        if (note != note_ || c != noteColour_) {
            note_ = note;
            noteColour_ = c;
            repaint();
        }
    }
    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        g.setColour(colour::surface);
        g.fillRoundedRectangle(r, 12.0f);
        g.setColour(colour::hairline.withAlpha(0.6f));
        g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, 1.0f);
        g.setFont(uiFont(600, 12.0f));
        g.setColour(colour::dim);
        g.drawText(title_, juce::Rectangle<float>(14.0f, 6.0f, r.getWidth() - 28.0f, 18.0f), juce::Justification::centredLeft);
        if (note_.isNotEmpty()) {
            g.setFont(uiFont(500, 11.0f));
            g.setColour(noteColour_);
            g.drawText(note_, juce::Rectangle<float>(14.0f, 6.0f, r.getWidth() - 28.0f, 18.0f), juce::Justification::centredRight);
        }
    }
private:
    juce::String title_, note_;
    juce::Colour noteColour_;
};

// One line of quiet text that sits on top of a panel.
class Caption final : public juce::Component {
public:
    Caption() { setInterceptsMouseClicks(false, false); }
    void setText(const juce::String& t) {
        if (t != text_) {
            text_ = t;
            repaint();
        }
    }
    void paint(juce::Graphics& g) override {
        g.setFont(uiFont(500, 11.0f));
        g.setColour(colour::faint);
        g.drawFittedText(text_, getLocalBounds(), juce::Justification::centred, 2, 0.8f);
    }
private:
    juce::String text_;
};

} // namespace

class DociskEditor::Root final : public juce::Component {
public:
    explicit Root(DociskProcessor& p)
        : processor(p), presetBar(p.presets()), history(p.state(), p.compressor().history), curve(p.state()),
          threshold(p.state(), ids::threshold, pl("Próg"), hue::threshold), ratio(p.state(), ids::ratio, pl("Ratio"), colour::accent),
          knee(p.state(), ids::knee, pl("Kolano"), colour::accent), range(p.state(), ids::range, pl("Zakres"), hue::reduction),
          attack(p.state(), ids::attack, pl("Atak"), colour::accent), release(p.state(), ids::release, pl("Powrót"), colour::accent),
          lookahead(p.state(), ids::lookahead, pl("Wyprzedzenie"), colour::accent),
          detector(p.state(), ids::detector, pl("Szczyt / RMS"), hue::key), link(p.state(), ids::link, pl("Łączenie"), hue::key),
          highPass(p.state(), ids::keyHighPass, pl("Filtr SC"), hue::key), tilt(p.state(), ids::keyTilt, pl("Barwa SC"), hue::key, true),
          makeup(p.state(), ids::makeup, pl("Zysk"), colour::accent, true), mix(p.state(), ids::mix, pl("Miks"), colour::accent),
          output(p.state(), ids::output, pl("Wyjście"), colour::accent, true) {
        for (auto* c : std::initializer_list<juce::Component*>{ &curvePanel, &timePanel, &keyPanel, &outPanel })
            addAndMakeVisible(c);
        for (auto* c : std::initializer_list<juce::Component*>{ &presetBar, &history, &curve, &meters, &character, &bypass,
                                                               &threshold, &ratio, &knee, &range, &quickRatio, &attack, &release, &autoRelease,
                                                               &lookahead, &detector, &link, &highPass, &tilt, &external, &listen,
                                                               &makeup, &autoMakeup, &mix, &output, &clip, &latencyCaption })
            addAndMakeVisible(c);

        threshold.setTip(pl("Od jakiego poziomu kompresor zaczyna pracować. Można go też przeciągać na krzywej i w historii."));
        ratio.setTip(pl("Jak mocno ściska: przy 4:1 cztery decybele ponad progiem robią się jednym. Na samym dole, poniżej 1:1, "
                        "głośne miejsca rosną: ekspansja w górę."));
        knee.setTip(pl("Szerokość łagodnego przejścia wokół progu. Zero to ostry próg, szerokie kolano wchodzi niezauważalnie."));
        range.setTip(pl("Najwięcej, ile kompresor może zabrać (albo dodać przy ekspansji). Na samej górze bez limitu."));
        attack.setTip(pl("Jak szybko kompresor łapie: krótki atak tłumi uderzenia, długi je przepuszcza."));
        release.setTip(pl("Jak szybko kompresor puszcza, kiedy dźwięk ścichnie."));
        lookahead.setTip(pl("Kompresor słyszy dźwięk wcześniej i zdąży z redukcją przed uderzeniem. Dodaje tyle samo opóźnienia, "
                            "które DAW wyrównuje."));
        detector.setTip(pl("Czego słucha detektor: szczytów (szybko, łapie każde uderzenie) czy średniej głośności RMS (spokojniej, bliżej ucha)."));
        link.setTip(pl("100%: oba kanały ścisza ten głośniejszy, obraz stereo stoi w miejscu. 0%: każdy kanał osobno."));
        highPass.setTip(pl("Detektor nie słyszy basu poniżej tej częstotliwości, więc stopa i bas nie pompują całością."));
        tilt.setTip(pl("Przechyla to, co słyszy detektor: w prawo bardziej reaguje na górę, w lewo na dół."));
        makeup.setTip(pl("Podnosi głośność po kompresji."));
        mix.setTip(pl("Ile skompresowanego sygnału w wyjściu. Poniżej 100% to kompresja równoległa: oryginał zostaje pod spodem."));
        output.setTip(pl("Głośność na samym końcu, przed miękkim clipem."));

        setupSwitch(autoRelease, ids::autoRelease, "Auto", "Powrót zależny od materiału: krótkie uderzenia puszcza szybko, długą kompresję powoli. Pokrętło ustawia długi powrót.", autoReleaseLink);
        setupSwitch(autoMakeup, ids::autoMakeup, "Auto", "Sam oddaje mniej więcej tyle, ile kompresja zabrała: połowę tego, co traci głośny sygnał na -10 dBFS. Pokrętło dodaje swoje.", autoMakeupLink);
        setupSwitch(clip, ids::clip, "Clip 0 dB", "Miękki clip na samym wyjściu, czterokrotnie nadpróbkowany. Do -1,5 dBFS nic nie rusza, wyżej łagodnie zaokrągla szczyty.", clipLink);
        setupSwitch(external, ids::external, "Zewnętrzny", "Detektor słucha wejścia sidechain zamiast samego śladu. Podłącz sygnał w DAW.", externalLink);
        setupSwitch(listen, ids::listen, "Słuchaj SC", "Słyszysz to, co słyszy detektor, z filtrem i barwą. Nie zapisuje się w presetach.", listenLink);
        listen.setOnFill(colour::weak, colour::ink);
        external.setOnFill(hue::key, colour::ink);

        bypass.setClickingTogglesState(true);
        bypass.setOnFill(colour::weak, colour::ink);
        bypass.setTooltip(pl("Czysty dźwięk, z płynnym przejściem i tym samym opóźnieniem."));
        bypassLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypass);

        character.setTextSize(12.5f);
        character.setItems({ pl("Czysty"), pl("Punch"), pl("Klej") }, { 74, 66, 58 });
        character.setItemTooltip(0, pl("Czysty: detektor w przód, czasy dokładnie takie, jak na pokrętłach. Przezroczysty i przewidywalny."));
        character.setItemTooltip(1, pl("Punch: atak w dwóch stopniach, więc pierwsze milisekundy uderzenia przechodzą, a potem łapie mocno."));
        character.setItemTooltip(2, pl("Klej: sprzężenie zwrotne, detektor słucha już ściśniętego sygnału. Łagodniej, bardziej spójnie."));
        characterLink = std::make_unique<juce::ParameterAttachment>(*p.state().getParameter(ids::character), [this](float v) { character.setSelected(juce::roundToInt(v)); }, nullptr);
        character.onPick = [this](int i) { characterLink->setValueAsCompleteGesture(static_cast<float>(i)); };
        characterLink->sendInitialUpdate();

        quickRatio.setTextSize(12.0f);
        quickRatio.setItems({ "1,5", "2", "4", "8", juce::String::fromUTF8("∞") }, { 40, 34, 34, 34, 34 });
        quickRatio.setTooltip(pl("Szybki wybór ratio."));
        quickRatio.onPick = [this](int i) { pickRatio(i); };
    }

    void setupSwitch(wers::ui::FlatButton& b, const char* id, const char* text, const char* tip, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& attachment) {
        b.setButtonText(pl(text));
        b.setClickingTogglesState(true);
        b.setOnFill(colour::accent, colour::ink);
        b.setTextSize(12.5f);
        b.setTooltip(pl(tip));
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state(), id, b);
    }

    void pickRatio(int i) {
        auto* p = processor.state().getParameter(ids::ratio);
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(kQuickRatios[juce::jlimit(0, 4, i)]));
        p->endChangeGesture();
    }

    void resized() override {
        presetBar.setBounds(222, 12, 500, 36);
        bypass.setBounds(DociskEditor::kWidth - 16 - 84, 12, 84, 36);
        character.setBounds(bypass.getX() - 24 - character.preferredWidth(), 15, character.preferredWidth(), 30);
        history.setBounds(16, 78, 676, 340);
        curve.setBounds(704, 78, 344, 340);
        meters.setBounds(1060, 78, 104, 340);

        int x = 16;
        auto panel = [&](Panel& frame, std::initializer_list<Knob*> knobs) {
            const int w = static_cast<int>(knobs.size()) * kKnobW + 2 * kPad;
            frame.setBounds(x, kPanelY, w, kPanelH);
            int kx = x + kPad;
            for (auto* k : knobs) {
                k->setBounds(kx, kPanelY + 28, kKnobW, 104);
                kx += kKnobW;
            }
            x += w + kGap;
        };
        panel(curvePanel, { &threshold, &ratio, &knee, &range });
        panel(timePanel, { &attack, &release, &lookahead });
        panel(keyPanel, { &detector, &link, &highPass, &tilt });
        panel(outPanel, { &makeup, &mix, &output });
        const int by = kPanelY + 140;
        quickRatio.setBounds(curvePanel.getX() + (curvePanel.getWidth() - quickRatio.preferredWidth()) / 2, by, quickRatio.preferredWidth(), 26);
        autoRelease.setBounds(release.getX() + 6, by, kKnobW - 12, 26);
        external.setBounds(keyPanel.getX() + kPad + 6, by, 2 * kKnobW - 12, 26);
        listen.setBounds(keyPanel.getX() + kPad + 2 * kKnobW + 6, by, 2 * kKnobW - 12, 26);
        autoMakeup.setBounds(makeup.getX() + 6, by, kKnobW - 12, 26);
        clip.setBounds(output.getX() - 10, by, kKnobW + 16, 26);
        latencyCaption.setBounds(lookahead.getX() - 4, by - 2, kKnobW + 8, 30);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::bg);
        g.setColour(colour::surface);
        g.fillRect(0, 0, DociskEditor::kWidth, 64);
        g.setColour(colour::hairline);
        g.fillRect(0, 64, DociskEditor::kWidth, 1);
        g.setFont(displayFont(800, 26.0f));
        g.setColour(colour::text);
        g.drawText("WERS", 22, 14, 80, 36, juce::Justification::centredLeft);
        const int wersW = juce::GlyphArrangement::getStringWidthInt(displayFont(800, 26.0f), "WERS");
        g.setFont(displayFont(400, 26.0f));
        g.setColour(colour::accent);
        g.drawText("Docisk", 22 + wersW + 5, 14, 140, 36, juce::Justification::centredLeft);
        g.setFont(uiFont(600, 12.0f));
        g.setColour(colour::dim);
        g.drawText(pl("CHARAKTER"), juce::Rectangle<int>(character.getX() - 96, 15, 88, 30), juce::Justification::centredRight);
    }

    void tick() {
        auto& comp = processor.compressor();
        auto& m = comp.meters;
        const float inL = Meters::take(m.inPeak[0]), inR = Meters::take(m.inPeak[1]);
        const float outL = Meters::take(m.outPeak[0]), outR = Meters::take(m.outPeak[1]);
        const float hold = Meters::take(m.gainHoldDb);
        const float now = m.gainDb.load(std::memory_order_relaxed);
        const float gain = hold < -0.01f ? juce::jmin(hold, now) : now;
        meters.update(inL, inR, outL, outR, gain, Meters::take(m.clipDb));
        curve.setSignal(m.levelDb.load(std::memory_order_relaxed), now);
        curve.tick();
        history.tick();
        for (auto* k : { &threshold, &ratio, &knee, &range, &attack, &release, &lookahead, &detector, &link, &highPass, &tilt, &makeup, &mix, &output })
            k->refresh();

        // Ratio shortcuts light up when the ratio is one of them.
        const float r = processor.state().getRawParameterValue(ids::ratio)->load();
        int pick = -1;
        for (int i = 0; i < 5; ++i)
            if (std::abs(r - kQuickRatios[i]) < 0.02f * kQuickRatios[i]) pick = i;
        if (pick != quickRatio.selected()) quickRatio.setSelected(pick);

        // Notes that say what the sidechain is doing.
        const bool ext = processor.state().getRawParameterValue(ids::external)->load() > 0.5f;
        const bool heard = ext && processor.sidechainConnected();
        if (ext && !heard) keyPanel.setNote(pl("brak sygnału SC"), colour::weak);
        else if (heard) keyPanel.setNote(pl("słucha sidechainu"), hue::key);
        else keyPanel.setNote({}, colour::faint);
        const bool listening = processor.state().getRawParameterValue(ids::listen)->load() > 0.5f;
        history.setNotes(listening ? pl("słyszysz sidechain, nie wynik") : juce::String());

        auto value = [this](const char* id) { return processor.state().getRawParameterValue(id)->load(); };
        const float autoGain = value(ids::autoMakeup) > 0.5f
                                   ? autoMakeupDb(value(ids::threshold), slopeForRatio(value(ids::ratio)), value(ids::knee), rangeLimitDb(value(ids::range)))
                                   : 0.0f;
        outPanel.setNote(autoGain != 0.0f ? pl("auto ") + (autoGain > 0.0f ? "+" : "") + juce::String(autoGain, 1).replaceCharacter('.', ',') + " dB" : juce::String(),
                         colour::accent);
        timePanel.setNote(character.selected() == 1 ? pl("atak dwustopniowy") : character.selected() == 2 ? pl("sprzężenie zwrotne") : juce::String(), colour::faint);

        const int latency = processor.getLatencySamples();
        // How much the lookahead delays everything, under its knob.
        latencyCaption.setText(latency == 0 ? pl("bez opóźnienia") : pl("opóźnienie") + juce::String::charToString(static_cast<juce::juce_wchar>(10)) + juce::String(latency) + pl(" próbek"));
        presetBar.refresh();
    }

    DociskProcessor& processor;
    wers::ui::PresetBar presetBar;
    HistoryView history;
    CurveView curve;
    MeterStrip meters;
    Panel curvePanel{ pl("KRZYWA") }, timePanel{ pl("CZAS") }, keyPanel{ pl("DETEKTOR I SIDECHAIN") }, outPanel{ pl("WYJŚCIE") };
    Knob threshold, ratio, knee, range, attack, release, lookahead, detector, link, highPass, tilt, makeup, mix, output;
    wers::ui::SegmentRow character, quickRatio;
    wers::ui::FlatButton bypass{ "Bypass" }, autoRelease{ "" }, autoMakeup{ "" }, clip{ "" }, external{ "" }, listen{ "" };
    std::unique_ptr<juce::ParameterAttachment> characterLink;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassLink, autoReleaseLink, autoMakeupLink, clipLink, externalLink, listenLink;
    Caption latencyCaption;
    juce::TooltipWindow tooltips{ this, 600 };
};

DociskEditor::DociskEditor(DociskProcessor& owner) : juce::AudioProcessorEditor(owner), processor_(owner) {
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

DociskEditor::~DociskEditor() {
    stopTimer();
    root_.reset();
    setLookAndFeel(nullptr);
}

void DociskEditor::paint(juce::Graphics& g) { g.fillAll(colour::bg); }

void DociskEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(kWidth);
    root_->setTransform(juce::AffineTransform::scale(scale));
    processor_.setUiScale(scale);
}

void DociskEditor::updateNow() { root_->tick(); }
ui::CurveView& DociskEditor::curve() { return root_->curve; }
ui::HistoryView& DociskEditor::history() { return root_->history; }
ui::MeterStrip& DociskEditor::meters() { return root_->meters; }
wers::ui::PresetBar& DociskEditor::presetBar() { return root_->presetBar; }
void DociskEditor::pickRatio(int index) { root_->pickRatio(index); }

} // namespace doc
