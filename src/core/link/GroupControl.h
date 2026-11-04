#pragma once
// What the beat instance reads from the vocal instances of its group.
#include "LinkHub.h"
#include "dsp/Place.h"

namespace wers {

// One object for the whole life of an instance. Switching groups swaps a pointer, and the groups
// live as long as the hub, so the audio thread can never be left holding something freed.
class GroupPlaceControl final : public PlaceControl {
public:
    GroupPlaceControl() noexcept = default;
    explicit GroupPlaceControl(LinkGroup& group) noexcept : group_(&group) {}
    void setGroup(LinkGroup* group) noexcept { group_.store(group); }
    bool vocalBands(double t, float* out) const noexcept override {
        LinkGroup* group = group_.load();
        if (group == nullptr || t < 0.0) return false;
        bool any = false;
        std::array<float, kCutBands> tmp{};
        for (int s = 0; s < kLinkSlots; ++s) {
            const Plan* plan = group->plan(s);
            bool have = false;
            if (plan != nullptr && !group->stale(s)) have = plan->vocalBandsAt(t, tmp.data());
            if (!have) have = group->ring(s).read(t, tmp.data());
            if (!have) continue;
            for (size_t j = 0; j < kCutBands; ++j) out[j] = any ? std::max(out[j], tmp[j]) : tmp[j];
            any = true;
        }
        return any;
    }
    bool fixCuts(double t, float* out) const noexcept override {
        LinkGroup* group = group_.load();
        if (group == nullptr || t < 0.0 || !group->fixesEnabled()) return false;
        bool any = false;
        std::array<float, kCutBands> tmp{};
        for (int s = 0; s < kLinkSlots; ++s) {
            const Plan* plan = group->plan(s);
            if (plan == nullptr || group->stale(s) || !plan->fixCutsAt(t, tmp.data())) continue;
            for (size_t j = 0; j < kCutBands; ++j) out[j] = any ? std::max(out[j], tmp[j]) : tmp[j];
            any = true;
        }
        return any;
    }
private:
    std::atomic<LinkGroup*> group_{ nullptr };
};

} // namespace wers
