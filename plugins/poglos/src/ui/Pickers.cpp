#include "Pickers.h"
#include "SpaceView.h"
#include "plugin/Params.h"

namespace pog::ui {

namespace {
constexpr int kAlgoCols = 4, kAlgoGap = 8;
constexpr int kEraGap = 8;

void tileFrame(juce::Graphics& g, juce::Rectangle<float> r, bool on, bool over, juce::Colour tint) {
    g.setColour(on ? tint.withAlpha(0.16f) : (over ? colour::raised.brighter(0.08f) : colour::raised.withAlpha(0.55f)));
    g.fillRoundedRectangle(r, 9.0f);
    g.setColour(on ? tint : (over ? colour::hairline.brighter(0.4f) : colour::hairline));
    g.drawRoundedRectangle(r.reduced(0.5f), 9.0f, on ? 1.6f : 1.0f);
}
} // namespace

// --- algorithms ---------------------------------------------------------------------------------

AlgoPicker::AlgoPicker(juce::AudioProcessorValueTreeState& state) {
    link_ = std::make_unique<juce::ParameterAttachment>(*state.getParameter(ids::algo), [this](float v) {
        selected_ = juce::jlimit(0, kAlgoCount - 1, juce::roundToInt(v));
        repaint();
    }, nullptr);
    link_->sendInitialUpdate();
    setTooltip(tooltipFor(selected_));
}

juce::String AlgoPicker::tooltipFor(int index) {
    static const char* tips[kAlgoCount] = {
        "Sala koncertowa: dalekie odbicia boczne, gęsty, rozśpiewany ogon. Do wszystkiego, co ma zagrać w przestrzeni.",
        "Pokój: krótko, blisko, wyraźne odbicia. Żeby nagranie przestało brzmieć jak z szafy.",
        "Płyta: bez odbić, gęsta od pierwszej chwili, jasna i gładka. Klasyka na wokal i werbel.",
        "Komora: twarde, betonowe ściany, odbicia parami, wyraźny środek pasma. Prawie bez ruchu.",
        "Ambient: odbić nie słychać, dźwięk po prostu robi się większy. Na krótko niewidzialny, na długo chmura.",
        "Katedra: bardzo długie linie, dalekie odbicia i powolne narastanie. Ogromnie.",
        "Błysk: duża sala, której ogon wraca podniesiony o interwał. Świetlista smuga nad dźwiękiem.",
        "Nieliniowy: gęsta chmura z własną obwiednią, ucięta na końcu bramki. Narastanie zmienia kształt: opadający, płaski albo odwrócony.",
    };
    return juce::String::fromUTF8(tips[juce::jlimit(0, kAlgoCount - 1, index)]);
}

juce::Rectangle<int> AlgoPicker::tileBounds(int index) const {
    const int rows = (kAlgoCount + kAlgoCols - 1) / kAlgoCols;
    const int w = (getWidth() - kAlgoGap * (kAlgoCols - 1)) / kAlgoCols;
    const int h = (getHeight() - kAlgoGap * (rows - 1)) / rows;
    return { (index % kAlgoCols) * (w + kAlgoGap), (index / kAlgoCols) * (h + kAlgoGap), w, h };
}

int AlgoPicker::indexAt(juce::Point<int> p) const {
    for (int i = 0; i < kAlgoCount; ++i)
        if (tileBounds(i).contains(p)) return i;
    return -1;
}

void AlgoPicker::paint(juce::Graphics& g) {
    const auto names = algoNames();
    for (int i = 0; i < kAlgoCount; ++i) {
        const auto r = tileBounds(i).toFloat();
        const bool on = i == selected_, over = i == hover_;
        tileFrame(g, r, on, over, hue::space);
        const auto icon = r.withTrimmedLeft(8.0f).withWidth(r.getHeight() - 14.0f).withSizeKeepingCentre(r.getHeight() - 16.0f, r.getHeight() - 16.0f);
        const auto c = on ? hue::space : colour::dim;
        const auto shape = spaceShape(static_cast<Algo>(i), icon);
        g.setColour(c.withAlpha(on ? 0.22f : 0.08f));
        g.fillPath(shape);
        drawShapeDetails(g, static_cast<Algo>(i), icon, c, on ? 0.6f : 0.35f);
        g.setColour(c);
        g.strokePath(shape, juce::PathStrokeType(1.4f));
        g.setFont(displayFont(on ? 700 : 600, 15.0f));
        g.setColour(on ? colour::text : colour::dim);
        g.drawFittedText(names[i], juce::Rectangle<int>(juce::roundToInt(icon.getRight()) + 6, 0, juce::roundToInt(r.getRight() - icon.getRight()) - 10, 0)
                                       .withY(juce::roundToInt(r.getY())).withHeight(juce::roundToInt(r.getHeight())),
                         juce::Justification::centredLeft, 1, 0.75f);
    }
}

void AlgoPicker::mouseMove(const juce::MouseEvent& e) {
    const int i = indexAt(e.getPosition());
    if (i != hover_) {
        hover_ = i;
        setTooltip(tooltipFor(i >= 0 ? i : selected_));
        setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void AlgoPicker::mouseExit(const juce::MouseEvent&) {
    hover_ = -1;
    repaint();
}

void AlgoPicker::mouseDown(const juce::MouseEvent& e) {
    const int i = indexAt(e.getPosition());
    if (i >= 0) link_->setValueAsCompleteGesture(static_cast<float>(i));
}

// --- eras ---------------------------------------------------------------------------------------

EraPicker::EraPicker(juce::AudioProcessorValueTreeState& state) {
    link_ = std::make_unique<juce::ParameterAttachment>(*state.getParameter(ids::era), [this](float v) {
        selected_ = juce::jlimit(0, kEraCount - 1, juce::roundToInt(v));
        repaint();
    }, nullptr);
    link_->sendInitialUpdate();
    setTooltip(pl("Epoka: charakter urządzenia, w którym ten pogłos mógłby powstać."));
}

juce::Rectangle<int> EraPicker::tileBounds(int index) const {
    const int w = (getWidth() - kEraGap * (kEraCount - 1)) / kEraCount;
    return { index * (w + kEraGap), 0, w, getHeight() };
}

int EraPicker::indexAt(juce::Point<int> p) const {
    for (int i = 0; i < kEraCount; ++i)
        if (tileBounds(i).contains(p)) return i;
    return -1;
}

void EraPicker::paint(juce::Graphics& g) {
    const auto names = eraNames();
    static const char* subs[kEraCount] = { "czysto, pełne pasmo", "jasno, z ziarnem", "ciemno, chwiejnie" };
    static const juce::Colour tints[kEraCount] = { hue::mid, hue::space, hue::high };
    for (int i = 0; i < kEraCount; ++i) {
        const auto r = tileBounds(i).toFloat();
        const bool on = i == selected_, over = i == hover_;
        const auto tint = tints[i];
        tileFrame(g, r, on, over, tint);
        // A small picture of the era: a smooth wave, a stepped one, a wobbling dark one.
        const auto pic = juce::Rectangle<float>(r.getX() + 14.0f, r.getY() + 12.0f, 46.0f, 22.0f);
        juce::Path wave;
        for (int k = 0; k <= 46; ++k) {
            const float t = static_cast<float>(k) / 46.0f;
            float y = std::sin(t * juce::MathConstants<float>::twoPi * 1.5f);
            if (i == 1) y = std::round(y * 3.0f) / 3.0f;
            if (i == 2) y = 0.7f * std::sin(t * juce::MathConstants<float>::twoPi * 1.5f + 0.6f * std::sin(t * 9.0f));
            const auto at = juce::Point<float>(pic.getX() + pic.getWidth() * t, pic.getCentreY() - y * pic.getHeight() * 0.5f);
            if (k == 0) wave.startNewSubPath(at);
            else wave.lineTo(at);
        }
        g.setColour(on ? tint : colour::faint);
        g.strokePath(wave, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto text = r.withTrimmedLeft(14.0f).withTrimmedRight(8.0f);
        g.setFont(displayFont(on ? 700 : 600, 16.0f));
        g.setColour(on ? colour::text : colour::dim);
        g.drawFittedText(names[i], text.withTop(r.getY() + 40.0f).withHeight(22.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        g.setFont(uiFont(500, 11.5f));
        g.setColour(on ? tint : colour::faint);
        g.drawFittedText(pl(subs[i]), text.withTop(r.getY() + 62.0f).withHeight(18.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
    }
}

void EraPicker::mouseMove(const juce::MouseEvent& e) {
    const int i = indexAt(e.getPosition());
    if (i != hover_) {
        hover_ = i;
        static const char* tips[kEraCount] = {
            "Dziś: czysto i gładko, pełne pasmo, łagodny chorus z odrobiną przypadku.",
            "Lata 80: jasna góra urwana przed powietrzem, krótkie słowo cyfrowe (ziarno w cichym ogonie), wyraźny chorus.",
            "Lata 70: ciemno, wąskie pasmo, szumiąca, nierówna modulacja. Brzmi jak sprzęt na lampach i taśmie.",
        };
        setTooltip(i >= 0 ? juce::String::fromUTF8(tips[i]) : pl("Epoka: charakter urządzenia, w którym ten pogłos mógłby powstać."));
        setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EraPicker::mouseExit(const juce::MouseEvent&) {
    hover_ = -1;
    repaint();
}

void EraPicker::mouseDown(const juce::MouseEvent& e) {
    const int i = indexAt(e.getPosition());
    if (i >= 0) link_->setValueAsCompleteGesture(static_cast<float>(i));
}

} // namespace pog::ui
