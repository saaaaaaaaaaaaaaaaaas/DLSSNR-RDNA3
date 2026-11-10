#pragma once
// Presentation facts have different producers from the per-layer NR diagnostics.
// Keep a coherent copy: a generated frame has no new upscaler extent or NR run.
#include <cstdint>
#include <cwctype>
#include <string>
#include <map>
#include <mutex>
#include <utility>

namespace nr::control {
inline bool fg_library_name(std::wstring name) {
    for (wchar_t& c : name) c = wchar_t(std::towlower(c));
    return name.rfind(L"libxess_fg", 0) == 0 || name.find(L"framegeneration") != std::wstring::npos ||
           name.rfind(L"amd_fidelityfx", 0) == 0 || name == L"sl.dlss_g.dll" || name == L"nvngx_dlssg.dll" ||
           name.rfind(L"dlssg_to_fsr3", 0) == 0 || name.rfind(L"dlssg_to_xefg", 0) == 0 ||
           name.rfind(L"dlss-enabler", 0) == 0 || name.rfind(L"ffx_backend_dx12", 0) == 0 ||
           name.rfind(L"ffx_frameinterpolation", 0) == 0 || name.rfind(L"ffx_fsr3", 0) == 0;
}
enum class FgSource { None, Measurement, DLSSG, FSR, XeSS, XeSSMFG };
enum class FgState { Unknown, Off, On };
struct FgInfo {
    FgState state = FgState::Unknown;
    FgSource source = FgSource::None;
    double multiplier = 0;
};
struct UpscalerExtent {
    uint32_t render_w = 0, render_h = 0, output_w = 0, output_h = 0;
    bool valid() const {
        return render_w && render_h && output_w && output_h && render_w <= 16384 && render_h <= 16384 &&
               output_w <= 16384 && output_h <= 16384;
    }
};
struct PresentationSnapshot { UpscalerExtent extent; FgInfo fg; };

class PresentationFacts {
    struct Provider { FgInfo info; uint64_t seen; };
    std::mutex mutex_;
    UpscalerExtent extent_;
    uint64_t extent_at_ = 0, measured_at_ = 0;
    bool extent_known_ = false, measured_known_ = false;
    FgInfo measured_;
    unsigned on_windows_ = 0;
    std::map<std::pair<FgSource, uintptr_t>, Provider> providers_;
    static bool fresh(uint64_t now, uint64_t seen) { return now >= seen && now - seen <= 2500; }
public:
    void upscaler(UpscalerExtent extent, uint64_t now) {
        if (!extent.valid()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        extent_ = extent; extent_at_ = now; extent_known_ = true;
    }
    // Successful provider calls only; loading a library or selecting it in an INI is not evidence of FG.
    void provider(FgSource source, uintptr_t context, bool enabled, double multiplier, uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto i = providers_.begin(); i != providers_.end();)
            if (!fresh(now, i->second.seen)) i = providers_.erase(i); else ++i;
        providers_[{source, context}] = {{enabled ? FgState::On : FgState::Off, source,
                                       enabled && multiplier >= 2 ? multiplier : 0}, now};
    }
    unsigned declaration(uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        unsigned multiplier = 0;
        for (const auto& p : providers_)
            if (p.first.first == FgSource::DLSSG && p.second.info.state == FgState::On && fresh(now, p.second.seen))
                multiplier = unsigned(p.second.info.multiplier);
        return multiplier;
    }
    void measurement(uint64_t presents, uint64_t frames, unsigned declared, uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool playing = frames >= 10;
        const double ratio = frames ? double(presents) / double(frames) : 0;
        on_windows_ = playing && ratio >= 1.3 ? on_windows_ + 1 : 0;
        measured_ = declared > 1 ? FgInfo{FgState::On, FgSource::DLSSG, double(declared)}
            : FgInfo{on_windows_ >= 2 ? FgState::On : FgState::Unknown, FgSource::Measurement, playing ? ratio : 0};
        measured_at_ = now; measured_known_ = true;
    }
    PresentationSnapshot snapshot(uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        PresentationSnapshot out;
        if (extent_known_ && fresh(now, extent_at_)) out.extent = extent_;
        if (measured_known_ && fresh(now, measured_at_)) out.fg = measured_;
        // An explicit DLSS-G declaration beats the aggregate Present ratio. Provider activity is independent of
        // whether DXGI exposes the generator's presents to our hooks (Proton can hide them).
        const Provider* latest = nullptr;
        for (const auto& p : providers_) {
            if (!fresh(now, p.second.seen)) continue;
            if (!latest || (p.second.info.state == FgState::On && latest->info.state != FgState::On) ||
                (p.second.info.state == latest->info.state && p.second.seen > latest->seen)) latest = &p.second;
        }
        // The NGX replacement can explicitly disable interpolation while an earlier DLSS-G declaration is fresh.
        for (const auto& p : providers_)
            if (p.first.first == FgSource::XeSSMFG && fresh(now, p.second.seen) &&
                (!latest || latest->info.source != FgSource::XeSSMFG || p.second.seen > latest->seen)) latest = &p.second;
        if (latest) out.fg = latest->info;
        return out;
    }
};
} // namespace nr::control
