#include "MaskStrip.h"
#include "Strings.h"

namespace barwa {

using namespace wers::ui;

namespace {
constexpr int kCaptionWidth = 208, kSidePadding = 24, kVerticalPadding = 8;
}

MaskStrip::MaskStrip() {
    setTitle(pl(text::maskStripTitle));
    setTooltip(pl(text::maskStripTip));
}

void MaskStrip::push(float share, bool maskOn) {
    history_[static_cast<size_t>(next_)] = juce::jlimit(0.0f, 1.0f, share);
    next_ = (next_ + 1) % kPoints;
    maskOn_ = maskOn;
    repaint();
}

void MaskStrip::paint(juce::Graphics& g) {
    g.fillAll(colour::surface);
    g.setColour(colour::hairline);
    g.fillRect(0, 0, getWidth(), 1);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    g.setFont(uiFont(600, 13.0f));
    g.setColour(colour::text);
    g.drawText(pl(text::maskStripTitle), kSidePadding, 8, kCaptionWidth - kSidePadding, 18, juce::Justification::centredLeft, false);
    g.setFont(uiFont(400, 12.0f));
    g.setColour(colour::dim);
    g.drawFittedText(pl(maskOn_ ? text::maskStripText : text::maskStripOff), kSidePadding, 26, kCaptionWidth - kSidePadding - 8, 24, juce::Justification::topLeft, 2, 1.0f);

    const auto area = juce::Rectangle<int>(kCaptionWidth, kVerticalPadding, getWidth() - kCaptionWidth - kSidePadding, getHeight() - 2 * kVerticalPadding).toFloat();
    g.setColour(colour::bg);
    g.fillRoundedRectangle(area, 4.0f);
    juce::Path shape;
    shape.startNewSubPath(area.getX(), area.getBottom());
    for (int i = 0; i < kPoints; ++i) {
        const float share = history_[static_cast<size_t>((next_ + i) % kPoints)];
        const float x = area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(kPoints - 1);
        shape.lineTo(x, area.getBottom() - share * area.getHeight());
    }
    shape.lineTo(area.getRight(), area.getBottom());
    shape.closeSubPath();
    g.setColour(maskOn_ ? colour::accent : colour::faint);
    g.fillPath(shape);
}

} // namespace barwa
