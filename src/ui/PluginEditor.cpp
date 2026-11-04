#include "PluginEditor.h"
#include <algorithm>

namespace wers {

using namespace ui;

// Everything is laid out at the design size and scaled as a whole.
class WersEditor::Root final : public juce::Component {
public:
    explicit Root(WersProcessor& p) : proc(p), words(p), panel(p), strip(p), beat(p), master(p) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &roles, &devices, &compare, &match, &groupButton, &words, &panel, &strip, &beat, &master, &bypass }) addChildComponent(c);
        roles.setVisible(true);
        roles.setItems({ pl("Wokal"), pl("Bit"), pl("Master") }, { 64, 52, 64 });
        devices.setItems({ pl("Telefon"), pl("Pchełki"), pl("Auto"), pl("Laptop"), pl("Studio") }, { 70, 70, 52, 66, 60 });
        groupButton.setVisible(true);
        bypass.setVisible(true);
        bypass.setClickingTogglesState(true);
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::bypass, bypass);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(colour::dark);
        g.setColour(colour::paper);
        g.setFont(displayFont(800, 32.0f));
        g.drawText("WERS", 28, 0, 96, layout::headerH, juce::Justification::centredLeft, false);
        if (devices.isVisible()) {
            g.setFont(uiFont(400, 14.0f));
            g.setColour(colour::quiet);
            g.drawText(proc.role() == Role::Master ? pl("Słucham na") : pl("Sprawdzam na"), 320, 0, 104, layout::headerH, juce::Justification::centredRight, false);
        }
    }
    void resized() override {
        roles.setBounds(128, 16, roles.preferredWidth(), 44);
        devices.setBounds(432, 16, devices.preferredWidth(), 44);
        match.setBounds(layout::width - 24 - 136, 16, 136, 44);
        compare.setBounds(match.getX() - 8 - 150, 16, 150, 44);
        groupButton.setBounds(compare.getX() - 8 - 60, 16, 60, 44);
        bypass.setBounds(groupButton.getX() - 8 - 66, 16, 66, 44);
        const int midH = layout::height - layout::headerH - layout::stripH;
        words.setBounds(0, layout::headerH, layout::width - layout::panelW, midH);
        panel.setBounds(layout::width - layout::panelW, layout::headerH, layout::panelW, midH);
        strip.setBounds(0, layout::headerH + midH, layout::width, layout::stripH);
        const juce::Rectangle<int> full(0, layout::headerH, layout::width, layout::height - layout::headerH);
        beat.setBounds(full);
        master.setBounds(full);
    }
    WersProcessor& proc;
    SegmentRow roles, devices;
    FlatButton compare{ pl("Słuchaj bez napraw"), FlatButton::Style::Outline, true };
    FlatButton match{ pl("Dopasuj do bitu") };
    FlatButton groupButton{ "Para 1" };
    FlatButton bypass{ "Bypass", FlatButton::Style::Outline, true };
    WordMapView words;
    WordPanel panel;
    ModuleStrip strip;
    BeatView beat;
    MasterView master;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::Component> lyricsOverlay;
};

namespace {
// A simple sheet for pasting lyrics.
class LyricsOverlay final : public juce::Component {
public:
    LyricsOverlay(const juce::String& text, std::function<void(const juce::String&)> onSave, std::function<void()> onClose) : save_(std::move(onSave)), close_(std::move(onClose)) {
        editor.setMultiLine(true);
        editor.setReturnKeyStartsNewLine(true);
        editor.setFont(uiFont(400, 18.0f));
        editor.setText(text, false);
        editor.setTitle(pl("Tekst numeru"));
        addAndMakeVisible(editor);
        addAndMakeVisible(saveButton);
        addAndMakeVisible(cancelButton);
        saveButton.setTextSize(20.0f);
        saveButton.onClick = [this] { save_(editor.getText()); close_(); };
        cancelButton.onClick = [this] { close_(); };
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(colour::ground.withAlpha(0.97f));
        g.setColour(colour::paper);
        g.setFont(displayFont(600, 30.0f));
        g.drawText(pl("Tekst numeru"), 40, 24, 500, 40, juce::Justification::centredLeft, false);
        g.setFont(uiFont(400, 15.0f));
        g.setColour(colour::legend);
        g.drawText(pl("Wklej tekst, a WERS poprawi pisownię rozpoznanych słów."), 40, 62, getWidth() - 80, 24, juce::Justification::centredLeft, false);
    }
    void resized() override {
        editor.setBounds(40, 96, getWidth() - 80, getHeight() - 96 - 84);
        saveButton.setBounds(40, getHeight() - 68, 200, 48);
        cancelButton.setBounds(248, getHeight() - 68, 110, 48);
    }
    juce::TextEditor editor;
private:
    FlatButton saveButton{ pl("Zapisz tekst"), FlatButton::Style::Solid }, cancelButton{ pl("Anuluj"), FlatButton::Style::Link };
    std::function<void(const juce::String&)> save_;
    std::function<void()> close_;
};
} // namespace

WersEditor::WersEditor(WersProcessor& p) : juce::AudioProcessorEditor(p), proc_(p) {
    setLookAndFeel(&look_);
    root_ = std::make_unique<Root>(p);
    addAndMakeVisible(*root_);
    root_->setBounds(0, 0, layout::width, layout::height);

    root_->roles.onPick = [this](int i) { proc_.chooseRole(static_cast<Role>(i)); applyRoleLayout(); };
    root_->devices.onPick = [this](int i) {
        const char* id = proc_.role() == Role::Master ? ids::monDevice : ids::viewDevice;
        if (auto* param = proc_.state().getParameter(id)) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(i)));
            param->endChangeGesture();
        }
        selected_ = -1;
    };
    root_->compare.onClick = [this] {
        if (auto* param = proc_.state().getParameter(ids::fixes)) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
            param->endChangeGesture();
        }
    };
    root_->match.onClick = [this] {
        const auto s = proc_.session().suggestSettings();
        if (s.valid) proc_.applySuggestion(s);
    };
    root_->groupButton.onClick = [this] { proc_.cycleGroup(); };
    root_->words.onSelect = [this](int i) { selected_ = i; pushResult(); };
    root_->words.onEditLyrics = [this] { openLyricsEditor(); };

    const float scale = juce::jlimit(0.6f, 1.6f, proc_.uiScale());
    setResizable(true, true);
    setResizeLimits(juce::roundToInt(layout::width * 0.6f), juce::roundToInt(layout::height * 0.6f), juce::roundToInt(layout::width * 1.6f), juce::roundToInt(layout::height * 1.6f));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(static_cast<double>(layout::width) / layout::height);
    setSize(juce::roundToInt(layout::width * scale), juce::roundToInt(layout::height * scale));
    applyRoleLayout();
    timerCallback();
    startTimerHz(30);
}

WersEditor::~WersEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void WersEditor::paint(juce::Graphics& g) { g.fillAll(colour::dark); }

void WersEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(layout::width);
    root_->setTransform(juce::AffineTransform::scale(scale));
    proc_.setUiScale(scale);
}

Device WersEditor::viewDevice() const {
    return static_cast<Device>(juce::jlimit(0, kNumDevices - 1, static_cast<int>(proc_.state().getRawParameterValue(ids::viewDevice)->load())));
}

void WersEditor::applyRoleLayout() {
    shownRole_ = proc_.role();
    const bool vocal = shownRole_ == Role::Vocal, beat = shownRole_ == Role::Beat, master = shownRole_ == Role::Master;
    root_->words.setVisible(vocal);
    root_->panel.setVisible(vocal);
    root_->strip.setVisible(vocal);
    root_->beat.setVisible(beat);
    root_->master.setVisible(master);
    root_->devices.setVisible(vocal || master);
    root_->compare.setVisible(vocal);
    root_->match.setVisible(vocal);
    root_->roles.setSelected(static_cast<int>(shownRole_));
    root_->repaint();
}

int WersEditor::pickDefaultWord() const {
    if (!shownResult_ || shownResult_->words.empty()) return -1;
    // Start on the word that suffers most: that is why someone opened the plugin.
    const auto d = static_cast<size_t>(viewDevice());
    int best = 0;
    for (int i = 1; i < static_cast<int>(shownResult_->words.size()); ++i)
        if (shownResult_->words[static_cast<size_t>(i)].verdict[d].score < shownResult_->words[static_cast<size_t>(best)].verdict[d].score) best = i;
    return best;
}

void WersEditor::pushResult() {
    const Device d = viewDevice();
    root_->words.refresh(shownResult_, d, selected_);
    root_->panel.refresh(shownResult_, d, selected_);
}

void WersEditor::timerCallback() {
    if (proc_.role() != shownRole_) applyRoleLayout();
    const int deviceParam = static_cast<int>(proc_.state().getRawParameterValue(proc_.role() == Role::Master ? ids::monDevice : ids::viewDevice)->load());
    root_->devices.setSelected(deviceParam);
    const bool fixesOn = proc_.state().getRawParameterValue(ids::fixes)->load() > 0.5f;
    root_->compare.setToggleState(!fixesOn, juce::dontSendNotification);
    root_->compare.setButtonText(fixesOn ? pl("Słuchaj bez napraw") : pl("Włącz naprawy"));
    root_->groupButton.setButtonText("Para " + juce::String(proc_.groupNumber()));

    switch (proc_.role()) {
        case Role::Vocal: {
            auto& s = proc_.session();
            const auto gen = s.generation();
            if (gen != lastGeneration_ || deviceParam != lastDevice_) {
                lastGeneration_ = gen;
                lastDevice_ = deviceParam;
                shownResult_ = s.result();
                if (shownResult_ && (selected_ < 0 || selected_ >= static_cast<int>(shownResult_->words.size()))) selected_ = pickDefaultWord();
                if (!shownResult_) selected_ = -1;
                pushResult();
            }
            root_->words.tick();
            root_->panel.tick();
            root_->strip.tick();
            root_->match.setEnabled(s.phase() == SessionPhase::Ready);
            break;
        }
        case Role::Beat: root_->beat.tick(); break;
        case Role::Master: root_->master.tick(); break;
    }
}

void WersEditor::openLyricsEditor() {
    auto overlay = std::make_unique<LyricsOverlay>(
        juce::String::fromUTF8(proc_.session().lyrics().c_str()),
        [this](const juce::String& text) {
            auto& session = proc_.session();
            session.setLyrics(text.toStdString());
            // The song is still in memory, so the corrected text can appear without another listen.
            const auto result = session.result();
            const bool anyFix = result != nullptr && std::any_of(result->words.begin(), result->words.end(), [](const Word& w) { return w.fix.active; });
            if (session.canAnalyseAgain() && proc_.model().state() == ModelManager::State::Ready && !anyFix) session.analyseAgain();
        },
        [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<WersEditor>(this)] { if (safe != nullptr) safe->root_->lyricsOverlay.reset(); }); });
    overlay->setBounds(0, 0, layout::width, layout::height);
    root_->addAndMakeVisible(*overlay);
    root_->lyricsOverlay = std::move(overlay);
}

} // namespace wers
