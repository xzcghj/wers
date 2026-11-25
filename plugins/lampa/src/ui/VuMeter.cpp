#include "VuMeter.h"

namespace lampa::ui {

namespace {
constexpr float kSpan = 0.84f; // radians either side of straight up, at the ends of the scale
constexpr float kRefDb = -18.0f;

juce::String commaDb(float v) { return juce::String(v, 1).replaceCharacter('.', ','); }
} // namespace

VuMeter::VuMeter() {
    setTooltip(juce::String::fromUTF8("Wskaźnik VU. Kliknij, żeby przełączyć: redukcja (o ile Lampa ścisza) albo poziom na wyjściu. "
                                      "Lampka LAMPA świeci tym mocniej, im mocniej grzeje się lampa; PAMIĘĆ pokazuje, ile redukcji komórka trzyma w pamięci."));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void VuMeter::setMode(Mode m) {
    if (m == mode_) return;
    mode_ = m;
    repaint();
}

float VuMeter::angleFor(float deflection) { return (juce::jlimit(-0.05f, 1.08f, deflection) - 0.5f) * 2.0f * kSpan; }

void VuMeter::update(float reductionDb, float outputLevel, float heat, float memoryDb, float dt) {
    reduction_ = reductionDb;
    outputVu_ = 20.0f * std::log10(juce::jmax(outputLevel, 1e-6f)) - kRefDb;
    const float target = mode_ == Mode::Reduction ? vuDeflection(-reductionDb) : (outputVu_ < -40.0f ? 0.0f : vuDeflection(outputVu_));
    needle_.step(target, dt);
    // The lamp lights from about 30 dB below where the tube bends, fully at the bend.
    const float glow = juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(juce::jmax(heat, 1e-5f)) + 30.0f) / 30.0f);
    heat_ += (glow - heat_) * 0.25f;
    memory_ += (memoryDb - memory_) * 0.25f;
    repaint();
}

void VuMeter::resized() { face_ = getLocalBounds().toFloat().reduced(14.0f); }

juce::Point<float> VuMeter::pivot() const { return { face_.getCentreX(), face_.getBottom() + 18.0f }; }
float VuMeter::arcRadius() const { return face_.getHeight() - 46.0f; }

void VuMeter::mouseUp(const juce::MouseEvent& e) {
    if (e.mouseWasClicked() && onFlip) onFlip();
}

void VuMeter::paint(juce::Graphics& g) {
    const auto all = getLocalBounds().toFloat();
    // Bezel: a dark frame with a lit edge on top.
    juce::ColourGradient bezel(juce::Colour(0xff35303f), all.getX(), all.getY(), juce::Colour(0xff0d0b12), all.getX(), all.getBottom(), false);
    g.setGradientFill(bezel);
    g.fillRoundedRectangle(all, 18.0f);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(all.reduced(0.5f), 18.0f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(face_.expanded(2.0f), 11.0f, 3.0f);
    drawFace(g);
}

void VuMeter::drawFace(juce::Graphics& g) {
    juce::Graphics::ScopedSaveState clip(g);
    juce::Path facePath;
    facePath.addRoundedRectangle(face_, 9.0f);
    g.reduceClipRegion(facePath);

    // Paper, backlit by a warm bulb that brightens a little as the tube heats.
    const auto p = pivot();
    const float r = arcRadius();
    g.setColour(juce::Colour(0xffd9c39a));
    g.fillRect(face_);
    juce::ColourGradient glow(warm::faceGlow.withAlpha(0.95f), p.x, p.y - r * 0.55f, juce::Colour(0x00d9c39a), p.x, face_.getY() - 40.0f, true);
    glow.addColour(0.55, warm::face.withAlpha(0.9f));
    g.setGradientFill(glow);
    g.fillRect(face_);
    if (heat_ > 0.02f) {
        juce::ColourGradient hot(warm::ember.withAlpha(0.10f * juce::jmin(1.0f, heat_)), p.x, p.y - r * 0.3f, juce::Colours::transparentBlack, p.x, face_.getY(), true);
        g.setGradientFill(hot);
        g.fillRect(face_);
    }

    // Scale.
    const auto at = [&](float defl, float radius) {
        const float a = angleFor(defl);
        return juce::Point<float>(p.x + radius * std::sin(a), p.y - radius * std::cos(a));
    };
    juce::Path baseline, red;
    baseline.addCentredArc(p.x, p.y, r, r, 0.0f, angleFor(vuDeflection(-20.0f)), angleFor(vuDeflection(0.0f)), true);
    red.addCentredArc(p.x, p.y + 0.0f, r + 3.0f, r + 3.0f, 0.0f, angleFor(vuDeflection(0.0f)), angleFor(vuDeflection(3.0f)), true);
    g.setColour(warm::ink);
    g.strokePath(baseline, juce::PathStrokeType(1.6f));
    g.setColour(juce::Colour(0xffd23c3c));
    g.strokePath(red, juce::PathStrokeType(7.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    const float labels[] = { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };
    for (float vu : labels) {
        const float d = vuDeflection(vu);
        const bool hot = vu > 0.0f;
        g.setColour(hot ? juce::Colour(0xffc02a2a) : warm::ink);
        g.drawLine({ at(d, r - 1.0f), at(d, r + 12.0f) }, vu == 0.0f ? 2.4f : 1.6f);
        const auto c = at(d, r + 26.0f);
        g.setFont(displayFont(600, vu == 0.0f ? 17.0f : 15.0f));
        const auto text = vu > 0.0f ? "+" + juce::String(juce::roundToInt(vu)) : juce::String(juce::roundToInt(vu)).replace("-", juce::String::fromUTF8("−"));
        g.drawText(text, juce::Rectangle<float>(c.x - 22.0f, c.y - 10.0f, 44.0f, 20.0f), juce::Justification::centred);
    }
    for (float vu : { -15.0f, -8.0f, -6.0f, -4.0f, -2.5f, -1.5f, -0.5f, 0.5f, 1.5f, 2.5f }) {
        const float d = vuDeflection(vu);
        g.setColour(vu > 0.0f ? juce::Colour(0xffc02a2a) : warm::ink.withAlpha(0.8f));
        g.drawLine({ at(d, r - 1.0f), at(d, r + 6.0f) }, 1.0f);
    }

    // Print: VU, what the needle shows, and the signs in the corners.
    g.setColour(warm::ink);
    g.setFont(displayFont(800, 34.0f));
    g.drawText("VU", juce::Rectangle<float>(p.x - 60.0f, p.y - r * 0.62f - 20.0f, 120.0f, 40.0f), juce::Justification::centred);
    g.setFont(uiFont(600, 12.0f));
    g.setColour(warm::ink.withAlpha(0.75f));
    const auto what = mode_ == Mode::Reduction ? juce::String::fromUTF8("REDUKCJA  dB") : juce::String::fromUTF8("WYJŚCIE  0 VU = −18 dBFS");
    g.drawText(what, juce::Rectangle<float>(p.x - 120.0f, p.y - r * 0.62f + 18.0f, 240.0f, 16.0f), juce::Justification::centred);
    g.setFont(displayFont(700, 22.0f));
    g.setColour(warm::ink);
    g.drawText(juce::String::fromUTF8("−"), juce::Rectangle<float>(face_.getX() + 14.0f, face_.getY() + 8.0f, 24.0f, 24.0f), juce::Justification::centred);
    g.setColour(juce::Colour(0xffc02a2a));
    g.drawText("+", juce::Rectangle<float>(face_.getRight() - 38.0f, face_.getY() + 8.0f, 24.0f, 24.0f), juce::Justification::centred);

    // The needle, with its shadow on the paper.
    const float a = angleFor(needle_.position);
    const auto dir = juce::Point<float>(std::sin(a), -std::cos(a));
    const auto tip = p + dir * (r + 14.0f), base = p + dir * 30.0f;
    g.setColour(juce::Colours::black.withAlpha(0.16f));
    g.drawLine({ base + juce::Point<float>(4.0f, 5.0f), tip + juce::Point<float>(4.0f, 5.0f) }, 3.0f);
    g.setColour(juce::Colour(0xff17120e));
    g.drawLine({ base, tip }, 2.2f);
    g.setColour(juce::Colour(0xffc02a2a));
    g.drawLine({ p + dir * (r - 6.0f), tip }, 2.2f);

    // The plate over the pivot, with the pilot lamps and the reading.
    const float plateTop = face_.getBottom() - 46.0f;
    juce::Path plate;
    plate.startNewSubPath(face_.getX(), face_.getBottom());
    plate.lineTo(face_.getX(), plateTop + 18.0f);
    plate.quadraticTo(p.x, plateTop - 18.0f, face_.getRight(), plateTop + 18.0f);
    plate.lineTo(face_.getRight(), face_.getBottom());
    plate.closeSubPath();
    juce::ColourGradient plateFill(juce::Colour(0xff2b2633), p.x, plateTop, juce::Colour(0xff141119), p.x, face_.getBottom(), false);
    g.setGradientFill(plateFill);
    g.fillPath(plate);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.strokePath(plate, juce::PathStrokeType(1.0f));

    auto lamp = [&](float cx, float level, const juce::String& name) {
        const float cy = face_.getBottom() - 20.0f;
        const float k = juce::jlimit(0.0f, 1.0f, level);
        if (k > 0.01f) {
            juce::ColourGradient halo(warm::amber.withAlpha(0.55f * k), cx, cy, juce::Colours::transparentBlack, cx + 22.0f, cy, true);
            g.setGradientFill(halo);
            g.fillEllipse(cx - 22.0f, cy - 22.0f, 44.0f, 44.0f);
        }
        g.setColour(juce::Colour(0xff3d2e1a).interpolatedWith(warm::amber.brighter(0.3f), k));
        g.fillEllipse(cx - 5.5f, cy - 5.5f, 11.0f, 11.0f);
        g.setColour(juce::Colours::white.withAlpha(0.25f + 0.4f * k));
        g.fillEllipse(cx - 2.5f, cy - 3.5f, 3.0f, 3.0f);
        g.setFont(uiFont(600, 10.5f));
        g.setColour(colour::dim);
        g.drawText(name, juce::Rectangle<float>(cx + 10.0f, cy - 8.0f, 70.0f, 16.0f), juce::Justification::centredLeft);
    };
    lamp(face_.getX() + 24.0f, heat_, juce::String::fromUTF8("LAMPA"));
    lamp(face_.getRight() - 96.0f, memory_ / 4.0f, juce::String::fromUTF8("PAMIĘĆ"));

    g.setFont(displayFont(700, 17.0f));
    g.setColour(warm::amber);
    juce::String reading;
    if (mode_ == Mode::Reduction) reading = reduction_ < 0.05f ? juce::String("0,0 dB") : juce::String::fromUTF8("−") + commaDb(reduction_) + " dB";
    else {
        // The number reads where the needle points, so the two never disagree.
        const float vu = vuFromDeflection(needle_.position);
        const auto sign = vu >= 0.05f ? juce::String("+") : (vu <= -0.05f ? juce::String::fromUTF8("−") : juce::String());
        reading = vu < -22.0f ? juce::String::fromUTF8("— VU") : sign + commaDb(std::abs(vu)) + " VU";
    }
    g.drawText(reading, juce::Rectangle<float>(p.x - 80.0f, face_.getBottom() - 30.0f, 160.0f, 22.0f), juce::Justification::centred);

    // Glass: a soft reflection over the top and a shade at the edges.
    juce::Path sheen;
    sheen.startNewSubPath(face_.getX(), face_.getY());
    sheen.lineTo(face_.getRight(), face_.getY());
    sheen.lineTo(face_.getRight(), face_.getY() + face_.getHeight() * 0.22f);
    sheen.quadraticTo(face_.getCentreX(), face_.getY() + face_.getHeight() * 0.48f, face_.getX(), face_.getY() + face_.getHeight() * 0.30f);
    sheen.closeSubPath();
    juce::ColourGradient glass(juce::Colours::white.withAlpha(0.20f), face_.getX(), face_.getY(), juce::Colours::white.withAlpha(0.0f), face_.getX(), face_.getY() + face_.getHeight() * 0.45f, false);
    g.setGradientFill(glass);
    g.fillPath(sheen);
    for (int i = 0; i < 6; ++i) {
        g.setColour(juce::Colours::black.withAlpha(0.07f - 0.01f * static_cast<float>(i)));
        g.drawRoundedRectangle(face_.reduced(static_cast<float>(i)), 9.0f, 1.0f);
    }
}

} // namespace lampa::ui
