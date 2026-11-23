// Included inside Stage::Impl. Relight sees the full-size reference in the codec's
// existing color domain, before NR encoding or after the complete NR stack.
// It never relights encoded neural RGB.
void retire_relight() {
    for (Image* im : {&relight_dummy, &relight_nd, &relight_half_nd, &relight_ao[0], &relight_ao[1],
                     &relight_hist[0], &relight_hist[1], &relight_hd[0], &relight_hd[1],
                     &relight_mip[0], &relight_mip[1], &relight_mip[2], &relight_mip[3],
                     &relight_lut, &relight_curve, &relight_color}) retire(*im);
    relight_valid = relight_tables = false;
}
void ensure_relight(VkCommandBuffer cmd) {
    if (!programs[kAoCompose].pipeline) {
        for (auto [p, name] : {std::pair<P,const char*>{kAoPrep,"aoprep"}, {kAoMip,"aomipall"},
                              {kAoMain,"aomain"}, {kAoFilter,"aofilter"}, {kAoTemporal,"aotemporal"},
                              {kAoCacao,"aocacao"}, {kAoLut,"aolut"}, {kAoCurve,"aocurve"},
                              {kAoCompose,"aocompose"}})
            if (!programs[p].pipeline) load(programs[p], std::string("ditto_") + name, 0);
    }
    if (relight_nd.handle) return;
    const uint32_t W=cfg.width,H=cfg.height,hw=(W+1)/2,hh=(H+1)/2;
    const auto rw=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    relight_dummy=make_image(1,1,VK_FORMAT_R32G32_UINT,rw);
    relight_nd=make_image(W,H,VK_FORMAT_R32G32B32A32_SFLOAT,rw);
    relight_half_nd=make_image(hw,hh,VK_FORMAT_R32G32B32A32_SFLOAT,rw);
    relight_color=make_image(W,H,VK_FORMAT_R16G16B16A16_SFLOAT,rw);
    relight_lut=make_image(1024,32,VK_FORMAT_R32_UINT,rw);
    relight_curve=make_image(4096,1,VK_FORMAT_R32_SFLOAT,rw);
    for (unsigned i=0;i<4;i++)
        relight_mip[i]=make_image((W+(1u<<(i+1))-1)>>(i+1),(H+(1u<<(i+1))-1)>>(i+1),VK_FORMAT_R32G32B32A32_UINT,rw);
    for (unsigned i=0;i<2;i++) {
        relight_ao[i]=make_image(hw,hh,VK_FORMAT_R16G16B16A16_SFLOAT,rw);
        relight_hist[i]=make_image(hw,hh,VK_FORMAT_R16G16B16A16_SFLOAT,rw);
        relight_hd[i]=make_image(hw,hh,VK_FORMAT_R32_SFLOAT,rw);
    }
    init_fresh(cmd);
    const VkClearColorValue zero{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdClearColorImage(cmd,relight_dummy.handle,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);
    for (unsigned i=0;i<2;i++) for (Image* im : {&relight_hist[i],&relight_hd[i]})
        vkCmdClearColorImage(cmd,im->handle,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);
    memory_barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    relight_valid=relight_tables=false;
}
void run_relight(VkCommandBuffer cmd,const StageFrame& fr,bool reset,VkImageView color) {
    ensure_relight(cmd);
    struct Frame {
        uint32_t rw,rh;float fovY,zNear,zFar;bool depthInverted,hdr;
        float metersPerUnit,mvToUv[2],mvRes[2];
    } frame{cfg.width,cfg.height,settings.ditto_fov*0.01745329252f,settings.ditto_near,settings.ditto_far,
            fr.depth_inverted,hdr!=0,1.f,{1.f,1.f},{float(cfg.width),float(cfg.height)}};
    DittoSettings ds{};
    ds.aoStrength=settings.ditto_ao;ds.giStrength=settings.ditto_gi;ds.aoSamples=float(settings.ditto_samples);
    AoConstants c{};ditto_fill_ao(c,ds,frame,hdr!=0,relight_frames++);
    c.flags=float(uint32_t(c.flags)|8u|16u|32u|262144u);
    c.halfRes=1.f;c.axis=0.f;
    c.temporal=relight_valid&&!reset?ds.aoTemporalWeight:0.f;
    const auto run=[&](P p,AoConstants a,std::initializer_list<std::pair<const uint32_t,VkImageView>> bind,uint32_t x,uint32_t y) {
        memory_barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_UNIFORM_READ_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdUpdateBuffer(cmd,ditto_constants,0,sizeof a,&a);
        memory_barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_UNIFORM_READ_BIT);
        dispatch(cmd,p,S(p,std::map<uint32_t,VkImageView>(bind),{{0,ditto_constants}}),nullptr,0,x,y);
    };
    const uint32_t gx=(cfg.width+7)/8,gy=(cfg.height+7)/8;
    const uint32_t hw=(cfg.width+1)/2,hh=(cfg.height+1)/2,hx=(hw+7)/8,hy=(hh+7)/8;
    const auto nd=relight_nd.view,hn=relight_half_nd.view,unused_uint=relight_dummy.view;
    if (!relight_tables) {
        run(kAoLut,c,{{20,relight_lut.view}},128,4);
        run(kAoCurve,c,{{20,relight_curve.view}},64,1);
        relight_tables=true;
    }
    run(kAoPrep,c,{{10,ditto_full_depth.view},{20,nd},{21,unused_uint}},gx,gy);
    AoConstants mip=c;mip.axis=1;
    run(kAoMip,mip,{{10,nd},{11,color},{17,unused_uint},{20,relight_mip[0].view},
        {21,relight_mip[1].view},{22,relight_mip[2].view},{23,relight_mip[3].view}},
        (relight_mip[0].w+15)/16,(relight_mip[0].h+15)/16);
    run(kAoMain,c,{{10,nd},{11,color},{12,relight_mip[0].view},{13,relight_mip[1].view},
        {14,relight_mip[2].view},{15,relight_mip[3].view},{16,relight_lut.view},{17,unused_uint},
        {20,relight_ao[0].view},{21,hn},{22,unused_uint}},(hw+15)/16,(hh+15)/16);
    AoConstants half=c;half.rs[0]=float(hw);half.rs[1]=float(hh);half.rsRcp[0]=1.f/hw;half.rsRcp[1]=1.f/hh;
    half.axis=2.f;
    run(kAoFilter,half,{{10,hn},{11,relight_ao[0].view},{17,unused_uint},{20,relight_ao[1].view}},hx,hy);
    half.axis=0.f;
    const unsigned next=relight_cur^1u;
    // Jittered vectors include previous-current sample displacement. Relight's
    // guide positions are already on those samples, so remove only that component.
    // The SR constants perform their own cancellation for residual reconstruction.
    run(kAoTemporal,half,{{10,hn},{11,relight_ao[1].view},{12,relight_hist[relight_cur].view},
        {13,relight_hd[relight_cur].view},{14,ditto_full_motion.view},{17,unused_uint},
        {20,relight_hist[next].view},{21,relight_hd[next].view}},hx,hy);
    run(kAoCacao,half,{{10,hn},{11,relight_hist[next].view},{17,unused_uint},{20,relight_ao[0].view}},hx,hy);
    run(kAoCompose,c,{{10,nd},{11,color},{12,relight_ao[0].view},{13,ditto_full_depth.view},
        {14,relight_curve.view},{17,unused_uint},{20,relight_color.view}},gx,gy);
    relight_cur=next;relight_valid=true;
}
