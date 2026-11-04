#include "WordMapView.h"
#include <algorithm>

namespace wers::ui {

namespace {
constexpr float kFontSize = 44.0f;
constexpr int kRowH = 72, kPad = 28, kTop = 24;

int weightForScore(float score) {
    // A weaker word is drawn thinner, so the page shows the problem before anything is read.
    const int w = 300 + juce::roundToInt((juce::jlimit(35.0f, 92.0f, score) - 35.0f) / 57.0f * 4.0f) * 100;
    return juce::jlimit(300, 700, w);
}

juce::String plural(int n, const char* one, const char* few, const char* many) {
    if (n == 1) return pl(one);
    const int t = n % 10, h = n % 100;
    return pl(t >= 2 && t <= 4 && !(h >= 12 && h <= 14) ? few : many);
}

const char* whereName(Device d) {
    switch (d) {
        case Device::Phone: return "Na telefonie";
        case Device::Earbuds: return "Na pchełkach";
        case Device::Car: return "W aucie";
        case Device::Laptop: return "Na laptopie";
        case Device::Studio: return "Na odsłuchach";
    }
    return "";
}
} // namespace

juce::String WordMapView::summaryText(int dying, int fading, Device device) {
    const auto where = pl(whereName(device));
    if (dying == 0 && fading == 0) return where + pl(" wszystkie słowa są czytelne.");
    juce::StringArray parts;
    if (dying > 0) parts.add(plural(dying, "ginie", "giną", "ginie") + " " + juce::String(dying) + " " + plural(dying, "słowo", "słowa", "słów"));
    if (fading > 0) parts.add(plural(fading, "słabnie", "słabną", "słabnie") + " " + juce::String(fading) + (dying > 0 ? juce::String() : " " + plural(fading, "słowo", "słowa", "słów")));
    return where + " " + parts.joinIntoString(", ") + ".";
}

WordMapView::WordMapView(WersProcessor& p) : proc_(p) {
    for (auto* b : { &listen_, &finish_, &cancel_, &again_, &lyrics_, &fixAll_, &getModel_ }) addChildComponent(b);
    listen_.setTextSize(20.0f);
    finish_.setTextSize(20.0f);
    listen_.onClick = [this] { proc_.startAnalysis(); };
    again_.onClick = [this] { proc_.startAnalysis(); };
    finish_.onClick = [this] { proc_.session().stopListeningAndAnalyse(); };
    cancel_.onClick = [this] { proc_.session().cancelListening(); };
    lyrics_.onClick = [this] { if (onEditLyrics) onEditLyrics(); };
    fixAll_.onClick = [this] { proc_.session().fixAllBelow(kScoreFades, device_); };
    getModel_.onClick = [this] { proc_.model().startDownload(); };
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

juce::Rectangle<int> WordMapView::wordsArea() const {
    return { kPad, kTop + 46, getWidth() - kPad * 2, getHeight() - (kTop + 46) - 42 };
}

void WordMapView::resized() {
    auto w = getWidth();
    listen_.setBounds(kPad, 210, 280, 56);
    finish_.setBounds(kPad, 210, 280, 56);
    cancel_.setBounds(kPad + 292, 220, 90, 36);
    again_.setBounds(w - kPad - 164, kTop - 2, 164, 30);
    lyrics_.setBounds(again_.getX() - 112, kTop - 2, 108, 30);
    fixAll_.setBounds(lyrics_.getX() - 8 - 152, kTop - 4, 152, 34);
    getModel_.setBounds(w - kPad - 130, getHeight() - 44, 130, 32);
    rebuild();
}

void WordMapView::refresh(const std::shared_ptr<const SongResult>& result, Device device, int selectedIndex) {
    const bool same = result == result_ && device == device_ && selectedIndex == selected_;
    result_ = result;
    device_ = device;
    selected_ = selectedIndex;
    if (!same) { rebuild(); repaint(); }
}

int WordMapView::wordCount(float below, float above) const {
    if (!result_) return 0;
    int n = 0;
    for (const auto& w : result_->words) {
        const float s = w.verdict[static_cast<size_t>(device_)].score;
        if (s < below && s >= above) ++n;
    }
    return n;
}

void WordMapView::rebuild() {
    boxes_.clear();
    lines_ = 0;
    dying_ = fading_ = 0;
    if (!result_) return;
    const float maxWidth = static_cast<float>(wordsArea().getWidth());
    float x = 0.0f;
    double prevEnd = -10.0;
    int line = 0, inLine = 0;
    for (size_t i = 0; i < result_->words.size(); ++i) {
        const Word& w = result_->words[i];
        const float s = w.verdict[static_cast<size_t>(device_)].score;
        const int weight = weightForScore(s);
        const juce::String label = w.text.empty() ? secondsToClock(w.t0) : juce::String::fromUTF8(w.text.c_str());
        const float width = juce::GlyphArrangement::getStringWidth(displayFont(weight, kFontSize), label) + 16.0f;
        // A long breath starts a new line, so the page keeps the shape of the verse.
        const bool pause = w.t0 - prevEnd > 0.45 && inLine > 0;
        if (pause || (x + width > maxWidth && inLine > 0)) { ++line; x = 0.0f; inLine = 0; }
        Box b;
        b.index = static_cast<int>(i); b.line = line; b.x = x; b.w = width; b.weight = weight;
        b.label = label;
        b.fill = s < kScoreDies ? colour::pink : (s < kScoreFades ? colour::yellow : juce::Colours::transparentBlack);
        b.text = s < kScoreFades ? colour::ground : colour::paper;
        b.underline = w.fix.active;
        if (s < kScoreDies) ++dying_; else if (s < kScoreFades) ++fading_;
        boxes_.push_back(b);
        x += width + 6.0f;
        ++inLine;
        prevEnd = w.t1;
    }
    lines_ = line + 1;
}

juce::Rectangle<float> WordMapView::boxBounds(const Box& b) const {
    const auto area = wordsArea();
    const float y = static_cast<float>(area.getY()) + (static_cast<float>(b.line) - scroll_) * kRowH + 6.0f;
    return { static_cast<float>(area.getX()) + b.x, y, b.w, kRowH - 12.0f };
}

int WordMapView::hitTest(juce::Point<int> p) const {
    const auto area = wordsArea();
    if (!area.contains(p)) return -1;
    for (const auto& b : boxes_)
        if (boxBounds(b).contains(p.toFloat())) return b.index;
    return -1;
}

void WordMapView::mouseDown(const juce::MouseEvent& e) {
    const int hit = hitTest(e.getPosition());
    if (hit >= 0 && onSelect) onSelect(hit);
}

void WordMapView::mouseMove(const juce::MouseEvent& e) {
    const int hit = hitTest(e.getPosition());
    if (hit != hovered_) {
        hovered_ = hit;
        setMouseCursor(hit >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void WordMapView::mouseExit(const juce::MouseEvent&) {
    if (hovered_ != -1) { hovered_ = -1; repaint(); }
}

void WordMapView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    const float visible = static_cast<float>(wordsArea().getHeight()) / kRowH;
    scrollTarget_ = juce::jlimit(0.0f, juce::jmax(0.0f, static_cast<float>(lines_) - visible), scrollTarget_ - wheel.deltaY * 3.0f);
    lastManualScroll_ = juce::Time::getMillisecondCounter();
}

void WordMapView::tick() {
    SongSession& s = proc_.session();
    const auto phase = s.phase();
    const float progress = s.progress();
    const bool changed = phase != phase_ || std::abs(progress - progress_) > 0.002f;
    phase_ = phase; progress_ = progress;

    const bool ready = phase == SessionPhase::Ready && result_ != nullptr;
    listen_.setVisible(phase == SessionPhase::Idle || phase == SessionPhase::Failed);
    finish_.setVisible(phase == SessionPhase::Listening && s.heardSeconds() >= 3.0);
    cancel_.setVisible(phase == SessionPhase::Listening);
    again_.setVisible(ready);
    lyrics_.setVisible(phase == SessionPhase::Idle || ready);
    const int bad = ready ? dying_ + fading_ : 0;
    fixAll_.setVisible(bad > 0);
    fixAll_.setButtonText(pl("Napraw wszystkie: ") + juce::String(bad));

    // One message at a time below the words, the most important first.
    juce::String banner, hint;
    bool showModel = false;
    const auto modelState = proc_.model().state();
    const bool downloading = modelState == ModelManager::State::Downloading, modelFailed = modelState == ModelManager::State::Failed;
    const juce::String downloadText = pl("Pobieram model tekstu: ") + juce::String(juce::roundToInt(proc_.model().progress() * 100.0f)) + "%";
    const juce::String failedText = pl("Nie udało się pobrać modelu tekstu. Sprawdź internet i spróbuj jeszcze raz.");
    if (ready) {
        if (proc_.isStale()) banner = pl("Wokal się zmienił, więc naprawy są wyłączone. Przesłuchaj numer jeszcze raz.");
        else if (downloading) banner = downloadText;
        else if (!result_->hasText && !proc_.canRecognise()) banner = pl("Rozpoznawanie tekstu nie działa na tym komputerze, więc widzisz fragmenty czasu.");
        else if (result_->textFailed) banner = pl("Nie udało się rozpoznać tekstu, więc widzisz fragmenty czasu. Przesłuchaj numer jeszcze raz.");
        else if (!result_->hasText && modelFailed) { banner = failedText; showModel = true; }
        else if (!result_->hasText && modelState == ModelManager::State::Ready) banner = pl("Model gotowy. Przesłuchaj numer jeszcze raz, a zobaczysz słowa.");
        else if (!result_->hasText) { banner = pl("Widzisz fragmenty czasu. Pobierz model tekstu (około 550 MB), a zobaczysz słowa."); showModel = true; }
        else if (!result_->hasBeat) banner = pl("Nie słyszałem bitu. Dodaj WERS na ślad bitu i przesłuchaj numer jeszcze raz.");
    } else if ((phase == SessionPhase::Idle || phase == SessionPhase::Failed) && proc_.canRecognise()) {
        if (downloading) hint = downloadText;
        else if (modelFailed) { hint = failedText; showModel = true; }
        else if (modelState == ModelManager::State::Missing) {
            hint = pl("Żeby widzieć tekst, a nie czas, pobierz model mowy (około 550 MB). Działa na tym komputerze, nagranie nigdzie nie wychodzi.");
            showModel = true;
        }
    }
    getModel_.setVisible(showModel);
    if (ready) getModel_.setBounds(getWidth() - kPad - 130, getHeight() - 44, 130, 32);
    else getModel_.setBounds(kPad, 340, 130, 32);
    if (banner != banner_ || hint != modelHint_) { banner_ = banner; modelHint_ = hint; repaint(); }

    // A song already heard gets its words as soon as the model arrives, unless that would drop fixes.
    if (lastModelState_ == ModelManager::State::Downloading && modelState == ModelManager::State::Ready && ready && !result_->hasText && s.canAnalyseAgain()) {
        const bool anyFix = std::any_of(result_->words.begin(), result_->words.end(), [](const Word& w) { return w.fix.active; });
        if (!anyFix) s.analyseAgain();
    }
    lastModelState_ = modelState;

    // Follow the playhead gently unless the user scrolled a moment ago.
    int current = -1;
    if (ready && proc_.isTransportPlaying()) {
        const double t = proc_.currentSeconds();
        const auto& words = result_->words;
        const auto it = std::upper_bound(words.begin(), words.end(), t, [](double v, const Word& w) { return v < w.t0; });
        if (it != words.begin()) {
            const auto idx = static_cast<size_t>(std::distance(words.begin(), it) - 1);
            if (t <= words[idx].t1 + 0.1) current = static_cast<int>(idx);
        }
        if (current >= 0 && current < static_cast<int>(boxes_.size()) && juce::Time::getMillisecondCounter() - lastManualScroll_ > 4000u) {
            const float visible = static_cast<float>(wordsArea().getHeight()) / kRowH;
            const float line = static_cast<float>(boxes_[static_cast<size_t>(current)].line);
            if (line < scrollTarget_ + 0.2f || line > scrollTarget_ + visible - 1.8f)
                scrollTarget_ = juce::jlimit(0.0f, juce::jmax(0.0f, static_cast<float>(lines_) - visible), line - 1.0f);
        }
    }
    bool repaintNeeded = changed || current != current_;
    current_ = current;
    if (std::abs(scrollTarget_ - scroll_) > 0.002f) {
        scroll_ += (scrollTarget_ - scroll_) * 0.25f;
        repaintNeeded = true;
    } else {
        scroll_ = scrollTarget_;
    }
    if (repaintNeeded || phase == SessionPhase::Listening) repaint();
}

void WordMapView::paintMessage(juce::Graphics& g, const juce::String& title, const juce::String& body, float progress) {
    g.setColour(colour::paper);
    g.setFont(displayFont(600, 34.0f));
    g.drawFittedText(title, kPad, 54, getWidth() - kPad * 2, 86, juce::Justification::topLeft, 2, 1.0f);
    g.setFont(uiFont(400, 16.0f));
    g.setColour(colour::legend);
    g.drawFittedText(body, kPad, 146, getWidth() - kPad * 2, 54, juce::Justification::topLeft, 3, 1.0f);
    if (progress >= 0.0f) {
        const juce::Rectangle<float> bar(kPad, 222.0f, static_cast<float>(getWidth() - kPad * 2), 10.0f);
        g.setColour(colour::track);
        g.fillRoundedRectangle(bar, 2.0f);
        g.setColour(colour::yellow);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, progress)), 2.0f);
    }
}

void WordMapView::paintWords(juce::Graphics& g) {
    g.setColour(colour::paper);
    g.setFont(displayFont(500, 20.0f));
    // The summary stops short of the first button and tightens a little when a number is long.
    const int summaryRight = (fixAll_.isVisible() ? fixAll_.getX() : lyrics_.getX()) - 10;
    g.drawFittedText(summaryText(dying_, fading_, device_), kPad, kTop, summaryRight - kPad, 28, juce::Justification::centredLeft, 1, 0.8f);
    const auto area = wordsArea();
    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(area.expanded(4, 0));
        for (const auto& b : boxes_) {
            const auto r = boxBounds(b);
            if (r.getBottom() < static_cast<float>(area.getY()) - 8.0f || r.getY() > static_cast<float>(area.getBottom()) + 8.0f) continue;
            if (b.fill.getAlpha() != 0) {
                g.setColour(b.fill);
                g.fillPath(markerShape(r));
            } else if (b.index == hovered_) {
                g.setColour(colour::paper.withAlpha(0.10f));
                g.fillPath(markerShape(r));
            }
            if (b.index == selected_) {
                g.setColour(colour::paper);
                g.strokePath(markerShape(r.expanded(2.0f)), juce::PathStrokeType(2.5f));
            }
            g.setColour(b.text);
            g.setFont(displayFont(b.weight, kFontSize));
            g.drawText(b.label, r.toNearestInt(), juce::Justification::centred, false);
            if (b.underline) {
                g.setColour(colour::yellow);
                g.fillRect(r.getX() + 8.0f, r.getBottom() - 6.0f, r.getWidth() - 16.0f, 3.0f);
            }
            if (b.index == current_) {
                g.setColour(b.fill.getAlpha() != 0 ? colour::ground : colour::paper);
                g.fillRect(r.getX() + 8.0f, r.getBottom() - 2.0f, r.getWidth() - 16.0f, 2.0f);
            }
        }
    }
    // A thin mark on the right edge shows there is more of the song to scroll to.
    const float visibleLines = static_cast<float>(area.getHeight()) / kRowH;
    if (static_cast<float>(lines_) > visibleLines) {
        const float trackH = static_cast<float>(area.getHeight()), barH = juce::jmax(24.0f, trackH * visibleLines / static_cast<float>(lines_));
        const float top = (trackH - barH) * juce::jlimit(0.0f, 1.0f, scroll_ / (static_cast<float>(lines_) - visibleLines));
        g.setColour(colour::paper.withAlpha(0.35f));
        g.fillRoundedRectangle(static_cast<float>(getWidth()) - 10.0f, static_cast<float>(area.getY()) + top, 4.0f, barH, 2.0f);
    }

    // Legend, then the current message.
    int x = kPad;
    const int y = getHeight() - 30;
    g.setFont(uiFont(400, 13.0f));
    const auto legendItem = [&](juce::Colour c, const juce::String& text) {
        g.setColour(c);
        g.fillPath(markerShape(juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y) + 3.0f, 18.0f, 12.0f)));
        g.setColour(colour::legend);
        const int w = juce::GlyphArrangement::getStringWidthInt(uiFont(400, 13.0f), text) + 2;
        g.drawText(text, x + 24, y, w, 18, juce::Justification::centredLeft, false);
        x += 24 + w + 16;
    };
    legendItem(colour::yellow, pl("słabnie"));
    legendItem(colour::pink, pl("ginie"));
    g.setColour(banner_.isNotEmpty() ? colour::paper : colour::legend);
    g.drawText(banner_.isNotEmpty() ? banner_ : pl("Kliknij słowo, żeby zobaczyć naprawę."), x, y, getWidth() - x - kPad - (getModel_.isVisible() ? 140 : 0), 18, juce::Justification::centredLeft, true);
}

void WordMapView::paint(juce::Graphics& g) {
    g.fillAll(colour::ground);
    // The squared paper of a school notebook.
    g.setColour(colour::paper.withAlpha(0.075f));
    for (int x = 0; x < getWidth(); x += 24) g.fillRect(x, 0, 1, getHeight());
    for (int y = 0; y < getHeight(); y += 24) g.fillRect(0, y, getWidth(), 1);

    SongSession& s = proc_.session();
    const auto paintHint = [&] {
        if (modelHint_.isEmpty()) return;
        g.setFont(uiFont(400, 14.0f));
        g.setColour(colour::legend);
        g.drawFittedText(modelHint_, kPad, 288, getWidth() - kPad * 2, 44, juce::Justification::topLeft, 2, 1.0f);
    };
    switch (phase_) {
        case SessionPhase::Idle:
            paintMessage(g, pl("Przesłuchaj numer, a pokażę słowa, które giną."),
                         pl("Dodaj też WERS na ślad bitu i wybierz tam rolę Bit. Potem kliknij przycisk i puść numer od początku do końca. Gdy zatrzymasz odtwarzanie, zacznie się analiza."), -1.0f);
            paintHint();
            return;
        case SessionPhase::Listening: {
            juce::String line = pl("Puść numer od początku do końca. Po zatrzymaniu zacznę analizę.");
            auto* group = proc_.group();
            if (group != nullptr && group->beatCount() == 0 && !proc_.sidechainActive()) line += pl(" Nie widzę bitu: dodaj WERS na jego ślad.");
            paintMessage(g, pl("Słucham: ") + secondsToClock(s.heardSeconds()), line, -1.0f);
            return;
        }
        case SessionPhase::Measuring:
            paintMessage(g, pl("Mierzę wokal i bit."), pl("To potrwa chwilę. Możesz dalej pracować."), progress_);
            return;
        case SessionPhase::Recognizing:
            paintMessage(g, pl("Rozpoznaję tekst."), pl("Model działa na tym komputerze, nagranie nigdzie nie wychodzi. Dłuższy numer zajmie kilka minut."), progress_);
            return;
        case SessionPhase::CheckingMix:
            paintMessage(g, pl("Sprawdzam tekst w miksie."), pl("Ten sam model słucha teraz wokalu razem z bitem, żeby sprawdzić, które słowa przetrwały."), progress_);
            return;
        case SessionPhase::Scoring:
            paintMessage(g, pl("Oceniam słowa."), pl("Sprawdzam każde słowo na pięciu urządzeniach."), progress_);
            return;
        case SessionPhase::Failed:
            paintMessage(g, pl("Nie słyszałem wokalu."), pl("Sprawdź, czy ten ślad gra podczas odtwarzania, i spróbuj jeszcze raz."), -1.0f);
            paintHint();
            return;
        case SessionPhase::Ready:
            break;
    }
    if (result_ != nullptr && result_->words.empty()) {
        paintMessage(g, pl("Nie znalazłem żadnych słów."), pl("Sprawdź, czy wokal gra na tym śladzie, i przesłuchaj numer jeszcze raz."), -1.0f);
        return;
    }
    paintWords(g);
}

} // namespace wers::ui
