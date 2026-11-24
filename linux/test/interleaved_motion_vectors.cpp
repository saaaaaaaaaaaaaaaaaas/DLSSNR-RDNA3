// Validate the real Interleaved motion-ring shader at every supported slot count.
#define NR_DITTO_FIXTURE_LIBRARY
#include "dittosr_stage.cpp"
#include "nrvk.hpp"
#include <cmath>
#include <limits>

int main(int argc,char** argv) try {
    if(argc!=2) throw std::runtime_error("usage: interleaved-motion-vectors <renodx_il_mv.spv>");
    Test device;
    nrvk::Context c;c.instance=device.host.instance;c.physical=device.host.physical;c.device=device.host.device;
    c.queue=device.host.queue;c.family=device.host.queue_family;c.mem=device.mem;c.adopted=true;
    constexpr unsigned W=96,H=64;
    auto mv=c.image(W,H,VK_FORMAT_R32G32_SFLOAT,true,true,true);
    nrvk::Context::Image ring[8],writes[8];
    for(unsigned i=0;i<8;++i) {
        ring[i]=c.image(W,H,VK_FORMAT_R32G32_SFLOAT,true,true,true);
        c.transition(ring[i],VK_IMAGE_LAYOUT_GENERAL);
        writes[i]=ring[i];writes[i].sampler=VK_NULL_HANDLE;
    }
    auto comp=c.image(W,H,VK_FORMAT_R16G16_SFLOAT,false,true,true);
    std::vector<nrvk::Context::Image*> bindings{&mv,&ring[0],&ring[1],&ring[2],&writes[0],&writes[1],&writes[2],&comp,
        &ring[3],&ring[4],&ring[5],&ring[6],&ring[7],&writes[3],&writes[4],&writes[5],&writes[6],&writes[7]};
    nrvk::Kernel kernel;kernel.create(c,argv[1],{},48,bindings);
    struct Push {uint32_t size[2],base[2],grid[2];float scale[2];uint32_t mode,cur,age,ring;} p{{W,H},{0,0},{W,H},{1,1},0,0,0,0};
    auto dispatch=[&](VkCommandBuffer cmd){
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,kernel.pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,kernel.layout,0,1,&kernel.set,0,nullptr);
        vkCmdPushConstants(cmd,kernel.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof p,&p);
        vkCmdDispatch(cmd,(W+15)/16,(H+15)/16,1);
    };
    std::vector<float> motion(W*H*2);
    for(unsigned slots=2;slots<=8;++slots) {
        p.ring=slots;
        for(unsigned f=0;f<slots;++f) {
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
                size_t k=(y*W+x)*2;
                motion[k]=.2f+float(f)*.07f + .002f*float(x);
                motion[k+1]=-.08f+float(f)*.015f;
            }
            c.upload(mv,motion.data(),motion.size()*4);p.mode=0;p.cur=f;p.age=0;
            c.one_shot(dispatch);
        }
        p.mode=1;p.cur=slots-1;p.age=slots;
        c.one_shot(dispatch);
        std::vector<_Float16> output(W*H*2);c.download(comp,output.data(),output.size()*2);
        for(unsigned y=8;y<H-8;++y)for(unsigned x=8;x<W-16;++x) {
            float px=x+.5f,py=y+.5f;
            for(int f=int(slots)-1;f>=0;--f) {px+=.2f+float(f)*.07f+.002f*(px-.5f);py+=-.08f+float(f)*.015f;}
            size_t k=(y*W+x)*2;
            assert(std::abs(float(output[k])-(px-(x+.5f))/W)<.00005f);
            assert(std::abs(float(output[k+1])-(py-(y+.5f))/H)<.00005f);
        }
        printf("%u slots: composed vector PASS\n",slots);
    }
    std::fill(motion.begin(),motion.end(),0.f);
    const size_t bad=(20*W+20)*2, edge=(20*W+W-2)*2;
    motion[bad]=std::numeric_limits<float>::quiet_NaN();motion[edge]=10.f;
    c.upload(mv,motion.data(),motion.size()*4);
    p.mode=0;p.cur=0;p.age=0;p.ring=8;c.one_shot(dispatch);
    p.mode=1;p.age=2;c.one_shot(dispatch);
    std::vector<_Float16> bad_output(W*H*2);c.download(comp,bad_output.data(),bad_output.size()*2);
    assert(std::isnan(float(bad_output[bad]))&&std::isnan(float(bad_output[edge])));
    puts("Invalid and out-of-bounds trajectories: rejected");
    kernel.destroy();c.destroy(comp);for(auto& im:ring)c.destroy(im);c.destroy(mv);c.destroy();
    return 0;
} catch(const std::exception& e) {fprintf(stderr,"%s\n",e.what());return 1;}
