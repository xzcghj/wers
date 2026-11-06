#include "LayerColumn.h"
#include "Strings.h"

namespace refren {

using namespace wers::ui;

namespace {
constexpr int kSub = static_cast<int>(Source::Sub);
constexpr float kSubOctaveDown = -12.0f;
constexpr float kDimmedAlpha = 0.4f;
}

ParamSlider::ParamSlider(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& name)
    : id(parameterId), parameter_(*state.getParameter(parameterId)), name_(name) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setTitle(name);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible(slider);
    link_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, slider);
    slider.setDoubleClickReturnValue(true, static_cast<double>(parameter_.convertFrom0to1(parameter_.getDefaultValue())));
}

void ParamSlider::resized() { slider.setBounds(getLocalBounds().withTrimmedTop(15)); }

void ParamSlider::paint(juce::Graphics& g) {
    const auto top = getLocalBounds().removeFromTop(16);
    g.setFont(uiFont(400, 12.0f));
    g.setColour(colour::dim);
    g.drawText(name_, top, juce::Justification::centredLeft, true);
    g.setFont(uiFont(600, 12.0f));
    g.setColour(colour::text);
    g.drawText(parameter_.getCurrentValueAsText(), top, juce::Justification::centredRight, false);
}

ChoiceRow::ChoiceRow(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& name)
    : id(parameterId), parameter_(*dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(parameterId))), name_(name),
      link_(parameter_, [this](float value) {
          const int index = juce::roundToInt(value);
          button_.setButtonText(parameter_.choices[index]);
          if (onChanged) onChanged(index);
      }) {
    button_.setTextSize(12.0f);
    button_.setTooltip(name);
    addAndMakeVisible(button_);
    link_.sendInitialUpdate();
    button_.onClick = [this] {
        juce::PopupMenu menu;
        const int now = parameter_.getIndex();
        for (int i = 0; i < parameter_.choices.size(); ++i) menu.addItem(i + 1, parameter_.choices[i], true, i == now);
        juce::Component::SafePointer<ChoiceRow> self(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&button_), [self](int picked) {
            if (self != nullptr && picked > 0) self->pick(picked - 1);
        });
    };
}

void ChoiceRow::pick(int index) {
    link_.setValueAsCompleteGesture(static_cast<float>(index));
    if (onPicked) onPicked(index);
}

void ChoiceRow::resized() {
    auto area = getLocalBounds();
    if (name_.isNotEmpty()) area.removeFromLeft(area.getWidth() * 9 / 20);
    button_.setBounds(area);
}

void ChoiceRow::paint(juce::Graphics& g) {
    g.setFont(uiFont(400, 12.0f));
    g.setColour(colour::dim);
    g.drawText(name_, getLocalBounds().withWidth(getWidth() * 9 / 20), juce::Justification::centredLeft, true);
}

LayerColumn::LayerColumn(RefrenProcessor& processor, int layer)
    : processor_(processor), layer_(layer), on_(pl(text::layer) + " " + juce::String(layer + 1)),
      source_(processor.state(), layerId(layer, ids::source), pl(text::layerSource)) {
    auto& state = processor.state();
    on_.setClickingTogglesState(true);
    on_.setOnFill(colour::accent, colour::ink);
    on_.setTextSize(13.0f);
    addAndMakeVisible(on_);
    onLink_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, layerId(layer, ids::on), on_);
    addAndMakeVisible(source_);
    source_.onPicked = [this](int index) {
        // A sub at the pitch of the voice makes no sense, and an octave down is what everybody wants from one.
        auto* semis = processor_.state().getParameter(layerId(layer_, ids::semis));
        auto& range = dynamic_cast<juce::RangedAudioParameter&>(*semis);
        if (index == kSub && std::abs(range.convertFrom0to1(semis->getValue())) < 0.5f) {
            semis->beginChangeGesture();
            semis->setValueNotifyingHost(range.convertTo0to1(kSubOctaveDown));
            semis->endChangeGesture();
        }
    };
    const std::array<std::pair<const char*, const char*>, 10> rows{ {
        { ids::voices, text::layerVoices },
        { ids::semis, text::layerSemis },
        { ids::formant, text::layerFormant },
        { ids::level, text::layerLevel },
        { ids::layerWidth, text::layerWidthShort },
        { ids::layerTime, text::layerTimeShort },
        { ids::detune, text::layerDetune },
        { ids::low, text::layerLow },
        { ids::high, text::layerHigh },
        { ids::note, text::layerNote },
    } };
    for (const auto& [suffix, name] : rows) addAndMakeVisible(sliders_.add(new ParamSlider(state, layerId(layer, suffix), pl(name))));
    shownSource_ = static_cast<Source>(juce::roundToInt(state.getRawParameterValue(layerId(layer, ids::source))->load()));
    source_.onChanged = [this](int index) {
        // The value comes with the call, because a raw value of the parameter may get its update only after this one.
        shownSource_ = static_cast<Source>(index);
        updateDimming();
    };
    on_.onStateChange = [this] { updateDimming(); };
    updateDimming();
}

bool LayerColumn::isDimmed(const char* suffix) const {
    const auto id = layerId(layer_, suffix);
    for (const auto* s : sliders_)
        if (s->id == id) return s->getAlpha() < 1.0f;
    return false;
}

void LayerColumn::updateDimming() {
    const auto source = shownSource_;
    const bool on = on_.getToggleState();
    const bool hasInterval = source == Source::Voice || source == Source::Sub;
    const bool hasColour = source == Source::Voice || source == Source::Robot;
    for (auto* s : sliders_) {
        bool useful = on;
        if (s->id == layerId(layer_, ids::semis)) useful = useful && hasInterval;
        else if (s->id == layerId(layer_, ids::formant)) useful = useful && hasColour;
        else if (s->id == layerId(layer_, ids::note)) useful = useful && source == Source::Robot;
        const float alpha = useful ? 1.0f : kDimmedAlpha;
        if (std::abs(s->getAlpha() - alpha) > 1.0e-3f) s->setAlpha(alpha);
    }
    const float sourceAlpha = on ? 1.0f : kDimmedAlpha;
    if (std::abs(source_.getAlpha() - sourceAlpha) > 1.0e-3f) source_.setAlpha(sourceAlpha);
}

juce::StringArray LayerColumn::idsShown() const {
    juce::StringArray all{ layerId(layer_, ids::on), source_.id };
    for (const auto* s : sliders_) all.add(s->id);
    return all;
}

void LayerColumn::pickSource(int index) { source_.pick(index); }

void LayerColumn::resized() {
    auto area = getLocalBounds().reduced(8);
    on_.setBounds(area.removeFromTop(30));
    area.removeFromTop(6);
    source_.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
    const int rowH = area.getHeight() / sliders_.size();
    for (auto* s : sliders_) s->setBounds(area.removeFromTop(rowH).withTrimmedBottom(4));
}

void LayerColumn::paint(juce::Graphics& g) {
    g.setColour(colour::surface);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);
}

} // namespace refren
