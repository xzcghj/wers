#include "BandCard.h"
#include "plugin/PowiewProcessor.h"

namespace powiew::ui {

namespace {
constexpr float kAddedTopDb = 10.0f;
}

BandCard::BandCard(PowiewProcessor& p, int band)
    : processor_(p), band_(band), tint_(band == 0 ? hue::presence : hue::air),
      amount_(p.state(), band == 0 ? ids::presence : ids::air, band == 0 ? pl("Obecność") : pl("Powietrze"), tint_),
      frequency_(p.state(), band == 0 ? ids::presenceHz : ids::airHz, pl("Częstotliwość"), tint_) {
    amount_.setLarge(true);
    if (band == 0) {
        amount_.setTip(pl("Środek góry, 3–10 kHz: wyrazistość, dykcja, atak. Podbicie i nowe harmoniczne rosną razem; na sybilantach detektor je cofa."));
        frequency_.setTip(pl("Środek pasma obecności. Wokal zwykle 4–7 kHz, werbel i gitara 5–6 kHz."));
    } else {
        amount_.setTip(pl("Sama góra, od 8 kHz w górę: przestrzeń, oddech, połysk. Półka i harmoniczne z pasma poniżej; detektor cofa je, gdy góra już jest jasna."));
        frequency_.setTip(pl("Od tej częstotliwości zaczyna się półka powietrza. Wyżej znaczy delikatniej i bardziej „drogo”."));
    }
    addAndMakeVisible(amount_);
    addAndMakeVisible(frequency_);
}

void BandCard::tick() {
    amount_.refresh();
    frequency_.refresh();
    const auto& e = processor_.engine();
    const float a = juce::jmax(0.0f, band_ == 0 ? e.presenceAddedDb() : e.airAddedDb());
    const float b = band_ == 0 ? e.presenceBackoff() : e.airBackoff();
    added_ += (a > added_ ? 0.5f : 0.15f) * (a - added_);
    backoff_ += (b > backoff_ ? 0.7f : 0.12f) * (b - backoff_);
    // The peak marker falls slowly, so a short "s" stays visible for a moment.
    backoffPeak_ = juce::jmax(backoff_, backoffPeak_ - 0.02f);
    repaint(bar(0).toNearestInt().expanded(4).withTop(bar(0).toNearestInt().getY() - 22).withRight(getWidth()));
    repaint(bar(1).toNearestInt().expanded(4).withTop(bar(1).toNearestInt().getY() - 22).withRight(getWidth()));
}

juce::Rectangle<float> BandCard::bar(int row) const {
    const float x = 186.0f;
    return { x, row == 0 ? 172.0f : 222.0f, static_cast<float>(getWidth()) - x - 18.0f, 7.0f };
}

void BandCard::resized() {
    amount_.setBounds(10, 34, 168, 206);
    frequency_.setBounds(186, 30, static_cast<int>(bar(0).getWidth()), 104);
}

void BandCard::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat();
    g.setColour(colour::surface);
    g.fillRoundedRectangle(r, 12.0f);
    g.setColour(tint_.withAlpha(0.32f));
    g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, 1.0f);
    g.setFont(uiFont(600, 11.5f));
    g.setColour(colour::faint);
    g.drawText(band_ == 0 ? pl("3–10 kHz · DYNAMICZNA OBECNOŚĆ") : pl("8–20 kHz · DYNAMICZNE POWIETRZE"), juce::Rectangle<float>(16.0f, 8.0f, r.getWidth() - 32.0f, 16.0f),
               juce::Justification::centredLeft);

    auto meter = [&](int row, const juce::String& name, const juce::String& value, float fill, juce::Colour c, float marker) {
        const auto b = bar(row);
        g.setFont(uiFont(600, 11.0f));
        g.setColour(colour::dim);
        g.drawText(name, juce::Rectangle<float>(b.getX(), b.getY() - 19.0f, b.getWidth() * 0.6f, 15.0f), juce::Justification::centredLeft);
        g.setFont(uiFont(600, 13.0f));
        g.setColour(fill > 0.02f ? c : colour::faint);
        g.drawText(value, juce::Rectangle<float>(b.getRight() - 90.0f, b.getY() - 20.0f, 90.0f, 16.0f), juce::Justification::centredRight);
        g.setColour(colour::bg);
        g.fillRoundedRectangle(b, 3.5f);
        if (fill > 0.0f) {
            juce::ColourGradient grad(c.withAlpha(0.55f), b.getX(), 0.0f, c, b.getRight(), 0.0f, false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(b.withWidth(juce::jmax(3.0f, b.getWidth() * juce::jlimit(0.0f, 1.0f, fill))), 3.5f);
        }
        if (marker > 0.01f) {
            g.setColour(c);
            g.fillRect(b.getX() + b.getWidth() * juce::jlimit(0.0f, 1.0f, marker) - 1.0f, b.getY() - 2.0f, 2.0f, b.getHeight() + 4.0f);
        }
    };
    meter(0, pl("DODANE"), "+" + juce::String(added_, 1).replaceCharacter('.', ',') + " dB", added_ / kAddedTopDb, tint_, 0.0f);
    meter(1, pl("HAMOWANIE"), juce::String(juce::roundToInt(100.0f * backoff_)) + "%", backoff_, colour::weak, backoffPeak_);
}

} // namespace powiew::ui
