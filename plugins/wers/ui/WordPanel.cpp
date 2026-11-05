#include "WordPanel.h"
#include "ScoreColour.h"

namespace wers::ui {

namespace {
const char* whereLower(Device d) {
    switch (d) {
        case Device::Phone: return "na telefonie";
        case Device::Earbuds: return "na pchełkach";
        case Device::Car: return "w aucie";
        case Device::Laptop: return "na laptopie";
        case Device::Studio: return "na odsłuchach";
    }
    return "";
}

juce::String topBands(const float* values, const double* centres, int count, const juce::String& sign) {
    // The one or two strongest moves say most of it.
    int a = -1, b = -1;
    for (int i = 0; i < count; ++i) {
        if (values[i] <= 0.0f) continue;
        if (a < 0 || values[i] > values[a]) { b = a; a = i; }
        else if (b < 0 || values[i] > values[b]) { b = i; }
    }
    if (a < 0) return {};
    juce::String out = sign + commaNumber(values[a]) + " dB przy " + frequencyLabel(centres[a]);
    if (b >= 0) out += ", " + sign + commaNumber(values[b]) + " dB przy " + frequencyLabel(centres[b]);
    return out;
}
} // namespace

WordPanel::WordPanel(WersProcessor& p) : proc_(p) {
    addChildComponent(fix_);
    addChildComponent(undo_);
    fix_.setTextSize(20.0f);
    undo_.setTextSize(20.0f);
    fix_.onClick = [this] { proc_.session().fixWord(index_, device_); };
    undo_.onClick = [this] { proc_.session().undoFix(index_); };
}

void WordPanel::resized() {
    const auto b = getLocalBounds().reduced(26, 22);
    fix_.setBounds(b.getX(), b.getBottom() - 52, b.getWidth(), 52);
    undo_.setBounds(b.getX(), b.getBottom() - 52, b.getWidth(), 52);
}

void WordPanel::refresh(const std::shared_ptr<const SongResult>& result, Device device, int selectedIndex) {
    // A new result also needs a new preview, because the old one was made from the old result.
    const bool changedWord = selectedIndex != index_ || device != device_ || result != result_;
    result_ = result; device_ = device; index_ = selectedIndex;
    if (changedWord && result_ && index_ >= 0 && index_ < static_cast<int>(result_->words.size()) && !result_->words[static_cast<size_t>(index_)].fix.active) {
        if (result_->words[static_cast<size_t>(index_)].verdict[static_cast<size_t>(device_)].score < kScoreFades)
            proc_.session().requestPreview(index_, device_);
    }
    repaint();
}

void WordPanel::tick() {
    const bool compare = proc_.state().getRawParameterValue(ids::fixes)->load() < 0.5f;
    if (compare != compare_) { compare_ = compare; repaint(); }
    bool canFix = false, canUndo = false;
    if (result_ && index_ >= 0 && index_ < static_cast<int>(result_->words.size()) && !compare_ && !proc_.isStale()) {
        const Word& w = result_->words[static_cast<size_t>(index_)];
        canUndo = w.fix.active;
        canFix = !w.fix.active && w.verdict[static_cast<size_t>(device_)].score < kScoreFades && proc_.session().phase() == SessionPhase::Ready;
    }
    const auto p = proc_.session().preview();
    if (p.index != preview_.index || p.predictedScore != preview_.predictedScore || p.target != preview_.target) { preview_ = p; repaint(); }
    // The planner may already know that no small fix helps this word: then there is nothing to press.
    if (preview_.index == index_ && preview_.target == device_ && !preview_.fix.active) canFix = false;
    fix_.setVisible(canFix);
    undo_.setVisible(canUndo);
}

juce::String WordPanel::statusLine(const Word& w) const {
    const auto& v = w.verdict[static_cast<size_t>(device_)];
    const auto where = pl(whereLower(device_));
    if (w.fix.active && !compare_) return pl("na 100, po naprawie ") + where;
    const float score = compare_ ? v.baseScore : v.score;
    const juce::String state = score < kScoreDies ? pl("ginie ") : (score < kScoreFades ? pl("słabnie ") : pl("czytelne "));
    return pl("na 100, ") + state + where;
}

juce::String WordPanel::causeLine(const Word& w) const {
    const auto& v = w.verdict[static_cast<size_t>(device_)];
    if (v.score >= kScoreFades || v.worstBand < 0) return {};
    const auto band = frequencyLabel(kSiiFreq[static_cast<size_t>(v.worstBand)]);
    if (v.cause == Cause::Beat) return pl("Bit najmocniej zakrywa słowo w okolicy ") + band + ".";
    return pl("Na tym urządzeniu słowo jest za ciche w okolicy ") + band + ".";
}

juce::String WordPanel::vocalFixLine(const WordFix& f) {
    return topBands(f.vocalBoostDb.data(), kBoostCentres.data(), kBoostBands, "+");
}

juce::String WordPanel::beatFixLine(const WordFix& f, double seconds) {
    const auto bands = topBands(f.beatCutDb.data(), kCutCentres.data(), kCutBands, juce::String::charToString(juce::juce_wchar(0x2212)));
    if (bands.isEmpty()) return {};
    return bands + pl(", środek stereo, przez ") + juce::String(juce::roundToInt(seconds * 100.0) * 10) + " ms";
}

void WordPanel::paintDevices(juce::Graphics& g, juce::Rectangle<int> area, const Word& w) const {
    static const char* const names[] = { "Telefon", "Pchełki", "Auto", "Laptop", "Studio" };
    const int cell = area.getWidth() / kNumDevices;
    for (int d = 0; d < kNumDevices; ++d) {
        auto r = area.removeFromLeft(cell);
        const auto& v = w.verdict[static_cast<size_t>(d)];
        const float score = compare_ ? v.baseScore : v.score;
        const bool here = d == static_cast<int>(device_);
        g.setFont(uiFont(here ? 600 : 400, 12.0f));
        g.setColour(here ? colour::text : colour::dim);
        g.drawText(pl(names[d]), r.removeFromTop(18), juce::Justification::centredLeft, false);
        g.setFont(displayFont(600, 18.0f));
        g.setColour(scoreColour(score));
        g.drawText(juce::String(juce::roundToInt(score)), r, juce::Justification::centredLeft, false);
    }
}

void WordPanel::paint(juce::Graphics& g) {
    g.fillAll(colour::surface);
    g.setColour(colour::hairline);
    g.fillRect(0, 0, 1, getHeight());
    auto b = getLocalBounds().reduced(26, 22);
    if (!result_ || index_ < 0 || index_ >= static_cast<int>(result_->words.size())) {
        g.setColour(colour::text);
        g.setFont(displayFont(600, 24.0f));
        g.drawFittedText(pl("Tu zobaczysz wybrane słowo."), b.removeFromTop(70), juce::Justification::topLeft, 2, 1.0f);
        g.setColour(colour::dim);
        g.setFont(uiFont(400, 14.0f));
        g.drawFittedText(pl("WERS oceni każde słowo na telefonie, pchełkach, w aucie, na laptopie i na odsłuchach."), b.removeFromTop(70), juce::Justification::topLeft, 3, 1.0f);
        return;
    }
    b.removeFromBottom(60); // the button
    const Word& w = result_->words[static_cast<size_t>(index_)];
    const auto& v = w.verdict[static_cast<size_t>(device_)];
    const float score = compare_ ? v.baseScore : v.score;
    const auto label = w.text.empty() ? secondsToClock(w.t0) : juce::String::fromUTF8(w.text.c_str());

    g.setColour(colour::text);
    g.setFont(displayFont(700, 42.0f));
    g.drawFittedText(label, b.removeFromTop(52), juce::Justification::centredLeft, 1, 0.6f);
    b.removeFromTop(2);

    auto scoreRow = b.removeFromTop(38);
    const auto scoreFont = displayFont(700, 30.0f);
    g.setFont(scoreFont);
    const auto scoreText = juce::String(juce::roundToInt(score));
    const int sw = juce::GlyphArrangement::getStringWidthInt(scoreFont, scoreText) + 8;
    g.setColour(scoreColour(score));
    g.drawText(scoreText, scoreRow.removeFromLeft(sw), juce::Justification::centredLeft, false);
    g.setFont(uiFont(400, 14.0f));
    g.setColour(colour::dim);
    g.drawText(statusLine(w), scoreRow, juce::Justification::centredLeft, true);
    b.removeFromTop(4);

    // The meter carries the two thresholds, so a score can be read against what it has to reach.
    const auto bar = b.removeFromTop(6).toFloat();
    g.setColour(colour::hairline);
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(scoreColour(score));
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, score / 100.0f)), 3.0f);
    g.setColour(colour::surface);
    for (float mark : { kScoreDies, kScoreFades }) g.fillRect(bar.getX() + bar.getWidth() * mark / 100.0f - 1.0f, bar.getY(), 2.0f, bar.getHeight());
    b.removeFromTop(16);

    paintDevices(g, b.removeFromTop(44), w);
    b.removeFromTop(16);
    const bool bad = score < kScoreFades || w.fix.active;

    g.setColour(colour::dim);
    if (compare_) {
        g.setFont(uiFont(400, 14.0f));
        g.drawFittedText(pl("Słuchasz bez napraw. Włącz je z powrotem, żeby edytować słowo."), b.removeFromTop(44), juce::Justification::topLeft, 2, 1.0f);
        return;
    }
    if (proc_.isStale()) {
        g.setFont(uiFont(400, 14.0f));
        g.drawFittedText(pl("Wokal się zmienił od analizy, więc naprawy są wyłączone."), b.removeFromTop(44), juce::Justification::topLeft, 2, 1.0f);
        return;
    }
    const auto cause = causeLine(w);
    if (cause.isNotEmpty()) {
        g.setFont(uiFont(500, 14.0f));
        g.setColour(colour::text);
        g.drawFittedText(cause, b.removeFromTop(38), juce::Justification::topLeft, 2, 1.0f);
        g.setColour(colour::dim);
        b.removeFromTop(2);
    }
    if (w.heardInMix == 0 && !w.fix.active) {
        g.setFont(uiFont(500, 14.0f));
        g.drawFittedText(pl("Model mowy nie rozpoznał tego słowa w miksie, choć słyszy je w samym wokalu."), b.removeFromTop(40), juce::Justification::topLeft, 2, 1.0f);
        b.removeFromTop(4);
    }
    if (!bad) {
        g.setFont(uiFont(400, 14.0f));
        g.drawFittedText(pl("To słowo jest czytelne na tym urządzeniu. Nie wymaga naprawy."), b.removeFromTop(44), juce::Justification::topLeft, 2, 1.0f);
        return;
    }
    const WordFix* fix = w.fix.active ? &w.fix : (preview_.index == index_ && preview_.target == device_ ? &preview_.fix : nullptr);
    if (fix != nullptr && fix->active) {
        const auto row = [&](const juce::String& name, const juce::String& text) {
            if (text.isEmpty()) return;
            auto r = b.removeFromTop(42);
            g.setFont(uiFont(400, 13.0f));
            g.setColour(colour::dim);
            g.drawText(name, r.removeFromLeft(70), juce::Justification::topLeft, false);
            g.setColour(colour::text);
            g.setFont(uiFont(400, 13.0f));
            g.drawFittedText(text, r, juce::Justification::topLeft, 2, 1.0f);
        };
        row(pl("W wokalu"), vocalFixLine(*fix));
        row(pl("W bicie"), beatFixLine(*fix, w.t1 - w.t0));
        if (!w.fix.active && preview_.index == index_) {
            g.setFont(uiFont(600, 14.0f));
            g.setColour(scoreColour(preview_.predictedScore));
            g.drawText(pl("Po naprawie: około ") + juce::String(juce::roundToInt(preview_.predictedScore)), b.removeFromTop(24), juce::Justification::centredLeft, false);
        }
        const float after = w.fix.active ? v.score : (preview_.index == index_ ? preview_.predictedScore : 100.0f);
        if (after < kScoreDies) {
            g.setFont(uiFont(400, 13.0f));
            g.setColour(colour::dim);
            g.drawFittedText(pl("Mała korekta tu nie wystarczy. Podnieś wokal w tym miejscu albo nagraj słowo wyraźniej."), b.removeFromTop(40), juce::Justification::topLeft, 2, 1.0f);
        }
    } else if (!w.fix.active && preview_.index == index_ && preview_.target == device_) {
        g.setFont(uiFont(400, 14.0f));
        g.drawFittedText(pl("Tego słowa nie da się poprawić małą korektą. Pomoże głośniejsze lub wyraźniejsze nagranie."), b.removeFromTop(44), juce::Justification::topLeft, 3, 1.0f);
    }
}

} // namespace wers::ui
