#include "LinkHub.h"
#include <algorithm>

namespace wers {

std::shared_ptr<LinkHub> LinkHub::get() {
    static std::mutex m;
    static std::weak_ptr<LinkHub> weak;
    std::lock_guard<std::mutex> g(m);
    auto hub = weak.lock();
    if (!hub) { hub = std::make_shared<LinkHub>(); weak = hub; }
    return hub;
}

LinkGroup& LinkHub::group(int index, const void* project) {
    std::lock_guard<std::mutex> g(mutex_);
    const auto at = static_cast<size_t>(std::clamp(index, 0, kLinkGroups - 1));
    for (auto& p : projects_) if (p.key == project) return (*p.groups)[at];
    projects_.push_back({ project, std::make_unique<std::array<LinkGroup, kLinkGroups>>() });
    return (*projects_.back().groups)[at];
}

int LinkGroup::claimSlot() {
    std::lock_guard<std::mutex> g(mutex_);
    for (int i = 0; i < kLinkSlots; ++i) {
        if (!taken_[static_cast<size_t>(i)]) {
            taken_[static_cast<size_t>(i)] = true;
            vocals_.fetch_add(1);
            return i;
        }
    }
    return -1;
}

void LinkGroup::releaseSlot(int slot) {
    if (slot < 0 || slot >= kLinkSlots) return;
    publishPlan(slot, nullptr);
    std::lock_guard<std::mutex> g(mutex_);
    taken_[static_cast<size_t>(slot)] = false;
    stale_[static_cast<size_t>(slot)].store(false);
    vocalFader_[static_cast<size_t>(slot)].store(0.0f);
    vocals_.fetch_sub(1);
}

void LinkGroup::purgeRetired() {
    // A reader holds a plan pointer for one audio block at most, so a few seconds is plenty.
    const auto now = std::chrono::steady_clock::now();
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(), [&](const Retired& r) { return now - r.when > std::chrono::seconds(5); }), retired_.end());
}

void LinkGroup::publishPlan(int slot, std::shared_ptr<const Plan> plan) {
    if (slot < 0 || slot >= kLinkSlots) return;
    std::lock_guard<std::mutex> g(mutex_);
    const auto s = static_cast<size_t>(slot);
    plans_[s].store(plan.get(), std::memory_order_release);
    if (owned_[s]) retired_.push_back({ std::move(owned_[s]), std::chrono::steady_clock::now() });
    owned_[s] = std::move(plan);
    purgeRetired();
}

void LinkGroup::addBeatStore(const std::shared_ptr<TimelineStore>& store) {
    std::lock_guard<std::mutex> g(mutex_);
    beatStores_.push_back(store);
}

void LinkGroup::removeBeatStore(const std::shared_ptr<TimelineStore>& store) {
    std::lock_guard<std::mutex> g(mutex_);
    beatStores_.erase(std::remove(beatStores_.begin(), beatStores_.end(), store), beatStores_.end());
}

std::vector<std::shared_ptr<TimelineStore>> LinkGroup::beatStores() const {
    std::lock_guard<std::mutex> g(mutex_);
    return beatStores_;
}

} // namespace wers
