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
}
void retire_ditto_pass(DittoPass& pass) {
    for (Image* im : {&pass.mda[0], &pass.mda[1], &pass.history[0], &pass.history[1], &pass.aux[0], &pass.aux[1]})
        retire(*im);
    pass.valid = false;
}
void retire_ditto() {
    for (auto& pass : dittos) retire_ditto_pass(pass);
    dittos.clear();
    for (Image* im : {&ditto_depth, &ditto_motion, &ditto_result, &ditto_change}) retire(*im);
}
void ensure_ditto(VkCommandBuffer cmd, uint32_t passes) {
    if (!programs[kDittoPrepare].pipeline) {
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
        if (!phase_layout) throw std::runtime_error("Ditto SR shader mismatch: replace the ASI and all five Ditto shaders together");
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0.f;
        check(vkCreateSampler(device, &si, nullptr, &ditto_sampler), "Ditto sampler");
        load(programs[kDittoPrepare], "ditto_prepare", sizeof(DittoPrepare));
        load(programs[kDittoConvert], "ditto_convert", 0);
        load(programs[kDittoMotion], "ditto_motion", 0);
        load(programs[kDittoUpscale], "ditto_upscale", 0);
        load(programs[kDittoSharpen], "ditto_sharpen", 0);
        make_buffer(256, &ditto_constants, &ditto_constants_memory, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
    const uint32_t NW = cfg.nr_width, NH = cfg.nr_height, W = cfg.width, H = cfg.height;
    const auto rw = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    std::vector<VkImage> clear;
    if (!ditto_depth.handle) {
        ditto_depth = make_image(NW, NH, VK_FORMAT_R32_SFLOAT, rw);
        ditto_motion = make_image(NW, NH, VK_FORMAT_R32G32B32A32_SFLOAT, rw);
        ditto_result = make_image(W, H, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
        ditto_change = make_image(1, 1, VK_FORMAT_R32_SFLOAT, rw);
        clear.push_back(ditto_change.handle);
    }
    while (dittos.size() > passes) { retire_ditto_pass(dittos.back()); dittos.pop_back(); }
    dittos.resize(passes);
    for (auto& pass : dittos) if (!pass.history[0].handle) {
        for (unsigned i = 0; i < 2; ++i) {
            pass.mda[i] = make_image(NW, NH, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
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
            {4, up_neural.view}, {5, ditto_depth.view}, {6, ditto_motion.view}};
}
void prepare_ditto(VkCommandBuffer cmd, const StageFrame& fr, Borrow*& slot, uint32_t passes) {
    ensure_ditto(cmd, passes);
    if (!slot) slot = &borrow_slot();
    Borrow& b = *slot;
    b.ditto_depth = ditto_view(fr.depth);
    b.ditto_motion = ditto_view(fr.motion);
    ditto_guide_barrier(cmd, fr.depth, false);
    ditto_guide_barrier(cmd, fr.motion, false);
    auto c = ditto_prepare_constants(fr);
    auto views = ditto_prepare_views();
    views[1] = b.ditto_depth; views[2] = b.ditto_motion;
    dispatch(cmd, kDittoPrepare, set_for(programs[kDittoPrepare], views, {}, b.pool),
             &c, sizeof c, (cfg.nr_width + 15) / 16, (cfg.nr_height + 15) / 16);
    ditto_guide_barrier(cmd, fr.motion, true);
    ditto_guide_barrier(cmd, fr.depth, true);
}
void run_ditto(VkCommandBuffer cmd, uint32_t pass_number, const StageFrame& fr, bool reset, VkImageView neural_view) {
    DittoPass& pass = dittos.at(pass_number);
    const auto f = nr::ditto::neural_frame(cfg.nr_width, cfg.nr_height, cfg.width, cfg.height,
        fr.render_width, fr.render_height, fr.jitter_x, fr.jitter_y, fr.jittered_motion, fr.depth_inverted);
    DittoConstants c{};
    ditto_fill_constants(c, DittoSettings{}, f, reset || !pass.valid, false, false, false, pass.previous_jitter);
    memory_barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdUpdateBuffer(cmd, ditto_constants, 0, sizeof c, &c);
    memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT);
    const uint32_t cur = pass.cur, next = cur ^ 1u;
    const std::map<uint32_t, VkBuffer> constants{{0, ditto_constants}};
    const auto run = [&](P p, std::initializer_list<std::pair<const uint32_t, VkImageView>> bindings, uint32_t x, uint32_t y) {
        dispatch(cmd, p, S(p, std::map<uint32_t, VkImageView>(bindings), constants), nullptr, 0, x, y);
    };
    const uint32_t nx = (cfg.nr_width + 7) / 8, ny = (cfg.nr_height + 7) / 8;
    // t5 and t6 are dynamically disabled; bind valid initialized images nonetheless.
    run(kDittoConvert, {{10, ditto_depth.view}, {11, ditto_motion.view}, {12, neural_view},
                       {13, pass.history[cur].view}, {15, ditto_change.view},
                       {20, pass.mda[next].view}, {21, ditto_change.view}}, nx, ny + 1);
    run(kDittoMotion, {{10, pass.aux[cur].view}, {11, pass.mda[next].view}, {12, neural_view},
                      {13, pass.mda[cur].view}, {15, ditto_change.view}, {20, pass.aux[next].view}}, nx, ny);
    const uint32_t ox = (cfg.width + 7) / 8, oy = (cfg.height + 7) / 8;
    run(kDittoUpscale, {{10, pass.history[cur].view}, {11, pass.mda[next].view}, {12, neural_view},
                       {13, pass.aux[next].view}, {14, ditto_change.view}, {15, ditto_change.view},
                       {16, ditto_change.view}, {20, pass.history[next].view}}, (cfg.width + 15) / 16, oy);
    run(kDittoSharpen, {{10, pass.history[next].view}, {11, pass.mda[next].view}, {15, ditto_change.view},
                       {21, ditto_result.view}}, ox, oy);
    // Keep the full-resolution proxy untouched. Only neural transport is replaced by Ditto's answer.
    auto phase = ditto_prepare_constants(fr);
    phase.size[0] = phase.source[0] = cfg.width; phase.size[1] = phase.source[1] = cfg.height;
    phase.guides = 0;
    const auto jitter = nr::ditto::output_phase(fr.jitter_x, fr.jitter_y, fr.render_width, fr.render_height,
                                              cfg.width, cfg.height);
    phase.jitter[0] = jitter[0]; phase.jitter[1] = jitter[1];
    dispatch(cmd, kDittoPrepare, S(kDittoPrepare, ditto_prepare_views()), &phase, sizeof phase,
             (cfg.width + 15) / 16, (cfg.height + 15) / 16);
    pass.cur = next; pass.valid = true;
    if (!ditto_logged) {
        nr::logf("[nr] Ditto SR: neural %ux%u -> up_neural %ux%u; resolve with up_proxy (game motion/jitter, per-pass history)",
                 cfg.nr_width, cfg.nr_height, cfg.width, cfg.height);
        ditto_logged = true;
    }
}
