// Exercise the actual RenoDX UI metadata, parser and INI round trip under Wine.
#include "nr_pe_config.hpp"
#include "nr_renodx_keys.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace nr::pe { void log(const char*, ...) {} }

int main() {
    using namespace nr;
    pe::RenoDXConfig c;
    const char* names[]={"NRIntensity","NRLocalTone","NRLocalStructure","NRSkinStructure"};
    const char* keys[]={"nrintensity","nrlocaltone","nrlocalstructure","nrskinstructure"};
    for (int k=0;k<4;k++) {
        bool found=false;
        for (const auto& entry : renodx_keys::kKeys) if (!std::strcmp(entry.name,names[k])) {
            assert(entry.hi==4.f);assert(entry.lo==(k==3?-1.f:0.f));found=true;
        }
        assert(found);assert(pe::parse_renodx_key(c,keys[k],"4.00"));
    }
    assert(c.values.intensity==4 && c.values.local_tone==4 && c.values.local_structure==4 && c.values.skin_structure==4);
    FILE* f=std::fopen("renodx-range-roundtrip.ini","wb");assert(f);pe::write_renodx(f,c);std::fclose(f);
    pe::RenoDXConfig saved;assert(pe::read_renodx("renodx-range-roundtrip.ini",saved));
    assert(saved.values.intensity==4 && saved.values.local_tone==4 && saved.values.local_structure==4 && saved.values.skin_structure==4);
    for (int k=0;k<4;k++) assert(pe::parse_renodx_key(c,keys[k],"4.01"));
    assert(c.values.intensity==1 && c.values.local_tone==1 && c.values.local_structure==1 && c.values.skin_structure==-1);
    assert(pe::parse_renodx_key(c,"nrskinstructure","-1"));assert(c.values.skin_structure==-1);
    std::remove("renodx-range-roundtrip.ini");
    std::puts("RenoDX Network metadata, parser and INI round trip: PASS");
}
