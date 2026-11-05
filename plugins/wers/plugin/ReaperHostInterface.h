#pragma once
// The interface REAPER offers through its VST3 host object, as published in reaper_vst3_interfaces.h.
#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>

namespace wers {

class IReaperHostApplication : public Steinberg::FUnknown {
public:
    virtual void* PLUGIN_API getReaperApi(Steinberg::CStringA name) = 0;
    // The object this plugin instance belongs to: track is 1, take 2, project 3.
    virtual void* PLUGIN_API getReaperParent(Steinberg::uint32 what) = 0;
    virtual void* PLUGIN_API reaperExtended(Steinberg::uint32 call, void* a, void* b, void* c) = 0;
};

inline const Steinberg::TUID& reaperHostIid() noexcept {
    static const Steinberg::TUID id = INLINE_UID(0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);
    return id;
}

} // namespace wers
