#include "nr_temporal_cadence.hpp"
#include <cassert>
#include <cstdio>

int main() {
    for(int n=2;n<=4;++n) {
        nr::TemporalCadence a,b;
        for(unsigned frame=0;frame<120;++frame) {
            // Independent features may interleave and reset their image history.
            // Their only scheduling input is a real evaluation and its interval.
            assert(a.next(n)==(frame%unsigned(n)==0));
            if(frame%3==0) assert(b.next(n)==((frame/3)%unsigned(n)==0));
        }
        assert(a.full==120/unsigned(n));
        assert(a.reconstructed+a.full==a.frames);
        assert(a.unexpected==0);
        auto resized=a;
        assert(resized.next(n)==a.next(n));
    }
    for (unsigned slots : {3u,5u,7u}) {
        nr::TemporalCadence pass[8];
        for (unsigned k=0;k<slots;++k) {
            assert(pass[k].next(2));
            pass[k].stagger_interleaved_start(k,slots,2);
        }
        for (unsigned frame=1;frame<121;++frame) {
            const unsigned slot=frame%slots;
            assert(pass[slot].next(2)==(frame%2==0));
            pass[slot].stagger_interleaved_start(slot,slots,2);
        }
    }
    std::puts("Temporal cadence: PASS (N=2..4, interleaved features, retained phase)");
}
