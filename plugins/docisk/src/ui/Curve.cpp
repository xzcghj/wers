#include "Curve.h"
#include "plugin/Params.h"

namespace doc::ui {

namespace {
juce::String dbText(float db) { return (db > 0.04f ? "+" : "") + juce::String(db, 1).replaceCharacter('.', ',') + " dB"; }
} // namespace

CurveView::CurveView(juce::AudioProcessorValueTreeState& state)
    : threshold_(*state.getParameter(ids::threshold)), ratio_(*state.getParameter(ids::ratio)), knee_(*state.getParameter(ids::knee)),
      range_(*state.getParameter(ids::range)) {
    setTooltip(pl("Krzywa: przeciągnij kółko na krzywej, żeby zmienić próg, kółko z prawej, żeby zmienić ratio, "
                  "a trójkąty pod krzywą, żeby poszerzyć kolano. Shift: dokładniej, Ctrl-klik: wartość domyślna."));
    for (auto& p : trail_) p = { -1.0f, -1.0f };
}

juce::Rectangle<float> CurveView::plot() const { return getLocalBounds().toFloat().reduced(1.0f).withTrimmedLeft(34.0f).withTrimmedTop(26.0f).withTrimmedRight(12.0f).withTrimmedBottom(30.0f); }
float CurveView::xForDb(float db) const { const auto r = plot(); return juce::jmap(db, kLowDb, kHighDb, r.getX(), r.getRight()); }
float CurveView::yForDb(float db) const { const auto r = plot(); return juce::jmap(db, kLowDb, kHighDb, r.getBottom(), r.getY()); }
float CurveView::dbForX(float x) const { const auto r = plot(); return juce::jmap(x, r.getX(), r.getRight(), kLowDb, kHighDb); }
float CurveView::dbForY(float y) const { const auto r = plot(); return juce::jmap(y, r.getBottom(), r.getY(), kLowDb, kHighDb); }

CurveView::Shape CurveView::shape() const {
    return { plain(threshold_), slopeForRatio(plain(ratio_)), plain(knee_), rangeLimitDb(plain(range_)) };
}

float CurveView::outputDb(float level, const Shape& s) const { return level + curveGainDb(level, s.threshold, s.slope, s.knee, s.range); }

juce::Point<float> CurveView::thresholdHandle() const {
    const auto s = shape();
    return { xForDb(s.threshold), yForDb(outputDb(s.threshold, s)) };
}

juce::Point<float> CurveView::ratioHandle() const {
    const auto s = shape();
    const auto r = plot();
    return { r.getRight(), juce::jlimit(r.getY(), r.getBottom(), yForDb(outputDb(kHighDb, s))) };
}

juce::Point<float> CurveView::kneeHandle(bool upper) const {
    const auto s = shape();
    return { xForDb(s.threshold + (upper ? 0.5f : -0.5f) * s.knee), plot().getBottom() + 9.0f };
}

CurveView::Handle CurveView::handleAt(juce::Point<float> p) const {
    if (p.getDistanceFrom(ratioHandle()) < 12.0f) return Handle::Ratio;
    if (p.getDistanceFrom(thresholdHandle()) < 12.0f) return Handle::Threshold;
    if (std::abs(p.y - kneeHandle(true).y) < 10.0f
        && (std::abs(p.x - kneeHandle(true).x) < 10.0f || std::abs(p.x - kneeHandle(false).x) < 10.0f))
        return Handle::Knee;
    // The dashed threshold line works too, anywhere along it.
    const auto r = plot();
    if (r.contains(p) && std::abs(p.x - thresholdHandle().x) < 5.0f) return Handle::Threshold;
    return Handle::None;
}

juce::RangedAudioParameter* CurveView::paramFor(Handle h) const {
    switch (h) {
    case Handle::Threshold: return &threshold_;
    case Handle::Ratio: return &ratio_;
    case Handle::Knee: return &knee_;
    default: return nullptr;
    }
}

void CurveView::set(juce::RangedAudioParameter& p, float value) { p.setValueNotifyingHost(p.convertTo0to1(value)); }

void CurveView::setSignal(float levelDb, float gainDb) {
    level_ = levelDb;
    gain_ = gainDb;
}

void CurveView::tick() {
    // The dot rises fast and falls slowly, like a meter, and leaves a short trail.
    shownLevel_ = level_ > shownLevel_ ? level_ : shownLevel_ + 0.25f * (level_ - shownLevel_);
    shownGain_ += 0.5f * (gain_ - shownGain_);
    if (shownLevel_ > kLowDb) trail_[trailAt_] = { xForDb(shownLevel_), yForDb(juce::jlimit(kLowDb, kHighDb, shownLevel_ + shownGain_)) };
    else trail_[trailAt_] = { -1.0f, -1.0f };
    trailAt_ = (trailAt_ + 1) % trail_.size();
    repaint();
}

void CurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::surface);
    g.fillRoundedRectangle(bounds, 12.0f);
    const auto r = plot();
    const auto s = shape();

    g.setFont(uiFont(500, 11.0f));
    for (float db = kLowDb + 6.0f; db < kHighDb; db += 6.0f) {
        const bool major = std::fmod(-db, 12.0f) < 0.5f;
        g.setColour(colour::hairline.withAlpha(major ? 0.9f : 0.4f));
        g.fillRect(xForDb(db), r.getY(), 1.0f, r.getHeight());
        g.fillRect(r.getX(), yForDb(db), r.getWidth(), 1.0f);
        if (major) {
            g.setColour(colour::faint);
            g.drawText(juce::String(juce::roundToInt(db)), juce::Rectangle<float>(0.0f, yForDb(db) - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
        }
    }
    g.setColour(colour::hairline);
    g.drawRect(r, 1.0f);

    // Knee region and the one-to-one diagonal.
    if (s.knee > 0.05f) {
        const float x0 = xForDb(s.threshold - 0.5f * s.knee), x1 = xForDb(s.threshold + 0.5f * s.knee);
        g.setColour(hue::threshold.withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(x0, r.getY(), x1 - x0, r.getHeight()).getIntersection(r));
    }
    {
        juce::Path diag, dashed;
        diag.startNewSubPath(r.getX(), r.getBottom());
        diag.lineTo(r.getRight(), r.getY());
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, diag, dashes, 2);
        g.setColour(colour::faint.withAlpha(0.7f));
        g.fillPath(dashed);
    }
    // Threshold line.
    const auto th = thresholdHandle();
    const bool thActive = hover_ == Handle::Threshold || drag_ == Handle::Threshold;
    {
        juce::Path line, dashed;
        line.startNewSubPath(th.x, r.getY());
        line.lineTo(th.x, r.getBottom());
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType(thActive ? 1.6f : 1.0f).createDashedStroke(dashed, line, dashes, 2);
        g.setColour(hue::threshold.withAlpha(thActive ? 0.9f : 0.5f));
        g.fillPath(dashed);
    }

    // The curve, with a glow, clipped to the plot.
    juce::Path curve;
    for (int i = 0; i <= 240; ++i) {
        const float in = juce::jmap(static_cast<float>(i), 0.0f, 240.0f, kLowDb, kHighDb);
        const float out = juce::jlimit(kLowDb - 6.0f, kHighDb + 6.0f, outputDb(in, s));
        if (i == 0) curve.startNewSubPath(xForDb(in), yForDb(out));
        else curve.lineTo(xForDb(in), yForDb(out));
    }
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());
    // Below the curve, shaded: the room the output can live in.
    juce::Path area(curve);
    area.lineTo(r.getRight(), r.getBottom() + 8.0f);
    area.lineTo(r.getX(), r.getBottom() + 8.0f);
    area.closeSubPath();
    g.setGradientFill(juce::ColourGradient(hue::output.withAlpha(0.13f), 0.0f, r.getY(), hue::output.withAlpha(0.02f), 0.0f, r.getBottom(), false));
    g.fillPath(area);
    g.setColour(hue::output.withAlpha(0.22f));
    g.strokePath(curve, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(hue::output);
    g.strokePath(curve, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The signal: a trail and a dot, with a coral drop line showing the reduction.
    for (size_t i = 0; i < trail_.size(); ++i) {
        const auto p = trail_[(trailAt_ + i) % trail_.size()];
        if (p.x < 0.0f) continue;
        const float age = static_cast<float>(i) / static_cast<float>(trail_.size());
        g.setColour(colour::text.withAlpha(0.05f + 0.25f * age));
        g.fillEllipse(p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
    }
    if (shownLevel_ > kLowDb) {
        const float x = xForDb(shownLevel_);
        const float yIn = yForDb(juce::jlimit(kLowDb, kHighDb, shownLevel_));
        const float yOut = yForDb(juce::jlimit(kLowDb, kHighDb, shownLevel_ + shownGain_));
        if (shownGain_ < -0.1f) {
            g.setColour(hue::reduction.withAlpha(0.8f));
            g.drawLine(x, yIn, x, yOut, 2.0f);
        }
        g.setColour(colour::text.withAlpha(0.25f));
        g.fillEllipse(x - 8.0f, yOut - 8.0f, 16.0f, 16.0f);
        g.setColour(colour::text);
        g.fillEllipse(x - 4.5f, yOut - 4.5f, 9.0f, 9.0f);
    }
    g.restoreState();

    // Handles.
    auto drawHandle = [&](juce::Point<float> p, juce::Colour c, bool active) {
        const float rad = active ? 8.0f : 6.5f;
        g.setColour(c.withAlpha(0.25f));
        g.fillEllipse(p.x - rad - 4.0f, p.y - rad - 4.0f, (rad + 4.0f) * 2.0f, (rad + 4.0f) * 2.0f);
        g.setColour(colour::bg);
        g.fillEllipse(p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour(c);
        g.drawEllipse(p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f, 2.2f);
    };
    drawHandle(th, hue::threshold, thActive);
    drawHandle(ratioHandle(), hue::output, hover_ == Handle::Ratio || drag_ == Handle::Ratio);
    const bool kneeActive = hover_ == Handle::Knee || drag_ == Handle::Knee;
    for (bool upper : { false, true }) {
        const auto p = kneeHandle(upper);
        juce::Path tri;
        const float w = kneeActive ? 7.0f : 5.5f;
        tri.addTriangle(p.x, p.y - w, p.x - w, p.y + w * 0.8f, p.x + w, p.y + w * 0.8f);
        g.setColour(hue::threshold.withAlpha(kneeActive ? 1.0f : 0.7f));
        g.fillPath(tri);
    }

    // Header: the settings in words.
    g.setFont(uiFont(600, 12.0f));
    g.setColour(colour::dim);
    g.drawText(pl("KRZYWA"), juce::Rectangle<float>(14.0f, 5.0f, 80.0f, 18.0f), juce::Justification::centredLeft);
    g.setFont(uiFont(600, 12.0f));
    juce::String info = ratioText(plain(ratio_)) + "   " + pl("kolano ") + juce::String(plain(knee_), 1).replaceCharacter('.', ',') + " dB";
    g.setColour(colour::text);
    g.drawText(info, juce::Rectangle<float>(80.0f, 5.0f, bounds.getWidth() - 92.0f, 18.0f), juce::Justification::centredRight);
    // While dragging, the value being changed, next to the handle.
    if (drag_ != Handle::None) {
        auto* p = paramFor(drag_);
        const auto text = p->getCurrentValueAsText();
        const auto at = drag_ == Handle::Ratio ? ratioHandle() + juce::Point<float>(-80.0f, -26.0f)
                        : drag_ == Handle::Knee ? kneeHandle(true) + juce::Point<float>(8.0f, -30.0f)
                                                : th + juce::Point<float>(10.0f, 10.0f);
        const auto box = juce::Rectangle<float>(juce::jlimit(r.getX(), r.getRight() - 76.0f, at.x), juce::jlimit(r.getY(), r.getBottom() - 20.0f, at.y), 76.0f, 20.0f);
        g.setColour(colour::raised);
        g.fillRoundedRectangle(box, 5.0f);
        g.setColour(colour::text);
        g.setFont(uiFont(600, 12.0f));
        g.drawText(text, box, juce::Justification::centred);
    }
    g.setFont(uiFont(500, 10.5f));
    g.setColour(colour::faint);
    // Out of the knee triangles' way: they live near the threshold, the label at whichever end is further from it.
    const bool left = thresholdHandle().x > r.getCentreX();
    g.drawText(pl("wejście dB"), juce::Rectangle<float>(left ? r.getX() : r.getRight() - 80.0f, r.getBottom() + 15.0f, 80.0f, 14.0f),
               left ? juce::Justification::centredLeft : juce::Justification::centredRight);
}

void CurveView::mouseMove(const juce::MouseEvent& e) {
    const auto h = handleAt(e.position);
    if (h != hover_) {
        hover_ = h;
        setMouseCursor(h == Handle::None ? juce::MouseCursor::NormalCursor
                       : h == Handle::Ratio ? juce::MouseCursor::UpDownResizeCursor
                                            : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void CurveView::mouseExit(const juce::MouseEvent&) {
    hover_ = Handle::None;
    repaint();
}

void CurveView::mouseDown(const juce::MouseEvent& e) {
    const auto h = handleAt(e.position);
    auto* p = paramFor(h);
    if (p == nullptr) return;
    if (e.mods.isCtrlDown()) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->getDefaultValue());
        p->endChangeGesture();
        repaint();
        return;
    }
    drag_ = h;
    dragStart_ = plain(*p);
    p->beginChangeGesture();
    repaint();
}

void CurveView::mouseDrag(const juce::MouseEvent& e) {
    auto* p = paramFor(drag_);
    if (p == nullptr) return;
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto s = shape();
    switch (drag_) {
    case Handle::Threshold: {
        const float moved = dbForX(e.position.x) - dbForX(e.mouseDownPosition.x);
        set(*p, juce::jlimit(-60.0f, 0.0f, dragStart_ + moved * fine));
        break;
    }
    case Handle::Knee: {
        // The knee widens both ways from the threshold; the handle follows the mouse.
        const float startX = e.mouseDownPosition.x;
        const float side = startX >= xForDb(s.threshold) ? 1.0f : -1.0f;
        const float moved = (dbForX(e.position.x) - dbForX(startX)) * side;
        set(*p, juce::jlimit(0.0f, 24.0f, dragStart_ + 2.0f * moved * fine));
        break;
    }
    case Handle::Ratio: {
        // Where the right end of the curve sits sets the slope above the threshold. The drag starts from the
        // ratio already set, so a fine (Shift) drag moves gently from there.
        const float over = kHighDb - s.threshold;
        float ratio = kRatioInfinite;
        if (over > 0.5f) {
            const float startOut = s.threshold + over / juce::jlimit(1.0f, kRatioInfinite, dragStart_);
            const float out = startOut + (dbForY(e.position.y) - dbForY(e.mouseDownPosition.y)) * fine;
            const float rise = out - s.threshold;
            ratio = rise <= over / kRatioInfinite ? kRatioInfinite : juce::jlimit(1.0f, kRatioInfinite, over / rise);
        }
        set(*p, ratio);
        break;
    }
    default: break;
    }
    repaint();
}

void CurveView::mouseUp(const juce::MouseEvent&) {
    if (auto* p = paramFor(drag_)) p->endChangeGesture();
    drag_ = Handle::None;
    repaint();
}

void CurveView::mouseDoubleClick(const juce::MouseEvent& e) {
    if (auto* p = paramFor(handleAt(e.position))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->getDefaultValue());
        p->endChangeGesture();
    }
}

} // namespace doc::ui
