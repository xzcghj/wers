#include "SpaceView.h"
#include "plugin/PoglosProcessor.h"

namespace pog::ui {

namespace {

// Shapes are drawn in a unit square and mapped onto the target, keeping their proportions.
// Wide areas stretch the shape a little, so a plan fills its panel without looking squashed.
juce::AffineTransform unitTo(juce::Rectangle<float> r) {
    const float sy = juce::jmin(r.getWidth(), r.getHeight());
    const float sx = juce::jmin(r.getWidth(), sy * 1.45f);
    return juce::AffineTransform::scale(sx, sy).translated(r.getCentreX() - sx * 0.5f, r.getCentreY() - sy * 0.5f);
}

juce::Point<float> unitPoint(juce::Rectangle<float> r, float x, float y) { return juce::Point<float>(x, y).transformedBy(unitTo(r)); }

} // namespace

juce::Path spaceShape(Algo a, juce::Rectangle<float> r) {
    juce::Path p;
    switch (a) {
    case Algo::Hall: // a shoebox with a curved stage end
        p.startNewSubPath(0.16f, 0.06f);
        p.lineTo(0.84f, 0.06f);
        p.lineTo(0.84f, 0.80f);
        p.quadraticTo(0.5f, 0.98f, 0.16f, 0.80f);
        p.closeSubPath();
        break;
    case Algo::Room:
        p.addRoundedRectangle(0.2f, 0.2f, 0.6f, 0.6f, 0.03f);
        break;
    case Algo::Plate:
        p.addRectangle(0.06f, 0.26f, 0.88f, 0.48f);
        break;
    case Algo::Chamber:
        p.addPolygon({ 0.5f, 0.5f }, 5, 0.40f, 0.35f);
        break;
    case Algo::Ambient:
        p.addEllipse(0.08f, 0.12f, 0.84f, 0.76f);
        break;
    case Algo::Cathedral: // a cross with a rounded apse
        p.startNewSubPath(0.40f, 0.98f);
        p.lineTo(0.40f, 0.46f);
        p.lineTo(0.08f, 0.46f);
        p.lineTo(0.08f, 0.30f);
        p.lineTo(0.40f, 0.30f);
        p.lineTo(0.40f, 0.14f);
        p.quadraticTo(0.5f, 0.0f, 0.60f, 0.14f);
        p.lineTo(0.60f, 0.30f);
        p.lineTo(0.92f, 0.30f);
        p.lineTo(0.92f, 0.46f);
        p.lineTo(0.60f, 0.46f);
        p.lineTo(0.60f, 0.98f);
        p.closeSubPath();
        break;
    case Algo::Shimmer:
        p.addStar({ 0.5f, 0.52f }, 4, 0.22f, 0.46f, 0.0f);
        break;
    case Algo::Nonlinear:
    default:
        p.addRectangle(0.14f, 0.16f, 0.72f, 0.68f);
        break;
    }
    p.applyTransform(unitTo(r));
    return p;
}

void drawShapeDetails(juce::Graphics& g, Algo a, juce::Rectangle<float> r, juce::Colour c, float alpha) {
    g.setColour(c.withAlpha(alpha));
    const float s = juce::jmin(r.getWidth(), r.getHeight());
    auto pt = [&](float x, float y) { return unitPoint(r, x, y); };
    switch (a) {
    case Algo::Plate: {
        // Springs at the corners and the driver in the middle.
        const juce::Point<float> corners[4] = { pt(0.06f, 0.26f), pt(0.94f, 0.26f), pt(0.06f, 0.74f), pt(0.94f, 0.74f) };
        const juce::Point<float> outer[4] = { pt(0.0f, 0.12f), pt(1.0f, 0.12f), pt(0.0f, 0.88f), pt(1.0f, 0.88f) };
        for (int k = 0; k < 4; ++k) {
            juce::Path spring;
            const auto from = corners[k], to = outer[k];
            spring.startNewSubPath(from);
            for (int i = 1; i <= 8; ++i) {
                const float t = static_cast<float>(i) / 8.0f;
                auto q = from + (to - from) * t;
                const auto n = juce::Point<float>(-(to - from).y, (to - from).x) / (to - from).getDistanceFromOrigin();
                q += n * (i % 2 ? 0.02f : -0.02f) * s * (i == 8 ? 0.0f : 1.0f);
                spring.lineTo(q);
            }
            g.strokePath(spring, juce::PathStrokeType(1.0f));
        }
        const auto m = pt(0.5f, 0.5f);
        g.drawEllipse(m.x - s * 0.04f, m.y - s * 0.04f, s * 0.08f, s * 0.08f, 1.2f);
        break;
    }
    case Algo::Nonlinear: {
        // The gate: bars across the far wall.
        for (int i = 0; i < 5; ++i) {
            const float x = 0.24f + 0.13f * static_cast<float>(i);
            g.drawLine(juce::Line<float>(pt(x, 0.16f), pt(x, 0.27f)), 1.4f);
        }
        break;
    }
    case Algo::Cathedral: {
        // Columns along the nave.
        for (int i = 0; i < 4; ++i) {
            const float y = 0.56f + 0.1f * static_cast<float>(i);
            for (float x : { 0.44f, 0.56f }) {
                const auto c0 = pt(x, y);
                g.fillEllipse(c0.x - s * 0.008f, c0.y - s * 0.008f, s * 0.016f, s * 0.016f);
            }
        }
        break;
    }
    case Algo::Shimmer: {
        // Sparks above.
        for (int i = 0; i < 6; ++i) {
            const auto c0 = pt(0.18f + 0.13f * static_cast<float>(i), 0.12f + 0.07f * static_cast<float>((i * 3) % 4));
            const float k = s * (0.012f + 0.006f * static_cast<float>(i % 3));
            g.drawLine(c0.x - k, c0.y, c0.x + k, c0.y, 1.0f);
            g.drawLine(c0.x, c0.y - k, c0.x, c0.y + k, 1.0f);
        }
        break;
    }
    case Algo::Hall: {
        // Rows of seats.
        for (int i = 0; i < 5; ++i) {
            const float y = 0.18f + 0.1f * static_cast<float>(i);
            g.drawLine(juce::Line<float>(pt(0.26f, y), pt(0.74f, y)), 0.8f);
        }
        break;
    }
    case Algo::Ambient: {
        for (float k : { 0.6f, 0.35f }) {
            const auto c0 = pt(0.5f, 0.5f);
            g.drawEllipse(c0.x - s * 0.42f * k, c0.y - s * 0.38f * k, s * 0.84f * k, s * 0.76f * k, 0.8f);
        }
        break;
    }
    default:
        break;
    }
}

SpaceView::SpaceView(PoglosProcessor& p) : processor_(p) { setOpaque(false); }

void SpaceView::tick() {
    shown_ = processor_.currentParams();
    const auto& rv = processor_.reverb();
    const float db = juce::jmax(rv.bandLevelDb(0), rv.bandLevelDb(1), rv.bandLevelDb(2));
    const float target = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 50.0f);
    level_ += (target > level_ ? 0.5f : 0.08f) * (target - level_);
    freeze_ = rv.freezeAmount();
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastMs_ > 0.0 ? juce::jlimit(0.0, 100.0, now - lastMs_) : 33.0;
    lastMs_ = now;
    // Rings travel slower in a bigger space, and stop when the tail is frozen.
    phase_ += dt / 1000.0 * (0.9 / sizeScale(shown_.size)) * (1.0 - 0.85 * freeze_);
    repaint();
}

void SpaceView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::surface);
    g.fillRoundedRectangle(bounds, 12.0f);
    g.setColour(colour::hairline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);

    const auto algo = shown_.algo;
    const auto tint = hue::space.interpolatedWith(hue::ice, freeze_);
    g.setFont(uiFont(600, 11.5f));
    g.setColour(colour::dim);
    g.drawText(pl("PRZESTRZEŃ"), juce::Rectangle<float>(16.0f, 10.0f, 150.0f, 16.0f), juce::Justification::centredLeft);
    g.setFont(displayFont(700, 16.0f));
    g.setColour(tint);
    const auto names = juce::StringArray{ pl("Sala"), pl("Pokój"), pl("Płyta"), pl("Komora"), pl("Ambient"), pl("Katedra"), pl("Błysk"), pl("Nieliniowy") };
    g.drawText(names[static_cast<int>(algo)], juce::Rectangle<float>(bounds.getRight() - 176.0f, 8.0f, 160.0f, 20.0f), juce::Justification::centredRight);

    // The drawing area; the outline grows with size.
    const auto area = bounds.reduced(18.0f).withTrimmedTop(20.0f).withTrimmedBottom(14.0f);
    const float grow = 0.45f + 0.55f * std::sqrt(juce::jlimit(0.0f, 1.0f, shown_.size));
    const auto box = area.withSizeKeepingCentre(area.getWidth() * grow, area.getHeight() * grow);
    const auto shape = spaceShape(algo, box);

    // A faint grid, like a floor plan.
    g.setColour(colour::hairline.withAlpha(0.35f));
    for (float x = area.getX(); x < area.getRight(); x += 18.0f) g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
    for (float y = area.getY(); y < area.getBottom(); y += 18.0f) g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());

    const auto centre = box.getCentre();
    juce::ColourGradient fill(tint.withAlpha(0.10f + 0.22f * level_), centre.x, centre.y, tint.withAlpha(0.03f), box.getX(), box.getY(), true);
    g.setGradientFill(fill);
    g.fillPath(shape);

    // Rings out from the source while the tail sounds, kept inside the walls.
    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(shape);
        const float maxR = juce::jmax(box.getWidth(), box.getHeight()) * 0.75f;
        const auto source = juce::Point<float>(centre.x, box.getY() + box.getHeight() * 0.68f);
        for (int k = 0; k < 4; ++k) {
            const float t = static_cast<float>(std::fmod(phase_ + k * 0.25, 1.0));
            const float r = maxR * t;
            g.setColour(tint.withAlpha((1.0f - t) * (0.08f + 0.5f * level_)));
            g.drawEllipse(source.x - r, source.y - r, r * 2.0f, r * 2.0f, 1.6f);
        }

        // Early reflections: each one a dot where it bounces, farther for later ones, its side by its pan.
        std::array<EarlyTap, kMaxEarlyTaps> taps{};
        const int n = earlyTaps(algo, taps);
        float maxMs = 1.0f, maxGain = 1e-4f;
        for (int k = 0; k < n; ++k) {
            maxMs = juce::jmax(maxMs, taps[static_cast<size_t>(k)].ms);
            maxGain = juce::jmax(maxGain, std::abs(taps[static_cast<size_t>(k)].gainL) + std::abs(taps[static_cast<size_t>(k)].gainR));
        }
        const float earlyShown = juce::jlimit(0.0f, 1.0f, 2.0f * (1.0f - shown_.balance));
        for (int k = 0; k < n; ++k) {
            const auto& t = taps[static_cast<size_t>(k)];
            const float side = (std::abs(t.gainR) - std::abs(t.gainL)) / (std::abs(t.gainR) + std::abs(t.gainL) + 1e-6f);
            const float angle = -juce::MathConstants<float>::halfPi + side * 1.35f + 0.25f * std::sin(static_cast<float>(k) * 2.4f);
            const float dist = (0.2f + 0.8f * t.ms / maxMs) * maxR * 0.62f;
            const auto at = source + juce::Point<float>(std::cos(angle), std::sin(angle)) * dist;
            const float strength = (std::abs(t.gainL) + std::abs(t.gainR)) / maxGain;
            const float a = (0.25f + 0.75f * earlyShown) * (0.35f + 0.65f * strength);
            g.setColour(tint.withAlpha(0.18f * a));
            g.drawLine(juce::Line<float>(source, at), 0.8f);
            const float dot = 2.0f + 3.5f * strength;
            g.setColour(colour::text.interpolatedWith(tint, 0.4f).withAlpha(a));
            g.fillEllipse(at.x - dot * 0.5f, at.y - dot * 0.5f, dot, dot);
        }
        g.setColour(colour::text);
        g.fillEllipse(source.x - 4.0f, source.y - 4.0f, 8.0f, 8.0f);
    }

    drawShapeDetails(g, algo, box, tint, 0.45f);
    g.setColour(tint.withAlpha(0.9f));
    g.strokePath(shape, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Size in words, and freeze.
    g.setFont(uiFont(500, 11.5f));
    g.setColour(colour::faint);
    const float scale = sizeScale(shown_.size);
    g.drawText(pl("skala ") + juce::String(scale, 2).replaceCharacter('.', ',') + juce::String::fromUTF8("×"),
               juce::Rectangle<float>(16.0f, bounds.getBottom() - 24.0f, 120.0f, 16.0f), juce::Justification::centredLeft);
    if (freeze_ > 0.05f) {
        g.setFont(uiFont(600, 12.0f));
        g.setColour(hue::ice.withAlpha(freeze_));
        g.drawText(pl("ZAMROŻONE"), juce::Rectangle<float>(bounds.getRight() - 130.0f, bounds.getBottom() - 24.0f, 114.0f, 16.0f), juce::Justification::centredRight);
    }
}

} // namespace pog::ui
