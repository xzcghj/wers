#include "StageView.h"
#include "Strings.h"

namespace refren {

using namespace wers::ui;

namespace {

constexpr float kQuietestDb = -48.0f, kLoudestDb = -6.0f, kMoveEnough = 0.005f, kLargestInterval = 24.0f;
constexpr int kOverloadTicks = 15, kRobotCentreNote = 60;

} // namespace

StageView::StageView(RefrenProcessor& processor) : processor_(processor) {
    auto& state = processor.state();
    for (int k = 0; k < kLayers; ++k) {
        auto& v = values_[static_cast<size_t>(k)];
        v.source = state.getRawParameterValue(layerId(k, ids::source));
        v.semis = state.getRawParameterValue(layerId(k, ids::semis));
        v.note = state.getRawParameterValue(layerId(k, ids::note));
        v.width = state.getRawParameterValue(layerId(k, ids::layerWidth));
    }
    widthScale_ = state.getRawParameterValue(ids::width);
    dots_.reserve(32);
    next_.reserve(32);
}

void StageView::refresh() {
    next_.clear();
    bool changed = false;
    const float mask = processor_.maskNow();
    for (int k = 0; k < kLayers; ++k) {
        const auto& v = values_[static_cast<size_t>(k)];
        const auto source = static_cast<Source>(juce::roundToInt(v.source->load()));
        const float interval = source == Source::Whisper ? 0.0f : source == Source::Robot ? v.note->load() - static_cast<float>(kRobotCentreNote) : v.semis->load();
        const float spread = juce::jmin(1.0f, v.width->load() * 0.01f * widthScale_->load() * 0.01f);
        const int count = processor_.activeVoices(k);
        for (int i = 0; i < count; ++i) {
            Dot dot;
            dot.pan = Layer::panShareOf(i, count) * spread;
            dot.interval = juce::jlimit(-kLargestInterval, kLargestInterval, interval);
            dot.layer = k;
            dot.index = i;
            next_.push_back(dot);
        }
        const float level = (wers::gainToDb(static_cast<double>(processor_.layerLevelNow(k))) - kQuietestDb) / (kLoudestDb - kQuietestDb);
        // Whisper is there for the sounds the mask holds back, so the mask leaves it alone.
        const float bright = juce::jlimit(0.0f, 1.0f, level) * (source == Source::Whisper ? 1.0f : mask);
        auto& shown = brightness_[static_cast<size_t>(k)];
        if (std::abs(bright - shown) > kMoveEnough || (bright <= 0.0f && shown > 0.0f)) {
            shown = bright;
            changed = true;
        }
    }
    if (next_ != dots_) {
        std::swap(next_, dots_);
        changed = true;
    }
    const bool wasOver = overloadTicks_ > 0;
    if (processor_.takeOutputPeak() > 1.0f) overloadTicks_ = kOverloadTicks;
    else if (overloadTicks_ > 0) --overloadTicks_;
    if (wasOver != (overloadTicks_ > 0)) changed = true;
    if (changed) {
        ++moves_;
        repaint();
    }
}

void StageView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::surface);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setFont(displayFont(600, 17.0f));
    g.setColour(colour::text);
    g.drawText(pl(text::stage), 14, 8, 200, 22, juce::Justification::centredLeft, false);
    g.setFont(uiFont(400, 12.0f));
    g.setColour(colour::faint);
    g.drawText(pl(text::left), 14, getHeight() - 26, 20, 18, juce::Justification::centredLeft, false);
    g.drawText(pl(text::right), getWidth() - 34, getHeight() - 26, 20, 18, juce::Justification::centredRight, false);

    constexpr float kSide = 22.0f, kTop = 44.0f, kBottom = 30.0f;
    const float cx = bounds.getCentreX(), cy = kTop + (bounds.getHeight() - kTop - kBottom) * 0.5f;
    const float reachX = bounds.getWidth() * 0.5f - kSide, reachY = (bounds.getHeight() - kTop - kBottom) * 0.5f;
    g.setColour(colour::hairline);
    g.drawHorizontalLine(juce::roundToInt(cy), kSide, bounds.getWidth() - kSide);
    g.drawVerticalLine(juce::roundToInt(cx), kTop, bounds.getHeight() - kBottom);

    for (const auto& dot : dots_) {
        // Voices of one layer lie on one line, so a small step keeps those at one place visible.
        const float x = cx + dot.pan * reachX;
        const float y = cy - dot.interval / kLargestInterval * reachY + static_cast<float>(dot.index % 3 - 1) * 5.0f;
        const float bright = brightness_[static_cast<size_t>(dot.layer)];
        g.setColour(colour::accent.withAlpha(0.18f + 0.82f * bright));
        g.fillEllipse(x - 4.5f, y - 4.5f, 9.0f, 9.0f);
    }
    g.setColour(overloadTicks_ > 0 ? colour::weak : colour::text);
    g.fillEllipse(cx - 9.0f, cy - 9.0f, 18.0f, 18.0f);
}

} // namespace refren
