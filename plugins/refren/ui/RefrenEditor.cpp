#include "RefrenEditor.h"
#include "Strings.h"

namespace refren {

using namespace wers::ui;

namespace {

namespace layout {
constexpr int width = 1040, height = 680, headerH = 64, bottomH = 84, margin = 12, gap = 8, stageW = 272, amountH = 64;
constexpr int bodyY = headerH + gap, bodyH = height - bottomH - gap - bodyY;
constexpr int layerW = 132, busW = 184;
} // namespace layout

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

// The largest control of the plugin, because it is the one everybody uses.
class AmountSlider final : public juce::Component {
public:
    AmountSlider(juce::AudioProcessorValueTreeState& state) : link_(std::make_unique<SliderAttachment>(state, ids::amount, slider)) {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setTitle(pl(text::amount));
        slider.setTooltip(pl(text::amountTip));
        slider.setDoubleClickReturnValue(true, 100.0);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible(slider);
    }
    juce::Slider slider;
    void resized() override { slider.setBounds(getLocalBounds().reduced(12, 0).withTrimmedTop(32).withTrimmedBottom(4)); }
    void paint(juce::Graphics& g) override {
        g.setColour(colour::surface);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);
        g.setFont(displayFont(600, 17.0f));
        g.setColour(colour::text);
        g.drawText(pl(text::amount), 14, 8, 100, 22, juce::Justification::centredLeft, false);
        g.setFont(displayFont(600, 17.0f));
        g.drawText(juce::String(juce::roundToInt(slider.getValue())) + "%", getWidth() - 94, 8, 80, 22, juce::Justification::centredRight, false);
    }
private:
    std::unique_ptr<SliderAttachment> link_;
};

// What the details show of the rest of the sound.
class BusColumn final : public juce::Component {
public:
    explicit BusColumn(juce::AudioProcessorValueTreeState& state) {
        const auto choice = [&](const char* id, const char* name) { rows_.push_back(add(choices_.add(new ChoiceRow(state, id, pl(name))))); };
        const auto number = [&](const char* id, const char* name) { rows_.push_back(add(sliders_.add(new ParamSlider(state, id, pl(name))))); };
        choice(ids::grainChar, text::grainCharShort);
        choice(ids::motion, text::motion);
        number(ids::motionRate, text::motionRate);
        choice(ids::echoTime, text::echoTime);
        number(ids::echoFeedback, text::echoFeedback);
        ping_.setClickingTogglesState(true);
        ping_.setOnFill(colour::accent, colour::ink);
        ping_.setTextSize(12.0f);
        rows_.push_back(add(&ping_));
        pingLink_ = std::make_unique<ButtonAttachment>(state, ids::echoPing, ping_);
        number(ids::echoTone, text::echoTone);
        number(ids::verbSize, text::verbSize);
        number(ids::verbDecay, text::verbDecay);
        number(ids::verbTone, text::verbTone);
        number(ids::verbPre, text::verbPre);
        number(ids::verbShimmer, text::verbShimmer);
        number(ids::duck, text::duck);
        number(ids::sendDry, text::sendDry);
        number(ids::dry, text::dry);
        number(ids::trim, text::trim);
    }
    juce::StringArray idsShown() const {
        juce::StringArray all{ ids::echoPing };
        for (const auto* c : choices_) all.add(c->id);
        for (const auto* s : sliders_) all.add(s->id);
        return all;
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        const int rowH = area.getHeight() / static_cast<int>(rows_.size());
        for (auto* row : rows_) row->setBounds(area.removeFromTop(rowH).withTrimmedBottom(3));
    }
    void paint(juce::Graphics& g) override {
        g.setColour(colour::surface);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);
    }
private:
    juce::Component* add(juce::Component* c) {
        addAndMakeVisible(c);
        return c;
    }
    juce::OwnedArray<ChoiceRow> choices_;
    juce::OwnedArray<ParamSlider> sliders_;
    FlatButton ping_{ pl(text::echoPing) };
    std::unique_ptr<ButtonAttachment> pingLink_;
    std::vector<juce::Component*> rows_;
};

} // namespace

// Everything is laid out at the design size and scaled as a whole.
class RefrenEditor::Root final : public juce::Component {
public:
    explicit Root(RefrenProcessor& p)
        : processor(p), presetBar(p.presets()), browser(p.presets(), presetBar, p.presetFolder()), stage(p), amount(p.state()),
          layer1(p, 0), layer2(p, 1), layer3(p, 2), layer4(p, 3), bus(p.state()) {
        auto& state = p.state();
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &presetBar, &randomise, &details, &delta, &bypass, &browser, &stage, &amount }) addAndMakeVisible(c);
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &layer1, &layer2, &layer3, &layer4, &bus }) addChildComponent(c);

        randomise.setTooltip(pl(text::randomiseTip));
        randomise.onClick = [this] { processor.randomise(random); };
        details.setClickingTogglesState(true);
        details.setOnFill(colour::raised, colour::text);
        details.setTooltip(pl(text::detailsTip));
        details.onClick = [this] { showDetails(details.getToggleState()); };
        delta.setClickingTogglesState(true);
        delta.setOnFill(colour::accent, colour::ink);
        delta.setTooltip(pl(text::deltaTip));
        deltaLink = std::make_unique<ButtonAttachment>(state, ids::delta, delta);
        bypass.setClickingTogglesState(true);
        bypass.setTooltip(pl(text::bypassTip));
        bypassLink = std::make_unique<ButtonAttachment>(state, ids::bypass, bypass);

        const std::array<std::tuple<const char*, const char*, const char*, const char*>, 9> row{ {
            { ids::width, text::width, "%", text::widthTip },
            { ids::time, text::time, "%", text::timeTip },
            { ids::tilt, text::tilt, "%", text::tiltTip },
            { ids::grain, text::grain, " dB", text::grainTip },
            { ids::motionDepth, text::motionDepth, "%", text::motionTip },
            { ids::verbMix, text::verbMix, "%", text::verbTip },
            { ids::echoMix, text::echoMix, "%", text::echoTip },
            { ids::consonants, text::consonantsShort, "%", text::consonantsTip },
            { ids::output, text::output, " dB", text::outputTip },
        } };
        for (const auto& [id, name, suffix, tip] : row) {
            auto* s = bottom.add(new LabeledSlider(pl(name), suffix));
            s->slider.setTooltip(pl(tip));
            addAndMakeVisible(s);
            bottomLinks.add(new SliderAttachment(state, id, s->slider));
            auto& parameter = *state.getParameter(id);
            s->slider.setDoubleClickReturnValue(true, static_cast<double>(parameter.convertFrom0to1(parameter.getDefaultValue())));
        }
        // Most presets have no movement chosen, and without one this slider would do nothing at all.
        motionSlider().onDragStart = [&state] {
            auto& motion = *state.getParameter(ids::motion);
            if (static_cast<int>(motion.convertFrom0to1(motion.getValue())) != static_cast<int>(Motion::None)) return;
            motion.beginChangeGesture();
            motion.setValueNotifyingHost(motion.convertTo0to1(static_cast<float>(Motion::Autopan)));
            motion.endChangeGesture();
        };
    }

    juce::Slider& motionSlider() { return bottom[4]->slider; }

    void showDetails(bool shown) {
        details.setToggleState(shown, juce::dontSendNotification);
        browser.setVisible(!shown);
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &layer1, &layer2, &layer3, &layer4, &bus }) c->setVisible(shown);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::bg);
        g.setColour(colour::surface);
        g.fillRect(0, 0, getWidth(), layout::headerH);
        g.fillRect(0, layout::height - layout::bottomH, getWidth(), layout::bottomH);
        g.setColour(colour::hairline);
        g.fillRect(0, layout::headerH - 1, getWidth(), 1);
        g.fillRect(0, layout::height - layout::bottomH, getWidth(), 1);
        g.setColour(colour::text);
        g.setFont(displayFont(800, 26.0f));
        g.drawText("WERS", 24, 0, 78, layout::headerH, juce::Justification::centredLeft, false);
        g.setFont(displayFont(400, 26.0f));
        g.drawText("Refren", 102, 0, 90, layout::headerH, juce::Justification::centredLeft, false);
    }

    void resized() override {
        constexpr int y = 14, h = 36;
        bypass.setBounds(layout::width - 24 - 66, y, 66, h);
        delta.setBounds(bypass.getX() - 6 - 60, y, 60, h);
        details.setBounds(delta.getX() - 6 - 86, y, 86, h);
        randomise.setBounds(details.getX() - 6 - 62, y, 62, h);
        presetBar.setBounds(200, y, randomise.getX() - 12 - 200, h);

        const int stageX = layout::width - layout::margin - layout::stageW;
        amount.setBounds(stageX, layout::bodyY + layout::bodyH - layout::amountH, layout::stageW, layout::amountH);
        stage.setBounds(stageX, layout::bodyY, layout::stageW, layout::bodyH - layout::amountH - layout::gap);
        const int bodyW = stageX - layout::gap - layout::margin;
        browser.setBounds(layout::margin, layout::bodyY, bodyW, layout::bodyH);

        const std::array<LayerColumn*, 4> columns{ &layer1, &layer2, &layer3, &layer4 };
        int x = layout::margin;
        for (auto* column : columns) {
            column->setBounds(x, layout::bodyY, layout::layerW, layout::bodyH);
            x += layout::layerW + 6;
        }
        bus.setBounds(x, layout::bodyY, bodyW - (x - layout::margin), layout::bodyH);

        constexpr int kSideMargin = 16, kBetween = 12, kSliderH = 48;
        const int count = bottom.size();
        const int sliderW = (layout::width - 2 * kSideMargin - (count - 1) * kBetween) / count;
        const int sliderY = layout::height - layout::bottomH + (layout::bottomH - kSliderH) / 2;
        for (int i = 0; i < count; ++i) bottom[i]->setBounds(kSideMargin + i * (sliderW + kBetween), sliderY, sliderW, kSliderH);
    }

    RefrenProcessor& processor;
    PresetBar presetBar;
    PresetBrowser browser;
    StageView stage;
    AmountSlider amount;
    LayerColumn layer1, layer2, layer3, layer4;
    BusColumn bus;
    FlatButton randomise{ pl(text::randomise) }, details{ pl(text::details) }, delta{ pl(text::delta) }, bypass{ pl(text::bypass) };
private:
    juce::Random random;
    juce::OwnedArray<LabeledSlider> bottom;
    juce::OwnedArray<SliderAttachment> bottomLinks;
    std::unique_ptr<ButtonAttachment> deltaLink, bypassLink;
    juce::TooltipWindow tooltips{ this, 500 };
};

RefrenEditor::RefrenEditor(RefrenProcessor& owner) : juce::AudioProcessorEditor(owner), processor_(owner) {
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

RefrenEditor::~RefrenEditor() {
    stopTimer();
    root_.reset();
    setLookAndFeel(nullptr);
}

void RefrenEditor::paint(juce::Graphics& g) { g.fillAll(colour::bg); }

void RefrenEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(layout::width);
    root_->setTransform(juce::AffineTransform::scale(scale));
    processor_.setUiScale(scale);
}

void RefrenEditor::timerCallback() {
    root_->presetBar.refresh();
    root_->browser.refresh();
    root_->stage.refresh();
}

PresetBrowser& RefrenEditor::browser() { return root_->browser; }

StageView& RefrenEditor::stage() { return root_->stage; }

juce::Slider& RefrenEditor::motionSlider() { return root_->motionSlider(); }

PresetBar& RefrenEditor::presetBar() { return root_->presetBar; }

LayerColumn& RefrenEditor::layerColumn(int layer) {
    const std::array<LayerColumn*, 4> columns{ &root_->layer1, &root_->layer2, &root_->layer3, &root_->layer4 };
    return *columns[static_cast<size_t>(juce::jlimit(0, 3, layer))];
}

void RefrenEditor::showDetails(bool shown) { root_->showDetails(shown); }

bool RefrenEditor::detailsShown() const { return root_->details.getToggleState(); }

juce::StringArray RefrenEditor::idsInDetails() const {
    auto all = root_->bus.idsShown();
    for (const auto* column : { &root_->layer1, &root_->layer2, &root_->layer3, &root_->layer4 }) all.addArray(column->idsShown());
    return all;
}

} // namespace refren
