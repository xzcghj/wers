#include "DecayView.h"
#include "plugin/PoglosProcessor.h"

namespace pog::ui {

namespace {

constexpr float kFloorDb = -72.0f;

juce::String secondsLabel(float s) {
    if (s < 1.0f) return juce::String(juce::roundToInt(s * 1000.0f)) + " ms";
    if (s >= 10.0f) return juce::String(juce::roundToInt(s)) + " s";
    return juce::String(s, 1).replaceCharacter('.', ',') + " s";
}

// When the first sound of the late network arrives: predelay plus the shortest line.
float lateOnset(const Params& p) {
    return p.predelayMs / 1000.0f + voiceFor(p.algo).lineMinMs * sizeScale(p.size) / 1000.0f;
}

} // namespace

DecayView::DecayView(PoglosProcessor& p) : processor_(p) { setOpaque(false); }

float DecayView::envelopeDb(const Params& p, int band, float t) {
    const float onset = p.algo == Algo::Nonlinear ? p.predelayMs / 1000.0f + 0.004f : lateOnset(p);
    if (t < onset) return -200.0f;
    const float since = t - onset;
    if (p.freeze) return 0.0f;
    if (p.algo == Algo::Nonlinear) {
        const float len = juce::jlimit(0.08f, kMaxGate, p.decay);
        const float x = since / len;
        if (x > 1.0f) return -200.0f;
        float peak = 1e-3f;
        for (float k = 0.0f; k <= 1.0f; k += 0.02f) peak = juce::jmax(peak, Reverb::gateEnvelope(p.attack, k));
        return 20.0f * std::log10(juce::jmax(Reverb::gateEnvelope(p.attack, x) / peak, 1e-5f));
    }
    const auto& v = voiceFor(p.algo);
    const float build = (v.buildMs + 350.0f * p.attack) * std::sqrt(sizeScale(p.size)) / 1000.0f;
    const float mix = juce::jlimit(0.0f, 1.0f, v.buildMix + p.attack);
    float gain = 1.0f;
    if (build > 0.002f && mix > 0.0f) gain = (1.0f - mix) + mix * std::pow(juce::jmin(1.0f, since / build), 1.5f);
    const float rt = rt60At(p, bandHz(band));
    return 20.0f * std::log10(juce::jmax(gain, 1e-4f)) - 60.0f * since / rt;
}

void DecayView::tick() {
    shown_ = processor_.currentParams();
    const auto& rv = processor_.reverb();
    for (int b = 0; b < 3; ++b) {
        const float target = rv.bandLevelDb(b);
        auto& l = live_[static_cast<size_t>(b)];
        l += (target > l ? 0.6f : 0.12f) * (target - l);
    }
    freeze_ = rv.freezeAmount();
    // A time axis that fits the longest band, rounded to a readable length.
    float longest = 0.3f;
    if (shown_.algo == Algo::Nonlinear) {
        longest = shown_.predelayMs / 1000.0f + juce::jlimit(0.08f, kMaxGate, shown_.decay) * 1.25f;
    } else {
        for (int b = 0; b < 3; ++b) longest = juce::jmax(longest, lateOnset(shown_) + rt60At(shown_, bandHz(b)) * 1.15f);
        longest += (voiceFor(shown_.algo).buildMs + 350.0f * shown_.attack) / 1000.0f;
    }
    if (shown_.freeze) longest = juce::jmax(longest, 4.0f);
    static constexpr float steps[] = { 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 24.0f, 32.0f, 48.0f, 64.0f };
    float pick = steps[14];
    for (float s : steps)
        if (s >= longest) {
            pick = s;
            break;
        }
    seconds_ = pick;
    repaint();
}

juce::Rectangle<float> DecayView::plotArea() const { return getLocalBounds().toFloat().withTrimmedLeft(46.0f).withTrimmedRight(78.0f).withTrimmedTop(44.0f).withTrimmedBottom(30.0f); }

float DecayView::xFor(float s) const {
    const auto a = plotArea();
    return a.getX() + a.getWidth() * juce::jlimit(0.0f, 1.0f, s / seconds_);
}

float DecayView::yFor(float db) const {
    const auto a = plotArea();
    return a.getY() + a.getHeight() * juce::jlimit(0.0f, 1.0f, db / kFloorDb);
}

void DecayView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::surface);
    g.fillRoundedRectangle(bounds, 12.0f);
    g.setColour(colour::hairline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);
    const auto plot = plotArea();

    g.setFont(uiFont(600, 11.5f));
    g.setColour(colour::dim);
    g.drawText(pl("ZANIK"), juce::Rectangle<float>(16.0f, 10.0f, 80.0f, 16.0f), juce::Justification::centredLeft);

    // Legend: the RT60 of each band, in its colour.
    const bool gate = shown_.algo == Algo::Nonlinear;
    {
        const char* names[3] = { "dół", "środek", "góra" };
        float x = 86.0f;
        for (int b = 0; b < 3; ++b) {
            juce::String text = pl(names[b]) + " ";
            if (shown_.freeze) text << juce::String::fromUTF8("∞");
            else if (gate) text << (b == 1 ? secondsLabel(juce::jlimit(0.08f, kMaxGate, shown_.decay)) : pl("–"));
            else text << secondsLabel(rt60At(shown_, bandHz(b)));
            const auto c = hue::forBand(b);
            g.setColour(c);
            g.fillRoundedRectangle(x, 14.0f, 14.0f, 4.0f, 2.0f);
            g.setFont(uiFont(600, 12.5f));
            g.setColour(colour::text);
            g.drawText(text, juce::Rectangle<float>(x + 19.0f, 7.0f, 120.0f, 18.0f), juce::Justification::centredLeft);
            x += 26.0f + juce::GlyphArrangement::getStringWidth(uiFont(600, 12.5f), text) + 14.0f;
        }
        g.setFont(uiFont(500, 11.0f));
        g.setColour(colour::faint);
        g.drawText(gate ? pl("bramka") : pl("RT60 na pasmo"), juce::Rectangle<float>(plot.getRight() - 160.0f, 9.0f, 160.0f, 16.0f), juce::Justification::centredRight);
    }

    // Grid.
    g.setFont(uiFont(500, 10.5f));
    for (float db : { 0.0f, -20.0f, -40.0f, -60.0f }) {
        const float y = yFor(db);
        g.setColour(db == -60.0f ? colour::hairline.brighter(0.3f) : colour::hairline.withAlpha(0.6f));
        g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        g.setColour(colour::faint);
        g.drawText(juce::String(juce::roundToInt(db)) + " dB", juce::Rectangle<float>(4.0f, y - 7.0f, 38.0f, 14.0f), juce::Justification::centredRight);
    }
    const float tick = seconds_ <= 0.5f ? 0.1f : seconds_ <= 1.5f ? 0.25f : seconds_ <= 4.0f ? 0.5f : seconds_ <= 8.0f ? 1.0f : seconds_ <= 16.0f ? 2.0f : seconds_ <= 32.0f ? 4.0f : 8.0f;
    for (float t = 0.0f; t <= seconds_ + 1e-3f; t += tick) {
        const float x = xFor(t);
        g.setColour(colour::hairline.withAlpha(0.45f));
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        g.setColour(colour::faint);
        g.drawText(secondsLabel(t), juce::Rectangle<float>(x - 30.0f, plot.getBottom() + 6.0f, 60.0f, 14.0f), juce::Justification::centred);
    }

    // Predelay.
    if (shown_.predelayMs > 0.5f) {
        const float x = xFor(shown_.predelayMs / 1000.0f);
        g.setColour(hue::space.withAlpha(0.5f));
        const float dash[2] = { 3.0f, 3.0f };
        g.drawDashedLine(juce::Line<float>(x, plot.getY(), x, plot.getBottom()), dash, 2, 1.0f);
    }

    // Early reflections: a line for each, as tall as it is loud.
    {
        std::array<EarlyTap, kMaxEarlyTaps> taps{};
        const int n = earlyTaps(shown_.algo, taps);
        const float early = juce::jmin(1.0f, 2.0f * (1.0f - shown_.balance));
        if (early > 0.01f) {
            const float scale = sizeScale(shown_.size);
            for (int k = 0; k < n; ++k) {
                const auto& t = taps[static_cast<size_t>(k)];
                const float level = std::sqrt(t.gainL * t.gainL + t.gainR * t.gainR) * early;
                const float db = 20.0f * std::log10(juce::jmax(level * 1.5f, 1e-4f)) - 6.0f;
                const float x = xFor(shown_.predelayMs / 1000.0f + t.ms * scale / 1000.0f);
                const float top = yFor(juce::jmin(-6.0f, db));
                g.setColour(hue::space.withAlpha(0.55f));
                g.drawLine(x, plot.getBottom(), x, top, 1.0f);
                g.setColour(hue::space);
                g.fillEllipse(x - 1.8f, top - 1.8f, 3.6f, 3.6f);
            }
        }
    }

    // The curves, high first so the middle sits on top.
    for (int b : { 2, 0, 1 }) {
        juce::Path curve;
        bool started = false;
        const int steps = juce::roundToInt(plot.getWidth());
        float lastDb = -200.0f;
        for (int i = 0; i <= steps; ++i) {
            const float t = seconds_ * static_cast<float>(i) / static_cast<float>(steps);
            const float db = envelopeDb(shown_, b, t);
            if (db < kFloorDb - 1.0f) {
                if (started && lastDb > kFloorDb) curve.lineTo(xFor(t), yFor(kFloorDb));
                lastDb = db;
                if (started) break;
                continue;
            }
            if (!started) {
                curve.startNewSubPath(xFor(t), yFor(kFloorDb));
                started = true;
            }
            curve.lineTo(xFor(t), yFor(db));
            lastDb = db;
        }
        if (!started) continue;
        const auto c = hue::forBand(b).interpolatedWith(hue::ice, freeze_ * 0.6f);
        if (b == 1) {
            juce::Path area(curve);
            area.lineTo(curve.getCurrentPosition().x, plot.getBottom());
            area.closeSubPath();
            juce::ColourGradient fill(c.withAlpha(0.28f), 0.0f, plot.getY(), c.withAlpha(0.02f), 0.0f, plot.getBottom(), false);
            g.setGradientFill(fill);
            g.fillPath(area);
        }
        g.setColour(c.withAlpha(0.25f));
        g.strokePath(curve, juce::PathStrokeType(b == 1 ? 7.0f : 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(c);
        g.strokePath(curve, juce::PathStrokeType(b == 1 ? 2.4f : 1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setFont(uiFont(500, 10.5f));
    g.setColour(colour::faint);
    g.drawText("RT60", juce::Rectangle<float>(plot.getRight() - 40.0f, yFor(-60.0f) - 15.0f, 38.0f, 13.0f), juce::Justification::centredRight);

    // Live wet level per band.
    const auto meters = juce::Rectangle<float>(plot.getRight() + 18.0f, plot.getY(), 52.0f, plot.getHeight());
    const char* letters[3] = { "D", "Ś", "G" };
    for (int b = 0; b < 3; ++b) {
        const auto bar = juce::Rectangle<float>(meters.getX() + static_cast<float>(b) * 18.0f, meters.getY(), 10.0f, meters.getHeight());
        g.setColour(colour::bg);
        g.fillRoundedRectangle(bar, 3.0f);
        const float level = juce::jlimit(0.0f, 1.0f, (live_[static_cast<size_t>(b)] - kFloorDb) / -kFloorDb);
        const auto c = hue::forBand(b);
        g.setColour(c);
        g.fillRoundedRectangle(bar.withTop(bar.getBottom() - bar.getHeight() * level), 3.0f);
        g.setFont(uiFont(600, 10.5f));
        g.setColour(colour::dim);
        g.drawText(pl(letters[b]), juce::Rectangle<float>(bar.getX() - 4.0f, plot.getBottom() + 6.0f, 18.0f, 14.0f), juce::Justification::centred);
    }
}

} // namespace pog::ui
