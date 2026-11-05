#pragma once
// Connects plugin instances inside one process. Everything the audio thread touches is lock free;
// live data is addressed by project time because hosts may process tracks ahead of each other.
#include "Plan.h"
#include "TimelineStore.h"
#include <chrono>
#include <mutex>

namespace wers {

inline constexpr int kLinkGroups = 8;
inline constexpr int kLinkSlots = 4;

// Live vocal band levels for roughly the last 20 seconds, indexed by project time.
class BandRing {
public:
    static constexpr int kFrames = 4096;
    static constexpr double kHop = 0.005;
    void write(double timeSec, const float* levelsDb) noexcept {
        const auto idx = static_cast<int64_t>(timeSec / kHop);
        Frame& f = frames_[static_cast<size_t>(idx & (kFrames - 1))];
        f.stamp.store(-1, std::memory_order_release);
        for (size_t j = 0; j < kCutBands; ++j) f.levels[j].store(levelsDb[j], std::memory_order_relaxed);
        f.stamp.store(idx, std::memory_order_release);
    }
    // True when data for this time, or for up to 30 ms before it, is available.
    bool read(double timeSec, float* out) const noexcept {
        const auto idx = static_cast<int64_t>(timeSec / kHop);
        for (int64_t back = 0; back < 6; ++back) {
            const Frame& f = frames_[static_cast<size_t>((idx - back) & (kFrames - 1))];
            if (f.stamp.load(std::memory_order_acquire) != idx - back) continue;
            for (size_t j = 0; j < kCutBands; ++j) out[j] = f.levels[j].load(std::memory_order_relaxed);
            if (f.stamp.load(std::memory_order_acquire) == idx - back) return true;
        }
        return false;
    }
private:
    struct Frame {
        std::atomic<int64_t> stamp{ -1 };
        std::array<std::atomic<float>, kCutBands> levels{};
    };
    std::array<Frame, kFrames> frames_{};
};

class LinkGroup {
public:
    // Vocal side, not realtime.
    int claimSlot();                     // -1 when all slots are taken
    void releaseSlot(int slot);
    void publishPlan(int slot, std::shared_ptr<const Plan> plan);
    // Beat side, not realtime.
    void addBeatStore(const std::shared_ptr<TimelineStore>& store);
    void removeBeatStore(const std::shared_ptr<TimelineStore>& store);
    std::vector<std::shared_ptr<TimelineStore>> beatStores() const;
    void beatInstanceDelta(int d) noexcept { beats_.fetch_add(d); }

    // Any thread, lock free.
    BandRing& ring(int slot) noexcept { return rings_[static_cast<size_t>(slot)]; }
    const Plan* plan(int slot) const noexcept { return plans_[static_cast<size_t>(slot)].load(std::memory_order_acquire); }
    void setStale(int slot, bool stale) noexcept { stale_[static_cast<size_t>(slot)].store(stale, std::memory_order_relaxed); }
    bool stale(int slot) const noexcept { return stale_[static_cast<size_t>(slot)].load(std::memory_order_relaxed); }
    void setFixesEnabled(bool on) noexcept { fixesEnabled_.store(on, std::memory_order_relaxed); }
    bool fixesEnabled() const noexcept { return fixesEnabled_.load(std::memory_order_relaxed); }
    // Depth of Miejsce: the beat instance owns it, a vocal instance may ask for a new value.
    void publishPlaceDepth(float d) noexcept { placeDepth_.store(d, std::memory_order_relaxed); }
    float placeDepth() const noexcept { return placeDepth_.load(std::memory_order_relaxed); }
    void requestPlaceDepth(float d) noexcept { requestedDepth_.store(d, std::memory_order_relaxed); }
    float takeRequestedPlaceDepth() noexcept { return requestedDepth_.exchange(-1.0f, std::memory_order_relaxed); }
    // Fader positions after the plugins, when the host tells them.
    void setVocalFaderDb(int slot, float db) noexcept { vocalFader_[static_cast<size_t>(slot)].store(db, std::memory_order_relaxed); }
    float vocalFaderDb(int slot) const noexcept { return vocalFader_[static_cast<size_t>(slot)].load(std::memory_order_relaxed); }
    void setBeatFaderDb(float db) noexcept { beatFader_.store(db, std::memory_order_relaxed); }
    float beatFaderDb() const noexcept { return beatFader_.load(std::memory_order_relaxed); }
    int vocalCount() const noexcept { return vocals_.load(std::memory_order_relaxed); }
    int beatCount() const noexcept { return beats_.load(std::memory_order_relaxed); }
private:
    void purgeRetired();
    mutable std::mutex mutex_;
    std::array<bool, kLinkSlots> taken_{};
    std::array<BandRing, kLinkSlots> rings_{};
    std::array<std::atomic<const Plan*>, kLinkSlots> plans_{};
    std::array<std::shared_ptr<const Plan>, kLinkSlots> owned_{};
    std::array<std::atomic<bool>, kLinkSlots> stale_{};
    struct Retired { std::shared_ptr<const Plan> plan; std::chrono::steady_clock::time_point when; };
    std::vector<Retired> retired_;
    std::vector<std::shared_ptr<TimelineStore>> beatStores_;
    std::atomic<bool> fixesEnabled_{ true };
    std::atomic<int> vocals_{ 0 }, beats_{ 0 };
    std::atomic<float> placeDepth_{ 0.0f }, requestedDepth_{ -1.0f }, beatFader_{ 0.0f };
    std::array<std::atomic<float>, kLinkSlots> vocalFader_{};
};

class LinkHub {
public:
    static std::shared_ptr<LinkHub> get();
    // Each open project has its own groups, so two songs open in tabs of a host never mix.
    // A group lives as long as the hub, so a reference to it stays good. Not realtime.
    LinkGroup& group(int index, const void* project = nullptr);
private:
    struct Project {
        const void* key;
        std::unique_ptr<std::array<LinkGroup, kLinkGroups>> groups;
    };
    std::mutex mutex_;
    std::vector<Project> projects_;
};

} // namespace wers
