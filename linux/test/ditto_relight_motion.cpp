// Procedural Motion qualification of the spatial/temporal/lighting integration.
// This analytic network is a transport oracle, not a learned-model/game quality test.
#define NR_DITTO_FIXTURE_LIBRARY
#include "dittosr_stage.cpp"
#include <filesystem>
#include <fstream>

struct MotionTest : Test {
    VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};VkPipeline pipeline{};VkDescriptorPool descriptors{};
    std::vector<VkImageView> views;
    MotionTest(const char* shader) {
        VkDescriptorSetLayoutBinding b[2]={{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
                                         {1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};d.bindingCount=2;d.pBindings=b;
        ck(vkCreateDescriptorSetLayout(host.device,&d,nullptr,&dsl));
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,12};
        VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&dsl;l.pushConstantRangeCount=1;l.pPushConstantRanges=&range;
        ck(vkCreatePipelineLayout(host.device,&l,nullptr,&layout));
        std::ifstream f(shader,std::ios::binary|std::ios::ate);assert(f);size_t bytes=size_t(f.tellg());f.seekg(0);
        std::vector<uint32_t> code(bytes/4);f.read(reinterpret_cast<char*>(code.data()),bytes);
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=bytes;mi.pCode=code.data();VkShaderModule module{};
        ck(vkCreateShaderModule(host.device,&mi,nullptr,&module));
        VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pi.layout=layout;
        pi.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
        ck(vkCreateComputePipelines(host.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));vkDestroyShaderModule(host.device,module,nullptr);
        VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2048},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2048}};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.maxSets=2048;pc.poolSizeCount=2;pc.pPoolSizes=sizes;
        ck(vkCreateDescriptorPool(host.device,&pc,nullptr,&descriptors));
    }
    ~MotionTest() {
        vkDeviceWaitIdle(host.device);
        for(auto v:views) vkDestroyImageView(host.device,v,nullptr);
        vkDestroyDescriptorPool(host.device,descriptors,nullptr);vkDestroyPipeline(host.device,pipeline,nullptr);
        vkDestroyPipelineLayout(host.device,layout,nullptr);vkDestroyDescriptorSetLayout(host.device,dsl,nullptr);
    }
    void upload(Image im,const std::vector<float>& values) {
        const bool half=im.format==VK_FORMAT_R16G16B16A16_SFLOAT;
        std::vector<_Float16> h;if(half) {h.resize(values.size());for(size_t i=0;i<h.size();i++) h[i]=_Float16(values[i]);}
        const size_t bytes=values.size()*(half?2:4);
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VkBuffer buf{};ck(vkCreateBuffer(host.device,&bi,nullptr,&buf));VkMemoryRequirements r{};vkGetBufferMemoryRequirements(host.device,buf,&r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=r.size;
        ai.memoryTypeIndex=memory_type(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory mem{};ck(vkAllocateMemory(host.device,&ai,nullptr,&mem));ck(vkBindBufferMemory(host.device,buf,mem,0));
        void* data{};ck(vkMapMemory(host.device,mem,0,bytes,0,&data));memcpy(data,half?static_cast<const void*>(h.data()):values.data(),bytes);vkUnmapMemory(host.device,mem);
        begin();barrier();VkBufferImageCopy c{};c.imageSubresource={im.format==VK_FORMAT_D32_SFLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={im.w,im.h,1};
        vkCmdCopyBufferToImage(cmd,buf,im.im,VK_IMAGE_LAYOUT_GENERAL,1,&c);barrier();end();
        vkDestroyBuffer(host.device,buf,nullptr);vkFreeMemory(host.device,mem,nullptr);
    }
    void network(const nr::ColourFrame& p,const nr::ColourFrame& n,bool identity) {
        VkImageView v[2]{};
        for(unsigned i=0;i<2;i++) {
            VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=i?n.image:p.image;ci.viewType=VK_IMAGE_VIEW_TYPE_2D;
            ci.format=i?n.format:p.format;ci.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};ck(vkCreateImageView(host.device,&ci,nullptr,&v[i]));views.push_back(v[i]);
        }
        VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=descriptors;a.descriptorSetCount=1;a.pSetLayouts=&dsl;VkDescriptorSet set{};
        ck(vkAllocateDescriptorSets(host.device,&a,&set));VkDescriptorImageInfo images[2]={{VK_NULL_HANDLE,v[0],VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,v[1],VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet w[2]{};
        for(unsigned i=0;i<2;i++) {w[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w[i].dstSet=set;w[i].dstBinding=i;w[i].descriptorCount=1;w[i].descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;w[i].pImageInfo=&images[i];}
        vkUpdateDescriptorSets(host.device,2,w,0,nullptr);barrier();
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
        const uint32_t push[3]={p.width,p.height,identity?1u:0u};vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,12,push);
        vkCmdDispatch(cmd,(p.width+7)/8,(p.height+7)/8,1);barrier();
    }
    std::vector<float> motion(const char* shaders,const std::filesystem::path& output,float scale,bool lighting,bool identity,bool inverted,uint32_t mode=4,float ao=.75f,float gi=2.f,bool before_nr=true) {
        const uint32_t W=321,H=181,F=20;
        nr::renodx::StageConfig c{};c.shaders=shaders;c.width=W;c.height=H;c.nr_width=std::max(1u,uint32_t(std::lround(W*scale)));c.nr_height=std::max(1u,uint32_t(std::lround(H*scale)));
        c.hdr_flag=true;c.frame_format=VK_FORMAT_R16G16B16A16_SFLOAT;c.settings.look_upsample=mode;c.settings.resolution_scale=scale;
#ifndef NR_DITTO_BASELINE
        c.settings.ditto_relight=lighting;c.settings.ditto_before_nr=before_nr;c.settings.ditto_ao=ao;c.settings.ditto_gi=gi;
#endif
        begin();Image color=image(W,H,c.frame_format),out=image(W,H,c.frame_format),depth=image(W,H,VK_FORMAT_D32_SFLOAT),mv=image(W,H,VK_FORMAT_R32G32B32A32_SFLOAT);end();
        nr::renodx::Stage stage(host,c);nr::renodx::StageFrame fr{};fr.colour=frame(color);fr.output=frame(out);fr.depth=frame(depth);fr.motion=frame(mv);
        fr.after_upscale=false;fr.jitter_known=true;fr.render_width=W;fr.render_height=H;fr.motion_scale_x=1.f/W;fr.motion_scale_y=1.f/H;fr.depth_inverted=inverted;
        double error=0,edge_error=0;uint64_t count=0,edges=0;float max_alpha=0,effect=0;
        unsigned calls=0;
        std::vector<float> input(W*H*4),z(W*H),motion(W*H*4),expected(W*H*4),answer;
        for(unsigned f=0;f<F;f++) {
            fr.reset=f==0||f==11;fr.jitter_x=f%2?.25f:-.25f;fr.jitter_y=f%3?.125f:-.25f;
            const float angle=.035f*f,cs=std::cos(angle),sn=std::sin(angle),cx=90.f+4.f*f;
            for(unsigned y=0;y<H;y++) for(unsigned x=0;x<W;x++) {
                const size_t i=size_t(y)*W+x,k=4*i;
                const float xx=cs*(x-cx)+sn*(float(y)-H*.5f),yy=-sn*(x-cx)+cs*(float(y)-H*.5f);
                const bool object=std::abs(xx)<4.f&&std::abs(yy)<H*.4f;
                const float texture=.28f+.06f*std::sin((x+f*1.25f)*1.7f)*std::cos(y*1.1f);
                input[k]=object?.06f:texture;input[k+1]=object?.11f:texture*.9f;input[k+2]=object?.65f:texture*.7f;input[k+3]=.375f;
                if(f>=11&&!object) {input[k]*=.7f;input[k+1]*=1.15f;}
                const float gain[3]={object?.85f:1.15f,1.05f,object?1.15f:.95f};
                for(unsigned j=0;j<3;j++) expected[k+j]=input[k+j]*(identity?1.f:gain[j]);expected[k+3]=input[k+3];
                // A sloped background and independently moving foreground, with a camera pan/rotation.
                const float view_z=object?2.f:5.f+float(y)/H*2.f;
                const float forward=100000.f/(100000.f-.1f)-100000.f*.1f/((100000.f-.1f)*view_z);
                z[i]=inverted?1.f-forward:forward;
                motion[k]=object?-4.f:1.25f;motion[k+1]=object?.035f*(float(x)-cx):0.f;motion[k+2]=motion[k+3]=0.f;
            }
            upload(color,input);upload(depth,z);upload(mv,motion);
            begin();stage.record(cmd,fr,[&](uint32_t,const nr::ColourFrame& p,const nr::ColourFrame& n,bool){calls++;network(p,n,identity);});end();
            answer=read(out);
            for(unsigned y=4;y<H-4;y++) for(unsigned x=4;x<W-4;x++) {
                const size_t k=4*(size_t(y)*W+x);
                const float xx=cs*(x-cx)+sn*(float(y)-H*.5f);
                for(unsigned j=0;j<3;j++) {
                    assert(std::isfinite(answer[k+j]));double d=answer[k+j]-expected[k+j];error+=d*d;count++;
                    if(std::abs(xx)<12) {edge_error+=d*d;edges++;}
                    effect=std::max(effect,std::abs(answer[k+j]-input[k+j]));
                }
                max_alpha=std::max(max_alpha,std::abs(answer[k+3]-input[k+3]));
            }
        }
        const double rmse=std::sqrt(error/count),edge_rmse=std::sqrt(edge_error/edges);
        printf("Motion scale=%.2f lighting=%d identity=%d reverse=%d mode=%u RMSE=%.6f edge=%.6f alpha=%g calls=%u effect=%g\n",scale,lighting,identity,inverted,mode,rmse,edge_rmse,max_alpha,calls,effect);fflush(stdout);
        assert(calls==F&&max_alpha<.002f);
#ifndef NR_DITTO_BASELINE
        if(identity&&!lighting) assert(rmse<.003);
        if(!lighting) assert(rmse<.12);
        if(lighting) assert(effect>.01f);
#endif
        std::filesystem::create_directories(output);
        const std::string name=std::to_string(int(scale*100))+"-"+std::to_string(lighting)+"-"+std::to_string(identity)+"-"+std::to_string(inverted)+"-"+std::to_string(mode)+"-"+std::to_string(int(ao*100))+"-"+std::to_string(int(gi*100))+(before_nr?"":"-after-nr");
        std::ofstream report(output/(name+".txt"));report<<rmse<<" "<<edge_rmse<<" "<<max_alpha<<" "<<calls<<"\n";
        std::ofstream raw(output/(name+".rgba32f"),std::ios::binary);raw.write(reinterpret_cast<const char*>(answer.data()),answer.size()*4);
        std::ofstream input_raw(output/(name+"-input.rgba32f"),std::ios::binary);input_raw.write(reinterpret_cast<const char*>(input.data()),input.size()*4);
        std::ofstream expected_raw(output/(name+"-expected.rgba32f"),std::ios::binary);expected_raw.write(reinterpret_cast<const char*>(expected.data()),expected.size()*4);
        return answer;
    }
};
int main(int argc,char** argv) {
    if(argc!=4) {puts("usage: ditto-relight-motion <renodx shaders> <test network.spv> <output directory>");return 2;}
    nr::set_log_sink(log_sink);MotionTest t(argv[2]);
    for(float s:{.25f,.33f,.5f}) {t.motion(argv[1],argv[3],s,false,true,true);t.motion(argv[1],argv[3],s,false,false,true);}
    t.motion(argv[1],argv[3],.33f,false,false,false);
    t.motion(argv[1],argv[3],.33f,false,false,true,1);
#ifndef NR_DITTO_BASELINE
    t.motion(argv[1],argv[3],.33f,true,false,true);
    t.motion(argv[1],argv[3],1.f,true,true,false);
    const auto plain=t.motion(argv[1],argv[3],.33f,true,true,true,4,0.f,0.f);
    const auto ao=t.motion(argv[1],argv[3],.33f,true,true,true,4,1.f,0.f);
    const auto gi=t.motion(argv[1],argv[3],.33f,true,true,true,4,0.f,4.f);
    float ao_delta=0,gi_delta=0;
    for(size_t i=0;i<plain.size();i++) if(i%4!=3) {ao_delta=std::max(ao_delta,std::abs(ao[i]-plain[i]));gi_delta=std::max(gi_delta,std::abs(gi[i]-plain[i]));}
    printf("Isolated lighting: AO delta=%g GI delta=%g\n",ao_delta,gi_delta);
    assert(ao_delta>.0001f&&gi_delta>.0001f);
#endif
    #ifndef NR_DITTO_BASELINE
    t.motion(argv[1],argv[3],.33f,true,false,true,4,.75f,2.f,false);
    #endif
    puts("Motion transport and Relight checks passed (analytic network, not game qualification)");
}
