#include "LampaLook.h"

namespace lampa::ui {

LampaLook::LampaLook() = default;

void LampaLook::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider& s) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const bool big = static_cast<bool>(s.getProperties().getWithDefault("big", false));
    const bool bipolar = static_cast<bool>(s.getProperties().getWithDefault("bipolar", false));
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float radius = size * 0.5f - (big ? 34.0f : 4.0f);
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    const auto tint = s.findColour(juce::Slider::thumbColourId);
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool active = s.isMouseOverOrDragging();
    const float trackW = big ? 5.0f : 3.5f;

    // Scale around a big knob: 0..10 like the old front panels.
    if (big) {
        for (int i = 0; i <= 10; ++i) {
            const float a = startAngle + (endAngle - startAngle) * static_cast<float>(i) / 10.0f;
            const auto dir = juce::Point<float>(std::sin(a), -std::cos(a));
            g.setColour(colour::faint.withMultipliedAlpha(alpha));
            g.drawLine({ centre + dir * (radius + 6.0f), centre + dir * (radius + (i % 5 == 0 ? 12.0f : 9.0f)) }, i % 5 == 0 ? 1.6f : 1.0f);
            if (i % 2 == 0) {
                g.setFont(uiFont(500, 11.5f));
                g.setColour(colour::dim.withMultipliedAlpha(alpha));
                const auto at = centre + dir * (radius + 22.0f);
                g.drawText(juce::String(i), juce::Rectangle<float>(at.x - 12.0f, at.y - 8.0f, 24.0f, 16.0f), juce::Justification::centred);
            }
        }
    }

    // Track and value arc.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(colour::hairline.withMultipliedAlpha(alpha));
    g.strokePath(track, juce::PathStrokeType(trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs(angle - from) > 0.001f) {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(tint.withMultipliedAlpha(alpha * 0.25f));
        g.strokePath(arc, juce::PathStrokeType(trackW * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(tint.withMultipliedAlpha(alpha));
        g.strokePath(arc, juce::PathStrokeType(trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body: a shadow, a skirt with grip and a domed cap.
    const float body = radius - (big ? 11.0f : 7.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f * alpha));
    g.fillEllipse(centre.x - body, centre.y - body + (big ? 5.0f : 3.0f), body * 2.0f, body * 2.0f);
    juce::ColourGradient skirt(juce::Colour(0xff3a3546), centre.x, centre.y - body, juce::Colour(0xff121018), centre.x, centre.y + body, false);
    g.setGradientFill(skirt);
    g.fillEllipse(centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
    const int grips = big ? 48 : 28;
    for (int i = 0; i < grips; ++i) {
        const float a = angle + juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(grips);
        const auto dir = juce::Point<float>(std::sin(a), -std::cos(a));
        const float light = 0.5f + 0.5f * -dir.y;
        g.setColour(juce::Colours::white.withAlpha((0.05f + 0.10f * light) * alpha));
        g.drawLine({ centre + dir * (body * 0.86f), centre + dir * (body - 1.0f) }, big ? 1.4f : 1.0f);
    }
    const float cap = body * 0.80f;
    juce::ColourGradient dome(juce::Colour(0xff4a4458).brighter(active ? 0.15f : 0.0f), centre.x - cap * 0.4f, centre.y - cap * 0.7f,
                              juce::Colour(0xff17141e), centre.x + cap * 0.3f, centre.y + cap, true);
    g.setGradientFill(dome);
    g.fillEllipse(centre.x - cap, centre.y - cap, cap * 2.0f, cap * 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.08f * alpha));
    g.drawEllipse(centre.x - cap, centre.y - cap, cap * 2.0f, cap * 2.0f, 1.0f);

    // Pointer: an amber line on the cap with a soft glow.
    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    const auto inner = centre + dir * (cap * 0.30f), outer = centre + dir * (cap - 3.0f);
    g.setColour(tint.withMultipliedAlpha(0.25f * alpha));
    g.drawLine({ inner, outer }, big ? 7.0f : 5.0f);
    g.setColour(tint.brighter(0.2f).withMultipliedAlpha(alpha));
    g.drawLine({ inner, outer }, big ? 3.0f : 2.2f);

    if (s.hasKeyboardFocus(false)) {
        g.setColour(colour::accent);
        g.drawEllipse(centre.x - radius - 4.0f, centre.y - radius - 4.0f, (radius + 4.0f) * 2.0f, (radius + 4.0f) * 2.0f, 1.2f);
    }
}

} // namespace lampa::ui
