#include "ReaperBridge.h"
#include "ReaperHostInterface.h"
#include <cmath>

namespace wers {

namespace {
IReaperHostApplication* reaperHost(void* p) noexcept { return static_cast<IReaperHostApplication*>(p); }
}

ReaperBridge::~ReaperBridge() {
    if (host_ != nullptr) reaperHost(host_)->release();
}

void ReaperBridge::setIHostApplication(Steinberg::FUnknown* host) {
    if (host == nullptr) return;
    void* object = nullptr;
    if (host->queryInterface(reaperHostIid(), &object) != Steinberg::kResultOk || object == nullptr) return;
    auto* reaper = static_cast<IReaperHostApplication*>(object);
    auto getValue = reinterpret_cast<GetValue>(reaper->getReaperApi("GetMediaTrackInfo_Value"));
    if (getValue == nullptr) {
        reaper->release();
        return;
    }
    if (host_ != nullptr) reaperHost(host_)->release();
    host_ = reaper;
    getValue_ = getValue;
    getParent_ = reinterpret_cast<GetParent>(reaper->getReaperApi("GetParentTrack"));
    validate_ = reinterpret_cast<Validate>(reaper->getReaperApi("ValidatePtr2"));
}

void* ReaperBridge::parentTrack() const noexcept {
    if (host_ == nullptr) return nullptr;
    void* track = reaperHost(host_)->getReaperParent(1);
    if (track == nullptr) return nullptr;
    // The plugin can outlive its place on a track for a moment when a project closes.
    if (validate_ != nullptr && !validate_(nullptr, track, "MediaTrack*")) return nullptr;
    return track;
}

const void* ReaperBridge::project() const noexcept {
    return host_ != nullptr ? reaperHost(host_)->getReaperParent(3) : nullptr;
}

float ReaperBridge::trackGainDb() const noexcept {
    void* track = parentTrack();
    if (track == nullptr || getValue_ == nullptr) return 0.0f;
    double gain = 1.0;
    for (int depth = 0; track != nullptr && depth < 32; ++depth) {
        gain *= getValue_(track, "D_VOL");
        // A track that does not send to its folder does not pass through the folder's fader.
        if (getParent_ == nullptr || getValue_(track, "B_MAINSEND") < 0.5) break;
        track = getParent_(track);
    }
    return static_cast<float>(20.0 * std::log10(juce::jlimit(1e-5, 100.0, gain)));
}

} // namespace wers
