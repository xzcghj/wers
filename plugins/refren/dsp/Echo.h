#pragma once
// A repeat in time with the song. When the tempo changes, the time slides to the new one.
#include "RefrenParams.h"
#include <vector>

namespace refren {

class Echo {
public:
    void prepare(double fs);
    void reset() noexcept;
    void set(EchoTime time, float feedback, bool ping, float toneHz, double bpm) noexcept;
    void tick(float inL, float inR, float& outL, float& outR) noexcept;
private:
    float read(const std::vector<float>& line) const noexcept;

    double fs_ = 48000.0;
    std::array<std::vector<float>, 2> line_;
    std::array<float, 2> tone_{};
    int size_ = 1, position_ = 0;
    double timeNow_ = 1.0, timeWanted_ = 1.0, glide_ = 0.001;
    float feedback_ = 0.0f, feedbackTarget_ = 0.0f, smooth_ = 0.001f, toneHz_ = 0.0f, toneCoef_ = 1.0f;
    bool ping_ = false, snap_ = true;
};

} // namespace refren
