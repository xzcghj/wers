#pragma once
// Every voice as a dot: across is where it stands, up and down is its interval, and how bright it is follows the level
// of its layer and the mask. The dry voice is the larger dot in the middle. All of it comes from atomic values.
#include "plugin/RefrenProcessor.h"
#include "Theme.h"

namespace refren {

class StageView final : public juce::Component {
public:
    explicit StageView(RefrenProcessor& processor);
    // Takes what is new, for the editor's timer. Paints only when something changed.
    void refresh();
    int voiceDots() const noexcept { return static_cast<int>(dots_.size()); }
    int dotCount() const noexcept { return voiceDots() + 1; }
    float brightnessOf(int layer) const noexcept { return brightness_[static_cast<size_t>(layer)]; }
    int moves() const noexcept { return moves_; }
    void paint(juce::Graphics&) override;
private:
    struct Dot {
        float pan = 0.0f, interval = 0.0f;
        int layer = 0, index = 0;
        bool operator==(const Dot&) const = default;
    };
    struct LayerValues {
        std::atomic<float>* source, *semis, *note, *width;
    };
    RefrenProcessor& processor_;
    std::array<LayerValues, kLayers> values_{};
    std::atomic<float>* widthScale_ = nullptr;
    std::vector<Dot> dots_, next_;
    std::array<float, kLayers> brightness_{};
    int overloadTicks_ = 0, moves_ = 0;
};

} // namespace refren
