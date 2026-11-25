#include "PowLook.h"

namespace powiew::ui {

PowLook::PowLook() = default;

void PowLook::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider& s) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float radius = size * 0.5f - 3.0f;
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    const auto tint = s.findColour(juce::Slider::thumbColourId);
    const bool bipolar = static_cast<bool>(s.getProperties().getWithDefault("bipolar", false));
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool active = s.isMouseOverOrDragging();

    // The two large knobs glow by as much as they add.
    if (static_cast<bool>(s.getProperties().getWithDefault("large", false)) && pos > 0.001f) {
        const float glow = radius + 6.0f;
        juce::ColourGradient halo(tint.withAlpha(0.22f * pos * alpha), centre.x, centre.y, tint.withAlpha(0.0f), centre.x + glow + 10.0f, centre.y, true);
        g.setGradientFill(halo);
        g.fillEllipse(centre.x - glow - 10.0f, centre.y - glow - 10.0f, (glow + 10.0f) * 2.0f, (glow + 10.0f) * 2.0f);
    }

    // Track.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(colour::hairline.withMultipliedAlpha(alpha));
    g.strokePath(track, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc, from the start or from the middle.
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs(angle - from) > 0.001f) {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(tint.withMultipliedAlpha(alpha * 0.28f));
        g.strokePath(arc, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(tint.withMultipliedAlpha(alpha));
        g.strokePath(arc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body.
    const float body = radius - 7.0f;
    juce::ColourGradient grad(colour::raised.brighter(active ? 0.25f : 0.12f), centre.x, centre.y - body,
                              colour::surface.darker(0.2f), centre.x, centre.y + body, false);
    g.setGradientFill(grad);
    g.fillEllipse(centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
    g.setColour(colour::hairline.withMultipliedAlpha(alpha));
    g.drawEllipse(centre.x - body, centre.y - body, body * 2.0f, body * 2.0f, 1.0f);

    // Pointer.
    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    const auto inner = centre + dir * (body * 0.35f), outer = centre + dir * (body - 3.0f);
    g.setColour(colour::text.withMultipliedAlpha(alpha));
    g.drawLine({ inner, outer }, 2.2f);
    g.setColour(tint.withMultipliedAlpha(alpha));
    const auto tip = centre + dir * radius;
    g.fillEllipse(tip.x - 3.0f, tip.y - 3.0f, 6.0f, 6.0f);

    if (s.hasKeyboardFocus(false)) {
        g.setColour(colour::accent);
        g.drawEllipse(centre.x - radius - 4.0f, centre.y - radius - 4.0f, (radius + 4.0f) * 2.0f, (radius + 4.0f) * 2.0f, 1.2f);
    }
}

} // namespace powiew::ui
