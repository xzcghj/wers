#include "BarwaEditor.h"
#include "Strings.h"

namespace barwa {

using namespace wers::ui;

namespace {

namespace layout {
constexpr int width = 1040, height = 680, headerH = 64, spectrumH = 296, stripH = 56;
constexpr int panelY = headerH + spectrumH + stripH, panelH = height - panelY;
constexpr int margin = 12, gap = 8, wholeW = 320;
} // namespace layout

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

// Connects a row of choices to a parameter, in both directions.
class ChoiceLink {
public:
    ChoiceLink(juce::RangedAudioParameter& parameter, std::function<void(int)> show)
        : attachment_(parameter, [show = std::move(show)](float value) { show(juce::roundToInt(value)); }) {
        attachment_.sendInitialUpdate();
    }
    void pick(int index) { attachment_.setValueAsCompleteGesture(static_cast<float>(index)); }
private:
    juce::ParameterAttachment attachment_;
};

void returnToDefaultOnDoubleClick(juce::Slider& slider, juce::RangedAudioParameter& parameter) {
    slider.setDoubleClickReturnValue(true, static_cast<double>(parameter.convertFrom0to1(parameter.getDefaultValue())));
}

// Four characters in one row when there is room for it, in two rows otherwise.
class CharacterPicker final : public juce::Component {
public:
    std::function<void(int)> onPick;
    CharacterPicker() {
        for (int i = 0; i < 4; ++i) {
            auto* b = buttons_.add(new FlatButton(pl(names()[static_cast<size_t>(i)]), FlatButton::Style::Segment));
            b->onClick = [this, i] { if (onPick) onPick(i); };
            addAndMakeVisible(b);
        }
    }
    void setSelected(int index) {
        for (int i = 0; i < 4; ++i) buttons_[i]->setToggleState(i == index, juce::dontSendNotification);
    }
    static int preferredHeight(int width) { return width >= 400 ? 46 : 62; }
    void resized() override {
        auto area = getLocalBounds().reduced(3);
        if (getWidth() >= 400) {
            // Widths follow the names, so the long ones are not squeezed.
            const std::array<float, 4> shares{ 0.2f, 0.2f, 0.32f, 0.28f };
            float x = static_cast<float>(area.getX());
            for (int i = 0; i < 4; ++i) {
                const float w = static_cast<float>(area.getWidth()) * shares[static_cast<size_t>(i)];
                buttons_[i]->setBounds(juce::roundToInt(x), area.getY(), juce::roundToInt(w) - 2, area.getHeight());
                x += w;
            }
            return;
        }
        const int rowH = (area.getHeight() - 2) / 2;
        auto top = area.removeFromTop(rowH);
        area.removeFromTop(2);
        const int firstColumn = juce::roundToInt(static_cast<float>(top.getWidth()) * 0.42f);
        buttons_[0]->setBounds(top.removeFromLeft(firstColumn).withTrimmedRight(1));
        buttons_[1]->setBounds(top.withTrimmedLeft(1));
        // The names of this pair are longer than the pair above, so they split evenly.
        buttons_[2]->setBounds(area.removeFromLeft(area.getWidth() / 2 + 8).withTrimmedRight(1));
        buttons_[3]->setBounds(area.withTrimmedLeft(1));
    }
    void paint(juce::Graphics& g) override {
        g.setColour(colour::bg);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    }
private:
    static const std::array<const char*, 4>& names() {
        static const std::array<const char*, 4> list{ text::lamp, text::tape, text::transformer, text::transistor };
        return list;
    }
    juce::OwnedArray<FlatButton> buttons_;
};

// The controls of one band.
class BandColumn final : public juce::Component {
public:
    BandColumn(BarwaProcessor& p, int index, const char* characterId, const char* driveId, const char* mixId, const char* soloId)
        : index_(index), processor_(p) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &solo, &picker, &drive, &mix }) addAndMakeVisible(c);
        auto& state = p.state();
        solo.setClickingTogglesState(true);
        solo.setTextSize(12.0f);
        solo.setOnFill(colour::accent, colour::ink);
        solo.setTooltip(pl(text::soloTip));
        soloLink = std::make_unique<ButtonAttachment>(state, soloId, solo);
        driveLink = std::make_unique<SliderAttachment>(state, driveId, drive.slider);
        mixLink = std::make_unique<SliderAttachment>(state, mixId, mix.slider);
        returnToDefaultOnDoubleClick(drive.slider, *state.getParameter(driveId));
        returnToDefaultOnDoubleClick(mix.slider, *state.getParameter(mixId));
        characterLink = std::make_unique<ChoiceLink>(*state.getParameter(characterId), [this](int i) { picker.setSelected(i); });
        picker.onPick = [this](int i) { characterLink->pick(i); };
    }
    void setRange(const juce::String& title, const juce::String& range) {
        if (title != title_ || range != range_) {
            title_ = title;
            range_ = range;
            repaint();
        }
    }
    void paint(juce::Graphics& g) override {
        g.setColour(colour::surface);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);
        g.setFont(displayFont(600, 17.0f));
        g.setColour(colour::text);
        g.drawText(title_, 14, 8, getWidth() - 80, 22, juce::Justification::centredLeft, false);
        g.setFont(uiFont(400, 12.0f));
        g.setColour(colour::dim);
        g.drawText(range_, 14, 30, getWidth() - 28, 16, juce::Justification::centredLeft, false);
    }
    void resized() override {
        solo.setBounds(getWidth() - 14 - 48, 10, 48, 26);
        const int pickerH = CharacterPicker::preferredHeight(getWidth());
        picker.setBounds(14, 52, getWidth() - 28, pickerH);
        const int sliderH = 48;
        mix.setBounds(14, getHeight() - 8 - sliderH, getWidth() - 28, sliderH);
        drive.setBounds(14, mix.getY() - sliderH, getWidth() - 28, sliderH);
    }
    const int index_;
    BarwaProcessor& processor_;
    FlatButton solo{ pl(text::solo).toUpperCase().substring(0, 1) + pl(text::solo).substring(1) };
    CharacterPicker picker;
    LabeledSlider drive{ pl(text::drive), " dB" };
    LabeledSlider mix{ pl(text::mix) };
private:
    std::unique_ptr<ButtonAttachment> soloLink;
    std::unique_ptr<SliderAttachment> driveLink, mixLink;
    std::unique_ptr<ChoiceLink> characterLink;
    juce::String title_, range_;
};

} // namespace

// Everything is laid out at the design size and scaled as a whole.
class BarwaEditor::Root final : public juce::Component {
public:
    explicit Root(BarwaProcessor& p)
        : processor(p), presetBar(p.presets()), spectrum(p),
          column1(p, 0, ids::char1, ids::drive1, ids::bandMix1, ids::solo1),
          column2(p, 1, ids::char2, ids::drive2, ids::bandMix2, ids::solo2),
          column3(p, 2, ids::char3, ids::drive3, ids::bandMix3, ids::solo3) {
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &bandRow, &presetBar, &delta, &bypass, &spectrum, &strip, &column1, &column2, &column3, &mask, &tone, &mix, &output, &autoLevel, &quality }) addAndMakeVisible(c);
        auto& state = p.state();

        bandRow.setItems({ "1", "2", "3" }, { 34, 34, 34 });
        bandRow.setTooltip(pl(text::bandsTip));
        bandLink = std::make_unique<ChoiceLink>(*state.getParameter(ids::bands), [this](int i) { bandRow.setSelected(i); });
        bandRow.onPick = [this](int i) { bandLink->pick(i); };

        quality.setItems({ "2x", "4x", "8x" }, { 38, 38, 38 });
        quality.setTooltip(pl(text::qualityTip));
        qualityLink = std::make_unique<ChoiceLink>(*state.getParameter(ids::quality), [this](int i) { quality.setSelected(i); });
        quality.onPick = [this](int i) { qualityLink->pick(i); };

        delta.setClickingTogglesState(true);
        delta.setOnFill(colour::accent, colour::ink);
        delta.setTooltip(pl(text::deltaTip));
        deltaLink = std::make_unique<ButtonAttachment>(state, ids::delta, delta);
        bypass.setClickingTogglesState(true);
        bypass.setTooltip(pl(text::bypassTip));
        bypassLink = std::make_unique<ButtonAttachment>(state, ids::bypass, bypass);
        autoLevel.setClickingTogglesState(true);
        autoLevel.setOnFill(colour::raised, colour::text);
        autoLevel.setTooltip(pl(text::autoLevelTip));
        autoLink = std::make_unique<ButtonAttachment>(state, ids::autoLevel, autoLevel);

        mask.slider.setTooltip(pl(text::maskTip));
        tone.slider.setTooltip(pl(text::toneTip));
        const std::array<std::pair<LabeledSlider*, const char*>, 4> sliders{ { { &mask, ids::mask }, { &tone, ids::tone }, { &mix, ids::mix }, { &output, ids::output } } };
        for (size_t i = 0; i < sliders.size(); ++i) {
            wholeLinks[i] = std::make_unique<SliderAttachment>(state, sliders[i].second, sliders[i].first->slider);
            returnToDefaultOnDoubleClick(sliders[i].first->slider, *state.getParameter(sliders[i].second));
        }
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::bg);
        g.setColour(colour::surface);
        g.fillRect(0, 0, getWidth(), layout::headerH);
        g.setColour(colour::hairline);
        g.fillRect(0, layout::headerH - 1, getWidth(), 1);
        g.setColour(colour::text);
        g.setFont(displayFont(800, 26.0f));
        g.drawText("WERS", 24, 0, 78, layout::headerH, juce::Justification::centredLeft, false);
        g.setFont(displayFont(400, 26.0f));
        g.drawText("Barwa", 102, 0, 82, layout::headerH, juce::Justification::centredLeft, false);
        g.setFont(uiFont(400, 14.0f));
        g.setColour(colour::dim);
        g.drawText(pl(text::bandsShort), 196, 0, 44, layout::headerH, juce::Justification::centredRight, false);

        const auto whole = wholeArea().toFloat();
        g.setColour(colour::surface);
        g.fillRoundedRectangle(whole, 10.0f);
        g.setFont(displayFont(600, 17.0f));
        g.setColour(colour::text);
        g.drawText(pl(text::whole), juce::roundToInt(whole.getX()) + 14, juce::roundToInt(whole.getY()) + 8, 200, 22, juce::Justification::centredLeft, false);
        g.setFont(uiFont(400, 14.0f));
        g.setColour(colour::dim);
        g.drawText(pl(text::quality), juce::roundToInt(whole.getX()) + 14, quality.getY(), 60, quality.getHeight(), juce::Justification::centredLeft, false);
    }

    juce::Rectangle<int> wholeArea() const {
        return { layout::width - layout::margin - layout::wholeW, layout::panelY + layout::gap, layout::wholeW, layout::panelH - layout::gap - layout::margin };
    }

    void resized() override {
        constexpr int y = 14, h = 36;
        bandRow.setBounds(246, y, bandRow.preferredWidth(), h);
        bypass.setBounds(layout::width - 24 - 66, y, 66, h);
        delta.setBounds(bypass.getX() - 6 - 60, y, 60, h);
        presetBar.setBounds(bandRow.getRight() + 16, y, delta.getX() - 16 - bandRow.getRight() - 16, h);
        spectrum.setBounds(0, layout::headerH, layout::width, layout::spectrumH);
        strip.setBounds(0, layout::headerH + layout::spectrumH, layout::width, layout::stripH);

        const auto whole = wholeArea();
        const int inner = whole.getWidth() - 28, half = (inner - 12) / 2, x = whole.getX() + 14;
        mask.setBounds(x, whole.getY() + 34, inner, 48);
        tone.setBounds(x, mask.getBottom() + 2, half, 48);
        mix.setBounds(x + half + 12, tone.getY(), half, 48);
        output.setBounds(x, tone.getBottom() + 2, half, 48);
        autoLevel.setBounds(x + half + 12, output.getY() + 12, half, 32);
        quality.setBounds(whole.getRight() - 14 - quality.preferredWidth(), output.getBottom() + 4, quality.preferredWidth(), 34);
        layoutColumns();
    }

    void layoutColumns() {
        const int bands = juce::roundToInt(processor.state().getRawParameterValue(ids::bands)->load()) + 1;
        const auto whole = wholeArea();
        const int total = whole.getX() - layout::gap - layout::margin;
        const int columnW = (total - (bands - 1) * layout::gap) / bands;
        const std::array<BandColumn*, 3> columns{ &column1, &column2, &column3 };
        for (int i = 0; i < 3; ++i) {
            auto* column = columns[static_cast<size_t>(i)];
            column->setVisible(i < bands);
            column->setBounds(layout::margin + i * (columnW + layout::gap), whole.getY(), columnW, whole.getHeight());
        }
        shownBands = bands;
    }

    void updateColumnTexts() {
        auto& state = processor.state();
        const float f1 = state.getRawParameterValue(ids::xover1)->load(), f2 = state.getRawParameterValue(ids::xover2)->load();
        const auto f1Text = frequencyLabel(static_cast<double>(f1)), f2Text = frequencyLabel(static_cast<double>(f2));
        const auto title = [](int n) { return pl(text::band) + " " + juce::String(n); };
        if (shownBands == 1) {
            column1.setRange(pl(text::wholeBand), pl(text::wholeBandText));
        } else if (shownBands == 2) {
            column1.setRange(title(1), pl(text::upTo) + " " + f1Text);
            column2.setRange(title(2), pl(text::from) + " " + f1Text);
        } else {
            column1.setRange(title(1), pl(text::upTo) + " " + f1Text);
            column2.setRange(title(2), f1Text + juce::String::fromUTF8(" – ") + f2Text);
            column3.setRange(title(3), pl(text::from) + " " + f2Text);
        }
    }

    BarwaProcessor& processor;
    SegmentRow bandRow, quality;
    PresetBar presetBar;
    FlatButton delta{ pl(text::delta), FlatButton::Style::Outline };
    FlatButton bypass{ pl(text::bypass), FlatButton::Style::Outline };
    FlatButton autoLevel{ pl(text::autoLevel), FlatButton::Style::Outline };
    SpectrumView spectrum;
    MaskStrip strip;
    BandColumn column1, column2, column3;
    LabeledSlider mask{ pl(text::mask) }, tone{ pl(text::tone) }, mix{ pl(text::mix) }, output{ pl(text::output), " dB" };
    int shownBands = 1;
private:
    std::unique_ptr<ChoiceLink> bandLink, qualityLink;
    std::unique_ptr<ButtonAttachment> deltaLink, bypassLink, autoLink;
    std::array<std::unique_ptr<SliderAttachment>, 4> wholeLinks;
    juce::TooltipWindow tooltips{ this, 500 };
};

BarwaEditor::BarwaEditor(BarwaProcessor& owner) : juce::AudioProcessorEditor(owner), processor_(owner) {
    setLookAndFeel(&look_);
    root_ = std::make_unique<Root>(owner);
    addAndMakeVisible(*root_);
    root_->setBounds(0, 0, layout::width, layout::height);
    const float scale = juce::jlimit(0.6f, 1.6f, owner.uiScale());
    setResizable(true, true);
    setResizeLimits(juce::roundToInt(layout::width * 0.6f), juce::roundToInt(layout::height * 0.6f), juce::roundToInt(layout::width * 1.6f), juce::roundToInt(layout::height * 1.6f));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(static_cast<double>(layout::width) / layout::height);
    setSize(juce::roundToInt(layout::width * scale), juce::roundToInt(layout::height * scale));
    timerCallback();
    startTimerHz(30);
}

BarwaEditor::~BarwaEditor() {
    stopTimer();
    root_.reset();
    setLookAndFeel(nullptr);
}

void BarwaEditor::paint(juce::Graphics& g) { g.fillAll(colour::bg); }

void BarwaEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(layout::width);
    root_->setTransform(juce::AffineTransform::scale(scale));
    processor_.setUiScale(scale);
}

void BarwaEditor::timerCallback() {
    auto& state = processor_.state();
    const int bands = juce::roundToInt(state.getRawParameterValue(ids::bands)->load()) + 1;
    if (bands != root_->shownBands) root_->layoutColumns();
    root_->updateColumnTexts();
    root_->presetBar.refresh();
    ticksWithoutSound_ = root_->spectrum.refresh(bands) ? 0 : ticksWithoutSound_ + 1;
    // Without sound the strip has nothing to tell, so it stands still.
    constexpr int kRestAfterTicks = 10;
    if (ticksWithoutSound_ < kRestAfterTicks) root_->strip.push(processor_.maskNow(), state.getRawParameterValue(ids::mask)->load() > 0.05f);
}

int BarwaEditor::visibleBandColumns() const {
    return (root_->column1.isVisible() ? 1 : 0) + (root_->column2.isVisible() ? 1 : 0) + (root_->column3.isVisible() ? 1 : 0);
}

SpectrumView& BarwaEditor::spectrum() { return root_->spectrum; }

MaskStrip& BarwaEditor::maskStrip() { return root_->strip; }

wers::ui::PresetBar& BarwaEditor::presetBar() { return root_->presetBar; }

} // namespace barwa
