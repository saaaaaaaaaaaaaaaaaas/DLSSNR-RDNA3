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
    assert(c.values.reconstruction_light==1.25f && c.values.reconstruction_detail==1.25f && c.values.ditto_sharpness==1.f);
    const char* adaptive_names[]={"NRReconstructionLight","NRReconstructionDetail","NRDittoSharpness"};
    float* adaptive_fields[]={&c.values.reconstruction_light,&c.values.reconstruction_detail,&c.values.ditto_sharpness};
    for (int k=0;k<3;k++) {
        bool found=false;
        for (int i=0;i<renodx_keys::kCount;i++) if (!std::strcmp(renodx_keys::kKeys[i].name,adaptive_names[k])) {
            assert(pe::renodx_field(c,i)==adaptive_fields[k]);found=true;
        }
        assert(found);
    }
    assert(pe::parse_renodx_key(c,"nrreconstructionlight","1.50"));
    assert(pe::parse_renodx_key(c,"nrreconstructiondetail","1.75"));
    assert(pe::parse_renodx_key(c,"nrdittosharpness","0.25"));
    FILE* f=std::fopen("renodx-range-roundtrip.ini","wb");assert(f);pe::write_renodx(f,c);std::fclose(f);
    pe::RenoDXConfig saved;assert(pe::read_renodx("renodx-range-roundtrip.ini",saved));
    assert(saved.values.intensity==4 && saved.values.local_tone==4 && saved.values.local_structure==4 && saved.values.skin_structure==4);
    assert(saved.values.reconstruction_light==1.5f && saved.values.reconstruction_detail==1.75f && saved.values.ditto_sharpness==.25f);
    for (int k=0;k<4;k++) assert(pe::parse_renodx_key(c,keys[k],"4.01"));
    assert(c.values.intensity==1 && c.values.local_tone==1 && c.values.local_structure==1 && c.values.skin_structure==-1);
    assert(pe::parse_renodx_key(c,"nrskinstructure","-1"));assert(c.values.skin_structure==-1);
    std::remove("renodx-range-roundtrip.ini");
    std::puts("RenoDX Network metadata, parser and INI round trip: PASS");
}
