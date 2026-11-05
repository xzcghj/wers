#include "SpectrumView.h"
#include "Strings.h"

namespace barwa {

using namespace wers::ui;

namespace {

constexpr double kLowestHz = 20.0, kHighestHz = 20000.0;
constexpr float kTopDb = 6.0f, kBottomDb = -84.0f;
constexpr float kSlopeDbPerOctave = 3.0f; // music falls with frequency, so the view leans the other way
constexpr float kRise = 0.6f, kFallDbPerTick = 1.6f;
constexpr float kAddedAboveDb = 1.0f;
constexpr float kHandleReach = 10.0f;
constexpr juce::uint32 kOverHoldMs = 1500;
constexpr int kAxisHeight = 22, kHandleLabelHeight = 22;

float powerToLevelDb(double power) noexcept {
    return power > 1.0e-14 ? static_cast<float>(10.0 * std::log10(power)) : -140.0f;
}

} // namespace

SpectrumView::SpectrumView(BarwaProcessor& processor) : processor_(processor) {
    split_ = { processor.state().getParameter(ids::xover1), processor.state().getParameter(ids::xover2) };
    setTitle(pl(text::spectrumTitle));
    window_.resize(kFftSize);
    for (int i = 0; i < kFftSize; ++i) window_[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / kFftSize);
    inHistory_.assign(kFftSize, 0.0f);
    outHistory_.assign(kFftSize, 0.0f);
    spectrum_.resize(kFftSize);
    inPower_.assign(kFftSize / 2, 0.0);
    outPower_.assign(kFftSize / 2, 0.0);
    shownIn_.fill(kBottomDb);
    shownOut_.fill(kBottomDb);
}

double SpectrumView::topFrequency() const { return juce::jmin(kHighestHz, processor_.hostRate() * 0.5); }

float SpectrumView::frequencyToX(double hz) const {
    return static_cast<float>(getWidth()) * static_cast<float>(std::log(hz / kLowestHz) / std::log(topFrequency() / kLowestHz));
}

double SpectrumView::xToFrequency(float x) const {
    return kLowestHz * std::pow(topFrequency() / kLowestHz, static_cast<double>(x) / juce::jmax(1, getWidth()));
}

float SpectrumView::levelToY(float db) const {
    const float top = kHandleLabelHeight + 4.0f, bottom = static_cast<float>(getHeight() - kAxisHeight);
    return juce::jmap(juce::jlimit(kBottomDb, kTopDb, db), kTopDb, kBottomDb, top, bottom);
}

float SpectrumView::handleX(int index) const {
    return frequencyToX(static_cast<double>(split_[static_cast<size_t>(index)]->convertFrom0to1(split_[static_cast<size_t>(index)]->getValue())));
}

int SpectrumView::handleAt(float x) const {
    int best = -1;
    float nearest = kHandleReach;
    for (int i = 0; i < handleCount(); ++i) {
        const float distance = std::abs(handleX(i) - x);
        if (distance <= nearest) {
            nearest = distance;
            best = i;
        }
    }
    return best;
}

void SpectrumView::mouseMove(const juce::MouseEvent& e) {
    const int now = handleAt(e.position.x);
    if (now != hovered_) {
        hovered_ = now;
        setMouseCursor(now >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SpectrumView::mouseExit(const juce::MouseEvent&) {
    if (hovered_ >= 0) {
        hovered_ = -1;
        repaint();
    }
}

void SpectrumView::mouseDown(const juce::MouseEvent& e) {
    dragged_ = handleAt(e.position.x);
    if (dragged_ >= 0) split_[static_cast<size_t>(dragged_)]->beginChangeGesture();
    mouseDrag(e);
}

void SpectrumView::mouseDrag(const juce::MouseEvent& e) {
    if (dragged_ < 0) return;
    auto* param = split_[static_cast<size_t>(dragged_)];
    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(xToFrequency(e.position.x))));
    repaint();
}

void SpectrumView::mouseUp(const juce::MouseEvent&) {
    if (dragged_ >= 0) split_[static_cast<size_t>(dragged_)]->endChangeGesture();
    dragged_ = -1;
}

void SpectrumView::resized() {}

void SpectrumView::analyse() {
    for (int i = 0; i < kFftSize; ++i)
        spectrum_[static_cast<size_t>(i)] = { static_cast<double>(inHistory_[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)]),
                                              static_cast<double>(outHistory_[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)]) };
    fft_.transform(spectrum_);
    // Two real signals in one transform. A full-scale sine reads 0 dB.
    const double scale = 16.0 / (static_cast<double>(kFftSize) * kFftSize);
    for (size_t k = 1; k < kFftSize / 2; ++k) {
        const auto a = spectrum_[k], b = std::conj(spectrum_[kFftSize - k]);
        inPower_[k] = std::norm(a + b) * 0.25 * scale;
        outPower_[k] = std::norm(a - b) * 0.25 * scale;
    }
    const double fs = processor_.hostRate();
    const double binsPerHz = kFftSize / fs;
    const double ratio = std::pow(topFrequency() / kLowestHz, 1.0 / (kPoints - 1));
    double hz = kLowestHz;
    for (size_t p = 0; p < kPoints; ++p, hz *= ratio) {
        // Where one point covers many bins it takes their mean, otherwise it slides between two of them.
        const double low = hz / std::sqrt(ratio) * binsPerHz, high = hz * std::sqrt(ratio) * binsPerHz;
        double inSum = 0.0, outSum = 0.0;
        const int first = juce::jlimit(1, kFftSize / 2 - 1, static_cast<int>(std::ceil(low)));
        const int last = juce::jlimit(1, kFftSize / 2 - 1, static_cast<int>(std::floor(high)));
        if (last >= first) {
            for (int k = first; k <= last; ++k) {
                inSum += inPower_[static_cast<size_t>(k)];
                outSum += outPower_[static_cast<size_t>(k)];
            }
            inSum /= last - first + 1;
            outSum /= last - first + 1;
        } else {
            const double position = juce::jlimit(1.0, kFftSize / 2 - 2.0, hz * binsPerHz);
            const int below = static_cast<int>(position);
            const double share = position - below;
            inSum = inPower_[static_cast<size_t>(below)] * (1.0 - share) + inPower_[static_cast<size_t>(below + 1)] * share;
            outSum = outPower_[static_cast<size_t>(below)] * (1.0 - share) + outPower_[static_cast<size_t>(below + 1)] * share;
        }
        const float slope = kSlopeDbPerOctave * static_cast<float>(std::log2(hz / 1000.0));
        const auto follow = [](float shown, float now) {
            return now > shown ? shown + (now - shown) * kRise : juce::jmax(now, shown - kFallDbPerTick);
        };
        shownIn_[p] = follow(shownIn_[p], juce::jmax(kBottomDb, powerToLevelDb(inSum) + slope));
        shownOut_[p] = follow(shownOut_[p], juce::jmax(kBottomDb, powerToLevelDb(outSum) + slope));
    }
}

float SpectrumView::loudestShownDb() const {
    float loudest = kBottomDb;
    for (size_t p = 0; p < kPoints; ++p) loudest = juce::jmax(loudest, shownIn_[p], shownOut_[p]);
    return loudest;
}

bool SpectrumView::refresh(int bands) {
    if (bands != bands_) {
        bands_ = bands;
        repaint();
    }
    constexpr int kHop = 256;
    std::array<float, kHop> in{}, out{};
    bool any = false;
    while (processor_.popScope(in.data(), out.data(), kHop)) {
        std::move(inHistory_.begin() + kHop, inHistory_.end(), inHistory_.begin());
        std::move(outHistory_.begin() + kHop, outHistory_.end(), outHistory_.begin());
        std::copy(in.begin(), in.end(), inHistory_.end() - kHop);
        std::copy(out.begin(), out.end(), outHistory_.end() - kHop);
        any = true;
    }
    if (any) {
        analyse();
        repaint();
    } else if (loudestShownDb() > kBottomDb) {
        // The host stopped sending sound, so what is on the screen falls away.
        for (size_t p = 0; p < kPoints; ++p) {
            shownIn_[p] = juce::jmax(kBottomDb, shownIn_[p] - kFallDbPerTick);
            shownOut_[p] = juce::jmax(kBottomDb, shownOut_[p] - kFallDbPerTick);
        }
        repaint();
    }
    if (processor_.takeOutputPeak() > 1.0f) overUntil_ = juce::Time::getMillisecondCounter() + kOverHoldMs;
    return any;
}

void SpectrumView::paintGrid(juce::Graphics& g) const {
    g.setFont(uiFont(400, 12.0f));
    const float bottom = static_cast<float>(getHeight() - kAxisHeight);
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) {
        if (hz >= topFrequency()) continue;
        const float x = frequencyToX(hz);
        g.setColour(colour::hairline.withAlpha(0.5f));
        g.fillRect(x, static_cast<float>(kHandleLabelHeight), 1.0f, bottom - kHandleLabelHeight);
        g.setColour(colour::dim);
        g.drawText(frequencyLabel(hz), juce::roundToInt(x) - 30, getHeight() - kAxisHeight, 60, kAxisHeight, juce::Justification::centred, false);
    }
    for (float db : { 0.0f, -24.0f, -48.0f, -72.0f }) {
        const float y = levelToY(db);
        g.setColour(colour::hairline.withAlpha(0.5f));
        g.fillRect(0.0f, y, static_cast<float>(getWidth()), 1.0f);
        g.setColour(colour::dim);
        g.drawText(commaNumber(db, 0) + " dB", 8, juce::roundToInt(y) - 16, 60, 16, juce::Justification::centredLeft, false);
    }
}

void SpectrumView::paintHandles(juce::Graphics& g) const {
    for (int i = 0; i < handleCount(); ++i) {
        const float x = handleX(i);
        const bool active = i == hovered_ || i == dragged_;
        g.setColour(active ? colour::accent : colour::dim);
        g.fillRect(x - 0.5f, static_cast<float>(kHandleLabelHeight), active ? 2.0f : 1.0f, static_cast<float>(getHeight() - kAxisHeight - kHandleLabelHeight));
        const auto value = split_[static_cast<size_t>(i)]->convertFrom0to1(split_[static_cast<size_t>(i)]->getValue());
        const auto label = juce::Rectangle<float>(58.0f, static_cast<float>(kHandleLabelHeight)).withCentre({ juce::jlimit(30.0f, static_cast<float>(getWidth()) - 30.0f, x), kHandleLabelHeight * 0.5f + 2.0f });
        g.setColour(active ? colour::accent : colour::raised);
        g.fillRoundedRectangle(label, 6.0f);
        g.setFont(uiFont(500, 12.0f));
        g.setColour(active ? colour::ink : colour::text);
        g.drawText(frequencyLabel(static_cast<double>(value)), label, juce::Justification::centred, false);
    }
}

void SpectrumView::paint(juce::Graphics& g) {
    g.fillAll(colour::bg);
    paintGrid(g);

    const auto xOf = [&](size_t p) { return static_cast<float>(getWidth()) * static_cast<float>(p) / static_cast<float>(kPoints - 1); };

    // What the plugin adds: the stretches where the output stands above the input.
    g.setColour(colour::accent.withAlpha(0.35f));
    for (size_t p = 0; p < kPoints;) {
        const auto added = [&](size_t q) { return shownOut_[q] > shownIn_[q] + kAddedAboveDb && shownOut_[q] > kBottomDb + 1.0f; };
        if (!added(p)) {
            ++p;
            continue;
        }
        size_t end = p;
        while (end + 1 < kPoints && added(end + 1)) ++end;
        juce::Path area;
        area.startNewSubPath(xOf(p), levelToY(shownIn_[p]));
        for (size_t q = p; q <= end; ++q) area.lineTo(xOf(q), levelToY(shownOut_[q]));
        for (size_t q = end + 1; q-- > p;) area.lineTo(xOf(q), levelToY(shownIn_[q]));
        area.closeSubPath();
        g.fillPath(area);
        p = end + 1;
    }

    const auto line = [&](const std::array<float, kPoints>& values) {
        juce::Path path;
        path.startNewSubPath(0.0f, levelToY(values[0]));
        for (size_t p = 1; p < kPoints; ++p) path.lineTo(xOf(p), levelToY(values[p]));
        return path;
    };
    g.setColour(colour::dim);
    g.strokePath(line(shownIn_), juce::PathStrokeType(1.0f, juce::PathStrokeType::curved));
    g.setColour(colour::text);
    g.strokePath(line(shownOut_), juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));

    paintHandles(g);

    if (juce::Time::getMillisecondCounter() < overUntil_) {
        const auto pill = juce::Rectangle<float>(static_cast<float>(getWidth()) - 170.0f, 4.0f, 158.0f, 22.0f);
        g.setColour(colour::weak);
        g.fillRoundedRectangle(pill, 6.0f);
        g.setFont(uiFont(600, 12.0f));
        g.setColour(colour::ink);
        g.drawText(pl(text::overload), pill, juce::Justification::centred, false);
    }
}

} // namespace barwa
