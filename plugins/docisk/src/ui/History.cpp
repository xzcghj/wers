#include "History.h"
#include "plugin/Params.h"

namespace doc::ui {

namespace {
juce::String dbText(float db, int decimals = 1) { return (db > 0.04f ? "+" : "") + juce::String(db, decimals).replaceCharacter('.', ',') + " dB"; }
float toDb(float g) { return 20.0f * std::log10(juce::jmax(g, 1e-6f)); }
} // namespace

HistoryView::HistoryView(juce::AudioProcessorValueTreeState& state, const History& history)
    : threshold_(*state.getParameter(ids::threshold)), history_(history) {
    incoming_.resize(History::kSize / 2);
    cursor_ = history_.written();
    setTooltip(pl("Historia: szare to wejście, miętowe wyjście, koralowe to redukcja. Przeciągnij bursztynową linię, żeby zmienić próg."));
}

juce::Rectangle<float> HistoryView::plot() const { return getLocalBounds().toFloat().reduced(1.0f).withTrimmedLeft(36.0f).withTrimmedTop(26.0f).withTrimmedBottom(8.0f); }

float HistoryView::yForDb(float db) const {
    const auto r = plot();
    return juce::jmap(juce::jlimit(kBottomDb, kTopDb, db), kTopDb, kBottomDb, r.getY(), r.getBottom());
}

float HistoryView::dbForY(float y) const {
    const auto r = plot();
    return juce::jmap(y, r.getY(), r.getBottom(), kTopDb, kBottomDb);
}

void HistoryView::resized() {
    frames_.assign(static_cast<size_t>(juce::jmax(1, juce::roundToInt(plot().getWidth()))), Frame{ 0.0f, 0.0f, 0.0f, -120.0f });
    head_ = 0;
}

void HistoryView::tick() {
    const int n = history_.read(cursor_, incoming_.data(), static_cast<int>(frames_.size()));
    for (int i = 0; i < n; ++i) {
        frames_[head_] = incoming_[static_cast<size_t>(i)];
        head_ = (head_ + 1) % frames_.size();
    }
    if (n > 0 || dragging_) repaint();
}

void HistoryView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::surface);
    g.fillRoundedRectangle(bounds, 12.0f);
    const auto r = plot();

    // Grid every 6 dB, labelled every 12.
    g.setFont(uiFont(500, 11.0f));
    for (float db = -6.0f; db > kBottomDb; db -= 6.0f) {
        const float y = yForDb(db);
        g.setColour(colour::hairline.withAlpha(std::fmod(-db, 12.0f) < 0.5f ? 0.9f : 0.45f));
        g.fillRect(r.getX(), y, r.getWidth(), 1.0f);
        if (std::fmod(-db, 12.0f) < 0.5f) {
            g.setColour(colour::faint);
            g.drawText(juce::String(juce::roundToInt(db)), juce::Rectangle<float>(0.0f, y - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight);
        }
    }
    g.setColour(colour::faint);
    g.drawText("0", juce::Rectangle<float>(0.0f, r.getY() - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight);

    // Oldest frame on the left. Each column is one 5 ms frame.
    const size_t count = frames_.size();
    auto frameAt = [&](size_t column) -> const Frame& { return frames_[(head_ + column) % count]; };
    juce::Path in, out, gain, gainLine, lift;
    in.startNewSubPath(r.getX(), r.getBottom());
    out.startNewSubPath(r.getX(), r.getBottom());
    gain.startNewSubPath(r.getX(), r.getY());
    for (size_t c = 0; c < count; ++c) {
        const auto& f = frameAt(c);
        const float x = r.getX() + static_cast<float>(c) + 0.5f;
        in.lineTo(x, yForDb(toDb(f.in)));
        out.lineTo(x, yForDb(toDb(f.out)));
        const float gy = yForDb(juce::jmin(0.0f, f.gain));
        gain.lineTo(x, gy);
        if (c == 0) gainLine.startNewSubPath(x, gy);
        else gainLine.lineTo(x, gy);
        // Upward expansion hangs from the top too, in mint, by how much it lifts.
        const float ly = yForDb(-juce::jmax(0.0f, f.gain));
        if (c == 0) lift.startNewSubPath(x, ly);
        else lift.lineTo(x, ly);
    }
    in.lineTo(r.getRight(), r.getBottom());
    in.closeSubPath();
    out.lineTo(r.getRight(), r.getBottom());
    out.closeSubPath();
    gain.lineTo(r.getRight(), r.getY());
    gain.closeSubPath();

    g.saveState();
    g.reduceClipRegion(r.toNearestInt());
    g.setColour(hue::input.withAlpha(0.45f));
    g.fillPath(in);
    g.setGradientFill(juce::ColourGradient(hue::output.withAlpha(0.34f), 0.0f, r.getY(), hue::output.withAlpha(0.06f), 0.0f, r.getBottom(), false));
    g.fillPath(out);
    g.setColour(hue::output.withAlpha(0.9f));
    juce::Path outEdge;
    for (size_t c = 0; c < count; ++c) {
        const float x = r.getX() + static_cast<float>(c) + 0.5f, y = yForDb(toDb(frameAt(c).out));
        if (c == 0) outEdge.startNewSubPath(x, y);
        else outEdge.lineTo(x, y);
    }
    g.strokePath(outEdge, juce::PathStrokeType(1.2f));
    g.setColour(hue::reduction.withAlpha(0.16f));
    g.fillPath(gain);
    g.setColour(hue::reduction);
    g.strokePath(gainLine, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (!lift.isEmpty() && lift.getBounds().getBottom() > r.getY() + 1.0f) {
        g.setColour(colour::text.withAlpha(0.85f));
        g.strokePath(lift, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.restoreState();

    // The threshold.
    const float thr = threshold_.convertFrom0to1(threshold_.getValue());
    const float ty = yForDb(thr);
    const bool active = hover_ || dragging_;
    juce::Path dash;
    const float dashes[] = { 6.0f, 4.0f };
    juce::Path line;
    line.startNewSubPath(r.getX(), ty);
    line.lineTo(r.getRight(), ty);
    juce::PathStrokeType(active ? 2.0f : 1.4f).createDashedStroke(dash, line, dashes, 2);
    g.setColour(hue::threshold.withAlpha(active ? 1.0f : 0.85f));
    g.fillPath(dash);
    g.setFont(uiFont(600, 11.5f));
    const auto label = pl("próg ") + dbText(thr);
    const float lw = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(uiFont(600, 11.5f), label)) + 12.0f;
    const auto tag = juce::Rectangle<float>(r.getRight() - lw - 6.0f, ty - (ty - r.getY() > 22.0f ? 20.0f : -4.0f), lw, 16.0f);
    g.setColour(colour::surface.withAlpha(0.85f));
    g.fillRoundedRectangle(tag, 4.0f);
    g.setColour(hue::threshold);
    g.drawText(label, tag, juce::Justification::centred);

    // Title, legend and the gain now.
    g.setFont(uiFont(600, 12.0f));
    g.setColour(colour::dim);
    g.drawText(pl("HISTORIA"), juce::Rectangle<float>(14.0f, 5.0f, 90.0f, 18.0f), juce::Justification::centredLeft);
    struct Item { const char* name; juce::Colour c; };
    const Item items[] = { { "wejście", hue::input }, { "wyjście", hue::output }, { "redukcja", hue::reduction }, { "ekspansja", colour::text } };
    float x = 104.0f;
    g.setFont(uiFont(500, 11.5f));
    for (const auto& it : items) {
        g.setColour(it.c);
        g.fillRoundedRectangle(x, 10.0f, 10.0f, 10.0f, 2.0f);
        g.setColour(colour::dim);
        const auto name = pl(it.name);
        g.drawText(name, juce::Rectangle<float>(x + 14.0f, 5.0f, 70.0f, 18.0f), juce::Justification::centredLeft);
        x += 24.0f + static_cast<float>(juce::GlyphArrangement::getStringWidthInt(uiFont(500, 11.5f), name));
    }
    if (warning_.isNotEmpty()) {
        g.setColour(colour::weak);
        g.setFont(uiFont(600, 12.0f));
        g.drawText(warning_, juce::Rectangle<float>(r.getRight() - 260.0f, 5.0f, 254.0f, 18.0f), juce::Justification::centredRight);
    }
}

bool HistoryView::nearThreshold(juce::Point<float> p) const {
    const float thr = threshold_.convertFrom0to1(threshold_.getValue());
    return plot().expanded(0.0f, 6.0f).contains(p) && std::abs(p.y - yForDb(thr)) < 7.0f;
}

void HistoryView::mouseMove(const juce::MouseEvent& e) {
    const bool h = nearThreshold(e.position);
    if (h != hover_) {
        hover_ = h;
        setMouseCursor(h ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void HistoryView::mouseExit(const juce::MouseEvent&) {
    if (hover_) {
        hover_ = false;
        repaint();
    }
}

void HistoryView::mouseDown(const juce::MouseEvent& e) {
    if (!nearThreshold(e.position)) return;
    if (e.mods.isCtrlDown()) {
        threshold_.beginChangeGesture();
        threshold_.setValueNotifyingHost(threshold_.getDefaultValue());
        threshold_.endChangeGesture();
        repaint();
        return;
    }
    dragging_ = true;
    dragStartDb_ = threshold_.convertFrom0to1(threshold_.getValue());
    threshold_.beginChangeGesture();
}

void HistoryView::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_) return;
    const float moved = dbForY(e.position.y) - dbForY(e.mouseDownPosition.y);
    const float db = dragStartDb_ + moved * (e.mods.isShiftDown() ? 0.2f : 1.0f);
    threshold_.setValueNotifyingHost(threshold_.convertTo0to1(juce::jlimit(-60.0f, 0.0f, db)));
    repaint();
}

void HistoryView::mouseUp(const juce::MouseEvent&) {
    if (!dragging_) return;
    dragging_ = false;
    threshold_.endChangeGesture();
}

void HistoryView::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!nearThreshold(e.position)) return;
    threshold_.beginChangeGesture();
    threshold_.setValueNotifyingHost(threshold_.getDefaultValue());
    threshold_.endChangeGesture();
}

} // namespace doc::ui
