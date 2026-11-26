// Included inside Stage::Impl. Ditto's resources share the stage's GPU completion lifetime.
struct DittoPrepare {
    uint32_t size[2], source[2], depth_base[2], depth_size[2], motion_base[2], motion_size[2];
    float motion_scale[2];
    uint32_t inverted, guides;
    float jitter[2];
};
static_assert(sizeof(DittoPrepare) == 72);

void invalidate_ditto() {
    for (auto& pass : dittos) pass.valid = false;
    relight_valid = false;
}
void retire_ditto_pass(DittoPass& pass) {
    for (Image* im : {&pass.mda[0], &pass.mda[1], &pass.history[0], &pass.history[1], &pass.aux[0], &pass.aux[1], &pass.env[0], &pass.env[1], &pass.stability[0], &pass.stability[1]})
        retire(*im);
    pass.valid = false;
}
void retire_ditto() {
    for (auto& pass : dittos) retire_ditto_pass(pass);
    dittos.clear();
    for (Image* im : {&ditto_depth, &ditto_motion, &ditto_result, &ditto_change, &ditto_edit, &ditto_full_depth, &ditto_full_motion}) retire(*im);
    retire_relight();
}
void ensure_ditto(VkCommandBuffer cmd, uint32_t passes) {
    if (!programs[kDittoResidual].pipeline || !ditto_constants) {
        // The old prepare shader downsampled a decoded frame. Reject that ABI instead of silently using it here.
        const auto code = read_spirv(cfg.shaders + "/renodx_ditto_prepare.spv");
        bool phase_layout = false;
        for (size_t i = 5; i < code.size();) {
            const uint32_t op = code[i] & 0xffffu, len = code[i] >> 16;
            if (!len || i + len > code.size()) break;
            if (op == 72 && len == 5 && code[i + 2] == 9 && code[i + 3] == 35 && code[i + 4] == 64)
                phase_layout = true; // push member 9: output jitter, at byte 64
            i += len;
        }
        if (!phase_layout) throw std::runtime_error("Ditto Relight shader mismatch: replace the ASI and all Ditto/Relight shaders together");
        const auto residual = read_spirv(cfg.shaders + "/renodx_ditto_residual.spv");
        bool motion_binding=false, detail_strength=false;
        for (size_t i=5; i<residual.size();) {
            const uint32_t op=residual[i]&0xffffu, len=residual[i]>>16;
            if(!len || i+len>residual.size()) break;
            if(op==71 && len==4 && residual[i+2]==33 && residual[i+3]==9) motion_binding=true;
            if(op==72 && len==5 && residual[i+3]==35 && residual[i+4]==36) detail_strength=true;
            i+=len;
        }
        if(!motion_binding || !detail_strength)
            throw std::runtime_error("Neural reconstruction shader ABI mismatch: update the runtime and Ditto shaders together");
        // The new cbuffer has 61 floats; accepting the previous 51-float shaders
        // would leave the new output/envelope unwritten instead of failing clearly.
        for (const char* name : {"convert", "motion", "upscale", "sharpen"}) {
            const auto shader = read_spirv(cfg.shaders + "/renodx_ditto_" + name + ".spv");
            bool new_layout = false;
            for (size_t i = 5; i < shader.size();) {
                const uint32_t op = shader[i] & 0xffffu, len = shader[i] >> 16;
                if (!len || i + len > shader.size()) break;
                if (op == 72 && len == 5 && shader[i + 3] == 35 && shader[i + 4] == 240)
                    new_layout = true;
                i += len;
            }
            if (!new_layout) throw std::runtime_error("Ditto Relight shader mismatch: replace the ASI and all Ditto/Relight shaders together");
        }
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0.f;
        if (!ditto_sampler) check(vkCreateSampler(device, &si, nullptr, &ditto_sampler), "Ditto sampler");
        if (!programs[kDittoPrepare].pipeline) load(programs[kDittoPrepare], "ditto_prepare", sizeof(DittoPrepare));
        if (!programs[kDittoConvert].pipeline) load(programs[kDittoConvert], "ditto_convert", 0);
        if (!programs[kDittoMotion].pipeline) load(programs[kDittoMotion], "ditto_motion", 0);
        if (!programs[kDittoUpscale].pipeline) load(programs[kDittoUpscale], "ditto_upscale", 0);
        if (!programs[kDittoSharpen].pipeline) load(programs[kDittoSharpen], "ditto_sharpen", 0);
        if (!programs[kDittoResidual].pipeline) load(programs[kDittoResidual], "ditto_residual", 40);
        if (!ditto_constants) make_buffer(256, &ditto_constants, &ditto_constants_memory, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
    const uint32_t NW = cfg.nr_width, NH = cfg.nr_height, W = cfg.width, H = cfg.height;
    const bool scaled = NW < W || NH < H;
    const auto rw = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    std::vector<VkImage> clear;
    if (!ditto_depth.handle) {
        ditto_depth = make_image(scaled ? NW : 1, scaled ? NH : 1, VK_FORMAT_R32_SFLOAT, rw);
        ditto_motion = make_image(scaled ? NW : 1, scaled ? NH : 1, VK_FORMAT_R32G32B32A32_SFLOAT, rw);
        ditto_result = make_image(scaled ? W : 1, scaled ? H : 1, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
        ditto_change = make_image(1, 1, VK_FORMAT_R32_SFLOAT, rw);
        ditto_edit = make_image(scaled ? NW : 1, scaled ? NH : 1, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
        ditto_full_depth = make_image(W, H, VK_FORMAT_R32_SFLOAT, rw);
        ditto_full_motion = make_image(W, H, VK_FORMAT_R32G32B32A32_SFLOAT, rw);
        clear.push_back(ditto_change.handle);
    }
    while (dittos.size() > passes) { retire_ditto_pass(dittos.back()); dittos.pop_back(); }
    dittos.resize(passes);
    for (auto& pass : dittos) if (!pass.history[0].handle) {
        for (unsigned i = 0; i < 2; ++i) {
            pass.mda[i] = make_image(NW, NH, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
            pass.env[i] = make_image(NW, NH, VK_FORMAT_R8G8_UNORM, rw);
            pass.stability[i] = make_image(W, H, VK_FORMAT_R8G8_UNORM, rw);
            clear.push_back(pass.env[i].handle); clear.push_back(pass.stability[i].handle);
            pass.aux[i] = make_image(NW, NH, VK_FORMAT_R8G8B8A8_UNORM, rw);
            pass.history[i] = make_image(W, H, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
            clear.push_back(pass.mda[i].handle); clear.push_back(pass.aux[i].handle); clear.push_back(pass.history[i].handle);
        }
        pass.valid = false;
    }
    init_fresh(cmd);
    // Reset frames can sample history before discarding it. Every new pass starts from initialized texels.
    if (!clear.empty()) {
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        for (VkImage image : clear) vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
}

static VkImageAspectFlags ditto_aspect(VkFormat f, bool barrier = false) {
    switch (f) {
        case VK_FORMAT_D16_UNORM: case VK_FORMAT_D32_SFLOAT: case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT: case VK_FORMAT_D24_UNORM_S8_UINT: case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | (barrier ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        default: return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}
void ditto_guide_barrier(VkCommandBuffer cmd, const ColourFrame& im, bool restore) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = restore ? VK_ACCESS_SHADER_READ_BIT : im.before_access;
    b.dstAccessMask = restore ? im.after_access : VK_ACCESS_SHADER_READ_BIT;
    b.oldLayout = restore ? VK_IMAGE_LAYOUT_GENERAL : im.before;
    b.newLayout = restore ? im.after : VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = im.image; b.subresourceRange = {ditto_aspect(im.format, true), 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, restore ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : im.before_stage,
                        restore ? im.after_stage : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}
VkImageView ditto_view(const ColourFrame& im) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = im.format;
    vi.subresourceRange = {ditto_aspect(im.format), 0, 1, 0, 1};
    VkImageView v{};
    check(vkCreateImageView(device, &vi, nullptr, &v), "Ditto guide view");
    return v;
}
DittoPrepare ditto_prepare_constants(const StageFrame& fr) const {
    return {{cfg.nr_width, cfg.nr_height}, {cfg.width, cfg.height},
            {fr.depth_x, fr.depth_y}, {fr.depth.width, fr.depth.height},
            {fr.motion_x, fr.motion_y}, {fr.motion.width, fr.motion.height},
            {fr.motion_scale_x, fr.motion_scale_y}, fr.depth_inverted ? 1u : 0u, 1u, {0.f, 0.f}};
}
std::map<uint32_t, VkImageView> ditto_prepare_views() const {
    return {{0, ditto_result.view}, {1, ditto_depth.view}, {2, ditto_motion.view},
            {4, up_neural.view ? up_neural.view : ph16.view}, {5, ditto_depth.view}, {6, ditto_motion.view}};
}
void prepare_ditto(VkCommandBuffer cmd, const StageFrame& fr, Borrow*& slot, uint32_t passes) {
    const bool scaled = cfg.nr_width < cfg.width || cfg.nr_height < cfg.height;
    ensure_ditto(cmd, scaled ? passes : 0);
    if (!slot) slot = &borrow_slot();
    Borrow& b = *slot;
    b.ditto_depth = ditto_view(fr.depth);
    b.ditto_motion = ditto_view(fr.motion);
    ditto_guide_barrier(cmd, fr.depth, false);
    ditto_guide_barrier(cmd, fr.motion, false);
    auto c = ditto_prepare_constants(fr);
    auto views = ditto_prepare_views();
    views[1] = b.ditto_depth; views[2] = b.ditto_motion;
    if (scaled) dispatch(cmd, kDittoPrepare, set_for(programs[kDittoPrepare], views, {}, b.pool),
                         &c, sizeof c, (cfg.nr_width + 15) / 16, (cfg.nr_height + 15) / 16);
    // Full-size guides keep lighting and thin silhouettes independent of the model scale.
    c.size[0] = cfg.width; c.size[1] = cfg.height; c.guides = 2;
    if (fr.jittered_motion && relight_valid && !fr.reset) {
        c.jitter[0] = -(relight_jitter[0] - fr.jitter_x) / fr.render_width;
        c.jitter[1] = -(relight_jitter[1] - fr.jitter_y) / fr.render_height;
    }
    relight_jitter[0] = fr.jitter_x; relight_jitter[1] = fr.jitter_y;
    views[5] = ditto_full_depth.view; views[6] = ditto_full_motion.view;
    dispatch(cmd, kDittoPrepare, set_for(programs[kDittoPrepare], views, {}, b.pool),
             &c, sizeof c, (cfg.width + 15) / 16, (cfg.height + 15) / 16);
    ditto_guide_barrier(cmd, fr.motion, true);
    ditto_guide_barrier(cmd, fr.depth, true);
}
void run_ditto(VkCommandBuffer cmd, uint32_t pass_number, const StageFrame& fr, bool reset, VkImageView neural_view, VkImageView reference_view) {
    DittoPass& pass = dittos.at(pass_number);
    const auto f = nr::ditto::neural_frame(cfg.nr_width, cfg.nr_height, cfg.width, cfg.height,
        fr.render_width, fr.render_height, fr.jitter_x, fr.jitter_y, fr.jittered_motion, fr.depth_inverted);
    DittoConstants c{};
    DittoSettings ds{}; ds.sharpness=settings.ditto_sharpness;
    ds.sharpenMode = ds.sharpness > 0.f ? 1 : 2; ds.historySharpen = 0.f;
    ditto_fill_constants(c, ds, f, reset || !pass.valid, false, false, false, pass.previous_jitter);
    memory_barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdUpdateBuffer(cmd, ditto_constants, 0, sizeof c, &c);
    memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT);
    struct ResidualPush { uint32_t size[2], low[2]; float jitter[2]; uint32_t mode, linear; float light,detail; };
    static_assert(sizeof(ResidualPush)==40);
    ResidualPush rp{{cfg.nr_width, cfg.nr_height}, {cfg.nr_width, cfg.nr_height}, {}, 0,
                    uint32_t(hdr != 0) | (fr.depth_inverted ? 2u : 0u),settings.reconstruction_light,settings.reconstruction_detail};
    const auto residual_views = [&] { return std::map<uint32_t, VkImageView>{{0, proxy.view}, {1, neural_view},
        {2, reference_view}, {3, ditto_result.view}, {4, ditto_depth.view}, {5, ditto_full_depth.view},
        {6, ditto_edit.view}, {7, up_proxy.view}, {8, ph16.view}, {9,ditto_full_motion.view}}; };
    dispatch(cmd, kDittoResidual, S(kDittoResidual, residual_views()), &rp, sizeof rp,
             (cfg.nr_width + 7) / 8, (cfg.nr_height + 7) / 8);
    const uint32_t cur = pass.cur, next = cur ^ 1u;
    const std::map<uint32_t, VkBuffer> constants{{0, ditto_constants}};
    const auto run = [&](P p, std::initializer_list<std::pair<const uint32_t, VkImageView>> bindings, uint32_t x, uint32_t y) {
        dispatch(cmd, p, S(p, std::map<uint32_t, VkImageView>(bindings), constants), nullptr, 0, x, y);
    };
    const uint32_t nx = (cfg.nr_width + 7) / 8, ny = (cfg.nr_height + 7) / 8;
    // t5 and t6 are dynamically disabled; bind valid initialized images nonetheless.
    run(kDittoConvert, {{10, ditto_depth.view}, {11, ditto_motion.view}, {12, ditto_edit.view},
                       {13, pass.history[cur].view}, {15, ditto_change.view},
                       {20, pass.mda[next].view}, {21, ditto_change.view}, {22, ditto_change.view}}, nx, ny + 1);
    run(kDittoMotion, {{10, pass.aux[cur].view}, {11, pass.mda[next].view}, {12, ditto_edit.view},
                      {13, pass.mda[cur].view}, {14, pass.env[cur].view}, {15, ditto_change.view}, {20, pass.aux[next].view}, {21, pass.env[next].view}}, nx, ny);
    const uint32_t ox = (cfg.width + 7) / 8, oy = (cfg.height + 7) / 8;
    run(kDittoUpscale, {{10, pass.history[cur].view}, {11, pass.mda[next].view}, {12, ditto_edit.view},
                       {13, pass.aux[next].view}, {14, ditto_change.view}, {15, ditto_change.view},
                       {16, ditto_change.view}, {17, pass.stability[cur].view}, {20, pass.history[next].view},
                       {21, ditto_result.view}, {22, pass.stability[next].view}}, (cfg.width + 15) / 16, oy);
    if (ds.sharpenMode != 2)
        run(kDittoSharpen, {{10,pass.history[next].view},{11,pass.mda[next].view},{15,ditto_change.view},{17,pass.stability[next].view},
                            {21,ditto_result.view}}, ox, oy);
    rp.size[0] = cfg.width; rp.size[1] = cfg.height; rp.mode = 1;
    const auto jitter = nr::ditto::output_phase(fr.jitter_x, fr.jitter_y, fr.render_width, fr.render_height,
                                               cfg.width, cfg.height);
    rp.jitter[0] = jitter[0]; rp.jitter[1] = jitter[1];
    auto rv = residual_views(); rv[6] = up_neural.view; rv[8] = ditto_edit.view;
    dispatch(cmd, kDittoResidual, S(kDittoResidual, rv), &rp, sizeof rp,
             (cfg.width + 7) / 8, (cfg.height + 7) / 8);
    pass.cur = next; pass.valid = true;
    if (!ditto_logged) {
        nr::logf("[nr] Ditto Relight 0.5.4: neural %ux%u -> up_neural %ux%u; guided residual -> up_neural, resolve with up_proxy (game motion/jitter, per-pass history)",
                 cfg.nr_width, cfg.nr_height, cfg.width, cfg.height);
        ditto_logged = true;
    }
}

#include "nr_ditto_relight.inl"
