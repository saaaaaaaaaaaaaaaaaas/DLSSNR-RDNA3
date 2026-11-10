// CPU regression: generated frames must not overwrite upscaler facts or turn provider FG into a parsed-text Off.
#include "nr_control_presentation.hpp"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
using namespace nr::control;
int main() {
    assert(fg_library_name(L"DLSSG_TO_XEFG_INTEL_IS_BETTER.dll"));
    assert(fg_library_name(L"libxess_fg.dll"));
    assert(fg_library_name(L"amd_fidelityfx_framegeneration_dx12.dll"));
    assert(fg_library_name(L"dlssg_to_fsr3_amd_is_better.dll"));
    assert(!fg_library_name(L"DD2.exe") && !fg_library_name(L"OptiScaler.dll"));
    PresentationFacts facts;
    assert(facts.snapshot(0).fg.state == FgState::Unknown);
    facts.upscaler({1280,720,1920,1080},100);
    facts.upscaler({0,0,1920,1080},101);  // no upscaler input on a generated frame
    assert(facts.snapshot(102).extent.render_w == 1280);
    facts.provider(FgSource::XeSSMFG,7,true,6,100);
    facts.measurement(60,60,0,1100);     // Proton's hooked DXGI may expose only the real frames
    auto f=facts.snapshot(1100).fg;
    assert(f.state==FgState::On && f.source==FgSource::XeSSMFG && f.multiplier==6);
    facts.provider(FgSource::DLSSG,8,true,6,1101);
    facts.measurement(60,60,6,1102);
    facts.provider(FgSource::XeSSMFG,7,false,0,1103);
    assert(facts.snapshot(1104).fg.state==FgState::Off);
    assert(facts.snapshot(5000).fg.state==FgState::Unknown);
    assert(!facts.snapshot(5000).extent.valid());
    facts.upscaler({960,540,1280,720},5000);
    assert(facts.snapshot(5000).extent.output_w==1280);
    PresentationFacts ratio;
    ratio.measurement(200,1,0,1000);     // loading presents cannot establish FG
    assert(ratio.snapshot(1000).fg.state==FgState::Unknown);
    ratio.measurement(60,60,0,2000);
    assert(ratio.snapshot(2000).fg.state==FgState::Unknown); // absence of visible presents is not proof of Off
    ratio.measurement(90,60,0,3000);
    assert(ratio.snapshot(3000).fg.state==FgState::Unknown);
    ratio.measurement(90,60,0,4000);
    assert(ratio.snapshot(4000).fg.state==FgState::On);
    ratio.measurement(60,60,3,5000);
    assert(ratio.snapshot(5000).fg.multiplier==3);          // explicit declaration has no numeric text parser
    ratio.provider(FgSource::FSR,1,true,0,5001);
    ratio.provider(FgSource::FSR,2,false,0,5002);
    assert(ratio.snapshot(5003).fg.state==FgState::On);     // destroying/pausing another context is not global Off
    ratio.provider(FgSource::XeSS,3,true,4,5004);
    assert(ratio.snapshot(5005).fg.multiplier==4);
    // The UI must see a complete tuple while an evaluate thread changes the extent.
    PresentationFacts concurrent;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for(unsigned i=1;i<50000;++i) concurrent.upscaler(i%2 ? UpscalerExtent{1280,720,1920,1080} : UpscalerExtent{960,540,1280,720},100);
        done=true;
    });
    do {
        const auto e=concurrent.snapshot(101).extent;
        if(e.valid()) assert((e.render_w==1280 && e.render_h==720 && e.output_w==1920 && e.output_h==1080) ||
                            (e.render_w==960 && e.render_h==540 && e.output_w==1280 && e.output_h==720));
    } while(!done.load());
    writer.join();
    puts("NR Control presentation: PASS (provider/MFG, unknown/off, expiry, resize and concurrent snapshots)");
}
