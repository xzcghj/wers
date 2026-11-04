#include "WordPanel.h"

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
    compare_ = proc_.state().getRawParameterValue(ids::fixes)->load() < 0.5f;
    bool canFix = false, canUndo = false;
    if (result_ && index_ >= 0 && index_ < static_cast<int>(result_->words.size()) && !compare_ && !proc_.isStale()) {
        const Word& w = result_->words[static_cast<size_t>(index_)];
        canUndo = w.fix.active;
        canFix = !w.fix.active && w.verdict[static_cast<size_t>(device_)].score < kScoreFades && proc_.session().phase() == SessionPhase::Ready;
    }
    fix_.setVisible(canFix);
    undo_.setVisible(canUndo);
    const auto p = proc_.session().preview();
    if (p.index != preview_.index || p.predictedScore != preview_.predictedScore || p.target != preview_.target) { preview_ = p; repaint(); }
}

std::vector<WordPanel::Chip> WordPanel::phonemeChips(const juce::String& word, bool hasConsonantFix) {
    std::vector<Chip> out;
    juce::String w = word.toLowerCase();
    const juce::String vowels = pl("aąeęioóuy"), polishConsonants = pl("ćłńśźż");
    const juce::StringArray two{ "sz", "cz", "rz", pl("dź"), pl("dż"), "dz" };
    int i = 0;
    while (i < w.length()) {
        const juce::String pair = w.substring(i, i + 2);
        if (two.contains(pair)) { out.push_back({ pair, hasConsonantFix }); i += 2; continue; }
        if (pair == "ch") { out.push_back({ pair, hasConsonantFix }); i += 2; continue; }
        const auto c = w[i];
        const bool vowel = vowels.containsChar(c);
        // The system's idea of a letter depends on its locale, so Polish letters are named here.
        if (vowel || polishConsonants.containsChar(c) || juce::CharacterFunctions::isLetter(c)) {
            out.push_back({ juce::String::charToString(c), hasConsonantFix && !vowel });
        }
        ++i;
    }
    return out;
}

juce::String WordPanel::statusLine(const Word& w) const {
    const auto& v = w.verdict[static_cast<size_t>(device_)];
    const auto where = pl(whereLower(device_));
    if (w.fix.active && !compare_) return pl("na 100, po naprawie ") + where;
    const juce::String state = v.score < kScoreDies ? pl("ginie ") : (v.score < kScoreFades ? pl("słabnie ") : pl("czytelne "));
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

void WordPanel::paint(juce::Graphics& g) {
    g.fillAll(colour::paper);
    auto b = getLocalBounds().reduced(26, 22);
    if (!result_ || index_ < 0 || index_ >= static_cast<int>(result_->words.size())) {
        g.setColour(colour::ground);
        g.setFont(displayFont(600, 24.0f));
        g.drawFittedText(pl("Tu zobaczysz wybrane słowo."), b.removeFromTop(70), juce::Justification::topLeft, 2, 1.0f);
        g.setFont(uiFont(400, 14.0f));
        g.drawFittedText(pl("WERS oceni każde słowo na telefonie, pchełkach, w aucie, na laptopie i na odsłuchach."), b.removeFromTop(70), juce::Justification::topLeft, 3, 1.0f);
        return;
    }
    const Word& w = result_->words[static_cast<size_t>(index_)];
    const auto& v = w.verdict[static_cast<size_t>(device_)];
    const float score = compare_ ? v.baseScore : v.score;
    const auto label = w.text.empty() ? secondsToClock(w.t0) : juce::String::fromUTF8(w.text.c_str());

    g.setColour(colour::ground);
    g.setFont(displayFont(700, 48.0f));
    g.drawFittedText(label, b.removeFromTop(58), juce::Justification::centredLeft, 1, 0.6f);
    b.removeFromTop(4);

    auto scoreRow = b.removeFromTop(30);
    g.setFont(displayFont(700, 26.0f));
    const auto scoreText = juce::String(juce::roundToInt(score));
    const int sw = juce::GlyphArrangement::getStringWidthInt(displayFont(700, 26.0f), scoreText) + 8;
    g.drawText(scoreText, scoreRow.removeFromLeft(sw), juce::Justification::centredLeft, false);
    g.setFont(uiFont(400, 14.0f));
    g.drawText(statusLine(w), scoreRow, juce::Justification::centredLeft, true);
    b.removeFromTop(4);
    auto bar = b.removeFromTop(8).toFloat();
    g.setColour(colour::meter);
    g.fillRoundedRectangle(bar, 1.0f);
    g.setColour(colour::ground);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, score / 100.0f)), 1.0f);
    b.removeFromTop(14);

    // Letters of the word: the dark ones get the correction.
    const bool bad = score < kScoreFades || w.fix.active;
    if (!w.text.empty()) {
        const auto chips = phonemeChips(label, bad);
        int x = b.getX(), y = b.getY();
        const auto chipFont = uiFont(600, 15.0f);
        for (const auto& c : chips) {
            const int cw = juce::jmax(28, juce::GlyphArrangement::getStringWidthInt(chipFont, c.text) + 12);
            if (x + cw > b.getRight()) { x = b.getX(); y += 34; }
            juce::Rectangle<float> r(static_cast<float>(x), static_cast<float>(y), static_cast<float>(cw), 30.0f);
            g.setColour(c.on ? colour::ground : colour::line);
            if (c.on) g.fillRoundedRectangle(r, 2.0f); else g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);
            g.setColour(c.on ? colour::paper : colour::ground);
            g.setFont(chipFont);
            g.drawText(c.text, r.toNearestInt(), juce::Justification::centred, false);
            x += cw + 3;
        }
        b.removeFromTop(y - b.getY() + 42);
    }

    g.setColour(colour::ground);
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
        g.drawFittedText(cause, b.removeFromTop(40), juce::Justification::topLeft, 2, 1.0f);
        b.removeFromTop(4);
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
            auto r = b.removeFromTop(44);
            g.setFont(uiFont(600, 14.0f));
            g.drawText(name, r.removeFromLeft(76), juce::Justification::topLeft, false);
            g.setFont(uiFont(400, 14.0f));
            g.drawFittedText(text, r, juce::Justification::topLeft, 2, 1.0f);
        };
        row(pl("W wokalu"), vocalFixLine(*fix));
        row(pl("W bicie"), beatFixLine(*fix, w.t1 - w.t0));
        if (!w.fix.active && preview_.index == index_) {
            g.setFont(uiFont(500, 14.0f));
            g.drawText(pl("Po naprawie: około ") + juce::String(juce::roundToInt(preview_.predictedScore)), b.removeFromTop(24), juce::Justification::centredLeft, false);
        }
        const float after = w.fix.active ? v.score : (preview_.index == index_ ? preview_.predictedScore : 100.0f);
        if (after < kScoreDies) {
            g.setFont(uiFont(400, 14.0f));
            g.drawFittedText(pl("Mała korekta tu nie wystarczy. Podnieś wokal w tym miejscu albo nagraj słowo wyraźniej."), b.removeFromTop(40), juce::Justification::topLeft, 2, 1.0f);
        }
    } else if (!w.fix.active && preview_.index == index_ && preview_.target == device_) {
        g.setFont(uiFont(400, 14.0f));
        g.drawFittedText(pl("Tego słowa nie da się poprawić małą korektą. Pomoże głośniejsze lub wyraźniejsze nagranie."), b.removeFromTop(44), juce::Justification::topLeft, 3, 1.0f);
    }
}

} // namespace wers::ui
