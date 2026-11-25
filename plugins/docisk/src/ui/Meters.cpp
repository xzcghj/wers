#include "Meters.h"

namespace doc::ui {

namespace {
float toDb(float g) { return 20.0f * std::log10(juce::jmax(g, 1e-6f)); }
juce::String number(float db) {
    if (db <= -99.0f) return juce::String::fromUTF8("–∞");
    return juce::String(db, 1).replaceCharacter('.', ',');
}
} // namespace

MeterStrip::MeterStrip() {
    setTooltip(juce::String::fromUTF8("Wejście, redukcja i wyjście. Liczby na górze to szczyty od ostatniego kliknięcia: kliknij, żeby je wyzerować."));
}

void MeterStrip::Bar::feed(float db) {
    db = juce::jmax(kFloorDb - 1.0f, db);
    // Fast up, about 20 dB a second down, like a peak meter.
    level = db > level ? db : juce::jmax(db, level - 0.7f);
    if (db >= hold) {
        hold = db;
        holdFrames = 45;
    } else if (--holdFrames < 0) {
        hold = juce::jmax(level, hold - 0.5f);
    }
}

void MeterStrip::update(float inL, float inR, float outL, float outR, float gainDb, float clipDb) {
    const float in[4] = { toDb(inL), toDb(inR), toDb(outL), toDb(outR) };
    for (size_t i = 0; i < 4; ++i) bars_[i].feed(in[i]);
    constexpr int kHoldFrames = 90; // three seconds at 30 frames a second
    auto hold = [](float& held, int& age, float now, bool higher) {
        if ((higher ? now >= held : now <= held) || ++age > kHoldFrames) {
            held = now;
            age = 0;
        }
    };
    hold(inMax_, inAge_, juce::jmax(in[0], in[1]), true);
    hold(outMax_, outAge_, juce::jmax(in[2], in[3]), true);
    // The reduction meter falls fast and lets go a little slower, so short dips stay visible.
    gain_ = gainDb < gain_ ? gainDb : gain_ + 0.35f * (gainDb - gain_);
    gainHold_ = juce::jmin(gainHold_, gainDb);
    hold(reductionMax_, reductionAge_, juce::jmin(0.0f, gainDb), false);
    if (clipDb > 0.0f) clipFrames_ = 30;
    else if (clipFrames_ > 0) --clipFrames_;
    repaint();
}

void MeterStrip::mouseDown(const juce::MouseEvent&) {
    inMax_ = outMax_ = -120.0f;
    reductionMax_ = 0.0f;
    repaint();
}

void MeterStrip::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::surface);
    g.fillRoundedRectangle(bounds, 12.0f);

    const float top = 50.0f, bottom = bounds.getBottom() - 28.0f;
    auto yForLevel = [&](float db) { return juce::jmap(juce::jlimit(kFloorDb, 0.0f, db), 0.0f, kFloorDb, top, bottom); };
    auto yForReduction = [&](float db) { return juce::jmap(juce::jlimit(0.0f, kMaxReductionDb, -db), 0.0f, kMaxReductionDb, top, bottom); };

    const float barW = 7.0f, gap = 3.0f;
    const float groupW = barW * 2.0f + gap;
    const float x0 = 10.0f, xGr = bounds.getCentreX() - 7.0f, x2 = bounds.getRight() - 10.0f - groupW;

    auto levelBar = [&](float x, const Bar& b) {
        const auto track = juce::Rectangle<float>(x, top, barW, bottom - top);
        g.setColour(colour::bg);
        g.fillRoundedRectangle(track, 2.0f);
        const float y = yForLevel(b.level);
        if (b.level > kFloorDb) {
            juce::ColourGradient grad(colour::dies, 0.0f, top, hue::output.darker(0.2f), 0.0f, bottom, false);
            grad.addColour(juce::jmap(-6.0f, 0.0f, kFloorDb, 0.0f, 1.0f), colour::weak);
            grad.addColour(juce::jmap(-12.0f, 0.0f, kFloorDb, 0.0f, 1.0f), hue::output);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(track.withTop(y), 2.0f);
        }
        if (b.hold > kFloorDb) {
            g.setColour(b.hold > -0.1f ? colour::dies : colour::text.withAlpha(0.8f));
            g.fillRect(x, yForLevel(b.hold) - 1.0f, barW, 2.0f);
        }
    };
    levelBar(x0, bars_[0]);
    levelBar(x0 + barW + gap, bars_[1]);
    levelBar(x2, bars_[2]);
    levelBar(x2 + barW + gap, bars_[3]);

    // Reduction grows down from the top.
    const auto grTrack = juce::Rectangle<float>(xGr, top, 14.0f, bottom - top);
    g.setColour(colour::bg);
    g.fillRoundedRectangle(grTrack, 2.0f);
    if (gain_ < -0.05f) {
        g.setColour(hue::reduction);
        g.fillRoundedRectangle(grTrack.withBottom(yForReduction(gain_)), 2.0f);
    } else if (gain_ > 0.05f) {
        // Upward expansion shows as a mint bar from the top, by the same scale.
        g.setColour(hue::output);
        g.fillRoundedRectangle(grTrack.withBottom(yForReduction(-gain_)), 2.0f);
    }

    // Scale between the bars.
    g.setFont(uiFont(500, 9.5f));
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f }) {
        const float y = yForLevel(db);
        g.setColour(colour::faint);
        g.drawText(juce::String(juce::roundToInt(-db)), juce::Rectangle<float>(x0 + groupW, y - 6.0f, xGr - x0 - groupW, 12.0f), juce::Justification::centred);
    }
    for (float db : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f }) {
        const float y = yForReduction(-db);
        g.setColour(hue::reduction.withAlpha(0.55f));
        g.drawText(juce::String(juce::roundToInt(db)), juce::Rectangle<float>(xGr + 14.0f, y - 6.0f, x2 - xGr - 14.0f, 12.0f), juce::Justification::centred);
    }

    // Names and held numbers.
    g.setFont(uiFont(600, 11.0f));
    g.setColour(colour::dim);
    g.drawText(juce::String::fromUTF8("WE"), juce::Rectangle<float>(x0 - 4.0f, 8.0f, groupW + 8.0f, 14.0f), juce::Justification::centred);
    g.drawText(juce::String::fromUTF8("GR"), juce::Rectangle<float>(xGr - 8.0f, 8.0f, 30.0f, 14.0f), juce::Justification::centred);
    g.drawText(juce::String::fromUTF8("WY"), juce::Rectangle<float>(x2 - 4.0f, 8.0f, groupW + 8.0f, 14.0f), juce::Justification::centred);
    g.setFont(uiFont(600, 11.0f));
    g.setColour(inMax_ > -0.1f ? colour::dies : colour::text);
    g.drawFittedText(number(inMax_), juce::Rectangle<int>(static_cast<int>(x0) - 8, 24, static_cast<int>(groupW) + 16, 16), juce::Justification::centred, 1, 0.7f);
    g.setColour(hue::reduction);
    g.drawFittedText(number(reductionMax_), juce::Rectangle<int>(static_cast<int>(xGr) - 10, 24, 34, 16), juce::Justification::centred, 1, 0.7f);
    g.setColour(outMax_ > -0.1f ? colour::dies : colour::text);
    g.drawFittedText(number(outMax_), juce::Rectangle<int>(static_cast<int>(x2) - 8, 24, static_cast<int>(groupW) + 16, 16), juce::Justification::centred, 1, 0.7f);

    // The clip lamp lights when the soft clip bent a peak.
    const auto lamp = juce::Rectangle<float>(x2 - 8.0f, bottom + 8.0f, groupW + 16.0f, 14.0f);
    g.setColour(clipFrames_ > 0 ? colour::weak : colour::bg);
    g.fillRoundedRectangle(lamp, 3.0f);
    g.setFont(uiFont(600, 9.5f));
    g.setColour(clipFrames_ > 0 ? colour::ink : colour::faint);
    g.drawText("CLIP", lamp, juce::Justification::centred);
    g.setColour(colour::faint);
    g.drawText("dB", juce::Rectangle<float>(xGr - 8.0f, bottom + 8.0f, 30.0f, 14.0f), juce::Justification::centred);
}

} // namespace doc::ui
