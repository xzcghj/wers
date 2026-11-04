#include "RoleViews.h"
#include "dsp/Monitor.h"

namespace wers::ui {

BeatView::BeatView(WersProcessor& p) : proc_(p) {
    addAndMakeVisible(depth_);
    depthA_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state(), ids::place, depth_.slider);
}

void BeatView::resized() { depth_.setBounds(28, 160, 420, 52); }

void BeatView::tick() {
    std::array<float, kCutBands> c{};
    proc_.beatCuts(c.data());
    bool changed = false;
    for (size_t i = 0; i < kCutBands; ++i) if (std::abs(c[i] - cuts_[i]) > 0.05f) { cuts_[i] = c[i]; changed = true; }

    auto* g = proc_.group();
    juce::String status;
    if (g == nullptr || g->vocalCount() == 0) {
        status = proc_.sidechainActive() ? pl("Działam z sidechainu. Dla napraw słów dodaj WERS na wokal.") : pl("Nie widzę wokalu. Dodaj WERS na wokal albo podaj go na sidechain.");
    } else {
        int events = 0; bool map = false;
        for (int s = 0; s < kLinkSlots; ++s) if (const Plan* pl_ = g->plan(s)) { events += static_cast<int>(pl_->events.size()); map = map || pl_->mapFrames > 0; }
        status = pl("Połączono z wokalem.");
        if (map) status += pl(" Znam wokal z analizy, więc robię miejsce z wyprzedzeniem.");
        else status += pl(" Po przesłuchaniu numeru będę robić miejsce z wyprzedzeniem.");
        if (events > 0) status += pl(" Naprawy słów: ") + juce::String(events) + ".";
    }
    if (status != status_) { status_ = status; changed = true; }
    if (changed) repaint();
}

void BeatView::paint(juce::Graphics& g) {
    g.fillAll(colour::ground);
    g.setColour(colour::paper.withAlpha(0.075f));
    for (int x = 0; x < getWidth(); x += 24) g.fillRect(x, 0, 1, getHeight());
    for (int y = 0; y < getHeight(); y += 24) g.fillRect(0, y, getWidth(), 1);

    g.setColour(colour::paper);
    g.setFont(displayFont(600, 34.0f));
    g.drawText("Miejsce", 28, 26, 500, 44, juce::Justification::centredLeft, false);
    g.setFont(uiFont(400, 16.0f));
    g.setColour(colour::legend);
    g.drawFittedText(pl("Ta wtyczka robi miejsce dla wokalu w środku bitu. Sub, kick i boki stereo zostają bez zmian."), 28, 76, 620, 48, juce::Justification::topLeft, 2, 1.0f);
    g.setFont(uiFont(500, 15.0f));
    g.setColour(colour::paper);
    g.drawFittedText(status_, 28, 226, 620, 48, juce::Justification::topLeft, 2, 1.0f);

    // How much each band is being cut right now. The bars hang from zero, like gain reduction meters.
    g.setFont(uiFont(500, 14.0f));
    g.setColour(colour::paper);
    g.drawText(pl("Ile tnę teraz"), 28, 276, 200, 20, juce::Justification::centredLeft, false);
    const juce::Rectangle<int> area(28, 302, getWidth() - 56, getHeight() - 350);
    const float slot = static_cast<float>(area.getWidth()) / kCutBands;
    for (int i = 0; i < kCutBands; ++i) {
        const auto x = static_cast<float>(area.getX()) + slot * static_cast<float>(i);
        const juce::Rectangle<float> bar(x + 6.0f, static_cast<float>(area.getY()), slot - 12.0f, static_cast<float>(area.getHeight()));
        g.setColour(colour::track.withAlpha(0.45f));
        g.fillRoundedRectangle(bar, 2.0f);
        const float cut = cuts_[static_cast<size_t>(i)];
        const float amt = juce::jlimit(0.0f, 1.0f, cut / 9.0f);
        g.setColour(colour::yellow);
        if (amt > 0.0f) g.fillRoundedRectangle(bar.withHeight(juce::jmax(2.0f, bar.getHeight() * amt)), 2.0f);
        if (cut >= 0.5f) {
            const bool inside = bar.getHeight() * amt > 30.0f;
            g.setColour(inside ? colour::ground : colour::paper);
            g.setFont(uiFont(600, 14.0f));
            const juce::String text = juce::String::charToString(juce::juce_wchar(0x2212)) + commaNumber(cut);
            g.drawText(text, juce::Rectangle<float>(bar.getX(), inside ? bar.getY() + 6.0f : bar.getY() + bar.getHeight() * amt + 4.0f, bar.getWidth(), 20.0f).toNearestInt(), juce::Justification::centred, false);
        }
        g.setColour(colour::legend);
        g.setFont(uiFont(400, 13.0f));
        g.drawText(frequencyLabel(kCutCentres[static_cast<size_t>(i)]), juce::Rectangle<float>(x, static_cast<float>(area.getBottom()) + 6.0f, slot, 20.0f).toNearestInt(), juce::Justification::centred, false);
    }
}

MasterView::MasterView(WersProcessor& p) : proc_(p) {
    norm_.setItems({ pl("Bez normalizacji"), pl("Spotify −14"), pl("Spotify −11"), pl("Spotify −19") }, { 150, 110, 110, 110 });
    norm_.onPick = [this](int i) {
        if (auto* param = proc_.state().getParameter(ids::monNorm)) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(i)));
            param->endChangeGesture();
        }
    };
    addAndMakeVisible(norm_);
    addAndMakeVisible(reset_);
    reset_.onClick = [this] { proc_.resetMeters(); };
}

void MasterView::resized() {
    norm_.setBounds(28, 96, norm_.preferredWidth(), 44);
    reset_.setBounds(getWidth() - 28 - 130, 96, 130, 44);
}

void MasterView::tick() {
    const double before = values_.seconds;
    values_ = proc_.meters();
    if (values_.seconds < before) { history_.clear(); historyStep_ = 0.5; historyNext_ = 0.0; }
    if (values_.seconds >= historyNext_ && values_.shortTerm > -100.0) {
        history_.push_back(static_cast<float>(values_.shortTerm));
        historyNext_ = values_.seconds + historyStep_;
        if (history_.size() >= 1200) {
            for (size_t i = 0; i < history_.size() / 2; ++i) history_[i] = history_[i * 2];
            history_.resize(history_.size() / 2);
            historyStep_ *= 2.0;
        }
    }
    deviceIndex_ = static_cast<int>(proc_.state().getRawParameterValue(ids::monDevice)->load());
    normIndex_ = static_cast<int>(proc_.state().getRawParameterValue(ids::monNorm)->load());
    norm_.setSelected(normIndex_);
    repaint();
}

void MasterView::paint(juce::Graphics& g) {
    g.fillAll(colour::ground);
    g.setColour(colour::paper.withAlpha(0.075f));
    for (int x = 0; x < getWidth(); x += 24) g.fillRect(x, 0, 1, getHeight());
    for (int y = 0; y < getHeight(); y += 24) g.fillRect(0, y, getWidth(), 1);

    g.setColour(colour::paper);
    g.setFont(displayFont(600, 34.0f));
    g.drawText(pl("Odsłuch Prawdy"), 28, 26, 600, 44, juce::Justification::centredLeft, false);

    const auto number = [](double v, const juce::String& unit) { return v > -100.0 ? commaNumber(static_cast<float>(v)) + " " + unit : juce::String::charToString(juce::juce_wchar(0x2013)); };
    const auto cell = [&](int x, const juce::String& name, const juce::String& value) {
        g.setColour(colour::legend);
        g.setFont(uiFont(400, 14.0f));
        g.drawText(name, x, 170, 280, 22, juce::Justification::centredLeft, false);
        g.setColour(colour::paper);
        g.setFont(displayFont(700, 44.0f));
        g.drawText(value, x, 192, 280, 58, juce::Justification::centredLeft, false);
    };
    cell(28, pl("Cały numer"), number(values_.integrated, "LUFS"));
    cell(318, pl("Ostatnie 3 sekundy"), number(values_.shortTerm, "LUFS"));
    cell(608, pl("Ta chwila"), number(values_.momentary, "LUFS"));
    cell(898, "True peak", number(values_.truePeak, "dBTP"));

    g.setFont(uiFont(500, 16.0f));
    juce::String advice;
    const bool loud = values_.integrated > -14.0 && values_.seconds > 5.0;
    if (loud && values_.truePeak > -2.0) advice = pl("Master głośniejszy niż −14 LUFS ma szczyty powyżej −2 dBTP. Spotify zaleca tu mniej, bo kodek może je zniekształcić.");
    else if (values_.seconds > 5.0 && values_.truePeak > -1.0) advice = pl("Szczyty powyżej −1 dBTP. Spotify zaleca zejść poniżej tej wartości.");
    else if (values_.seconds > 5.0) advice = pl("Szczyty w normie zaleceń Spotify.");
    else advice = pl("Puść numer, a zmierzę głośność.");
    if (values_.seconds > 5.0 && values_.integrated > -70.0) {
        // What actually happens to the song on Spotify at its default setting.
        const double spotify = spotifyGainDb(values_.integrated, values_.truePeak, NormMode::Normal14);
        if (spotify < -0.05) advice += " " + pl("Spotify ściszy ten numer o ") + commaNumber(static_cast<float>(-spotify)) + " dB.";
        else if (spotify > 0.05) advice += " " + pl("Spotify pogłośni ten numer o ") + commaNumber(static_cast<float>(spotify)) + " dB.";
    }
    g.setColour(colour::paper);
    g.drawFittedText(advice, 28, 262, getWidth() - 56, 46, juce::Justification::topLeft, 2, 1.0f);

    g.setFont(uiFont(400, 15.0f));
    g.setColour(colour::legend);
    g.drawFittedText(deviceIndex_ != 4 ? pl("Słuchasz symulacji urządzenia. W renderze offline wyłącza się sama, ale podczas nagrywania wyjścia w czasie rzeczywistym wybierz Studio.")
                                       : pl("Wybierz urządzenie u góry, żeby usłyszeć miks tak, jak na nim zagra. Na końcu łańcucha mastera ta rola tylko mierzy, dopóki nie włączysz symulacji."),
                     28, 312, getWidth() - 56, 44, juce::Justification::topLeft, 2, 1.0f);

    paintHistory(g, juce::Rectangle<float>(28.0f, 388.0f, static_cast<float>(getWidth() - 56), static_cast<float>(getHeight() - 388 - 28)));
}

void MasterView::paintHistory(juce::Graphics& g, juce::Rectangle<float> area) const {
    constexpr float kTop = -2.0f, kBottom = -32.0f;
    const auto yFor = [&](float lufs) { return area.getY() + area.getHeight() * (kTop - juce::jlimit(kBottom, kTop, lufs)) / (kTop - kBottom); };
    g.setFont(uiFont(500, 14.0f));
    g.setColour(colour::paper);
    g.drawText(pl("Głośność w czasie (ostatnie 3 sekundy, LUFS)"), area.withHeight(20.0f).translated(0.0f, -24.0f).toNearestInt(), juce::Justification::centredLeft, false);
    g.setColour(colour::track.withAlpha(0.45f));
    g.fillRoundedRectangle(area, 2.0f);

    // The reference lines are the levels Spotify normalises to.
    g.setFont(uiFont(400, 13.0f));
    for (const auto& line : { std::pair<float, const char*>{ -11.0f, "Spotify głośno" }, { -14.0f, "Spotify normalnie" }, { -19.0f, "Spotify cicho" } }) {
        const float y = yFor(line.first);
        g.setColour(colour::line);
        g.fillRect(area.getX(), y, area.getWidth(), 1.0f);
        g.setColour(colour::legend);
        g.drawText(juce::String::charToString(juce::juce_wchar(0x2212)) + juce::String(juce::roundToInt(-line.first)) + "  " + pl(line.second), juce::Rectangle<float>(area.getRight() - 210.0f, y - 19.0f, 202.0f, 18.0f).toNearestInt(), juce::Justification::centredRight, false);
    }
    if (history_.size() < 2) return;
    juce::Path path;
    const float dx = area.getWidth() / static_cast<float>(juce::jmax<size_t>(history_.size() - 1, 240));
    for (size_t i = 0; i < history_.size(); ++i) {
        const float x = area.getX() + dx * static_cast<float>(i), y = yFor(history_[i]);
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(colour::yellow);
    g.strokePath(path, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace wers::ui
