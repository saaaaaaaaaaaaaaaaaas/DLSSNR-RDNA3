// Integration contract test: a deterministic identity network, real codecs and Ditto shaders.
// Software Vulkan verifies wiring and lifetime only; this is not an NR image-quality or GPU performance test.
#include "nr_renodx.hpp"
#include "nr_dittosr_phase.hpp"
#include "nr_log.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

static void ck(VkResult r) { if (r) throw std::runtime_error("Vulkan failure " + std::to_string(r)); }
static bool ditto_ran = false;
static void log_sink(const char* s) { puts(s); ditto_ran |= std::strstr(s, "[nr] Ditto SR: neural") != nullptr; }
struct Test {
    nr::HostDevice host;
    VkCommandPool pool{};
    VkCommandBuffer cmd{};
    VkPhysicalDeviceMemoryProperties mem{};
    struct Image { VkImage im{}; VkDeviceMemory mem{}; VkFormat format; uint32_t w,h; };
    std::vector<Image> images;
    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want) {
        for (uint32_t i=0;i<mem.memoryTypeCount;++i)
            if ((bits&(1u<<i)) && (mem.memoryTypes[i].propertyFlags&want)==want) return i;
        throw std::runtime_error("No memory type");
    }
    Test() {
        VkApplicationInfo a{VK_STRUCTURE_TYPE_APPLICATION_INFO}; a.apiVersion=VK_API_VERSION_1_3;
        VkInstanceCreateInfo i{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; i.pApplicationInfo=&a;
        ck(vkCreateInstance(&i,nullptr,&host.instance));
        uint32_t n=0; ck(vkEnumeratePhysicalDevices(host.instance,&n,nullptr));
        assert(n); std::vector<VkPhysicalDevice> devices(n);
        ck(vkEnumeratePhysicalDevices(host.instance,&n,devices.data())); host.physical=devices[0];
        VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(host.physical,&props);
        printf("Device: %s (software execution is not target GPU validation)\n",props.deviceName);
        uint32_t nq=0; vkGetPhysicalDeviceQueueFamilyProperties(host.physical,&nq,nullptr);
        std::vector<VkQueueFamilyProperties> q(nq); vkGetPhysicalDeviceQueueFamilyProperties(host.physical,&nq,q.data());
        for (uint32_t k=0;k<nq;++k) if ((q[k].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)) { host.queue_family=k; break; }
        const float priority=1.f;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex=host.queue_family; qi.queueCount=1; qi.pQueuePriorities=&priority;
        VkPhysicalDeviceFeatures features{}; vkGetPhysicalDeviceFeatures(host.physical,&features);
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; d.queueCreateInfoCount=1; d.pQueueCreateInfos=&qi; d.pEnabledFeatures=&features;
        ck(vkCreateDevice(host.physical,&d,nullptr,&host.device)); vkGetDeviceQueue(host.device,host.queue_family,0,&host.queue);
        vkGetPhysicalDeviceMemoryProperties(host.physical,&mem);
        VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; p.queueFamilyIndex=host.queue_family; p.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ck(vkCreateCommandPool(host.device,&p,nullptr,&pool));
        VkCommandBufferAllocateInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; b.commandPool=pool; b.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; b.commandBufferCount=1;
        ck(vkAllocateCommandBuffers(host.device,&b,&cmd));
    }
    ~Test() {
        vkDeviceWaitIdle(host.device);
        for (auto im:images) { vkDestroyImage(host.device,im.im,nullptr); vkFreeMemory(host.device,im.mem,nullptr); }
        vkDestroyCommandPool(host.device,pool,nullptr); vkDestroyDevice(host.device,nullptr); vkDestroyInstance(host.instance,nullptr);
    }
    void begin() { ck(vkResetCommandBuffer(cmd,0)); VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; ck(vkBeginCommandBuffer(cmd,&b)); }
    void end() { ck(vkEndCommandBuffer(cmd)); VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO}; s.commandBufferCount=1; s.pCommandBuffers=&cmd; ck(vkQueueSubmit(host.queue,1,&s,VK_NULL_HANDLE)); ck(vkQueueWaitIdle(host.queue)); }
    void barrier() {
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT; b.dstAccessMask=b.srcAccessMask;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);
    }
    Image image(uint32_t w,uint32_t h,VkFormat f) {
        Image out{}; out.w=w;out.h=h;out.format=f;
        VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; i.imageType=VK_IMAGE_TYPE_2D;i.format=f;i.extent={w,h,1};i.mipLevels=i.arrayLayers=1;
        i.samples=VK_SAMPLE_COUNT_1_BIT;i.tiling=VK_IMAGE_TILING_OPTIMAL;
        i.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if(f!=VK_FORMAT_D32_SFLOAT && f!=VK_FORMAT_R8G8B8A8_SRGB) i.usage|=VK_IMAGE_USAGE_STORAGE_BIT;
        ck(vkCreateImage(host.device,&i,nullptr,&out.im));
        VkMemoryRequirements r{}; vkGetImageMemoryRequirements(host.device,out.im,&r);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=memory_type(r.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ck(vkAllocateMemory(host.device,&a,nullptr,&out.mem));ck(vkBindImageMemory(host.device,out.im,out.mem,0));images.push_back(out);
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=out.im;
        b.subresourceRange={f==VK_FORMAT_D32_SFLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);return out;
    }
    void clear(Image im,float r,float g,float b) {
        VkImageSubresourceRange range{im.format==VK_FORMAT_D32_SFLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        if(im.format==VK_FORMAT_D32_SFLOAT) { VkClearDepthStencilValue c{r,0};vkCmdClearDepthStencilImage(cmd,im.im,VK_IMAGE_LAYOUT_GENERAL,&c,1,&range); }
        else { VkClearColorValue c{{r,g,b,1.f}};vkCmdClearColorImage(cmd,im.im,VK_IMAGE_LAYOUT_GENERAL,&c,1,&range); }
    }
    nr::ColourFrame frame(Image i) {
        nr::ColourFrame f{};f.image=i.im;f.format=i.format;f.width=i.w;f.height=i.h;
        f.before=f.after=VK_IMAGE_LAYOUT_GENERAL; f.before_stage=f.after_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        f.before_access=f.after_access=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        f.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if(i.format!=VK_FORMAT_D32_SFLOAT && i.format!=VK_FORMAT_R8G8B8A8_SRGB) f.usage|=VK_IMAGE_USAGE_STORAGE_BIT;
        return f;
    }
    std::vector<float> read(Image im) {
        const bool half=im.format==VK_FORMAT_R16G16B16A16_SFLOAT;
        const uint32_t bytes=im.w*im.h*(half?8:4);
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkBuffer buf;ck(vkCreateBuffer(host.device,&bi,nullptr,&buf));VkMemoryRequirements r;vkGetBufferMemoryRequirements(host.device,buf,&r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=r.size;ai.memoryTypeIndex=memory_type(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory m;ck(vkAllocateMemory(host.device,&ai,nullptr,&m));ck(vkBindBufferMemory(host.device,buf,m,0));
        begin();barrier();VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={im.w,im.h,1};
        vkCmdCopyImageToBuffer(cmd,im.im,VK_IMAGE_LAYOUT_GENERAL,buf,1,&c);barrier();end();
        void* data;ck(vkMapMemory(host.device,m,0,bytes,0,&data));std::vector<float> out(im.w*im.h*4);
        for(size_t k=0;k<out.size();++k) {
            if(half) { uint16_t v;memcpy(&v,static_cast<char*>(data)+2*k,2); _Float16 h;memcpy(&h,&v,2);out[k]=float(h); }
            else out[k]=static_cast<unsigned char*>(data)[k]/255.f;
        }
        vkUnmapMemory(host.device,m);vkDestroyBuffer(host.device,buf,nullptr);vkFreeMemory(host.device,m,nullptr);return out;
    }
    void upload_checker(Image im) {
        std::vector<_Float16> values(im.w*im.h*4);
        for(uint32_t y=0;y<im.h;++y) for(uint32_t x=0;x<im.w;++x) {
            const size_t k=(size_t(y)*im.w+x)*4;
            const float v=(x+y)%2?.75f:.25f;
            values[k]=values[k+1]=values[k+2]=_Float16(v); values[k+3]=_Float16(1.f);
        }
        const size_t bytes=values.size()*sizeof(_Float16);
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VkBuffer buf;ck(vkCreateBuffer(host.device,&bi,nullptr,&buf));
        VkMemoryRequirements r;vkGetBufferMemoryRequirements(host.device,buf,&r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=r.size;
        ai.memoryTypeIndex=memory_type(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory memory;ck(vkAllocateMemory(host.device,&ai,nullptr,&memory));ck(vkBindBufferMemory(host.device,buf,memory,0));
        void* mapped;ck(vkMapMemory(host.device,memory,0,bytes,0,&mapped));memcpy(mapped,values.data(),bytes);vkUnmapMemory(host.device,memory);
        begin();barrier();VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={im.w,im.h,1};
        vkCmdCopyBufferToImage(cmd,buf,im.im,VK_IMAGE_LAYOUT_GENERAL,1,&copy);barrier();end();
        vkDestroyBuffer(host.device,buf,nullptr);vkFreeMemory(host.device,memory,nullptr);
    }
    void composition_detail(const char* shaders) {
        const uint32_t w=96,h=64;
        nr::renodx::StageConfig c{};c.shaders=shaders;c.width=w;c.height=h;c.nr_width=w/2;c.nr_height=h/2;
        c.hdr_flag=true;c.frame_format=VK_FORMAT_R16G16B16A16_SFLOAT;
        c.settings.resolution_scale=.5f;c.settings.look_upsample=4;
        begin();auto color=image(w,h,c.frame_format),out=image(w,h,c.frame_format);
        auto depth=image(w,h,VK_FORMAT_D32_SFLOAT),mv=image(w,h,VK_FORMAT_R32G32_SFLOAT);
        clear(depth,.5f,0,0);clear(mv,0,0,0);clear(out,0,0,0);barrier();end();upload_checker(color);
        nr::renodx::Stage stage(host,c);nr::renodx::StageFrame fr{};
        fr.colour=frame(color);fr.output=frame(out);fr.depth=frame(depth);fr.motion=frame(mv);
        fr.after_upscale=false;fr.jitter_known=true;fr.render_width=w;fr.render_height=h;
        fr.motion_scale_x=1.f/w;fr.motion_scale_y=1.f/h;fr.depth_inverted=true;fr.reset=true;
        begin();stage.record(cmd,fr,[&](uint32_t,const nr::ColourFrame& proxy,const nr::ColourFrame& neural,bool) {
            barrier();VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            copy.extent={proxy.width,proxy.height,1};vkCmdCopyImage(cmd,proxy.image,VK_IMAGE_LAYOUT_GENERAL,neural.image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);barrier();
        });end();
        auto result=read(out),reference=read(color);float error=0.f,light=0.f,dark=0.f;
        for(uint32_t y=4;y<h-4;++y) for(uint32_t x=4;x<w-4;++x) {
            const size_t k=(size_t(y)*w+x)*4;error=std::max(error,std::abs(result[k]-reference[k]));
            if((x+y)%2) light+=result[k];else dark+=result[k];
        }
        const float contrast=(light-dark)/float((w-8)*(h-8)/2);
        printf("Full-reference composition: max-error=%g contrast=%g (identity network)\n",error,contrast);fflush(stdout);
        assert(error<.02f && contrast>.4f);
    }
    void composition_controls(const char* shaders) {
        const uint32_t w=96,h=64;
        nr::renodx::StageConfig c{};c.shaders=shaders;c.width=w;c.height=h;c.nr_width=w/2;c.nr_height=h/2;
        c.hdr_flag=true;c.frame_format=VK_FORMAT_R16G16B16A16_SFLOAT;c.settings.resolution_scale=.5f;c.settings.look_upsample=4;
        begin();auto color=image(w,h,c.frame_format),out=image(w,h,c.frame_format);
        auto depth=image(w,h,VK_FORMAT_D32_SFLOAT),mv=image(w,h,VK_FORMAT_R32G32_SFLOAT);
        clear(color,.25f,.5f,.75f);clear(depth,.5f,0,0);clear(mv,.5f,-.25f,0);clear(out,0,0,0);barrier();end();
        nr::renodx::StageFrame fr{};fr.colour=frame(color);fr.output=frame(out);fr.depth=frame(depth);fr.motion=frame(mv);
        fr.after_upscale=false;fr.jitter_known=true;fr.render_width=w;fr.render_height=h;
        fr.motion_scale_x=1.f/w;fr.motion_scale_y=1.f/h;fr.depth_inverted=true;
        unsigned calls=0;
        auto network=[&](uint32_t pass,const nr::ColourFrame&,const nr::ColourFrame& neural,bool) {
            ++calls;barrier();const VkClearColorValue v=pass==0?VkClearColorValue{{.7f,.25f,.4f,1.f}}:VkClearColorValue{{.2f,.6f,.8f,1.f}};
            const VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            vkCmdClearColorImage(cmd,neural.image,VK_IMAGE_LAYOUT_GENERAL,&v,1,&r);barrier();
        };
        nr::renodx::Stage one(host,c);
        fr.reset=true;begin();one.record(cmd,fr,network);end();auto full=read(out),original=read(color);
        c.settings.color_strength=.5f;one.set(c.settings);begin();one.record(cmd,fr,network);end();auto half=read(out);
        float blend_error=0.f,effect=0.f;
        for(size_t k=0;k<full.size();++k) if(k%4!=3) {
            blend_error=std::max(blend_error,std::abs(half[k]-(full[k]+original[k])*.5f));
            effect=std::max(effect,std::abs(full[k]-original[k]));
        }
        printf("Composition strength: half-blend-error=%g full-effect=%g\n",blend_error,effect);fflush(stdout);
        assert(effect>.05f && blend_error<.004f);
        c.settings.color_strength=0.f;one.set(c.settings);calls=0;begin();one.record(cmd,fr,network);end();
        assert(calls==0);auto zero=read(out);for(size_t k=0;k<zero.size();++k) assert(zero[k]==original[k]);
        c.settings.color_strength=1.f;c.settings.passes=2;
        nr::renodx::Stage two(host,c);calls=0;begin();two.record(cmd,fr,network);end();auto first=read(out);
        fr.reset=false;
        for(unsigned k=0;k<6;++k) { begin();two.record(cmd,fr,network);end(); }
        auto settled=read(out);float drift=0.f;
        for(size_t k=0;k<first.size();++k) drift=std::max(drift,std::abs(first[k]-settled[k]));
        printf("Independent pass histories: drift=%g, network-calls=%u\n",drift,calls);fflush(stdout);
        assert(calls==14 && drift<.004f);
    }
    void test(const char* shaders,bool hdr,uint32_t w,uint32_t h,float scale,uint32_t passes,bool prep, bool bypass_last = false, bool srgb = false) {
        nr::renodx::StageConfig c{};c.shaders=shaders;c.width=w;c.height=h;c.hdr_flag=hdr;
        c.settings.resolution_scale=scale;c.settings.look_upsample=4;c.settings.passes=passes;
        if(prep) { c.settings.look_mode=true;c.settings.look_stabilize=2;c.settings.detail_stability=2; }
        if(bypass_last) c.settings.pass_transfer[passes-2]=c.settings.pass_color[passes-2]=0.f;
        nr::renodx::working_resolution(c.settings,w,h,w,h,&c.nr_width,&c.nr_height);
        c.frame_format=hdr?VK_FORMAT_R16G16B16A16_SFLOAT:srgb?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;
        begin();auto color=image(w,h,c.frame_format), output=image(w,h,c.frame_format);
        auto depth=image(w+8,h+8,VK_FORMAT_D32_SFLOAT), mv=image(w+16,h+16,VK_FORMAT_R32G32_SFLOAT);
        clear(color,.25f,.5f,.75f);clear(output,0,0,0);clear(depth,.5f,0,0);clear(mv,0,0,0);barrier();end();
        nr::renodx::Stage stage(host,c);
        nr::renodx::StageFrame fr{};fr.colour=frame(color);fr.output=frame(output);fr.after_upscale=false;
        fr.depth=frame(depth);fr.depth_x=3;fr.depth_y=4;fr.depth.width=w;fr.depth.height=h;fr.depth_inverted=true;
        fr.motion=frame(mv);fr.motion_x=5;fr.motion_y=6;fr.motion.width=w;fr.motion.height=h;
        fr.motion_scale_x=1.f/w;fr.motion_scale_y=1.f/h;
        fr.render_width=w;fr.render_height=h;fr.jitter_known=true;
        if(prep) { fr.preprocess.enabled=true;fr.preprocess.curve=1;fr.preprocess.inverse=true; }
        auto identity=[&](uint32_t,const nr::ColourFrame& p,const nr::ColourFrame& n,bool) {
            assert(p.width==c.nr_width && p.height==c.nr_height);barrier();
            VkImageCopy r{};r.srcSubresource=r.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};r.extent={p.width,p.height,1};
            vkCmdCopyImage(cmd,p.image,VK_IMAGE_LAYOUT_GENERAL,n.image,VK_IMAGE_LAYOUT_GENERAL,1,&r);barrier();
        };
        ditto_ran=false;
        for(unsigned k=0;k<(prep?72u:6u);++k) { fr.reset=k==0||k==3;fr.jitter_x=k%2?.3f:-.3f;fr.jitter_y=k%2?-.2f:.2f;
            begin();if(k==3) { barrier();clear(color,.75f,.25f,.5f);barrier(); }
            stage.record(cmd,fr,identity);end(); }
        assert(ditto_ran==(scale<1.f));
        auto out=read(output);auto target=read(color);float max_error=0;
        for(size_t k=0;k<out.size();++k) { assert(std::isfinite(out[k])); max_error=std::max(max_error,std::abs(out[k]-target[k%4])); }
        printf("%s %ux%u NR %ux%u passes=%u prep=%d max-error=%g\n",hdr?"HDR":"SDR",w,h,c.nr_width,c.nr_height,passes,prep,max_error);
        assert(max_error<.02f);
        c.settings.look_upsample=2;stage.set(c.settings);begin();stage.record(cmd,fr,identity);end();
        c.settings.look_upsample=4;stage.set(c.settings);begin();stage.record(cmd,fr,identity);end();
    }
};
int main(int argc,char** argv) {
    if(argc!=2) { puts("usage: dittosr-stage <renodx shader directory>");return 2; }
    float previous[2]={};DittoConstants k{};
    auto f=nr::ditto::neural_frame(422,238,1280,720,1280,720,.3f,-.2f,false,true);
    ditto_fill_constants(k,DittoSettings{},f,true,false,false,false,previous);
    assert(std::abs(k.jit[0]-.3f*422/1280)<1e-8f && std::abs(k.jit[1]+.2f*238/720)<1e-8f);
    assert(k.hdr==0.f && k.preExposure==1.f && k.mvScale[0]==1.f && k.mvScale[1]==1.f);
    f=nr::ditto::neural_frame(422,238,1280,720,1280,720,-.3f,.2f,true,true);
    ditto_fill_constants(k,DittoSettings{},f,false,false,false,false,previous);
    assert(std::abs(k.jitCancel[0]-.6f/1280)<1e-9f && std::abs(k.jitCancel[1]+.4f/720)<1e-9f);
    auto phase=nr::ditto::output_phase(.3f,-.2f,1280,720,1280,720);
    assert(std::abs(phase[0]-.3f)<1e-8f && std::abs(phase[1]+.2f)<1e-8f);
    nr::renodx::Settings settings;settings.resolution_scale=.33f;uint32_t w,h;
    nr::renodx::working_resolution(settings,1280,720,1280,720,&w,&h);assert(w==422 && h==238);
    nr::set_log_sink(log_sink);Test t;
    t.test(argv[1],false,96,64,.33f,1,false);
    t.test(argv[1],true,96,64,.33f,1,false);
    t.test(argv[1],true,99,65,.5f,2,true);
    t.test(argv[1],false,99,65,.5f,2,false);
    t.test(argv[1],true,99,65,.5f,2,false,true);
    t.test(argv[1],true,96,64,1.f,1,false);
    t.test(argv[1],false,96,64,.33f,1,false,false,true);
    t.composition_detail(argv[1]);
    t.composition_controls(argv[1]);
    puts("Ditto SR integration contracts passed");
}
