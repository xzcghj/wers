#include "AirView.h"
#include "plugin/PowiewProcessor.h"

namespace powiew::ui {

namespace {
constexpr float kLoHz = 30.0f, kHiHz = 22000.0f, kLogShare = 0.62f;
constexpr const char* kAmountIds[2] = { ids::presence, ids::air };
constexpr const char* kHzIds[2] = { ids::presenceHz, ids::airHz };

juce::String hzText(float hz) {
    if (hz >= 1000.0f) return juce::String(hz / 1000.0f, hz >= 10000.0f ? 1 : 1).replaceCharacter('.', ',') + " kHz";
    return juce::String(juce::roundToInt(hz)) + " Hz";
}
} // namespace

AirView::AirView(PowiewProcessor& p) : processor_(p) {
    window_.resize(kFftSize);
    for (int i = 0; i < kFftSize; ++i) window_[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(kFftSize - 1));
    out_.resize(kFftSize);
    add_.resize(kFftSize);
    outFft_.assign(kFftSize * 2, 0.0f);
    addFft_.assign(kFftSize * 2, 0.0f);
    outDb_.assign(kFftSize / 2 + 1, kFloorDb);
    addDb_.assign(kFftSize / 2 + 1, kFloorDb);
    addShown_.assign(kFftSize / 2 + 1, kFloorDb);
    setTooltip(pl("Szare: widmo wyjścia. W kolorze: to, co Powiew dodaje. Linia przerywana: podbicie ustawione pokrętłami; ciągła: tyle, ile detektor przepuszcza teraz. "
                  "Przeciągnij uchwyt w bok, by zmienić częstotliwość, w górę i w dół, by zmienić ilość. Shift: dokładniej. Dwuklik: zero."));
}

float AirView::xForHz(float hz) const {
    const auto r = plot();
    hz = juce::jlimit(kLoHz, kHiHz, hz);
    const float t = kLogShare * std::log(hz / kLoHz) / std::log(kHiHz / kLoHz) + (1.0f - kLogShare) * (hz - kLoHz) / (kHiHz - kLoHz);
    return r.getX() + t * r.getWidth();
}

float AirView::hzForX(float x) const {
    // The axis has no closed inverse; it rises everywhere, so halving the interval finds it.
    float lo = kLoHz, hi = kHiHz;
    for (int i = 0; i < 40; ++i) {
        const float mid = std::sqrt(lo * hi);
        (xForHz(mid) < x ? lo : hi) = mid;
    }
    return std::sqrt(lo * hi);
}

float AirView::yForBoost(float db) const {
    const auto r = plot();
    const float zero = r.getY() + 0.74f * r.getHeight(), top = r.getY() + 0.12f * r.getHeight();
    return zero + (top - zero) * db / kCurveTopDb;
}

float AirView::boostForY(float y) const {
    const auto r = plot();
    const float zero = r.getY() + 0.74f * r.getHeight(), top = r.getY() + 0.12f * r.getHeight();
    return (y - zero) / (top - zero) * kCurveTopDb;
}

float AirView::maxDb(int band) const {
    const int c = juce::jlimit(0, 2, juce::roundToInt(processor_.state().getRawParameterValue(ids::character)->load()));
    return band == 0 ? kShapes[static_cast<size_t>(c)].presenceDb : kShapes[static_cast<size_t>(c)].airDb;
}

juce::Point<float> AirView::handle(int band) const {
    const float amount = processor_.state().getRawParameterValue(kAmountIds[band])->load() * 0.01f;
    const float hz = processor_.state().getRawParameterValue(kHzIds[band])->load();
    return { xForHz(hz), yForBoost(amount * maxDb(band)) };
}

void AirView::tick() {
    processor_.engine().copySpectrumSources(out_.data(), add_.data(), kFftSize);
    for (size_t i = 0; i < static_cast<size_t>(kFftSize); ++i) {
        outFft_[i] = out_[i] * window_[i];
        addFft_[i] = add_[i] * window_[i];
    }
    std::fill(outFft_.begin() + kFftSize, outFft_.end(), 0.0f);
    std::fill(addFft_.begin() + kFftSize, addFft_.end(), 0.0f);
    fft_.performFrequencyOnlyForwardTransform(outFft_.data(), true);
    fft_.performFrequencyOnlyForwardTransform(addFft_.data(), true);
    // A Hann window keeps half the amplitude: four over the size gives a sine's level back.
    const float norm = 4.0f / static_cast<float>(kFftSize);
    for (size_t b = 0; b < outDb_.size(); ++b) {
        const float o = juce::Decibels::gainToDecibels(outFft_[b] * norm, kFloorDb - 10.0f);
        const float a = juce::Decibels::gainToDecibels(addFft_[b] * norm, kFloorDb - 10.0f);
        outDb_[b] += (o > outDb_[b] ? 0.6f : 0.12f) * (o - outDb_[b]);
        addDb_[b] += (a > addDb_[b] ? 0.6f : 0.12f) * (a - addDb_[b]);
        // What sits far under the output (the bell's gentle skirt in the bass) changes nothing you hear: fade it out.
        const float under = outDb_[b] - addDb_[b];
        addShown_[b] = under > 20.0f ? addDb_[b] - 2.0f * (under - 20.0f) : addDb_[b];
    }
    repaint();
}

juce::Path AirView::spectrumPath(const std::vector<float>& dbs, const juce::Rectangle<float>& area, bool closed) const {
    const double fs = processor_.sampleRate();
    juce::Path path;
    bool started = false;
    float lastX = area.getX();
    const float right = juce::jmin(area.getRight(), xForHz(static_cast<float>(0.5 * fs)));
    for (float x = area.getX(); x <= right; x += 2.0f) {
        const float f0 = hzForX(x - 1.0f), f1 = hzForX(x + 1.0f);
        const float bin0 = static_cast<float>(f0 / fs * kFftSize), bin1 = static_cast<float>(f1 / fs * kFftSize);
        float level = kFloorDb;
        if (bin1 - bin0 < 1.0f) {
            // Fewer than one bin per step at the bottom: read between bins, so the curve stays smooth.
            const float at = juce::jlimit(1.0f, static_cast<float>(kFftSize / 2 - 1), 0.5f * (bin0 + bin1));
            const int i = static_cast<int>(at);
            const float t = at - static_cast<float>(i);
            level = dbs[static_cast<size_t>(i)] + t * (dbs[static_cast<size_t>(i + 1)] - dbs[static_cast<size_t>(i)]);
        } else {
            const int b0 = juce::jlimit(1, kFftSize / 2, static_cast<int>(bin0));
            const int b1 = juce::jlimit(b0, kFftSize / 2, static_cast<int>(bin1));
            float sum = 0.0f;
            for (int b = b0; b <= b1; ++b) sum += juce::Decibels::decibelsToGain(dbs[static_cast<size_t>(b)]);
            // Average power over the bins, so noise does not read louder where bins are crowded.
            level = juce::Decibels::gainToDecibels(sum / static_cast<float>(b1 - b0 + 1), kFloorDb - 10.0f);
        }
        // A gentle tilt (+3 dB an octave from 1 kHz) so music reads level across the screen, as it does to the ear.
        level += 3.0f * std::log2(juce::jmax(20.0f, 0.5f * (f0 + f1)) / 1000.0f);
        const float y = area.getBottom() - area.getHeight() * juce::jlimit(0.0f, 1.0f, (level - kFloorDb) / (kTopDb - kFloorDb));
        if (!started) {
            if (closed) {
                path.startNewSubPath(x, area.getBottom());
                path.lineTo(x, y);
            } else {
                path.startNewSubPath(x, y);
            }
            started = true;
        } else {
            path.lineTo(x, y);
        }
        lastX = x;
    }
    if (closed && started) {
        path.lineTo(lastX, area.getBottom());
        path.closeSubPath();
    }
    return path;
}

void AirView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    const auto area = plot();
    g.setColour(colour::surface);
    g.fillRoundedRectangle(bounds, 12.0f);
    juce::Graphics::ScopedSaveState clip(g);
    juce::Path rounded;
    rounded.addRoundedRectangle(bounds, 12.0f);
    g.reduceClipRegion(rounded);

    const auto& engine = processor_.engine();
    const double fs = processor_.sampleRate();
    const float pHz = engine.presenceHzNow(), aHz = engine.airHzNow();

    // The bands' homes: a soft wash of colour where each one works.
    {
        const float x0 = xForHz(pHz * 0.6f), x1 = xForHz(pHz * 1.7f);
        juce::ColourGradient wash(hue::presence.withAlpha(0.0f), x0, 0.0f, hue::presence.withAlpha(0.0f), x1, 0.0f, false);
        wash.addColour(0.5, hue::presence.withAlpha(0.07f));
        g.setGradientFill(wash);
        g.fillRect(juce::Rectangle<float>(x0, area.getY(), x1 - x0, area.getHeight()));
        const float a0 = xForHz(aHz * 0.7f);
        juce::ColourGradient wash2(hue::air.withAlpha(0.0f), a0, 0.0f, hue::air.withAlpha(0.08f), xForHz(aHz * 1.3f), 0.0f, false);
        g.setGradientFill(wash2);
        g.fillRect(juce::Rectangle<float>(a0, area.getY(), area.getRight() - a0, area.getHeight()));
    }

    // Grid.
    g.setFont(uiFont(500, 11.0f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 3000.0f, 5000.0f, 7000.0f, 10000.0f, 14000.0f, 20000.0f }) {
        const float x = xForHz(hz);
        g.setColour(colour::hairline.withAlpha(0.55f));
        g.fillRect(x, area.getY(), 1.0f, area.getHeight());
        g.setColour(colour::faint);
        const auto label = hz >= 1000.0f ? juce::String(juce::roundToInt(hz / 1000.0f)) + "k" : juce::String(juce::roundToInt(hz));
        g.drawText(label, juce::Rectangle<float>(x - 20.0f, area.getBottom() + 2.0f, 40.0f, 16.0f), juce::Justification::centred);
    }
    for (float db : { 0.0f, 3.0f, 6.0f, 9.0f, 12.0f }) {
        const float y = yForBoost(db);
        g.setColour(db == 0.0f ? colour::hairline : colour::hairline.withAlpha(0.35f));
        g.fillRect(area.getX(), y, area.getWidth(), 1.0f);
        if (db > 0.0f) {
            g.setColour(colour::faint);
            g.drawText("+" + juce::String(juce::roundToInt(db)) + " dB", juce::Rectangle<float>(area.getRight() - 52.0f, y - 14.0f, 46.0f, 13.0f), juce::Justification::centredRight);
        }
    }

    // The output, quietly, then what is added, in colour on top of it.
    const auto outFill = spectrumPath(outDb_, area, true);
    g.setColour(colour::text.withAlpha(0.07f));
    g.fillPath(outFill);
    g.setColour(colour::dim.withAlpha(0.45f));
    g.strokePath(spectrumPath(outDb_, area, false), juce::PathStrokeType(1.2f));

    const auto addFill = spectrumPath(addShown_, area, true);
    juce::ColourGradient airy(hue::presence.withAlpha(0.55f), xForHz(3000.0f), 0.0f, hue::air.withAlpha(0.7f), xForHz(15000.0f), 0.0f, false);
    g.setGradientFill(airy);
    g.fillPath(addFill);
    juce::ColourGradient edge(hue::presence, xForHz(3000.0f), 0.0f, hue::air, xForHz(15000.0f), 0.0f, false);
    g.setGradientFill(edge);
    g.strokePath(spectrumPath(addShown_, area, false), juce::PathStrokeType(1.6f));

    // The curve: dashed as set, solid as it is right now.
    juce::Path setCurve, nowCurve;
    const float right = juce::jmin(area.getRight(), xForHz(static_cast<float>(0.49 * fs)));
    for (float x = area.getX(); x <= right; x += 2.0f) {
        const double hz = hzForX(x);
        const float ySet = yForBoost(static_cast<float>(boostDbAt(hz, fs, pHz, engine.presenceAmountSet(), aHz, engine.airAmountSet())));
        const float yNow = yForBoost(static_cast<float>(boostDbAt(hz, fs, pHz, engine.presenceAmountNow(), aHz, engine.airAmountNow())));
        if (x == area.getX()) {
            setCurve.startNewSubPath(x, ySet);
            nowCurve.startNewSubPath(x, yNow);
        } else {
            setCurve.lineTo(x, ySet);
            nowCurve.lineTo(x, yNow);
        }
    }
    juce::Path dashed;
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType(1.4f).createDashedStroke(dashed, setCurve, dashes, 2);
    g.setColour(colour::text.withAlpha(0.45f));
    g.fillPath(dashed);
    g.setColour(colour::text.withAlpha(0.18f));
    g.strokePath(nowCurve, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour::text);
    g.strokePath(nowCurve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Handles, with their reading while you touch them.
    for (int b = 0; b < 2; ++b) {
        const auto at = handle(b);
        const auto tint = b == 0 ? hue::presence : hue::air;
        const bool hot = hover_ == b || dragging_ == b;
        const float r = hot ? 9.0f : 7.5f;
        g.setColour(tint.withAlpha(hot ? 0.3f : 0.18f));
        g.fillEllipse(at.x - r - 5.0f, at.y - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f);
        g.setColour(tint);
        g.fillEllipse(at.x - r, at.y - r, r * 2.0f, r * 2.0f);
        g.setColour(colour::ink);
        g.drawEllipse(at.x - r, at.y - r, r * 2.0f, r * 2.0f, 1.5f);
        g.setFont(uiFont(600, 12.0f));
        g.setColour(tint);
        const auto name = b == 0 ? pl("Obecność") : pl("Powietrze");
        auto text = name;
        if (hot) {
            const float amount = processor_.state().getRawParameterValue(kAmountIds[b])->load();
            const float hz = processor_.state().getRawParameterValue(kHzIds[b])->load();
            text << "  " << hzText(hz) << juce::String::fromUTF8(" · ") << juce::String(juce::roundToInt(amount)) << "%";
        }
        const float w = 190.0f;
        float tx = at.x - w * 0.5f;
        tx = juce::jlimit(area.getX() + 4.0f, area.getRight() - w - 4.0f, tx);
        g.drawText(text, juce::Rectangle<float>(tx, at.y - r - 24.0f, w, 16.0f), juce::Justification::centred);
    }

    // Legend.
    g.setFont(uiFont(500, 11.5f));
    float lx = area.getX() + 14.0f;
    const float ly = area.getY() + 10.0f;
    auto key = [&](juce::Colour c, const juce::String& text, bool line) {
        g.setColour(c);
        if (line) g.fillRect(lx, ly + 6.0f, 14.0f, 2.0f);
        else g.fillRoundedRectangle(lx, ly + 2.0f, 14.0f, 10.0f, 2.0f);
        g.setColour(colour::dim);
        const int w = juce::GlyphArrangement::getStringWidthInt(uiFont(500, 11.5f), text);
        g.drawText(text, juce::Rectangle<float>(lx + 20.0f, ly, static_cast<float>(w) + 4.0f, 14.0f), juce::Justification::centredLeft);
        lx += 20.0f + static_cast<float>(w) + 18.0f;
    };
    key(colour::dim.withAlpha(0.7f), pl("wyjście"), true);
    key(hue::air.interpolatedWith(hue::presence, 0.4f), pl("dodane"), false);
    key(colour::text, pl("podbicie teraz"), true);
    key(colour::text.withAlpha(0.45f), pl("ustawione"), true);
}

int AirView::handleAt(juce::Point<float> at) const {
    int best = -1;
    float distance = 20.0f;
    for (int b = 0; b < 2; ++b) {
        const float d = handle(b).getDistanceFrom(at);
        if (d < distance) {
            distance = d;
            best = b;
        }
    }
    return best;
}

void AirView::mouseMove(const juce::MouseEvent& e) {
    const int h = handleAt(e.position);
    if (h != hover_) {
        hover_ = h;
        setMouseCursor(h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void AirView::mouseExit(const juce::MouseEvent&) {
    if (hover_ != -1) {
        hover_ = -1;
        repaint();
    }
}

void AirView::mouseDown(const juce::MouseEvent& e) {
    dragging_ = handleAt(e.position);
    if (dragging_ < 0) return;
    dragStart_ = e.position;
    startAmount_ = processor_.state().getRawParameterValue(kAmountIds[dragging_])->load();
    startHz_ = processor_.state().getRawParameterValue(kHzIds[dragging_])->load();
    processor_.state().getParameter(kAmountIds[dragging_])->beginChangeGesture();
    processor_.state().getParameter(kHzIds[dragging_])->beginChangeGesture();
}

void AirView::setFromMouse(int band, juce::Point<float> at, bool fine) {
    const float k = fine ? 0.2f : 1.0f;
    const auto delta = (at - dragStart_) * k;
    auto* amount = processor_.state().getParameter(kAmountIds[band]);
    auto* hz = processor_.state().getParameter(kHzIds[band]);
    const float dbMoved = boostForY(dragStart_.y + delta.y) - boostForY(dragStart_.y);
    const float newAmount = juce::jlimit(0.0f, 100.0f, startAmount_ + 100.0f * dbMoved / maxDb(band));
    const float newHz = hzForX(xForHz(startHz_) + delta.x);
    amount->setValueNotifyingHost(amount->convertTo0to1(newAmount));
    hz->setValueNotifyingHost(hz->convertTo0to1(newHz));
    repaint();
}

void AirView::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ >= 0) setFromMouse(dragging_, e.position, e.mods.isShiftDown());
}

void AirView::mouseUp(const juce::MouseEvent&) {
    if (dragging_ < 0) return;
    processor_.state().getParameter(kAmountIds[dragging_])->endChangeGesture();
    processor_.state().getParameter(kHzIds[dragging_])->endChangeGesture();
    dragging_ = -1;
    repaint();
}

void AirView::mouseDoubleClick(const juce::MouseEvent& e) {
    const int h = handleAt(e.position);
    if (h < 0) return;
    auto* amount = processor_.state().getParameter(kAmountIds[h]);
    amount->setValueNotifyingHost(0.0f);
}

} // namespace powiew::ui
