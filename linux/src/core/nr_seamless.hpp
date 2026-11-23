#pragma once
#include "nr_temporal_cadence.hpp"
#include "nrvk.hpp"
#include <filesystem>
#include <array>

namespace nr {
// One feature, one causal history. No host-visible buffer, private queue or
// completion wait. The owning Runtime already serializes its command buffers.
struct SeamlessHistory {
    nrvk::Context* ctx{};
    nrvk::Context::Image residual[2]{}, guide[2]{}, projected{};
    nrvk::Context::Image tone[2]{}, target[2]{}, statistics[2]{};
    nrvk::Context::Image projected_tone{}, projected_target{}, projected_statistics{}, fresh_gain{};
    nrvk::Kernel reproject[2];
    VkSampler sampler{};
    uint32_t current{};
    bool have{};
    uint32_t history_samples{};
    std::array<float,7> settings{};
    TemporalCadence cadence;
    struct Push {
        uint32_t width,height,mode,valid;
        float scale_x,scale_y,depth_tolerance,color_tolerance;
        uint32_t has_depth,reversed_z,fill,visualize,round_u8,age;
        uint32_t samples{};
    };
    static_assert(sizeof(Push)==60);

    static void check_shader(const std::filesystem::path& path) {
        const auto code=nrvk::read_spirv(path.string());bool fields=false,samples=false;
        for(size_t i=5;i<code.size();) {
            const uint32_t n=code[i]>>16,op=code[i]&0xffffu;
            if(!n||i+n>code.size()) break;
            if(op==71&&n==4&&code[i+2]==33&&code[i+3]==18) fields=true;
            if(op==72&&n==5&&code[i+3]==35&&code[i+4]==56) samples=true;
            i+=n;
        }
        if(!fields||!samples) throw std::runtime_error("Residual shader ABI mismatch: replace the runtime and every runtime_seamless.spv together");
    }
    void create(nrvk::Context& c, const std::filesystem::path& shaders,
                nrvk::Context::Image& shown, nrvk::Context::Image& answer,
                nrvk::Context::Image& motion, nrvk::Context::Image& depth) {
        check_shader(shaders/"runtime_seamless.spv");
        ctx=&c;
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter=si.minFilter=VK_FILTER_NEAREST;
        si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        // Construction during recording queues transitions in that same command
        // buffer. It must not submit/wait on a queue containing the game frame.
        const bool deferred=c.defer_transitions; c.defer_transitions=true;
        const size_t pending=c.pending.size();
        try {
            NRVK_CHECK(vkCreateSampler(c.device,&si,nullptr,&sampler));
            for(int k=0;k<2;++k) {
                // Both usages are required: storage when written, sampled by
                // the next frame. Keep the owning view sampler-free so writes
                // bind as storage; the read aliases use the shared sampler.
                residual[k]=c.image(shown.w,shown.h,VK_FORMAT_R16G16B16A16_SFLOAT,true,false);
                guide[k]=c.image(shown.w,shown.h,VK_FORMAT_R32G32B32A32_SFLOAT,true,false);
                for(auto* field : {&tone[k],&target[k],&statistics[k]}) {
                    *field=c.image(shown.w,shown.h,VK_FORMAT_R16G16B16A16_SFLOAT,true,false);
                    vkDestroySampler(c.device,field->sampler,nullptr);field->sampler=VK_NULL_HANDLE;
                    c.transition(*field,VK_IMAGE_LAYOUT_GENERAL);
                }
                vkDestroySampler(c.device,residual[k].sampler,nullptr);residual[k].sampler=VK_NULL_HANDLE;
                vkDestroySampler(c.device,guide[k].sampler,nullptr);guide[k].sampler=VK_NULL_HANDLE;
                c.transition(residual[k],VK_IMAGE_LAYOUT_GENERAL);c.transition(guide[k],VK_IMAGE_LAYOUT_GENERAL);
            }
            projected=c.image(shown.w,shown.h,VK_FORMAT_R16G16B16A16_SFLOAT,false);
            c.transition(projected,VK_IMAGE_LAYOUT_GENERAL);
            for(auto* field : {&projected_tone,&projected_target,&projected_statistics,&fresh_gain}) {
                *field=c.image(shown.w,shown.h,VK_FORMAT_R16G16B16A16_SFLOAT,false);
                c.transition(*field,VK_IMAGE_LAYOUT_GENERAL);
            }
            for(int k=0;k<2;++k) {
                auto rs=residual[k],gs=guide[k],ts=tone[k],as=target[k],ss=statistics[k];
                rs.sampler=gs.sampler=ts.sampler=as.sampler=ss.sampler=sampler;
                const auto path=(shaders/"runtime_seamless.spv").string();
                std::vector<nrvk::Context::Image*> images{&shown,&answer,&motion,&depth,&rs,&gs,&projected,&residual[k^1],&guide[k^1],
                    &ts,&as,&ss,&projected_tone,&projected_target,&projected_statistics,
                    &tone[k^1],&target[k^1],&statistics[k^1],&fresh_gain};
                reproject[k].create(c,path,{},sizeof(Push),images);
            }
        } catch(...) {
            c.defer_transitions=deferred;
            c.pending.resize(pending);
            release(); // a later real frame can retry; never keep partial sets
            throw;
        }
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
        p.samples=p.valid?history_samples:0;p.mode=0;dispatch(cmd,reproject[current],p);
    }
    void finish(VkCommandBuffer cmd,Push p,bool full) {
        p.samples=p.valid?history_samples:0;
        if(full) {p.mode=4;dispatch(cmd,reproject[current],p);} // fresh log-gain, computed once per model pixel
        p.mode=full?1:2;dispatch(cmd,reproject[current],p);
        p.mode=3;dispatch(cmd,reproject[current],p); // identical composition on both cadence phases
        history_samples=p.valid?std::min(history_samples+1,3u):1u;
        current^=1;have=true;
    }
    void release() {
        if(!ctx) return;
        for(auto& k:reproject) if(k.device) k.destroy();
        for(auto& im:residual) ctx->destroy(im);
        for(auto& im:guide) ctx->destroy(im);
        ctx->destroy(projected);
        for(auto* fields : {&tone,&target,&statistics}) for(auto& im:*fields) ctx->destroy(im);
        for(auto* im : {&projected_tone,&projected_target,&projected_statistics,&fresh_gain}) ctx->destroy(*im);
        if(sampler) vkDestroySampler(ctx->device,sampler,nullptr);
        sampler=VK_NULL_HANDLE;ctx=nullptr;have=false;history_samples=0;
    }
    ~SeamlessHistory() { release(); }
};
} // namespace nr
