#pragma once
// What the beat instance reads from the vocal instances of its group.
#include "LinkHub.h"
#include "dsp/Place.h"

namespace wers {

class GroupPlaceControl final : public PlaceControl {
public:
    explicit GroupPlaceControl(LinkGroup& group) noexcept : group_(group) {}
    bool vocalBands(double t, float* out) const noexcept override {
        if (t < 0.0) return false;
        bool any = false;
        std::array<float, kCutBands> tmp{};
        for (int s = 0; s < kLinkSlots; ++s) {
            const Plan* plan = group_.plan(s);
            bool have = false;
            if (plan != nullptr && !group_.stale(s)) have = plan->vocalBandsAt(t, tmp.data());
            if (!have) have = group_.ring(s).read(t, tmp.data());
            if (!have) continue;
            for (size_t j = 0; j < kCutBands; ++j) out[j] = any ? std::max(out[j], tmp[j]) : tmp[j];
            any = true;
        }
        return any;
    }
    bool fixCuts(double t, float* out) const noexcept override {
        if (t < 0.0 || !group_.fixesEnabled()) return false;
        bool any = false;
        std::array<float, kCutBands> tmp{};
        for (int s = 0; s < kLinkSlots; ++s) {
            const Plan* plan = group_.plan(s);
            if (plan == nullptr || group_.stale(s) || !plan->fixCutsAt(t, tmp.data())) continue;
            for (size_t j = 0; j < kCutBands; ++j) out[j] = any ? std::max(out[j], tmp[j]) : tmp[j];
            any = true;
        }
        return any;
    }
private:
    LinkGroup& group_;
};

} // namespace wers
