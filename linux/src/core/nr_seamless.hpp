#pragma once
#include "nr_temporal_cadence.hpp"
#include "nrvk.hpp"
#include <filesystem>

namespace nr {
// One feature, one causal history. No host-visible buffer, private queue or
// completion wait. The owning Runtime already serializes its command buffers.
struct SeamlessHistory {
    nrvk::Context* ctx{};
    nrvk::Context::Image residual[2]{}, guide[2]{}, projected{};
    nrvk::Kernel reproject[2];
    VkSampler sampler{};
    uint32_t current{};
    bool have{};
    TemporalCadence cadence;
    struct Push {
        uint32_t width,height,mode,valid;
        float scale_x,scale_y,depth_tolerance,color_tolerance;
        uint32_t has_depth,reversed_z,fill,visualize,round_u8,age;
    };
    static_assert(sizeof(Push)==56);

    void create(nrvk::Context& c, const std::filesystem::path& shaders,
                nrvk::Context::Image& shown, nrvk::Context::Image& answer,
                nrvk::Context::Image& motion, nrvk::Context::Image& depth) {
        ctx=&c;
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter=si.minFilter=VK_FILTER_NEAREST;
        si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        NRVK_CHECK(vkCreateSampler(c.device,&si,nullptr,&sampler));
        // Construction during recording queues transitions in that same command
        // buffer. It must not submit/wait on a queue containing the game frame.
        const bool deferred=c.defer_transitions; c.defer_transitions=true;
        try {
            for(int k=0;k<2;++k) {
                residual[k]=c.image(shown.w,shown.h,VK_FORMAT_R16G16B16A16_SFLOAT,false);
                guide[k]=c.image(shown.w,shown.h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
                c.transition(residual[k],VK_IMAGE_LAYOUT_GENERAL);c.transition(guide[k],VK_IMAGE_LAYOUT_GENERAL);
            }
            projected=c.image(shown.w,shown.h,VK_FORMAT_R16G16B16A16_SFLOAT,false);
            c.transition(projected,VK_IMAGE_LAYOUT_GENERAL);
            for(int k=0;k<2;++k) {
                auto rs=residual[k],gs=guide[k];rs.sampler=gs.sampler=sampler;
                const auto path=(shaders/"runtime_seamless.spv").string();
                std::vector<nrvk::Context::Image*> images{&shown,&answer,&motion,&depth,&rs,&gs,&projected,&residual[k^1],&guide[k^1]};
                reproject[k].create(c,path,{},sizeof(Push),images);
            }
        } catch(...) {c.defer_transitions=deferred;throw;}
        c.defer_transitions=deferred;
    }
    static void barrier(VkCommandBuffer cmd) {
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);
    }
    void dispatch(VkCommandBuffer cmd,const nrvk::Kernel& k,const Push& p) {
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,k.pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,k.layout,0,1,&k.set,0,nullptr);
        vkCmdPushConstants(cmd,k.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push),&p);
        vkCmdDispatch(cmd,(p.width+7)/8,(p.height+7)/8,1);barrier(cmd);
    }
    void project(VkCommandBuffer cmd,Push p) {
        if(!ctx->pending.empty()) {
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,uint32_t(ctx->pending.size()),ctx->pending.data());
            ctx->pending.clear();
        }
        p.mode=0;dispatch(cmd,reproject[current],p);
    }
    void finish(VkCommandBuffer cmd,Push p,bool full) {
        p.mode=full?1:2;dispatch(cmd,reproject[current],p);
        if(full) {p.mode=3;dispatch(cmd,reproject[current],p);}
        current^=1;have=true;
    }
    ~SeamlessHistory() {
        if(!ctx) return;
        for(auto& k:reproject) if(k.device) k.destroy();
        for(auto& im:residual) ctx->destroy(im);
        for(auto& im:guide) ctx->destroy(im);
        ctx->destroy(projected);
        if(sampler) vkDestroySampler(ctx->device,sampler,nullptr);
    }
};
} // namespace nr
