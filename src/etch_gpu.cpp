// SDL3_GPU device lifecycle, pipeline cache, batched Prim submission, and PNG readback.
// Compiled via src/etch_unity.cpp.

#include <cmath>

namespace etch {

namespace {

constexpr bool is_srgb_gpu_format(SDL_GPUTextureFormat fmt) noexcept {
    return fmt == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB ||
           fmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
}

inline float srgb_u8_to_linear(uint8_t u) noexcept {
    const float c = static_cast<float>(u) * (1.0f / 255.0f);
    return (c <= 0.04045f)
        ? (c * (1.0f / 12.92f))
        : std::pow((c + 0.055f) * (1.0f / 1.055f), 2.4f);
}

inline SDL_FColor make_clear_fcolor(Color c, SDL_GPUTextureFormat target_fmt) noexcept {
    if (is_srgb_gpu_format(target_fmt)) {
        return SDL_FColor{
            srgb_u8_to_linear(c.r),
            srgb_u8_to_linear(c.g),
            srgb_u8_to_linear(c.b),
            static_cast<float>(c.a) * (1.0f / 255.0f),
        };
    }
    return SDL_FColor{
        static_cast<float>(c.r) * (1.0f / 255.0f),
        static_cast<float>(c.g) * (1.0f / 255.0f),
        static_cast<float>(c.b) * (1.0f / 255.0f),
        static_cast<float>(c.a) * (1.0f / 255.0f),
    };
}

inline constexpr SDL_GPUTextureFormat kBlurTexFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;

struct alignas(16) KawaseUniform {
    float    texel_size[2];
    float    offset;
    uint32_t mode;
};
static_assert(sizeof(KawaseUniform) == 16);
static_assert(sizeof(KawaseUniform) % 16 == 0);

inline float clampf_blur(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

SDL_GPUGraphicsPipeline* create_graphics_pipeline(
    SDL_GPUDevice* device,
    SDL_GPUShader* vs,
    SDL_GPUShader* ps,
    SDL_GPUTextureFormat target_fmt,
    bool enable_blend) noexcept
{
    SDL_GPUColorTargetBlendState blend = {};
    blend.enable_blend = enable_blend;
    if (enable_blend) {
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    blend.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
        SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    const SDL_GPUColorTargetDescription target_desc = {
        .format = target_fmt,
        .blend_state = blend,
    };

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = vs;
    info.fragment_shader = ps;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = false;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &target_desc;
    info.target_info.num_color_targets = 1u;
    info.target_info.has_depth_stencil_target = false;

    SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (!pipe) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return nullptr;
    }
    return pipe;
}

SDL_GPUGraphicsPipeline* get_or_create_cached_pipeline(
    SDL_GPUDevice* device,
    SDL_GPUShader* vs,
    SDL_GPUShader* ps,
    SDL_GPUTextureFormat target_fmt,
    bool enable_blend,
    CachedPipeline* cache,
    uint32_t* count) noexcept
{
    if (!device || !vs || !ps || target_fmt == SDL_GPU_TEXTUREFORMAT_INVALID) {
        return nullptr;
    }
    for (uint32_t i = 0u; i < *count; ++i) {
        if (cache[i].format == target_fmt) {
            return cache[i].pipeline;
        }
    }
    if (*count >= kMaxCachedPipelines) {
        return nullptr;
    }
    SDL_GPUGraphicsPipeline* pipe = create_graphics_pipeline(device, vs, ps, target_fmt, enable_blend);
    if (!pipe) {
        return nullptr;
    }
    cache[(*count)++] = CachedPipeline{
        .format = target_fmt,
        .pipeline = pipe,
    };
    return pipe;
}

SDL_GPUGraphicsPipeline* get_or_create_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    if (!ctx) {
        return nullptr;
    }
    return get_or_create_cached_pipeline(
        ctx->device,
        ctx->vs_shader,
        ctx->ps_shader,
        target_fmt,
        true,
        ctx->pipelines,
        &ctx->pipeline_count);
}

SDL_GPUGraphicsPipeline* get_or_create_present_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    if (!ctx) {
        return nullptr;
    }
    return get_or_create_cached_pipeline(
        ctx->device,
        ctx->kawase_vs_shader,
        ctx->kawase_ps_shader,
        target_fmt,
        false,
        ctx->present_pipelines,
        &ctx->present_pipeline_count);
}

SDL_GPUGraphicsPipeline* get_or_create_kawase_pipeline(Context* ctx) noexcept {
    if (!ctx->kawase_pipe) {
        ctx->kawase_pipe = create_graphics_pipeline(
            ctx->device,
            ctx->kawase_vs_shader,
            ctx->kawase_ps_shader,
            kBlurTexFormat,
            false);
    }
    return ctx->kawase_pipe;
}

void release_blur_targets(Context* ctx) noexcept {
    if (ctx->active_ui->scene_tex) {
        SDL_ReleaseGPUTexture(ctx->device, ctx->active_ui->scene_tex);
        ctx->active_ui->scene_tex = nullptr;
    }
    if (ctx->active_ui->blur_ping_tex) {
        SDL_ReleaseGPUTexture(ctx->device, ctx->active_ui->blur_ping_tex);
        ctx->active_ui->blur_ping_tex = nullptr;
    }
    if (ctx->active_ui->blur_pong_tex) {
        SDL_ReleaseGPUTexture(ctx->device, ctx->active_ui->blur_pong_tex);
        ctx->active_ui->blur_pong_tex = nullptr;
    }
    ctx->active_ui->blur_rt_w = 0u;
    ctx->active_ui->blur_rt_h = 0u;
}

bool ensure_blur_targets(Context* ctx, uint32_t width, uint32_t height) noexcept {
    if (ctx->active_ui->scene_tex && ctx->active_ui->blur_ping_tex && ctx->active_ui->blur_pong_tex &&
        ctx->active_ui->blur_rt_w == width && ctx->active_ui->blur_rt_h == height)
    {
        return true;
    }
    release_blur_targets(ctx);

    const uint32_t half_w = (width > 1u) ? (width / 2u) : 1u;
    const uint32_t half_h = (height > 1u) ? (height / 2u) : 1u;
    const uint32_t quarter_w = (width > 3u) ? (width / 4u) : 1u;
    const uint32_t quarter_h = (height > 3u) ? (height / 4u) : 1u;

    auto make_rt = [&](uint32_t w, uint32_t h) noexcept -> SDL_GPUTexture* {
        const SDL_GPUTextureCreateInfo info = {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = kBlurTexFormat,
            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = w,
            .height = h,
            .layer_count_or_depth = 1u,
            .num_levels = 1u,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0u,
        };
        return SDL_CreateGPUTexture(ctx->device, &info);
    };

    ctx->active_ui->scene_tex = make_rt(width, height);
    ctx->active_ui->blur_ping_tex = make_rt(half_w, half_h);
    ctx->active_ui->blur_pong_tex = make_rt(quarter_w, quarter_h);
    if (!ctx->active_ui->scene_tex || !ctx->active_ui->blur_ping_tex || !ctx->active_ui->blur_pong_tex) {
        release_blur_targets(ctx);
        return false;
    }
    ctx->active_ui->blur_rt_w = width;
    ctx->active_ui->blur_rt_h = height;
    return true;
}

bool ensure_slug_storage_pair(
    SDL_GPUDevice* device,
    uint32_t needed_count,
    uint32_t elem_size,
    SDL_GPUBuffer** gpu_buf,
    uint32_t* gpu_cap,
    SDL_GPUTransferBuffer** tbuf,
    uint32_t* tbuf_cap,
    uint32_t* uploaded_count) noexcept
{
    if (needed_count == 0u) {
        return true;
    }
    if (*gpu_cap < needed_count) {
        uint32_t new_cap = (*gpu_cap > 0u) ? *gpu_cap : 256u;
        while (new_cap < needed_count) {
            new_cap *= 2u;
        }
        const SDL_GPUBufferCreateInfo info = {
            .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            .size = new_cap * elem_size,
            .props = 0u,
        };
        SDL_GPUBuffer* next_buf = SDL_CreateGPUBuffer(device, &info);
        if (!next_buf) {
            return false;
        }
        if (*gpu_buf) {
            SDL_ReleaseGPUBuffer(device, *gpu_buf);
        }
        *gpu_buf = next_buf;
        *gpu_cap = new_cap;
        *uploaded_count = 0u;
    }

    const uint32_t delta_count = needed_count - *uploaded_count;
    if (delta_count > 0u && *tbuf_cap < delta_count) {
        uint32_t new_tcap = (*tbuf_cap > 0u) ? *tbuf_cap : 256u;
        while (new_tcap < delta_count) {
            new_tcap *= 2u;
        }
        const SDL_GPUTransferBufferCreateInfo tinfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = new_tcap * elem_size,
            .props = 0u,
        };
        SDL_GPUTransferBuffer* next_tb = SDL_CreateGPUTransferBuffer(device, &tinfo);
        if (!next_tb) {
            return false;
        }
        if (*tbuf) {
            SDL_ReleaseGPUTransferBuffer(device, *tbuf);
        }
        *tbuf = next_tb;
        *tbuf_cap = new_tcap;
    }
    return true;
}

void upload_slug_delta(
    SDL_GPUDevice* device,
    SDL_GPUCopyPass* copy_pass,
    const void* host_data,
    uint32_t elem_size,
    uint32_t total_count,
    uint32_t* uploaded_count,
    SDL_GPUBuffer* gpu_buf,
    SDL_GPUTransferBuffer* tbuf) noexcept
{
    if (*uploaded_count >= total_count || !host_data || !gpu_buf || !tbuf) {
        return;
    }
    const uint32_t first = *uploaded_count;
    const uint32_t count = total_count - first;
    const uint32_t byte_offset = first * elem_size;
    const uint32_t byte_size = count * elem_size;
    void* mapped = SDL_MapGPUTransferBuffer(device, tbuf, true);
    if (!mapped) {
        return;
    }
    std::memcpy(mapped, static_cast<const uint8_t*>(host_data) + byte_offset, byte_size);
    SDL_UnmapGPUTransferBuffer(device, tbuf);

    const SDL_GPUTransferBufferLocation src = {
        .transfer_buffer = tbuf,
        .offset = 0u,
    };
    const SDL_GPUBufferRegion dst = {
        .buffer = gpu_buf,
        .offset = byte_offset,
        .size = byte_size,
    };
    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);
    *uploaded_count = total_count;
}

void emit_uber_batches(
    Context* ctx,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPURenderPass* render_pass,
    SDL_GPUGraphicsPipeline* pipeline,
    SDL_GPUTextureFormat target_fmt,
    uint32_t target_w,
    uint32_t target_h,
    uint32_t begin_batch,
    uint32_t end_batch) noexcept
{
    if (ctx->prim_count == 0u || begin_batch >= end_batch) {
        return;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);

    const SDL_GPUViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .w = static_cast<float>(target_w),
        .h = static_cast<float>(target_h),
        .min_depth = 0.0f,
        .max_depth = 1.0f,
    };
    SDL_SetGPUViewport(render_pass, &viewport);

    const SDL_Rect scissor = {
        .x = 0,
        .y = 0,
        .w = static_cast<int>(target_w),
        .h = static_cast<int>(target_h),
    };
    SDL_SetGPUScissor(render_pass, &scissor);

    SDL_GPUBuffer* storage_bufs[1] = { ctx->prim_gpu_buf };
    SDL_BindGPUVertexStorageBuffers(render_pass, 0u, storage_bufs, 1u);

    SDL_GPUBuffer* slug_bufs[2] = { ctx->slug_curve_gpu_buf, ctx->slug_band_gpu_buf };
    SDL_BindGPUFragmentStorageBuffers(render_pass, 0u, slug_bufs, 2u);

    const uint32_t manual_srgb_bit = is_srgb_gpu_format(target_fmt) ? 0u : (1u << 31);
    const float inv_w = 1.0f / static_cast<float>(target_w);
    const float inv_h = 1.0f / static_cast<float>(target_h);
    SDL_GPUTexture* sentinel = blur_tex_sentinel();

    for (uint32_t b = begin_batch; b < end_batch; ++b) {
        const DrawBatch& batch = ctx->batches[b];
        if (batch.prim_count == 0u) {
            continue;
        }

        auto resolve_tex = [&](SDL_GPUTexture* t) noexcept -> SDL_GPUTexture* {
            if (t == sentinel) {
                return ctx->active_ui->blur_ping_tex ? ctx->active_ui->blur_ping_tex : ctx->atlas.gpu_tex;
            }
            return t ? t : ctx->atlas.gpu_tex;
        };

        SDL_GPUTextureSamplerBinding bindings[4] = {
            { ctx->atlas.gpu_tex, ctx->linear_sampler },
            { resolve_tex(batch.ext_textures[0]), ctx->linear_sampler },
            { resolve_tex(batch.ext_textures[1]), ctx->linear_sampler },
            { resolve_tex(batch.ext_textures[2]), ctx->linear_sampler },
        };
        SDL_BindGPUFragmentSamplers(render_pass, 0u, bindings, 4u);

        const GlobalsUniform globals = {
            .viewport_size = { static_cast<float>(target_w), static_cast<float>(target_h) },
            .inv_viewport_size = { inv_w, inv_h },
            .dpi_scale = ctx->active_ui->dpi_scale,
            .time_seconds = ctx->active_ui->elapsed_seconds,
            .frame_index = ctx->active_ui->frame_index,
            .flags = (batch.first_prim & 0x00FFFFFFu) | manual_srgb_bit,
        };
        SDL_PushGPUVertexUniformData(cmd, 0u, &globals, static_cast<Uint32>(sizeof(globals)));

        // Portable across D3D12/Vulkan/Metal: always pass 0 for first_vertex & first_instance.
        SDL_DrawGPUPrimitives(render_pass, 6u, batch.prim_count, 0u, 0u);
    }
}

bool run_uber_render_pass(
    Context* ctx,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUTexture* dst_tex,
    SDL_GPUTextureFormat dst_fmt,
    uint32_t dst_w,
    uint32_t dst_h,
    SDL_GPUGraphicsPipeline* pipeline,
    SDL_GPULoadOp load_op,
    Color clear_color,
    uint32_t begin_batch,
    uint32_t end_batch) noexcept
{
    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = dst_tex;
    color_target.clear_color = make_clear_fcolor(clear_color, dst_fmt);
    color_target.load_op = load_op;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmd, &color_target, 1u, nullptr);
    if (!render_pass) {
        return false;
    }
    emit_uber_batches(ctx, cmd, render_pass, pipeline, dst_fmt, dst_w, dst_h, begin_batch, end_batch);
    SDL_EndGPURenderPass(render_pass);
    return true;
}

bool run_kawase_step(
    Context* ctx,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUGraphicsPipeline* pipe,
    SDL_GPUTexture* src_tex,
    SDL_GPUTexture* dst_tex,
    uint32_t dst_w,
    uint32_t dst_h,
    float texel_x,
    float texel_y,
    float offset,
    uint32_t mode) noexcept
{
    SDL_GPUColorTargetInfo ct = {};
    ct.texture = dst_tex;
    ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1u, nullptr);
    if (!pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipe);

    const SDL_GPUViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .w = static_cast<float>(dst_w),
        .h = static_cast<float>(dst_h),
        .min_depth = 0.0f,
        .max_depth = 1.0f,
    };
    SDL_SetGPUViewport(pass, &viewport);

    const SDL_Rect scissor = {
        .x = 0,
        .y = 0,
        .w = static_cast<int>(dst_w),
        .h = static_cast<int>(dst_h),
    };
    SDL_SetGPUScissor(pass, &scissor);

    const SDL_GPUTextureSamplerBinding smp = { src_tex, ctx->linear_sampler };
    SDL_BindGPUFragmentSamplers(pass, 0u, &smp, 1u);

    const KawaseUniform u = {
        .texel_size = { texel_x, texel_y },
        .offset = offset,
        .mode = mode,
    };
    SDL_PushGPUFragmentUniformData(cmd, 0u, &u, static_cast<Uint32>(sizeof(u)));
    SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
    SDL_EndGPURenderPass(pass);
    return true;
}

bool claim_window_swapchain(Context* ctx, SDL_Window* win) noexcept {
    if (!ctx || !ctx->device || !win) {
        return false;
    }
    for (uint32_t i = 0; i < kMaxClaimedWindows; ++i)
        if (ctx->claimed_windows[i].window == win) return true;
    if (ctx->claimed_window_count == kMaxClaimedWindows) return false;
    uint32_t index = 0;
    while (ctx->claimed_windows[index].window) ++index;
    ClaimedWindowSlot& slot = ctx->claimed_windows[index];
    WindowUiState& ui = slot.ui;
    ui = WindowUiState{};
    if (!state_map_init(&ui.states, kDefaultStateCapacity)) return false;
    const uint32_t size = Clay_MinMemorySize();
    ui.clay_mem = std::malloc(size);
    if (!ui.clay_mem) { state_map_destroy(&ui.states); return false; }
    Clay_Context* prev_clay = Clay_GetCurrentContext();
    ui.clay_ctx = Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(size, ui.clay_mem),
        Clay_Dimensions{1280, 720}, Clay_ErrorHandler{etch_clay_error_handler, ctx});
    if (ui.clay_ctx) Clay_SetMeasureTextFunction(etch_measure_text, ctx);
    Clay_SetCurrentContext(prev_clay);
    if (!ui.clay_ctx) {
        std::free(ui.clay_mem); state_map_destroy(&ui.states); ui = WindowUiState{}; return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(ctx->device, win)) {
        std::free(ui.clay_mem); state_map_destroy(&ui.states); ui = WindowUiState{}; return false;
    }
    slot.window = win;
    ++ctx->claimed_window_count;
    if (!ctx->window) ctx->window = win;

    if (SDL_WindowSupportsGPUSwapchainComposition(
            ctx->device,
            win,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR))
    {
        SDL_SetGPUSwapchainParameters(
            ctx->device,
            win,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
            SDL_GPU_PRESENTMODE_VSYNC);
    }

    const SDL_GPUTextureFormat swap_fmt = SDL_GetGPUSwapchainTextureFormat(ctx->device, win);
    get_or_create_pipeline(ctx, swap_fmt);
    if (ui.chrome_desc.enabled && SDL_SetWindowHitTest(win, etch_sdl_hit_test, ctx))
        slot.hit_test_attached = true;
    return true;
}

bool record_frame_commands(
    Context* ctx,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUTexture* target_tex,
    SDL_GPUTextureFormat target_fmt,
    uint32_t target_w,
    uint32_t target_h,
    Color clear_color) noexcept
{
    if (!ctx || !cmd || !target_tex || target_w == 0u || target_h == 0u) {
        return false;
    }

    SDL_GPUGraphicsPipeline* pipeline = get_or_create_pipeline(ctx, target_fmt);
    if (!pipeline) {
        return false;
    }

    const bool slug_curves_pending = ctx->atlas.uploaded_slug_curves < ctx->atlas.slug_curve_count;
    const bool slug_bands_pending = ctx->atlas.uploaded_slug_bands < ctx->atlas.slug_band_count;
    if (slug_curves_pending &&
        !ensure_slug_storage_pair(
            ctx->device,
            ctx->atlas.slug_curve_count,
            static_cast<uint32_t>(sizeof(SlugCurveGpu)),
            &ctx->slug_curve_gpu_buf,
            &ctx->slug_curve_gpu_cap,
            &ctx->slug_curve_tbuf,
            &ctx->slug_curve_tbuf_cap,
            &ctx->atlas.uploaded_slug_curves))
    {
        return false;
    }
    if (slug_bands_pending &&
        !ensure_slug_storage_pair(
            ctx->device,
            ctx->atlas.slug_band_count,
            static_cast<uint32_t>(sizeof(SlugBandEntryGpu)),
            &ctx->slug_band_gpu_buf,
            &ctx->slug_band_gpu_cap,
            &ctx->slug_band_tbuf,
            &ctx->slug_band_tbuf_cap,
            &ctx->atlas.uploaded_slug_bands))
    {
        return false;
    }

    if (ctx->atlas.dirty || ctx->prim_count > 0u || slug_curves_pending || slug_bands_pending) {
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
        if (!copy_pass) {
            return false;
        }

        if (ctx->atlas.dirty && ctx->atlas.gpu_tex && ctx->atlas_transfer_buf) {
            void* mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->atlas_transfer_buf, true);
            if (mapped) {
                std::memcpy(mapped, ctx->atlas.pixels, size_t(kAtlasSize) * kAtlasSize * 4u);
                SDL_UnmapGPUTransferBuffer(ctx->device, ctx->atlas_transfer_buf);

                const SDL_GPUTextureTransferInfo src = {
                    .transfer_buffer = ctx->atlas_transfer_buf,
                    .offset = 0u,
                    .pixels_per_row = kAtlasSize,
                    .rows_per_layer = kAtlasSize,
                };
                const SDL_GPUTextureRegion dst = {
                    .texture = ctx->atlas.gpu_tex,
                    .mip_level = 0u,
                    .layer = 0u,
                    .x = 0u,
                    .y = 0u,
                    .z = 0u,
                    .w = kAtlasSize,
                    .h = kAtlasSize,
                    .d = 1u,
                };
                SDL_UploadToGPUTexture(copy_pass, &src, &dst, false);
            }
            ctx->atlas.dirty = false;
        }

        upload_slug_delta(
            ctx->device,
            copy_pass,
            ctx->atlas.slug_curves,
            static_cast<uint32_t>(sizeof(SlugCurveGpu)),
            ctx->atlas.slug_curve_count,
            &ctx->atlas.uploaded_slug_curves,
            ctx->slug_curve_gpu_buf,
            ctx->slug_curve_tbuf);

        upload_slug_delta(
            ctx->device,
            copy_pass,
            ctx->atlas.slug_bands,
            static_cast<uint32_t>(sizeof(SlugBandEntryGpu)),
            ctx->atlas.slug_band_count,
            &ctx->atlas.uploaded_slug_bands,
            ctx->slug_band_gpu_buf,
            ctx->slug_band_tbuf);

        if (ctx->prim_count > 0u && ctx->prim_gpu_buf && ctx->prim_transfer_buf) {
            const uint32_t byte_size = ctx->prim_count * static_cast<uint32_t>(sizeof(Prim));
            void* mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf, true);
            if (mapped) {
                std::memcpy(mapped, ctx->prims, byte_size);
                SDL_UnmapGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf);

                const SDL_GPUTransferBufferLocation src = {
                    .transfer_buffer = ctx->prim_transfer_buf,
                    .offset = 0u,
                };
                const SDL_GPUBufferRegion dst = {
                    .buffer = ctx->prim_gpu_buf,
                    .offset = 0u,
                    .size = byte_size,
                };
                SDL_UploadToGPUBuffer(copy_pass, &src, &dst, true);
            }
        }

        SDL_EndGPUCopyPass(copy_pass);
    }

    if (ctx->blur_split_count == 0u) {
        return run_uber_render_pass(
            ctx,
            cmd,
            target_tex,
            target_fmt,
            target_w,
            target_h,
            pipeline,
            SDL_GPU_LOADOP_CLEAR,
            clear_color,
            0u,
            ctx->batch_count);
    }

    if (!ensure_blur_targets(ctx, target_w, target_h)) {
        return false;
    }
    SDL_GPUGraphicsPipeline* scene_pipe = get_or_create_pipeline(ctx, kBlurTexFormat);
    SDL_GPUGraphicsPipeline* kawase_pipe = get_or_create_kawase_pipeline(ctx);
    const bool target_srgb = is_srgb_gpu_format(target_fmt);
    SDL_GPUGraphicsPipeline* present_pipe = target_srgb ? nullptr : get_or_create_present_pipeline(ctx, target_fmt);
    if (!scene_pipe || !kawase_pipe || (!target_srgb && !present_pipe)) {
        return false;
    }

    const uint32_t half_w = (target_w > 1u) ? (target_w / 2u) : 1u;
    const uint32_t half_h = (target_h > 1u) ? (target_h / 2u) : 1u;
    const uint32_t quarter_w = (target_w > 3u) ? (target_w / 4u) : 1u;
    const uint32_t quarter_h = (target_h > 3u) ? (target_h / 4u) : 1u;

    for (uint32_t s = 0u; s < ctx->blur_split_count; ++s) {
        const uint32_t prev_batch = (s == 0u) ? 0u : ctx->blur_splits[s - 1u].split_batch;
        const uint32_t split_batch = ctx->blur_splits[s].split_batch;
        const float radius_px = ctx->blur_splits[s].blur_radius_px;

        if (s == 0u || split_batch > prev_batch) {
            if (!run_uber_render_pass(
                    ctx,
                    cmd,
                    ctx->active_ui->scene_tex,
                    kBlurTexFormat,
                    target_w,
                    target_h,
                    scene_pipe,
                    (s == 0u) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
                    clear_color,
                    prev_batch,
                    split_batch))
            {
                return false;
            }
        }

        const float off_down1 = clampf_blur(1.0f + radius_px * 0.04f, 0.5f, 3.0f);
        const float off_down2 = clampf_blur(1.0f + radius_px * 0.06f, 0.5f, 4.0f);
        const float off_up    = clampf_blur(0.6f + radius_px * 0.03f, 0.5f, 2.5f);

        if (!run_kawase_step(
                ctx, cmd, kawase_pipe,
                ctx->active_ui->scene_tex, ctx->active_ui->blur_ping_tex, half_w, half_h,
                1.0f / static_cast<float>(target_w), 1.0f / static_cast<float>(target_h),
                off_down1, 0u))
        {
            return false;
        }
        if (!run_kawase_step(
                ctx, cmd, kawase_pipe,
                ctx->active_ui->blur_ping_tex, ctx->active_ui->blur_pong_tex, quarter_w, quarter_h,
                1.0f / static_cast<float>(half_w), 1.0f / static_cast<float>(half_h),
                off_down2, 0u))
        {
            return false;
        }
        if (!run_kawase_step(
                ctx, cmd, kawase_pipe,
                ctx->active_ui->blur_pong_tex, ctx->active_ui->blur_ping_tex, half_w, half_h,
                1.0f / static_cast<float>(quarter_w), 1.0f / static_cast<float>(quarter_h),
                off_up, 1u))
        {
            return false;
        }
    }

    const uint32_t last_batch = ctx->blur_splits[ctx->blur_split_count - 1u].split_batch;
    if (target_srgb) {
        const SDL_GPUBlitInfo blit = {
            .source = {
                .texture = ctx->active_ui->scene_tex,
                .mip_level = 0u,
                .layer_or_depth_plane = 0u,
                .x = 0u,
                .y = 0u,
                .w = target_w,
                .h = target_h,
            },
            .destination = {
                .texture = target_tex,
                .mip_level = 0u,
                .layer_or_depth_plane = 0u,
                .x = 0u,
                .y = 0u,
                .w = target_w,
                .h = target_h,
            },
            .load_op = SDL_GPU_LOADOP_DONT_CARE,
            .clear_color = {},
            .flip_mode = SDL_FLIP_NONE,
            .filter = SDL_GPU_FILTER_NEAREST,
            .cycle = false,
        };
        SDL_BlitGPUTexture(cmd, &blit);
        return run_uber_render_pass(
            ctx,
            cmd,
            target_tex,
            target_fmt,
            target_w,
            target_h,
            pipeline,
            SDL_GPU_LOADOP_LOAD,
            clear_color,
            last_batch,
            ctx->batch_count);
    }

    if (!run_uber_render_pass(
            ctx,
            cmd,
            ctx->active_ui->scene_tex,
            kBlurTexFormat,
            target_w,
            target_h,
            scene_pipe,
            SDL_GPU_LOADOP_LOAD,
            clear_color,
            last_batch,
            ctx->batch_count))
    {
        return false;
    }
    return run_kawase_step(
        ctx, cmd, present_pipe,
        ctx->active_ui->scene_tex, target_tex, target_w, target_h,
        1.0f / static_cast<float>(target_w), 1.0f / static_cast<float>(target_h),
        0.0f, 2u);
}

bool ensure_headless_resources(Context* ctx, uint32_t width, uint32_t height, uint32_t stride_bytes) noexcept {
    if (!ctx || !ctx->device) {
        return false;
    }

    if (!ctx->headless_tex || ctx->headless_w != width || ctx->headless_h != height) {
        if (ctx->headless_tex) {
            SDL_ReleaseGPUTexture(ctx->device, ctx->headless_tex);
            ctx->headless_tex = nullptr;
        }
        const SDL_GPUTextureCreateInfo tex_info = {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
            .width = width,
            .height = height,
            .layer_count_or_depth = 1u,
            .num_levels = 1u,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0u,
        };
        ctx->headless_tex = SDL_CreateGPUTexture(ctx->device, &tex_info);
        if (!ctx->headless_tex) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUTexture (headless) failed: %s", SDL_GetError());
            return false;
        }
        ctx->headless_w = width;
        ctx->headless_h = height;
    }

    const uint32_t needed_dl = stride_bytes * height;
    if (!ctx->headless_dl_buf || ctx->headless_dl_size < needed_dl) {
        if (ctx->headless_dl_buf) {
            SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->headless_dl_buf);
            ctx->headless_dl_buf = nullptr;
        }
        const SDL_GPUTransferBufferCreateInfo dl_info = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
            .size = needed_dl,
            .props = 0u,
        };
        ctx->headless_dl_buf = SDL_CreateGPUTransferBuffer(ctx->device, &dl_info);
        if (!ctx->headless_dl_buf) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "SDL_CreateGPUTransferBuffer (headless download) failed: %s",
                SDL_GetError());
            return false;
        }
        ctx->headless_dl_size = needed_dl;
    }

    return true;
}

} // namespace

Context* create_context(const ContextDesc& desc) noexcept {
    Context* ctx = static_cast<Context*>(std::calloc(1u, sizeof(Context)));
    if (!ctx) {
        return nullptr;
    }

    ctx->active_ui = &ctx->headless_ui;
    ctx->headless = desc.headless;
    ctx->max_prims = (desc.max_prims_per_frame > 0u) ? desc.max_prims_per_frame : 65536u;
    if (ctx->max_prims > (1u << 20)) {
        ctx->max_prims = (1u << 20);
    }
    ctx->frame_arena = arena_create(desc.frame_arena_bytes ? desc.frame_arena_bytes : (4u * 1024u * 1024u));
    if (!ctx->frame_arena.base) {
        destroy_context(ctx);
        return nullptr;
    }

    if (!state_map_init(&ctx->active_ui->states, kDefaultStateCapacity)) {
        destroy_context(ctx);
        return nullptr;
    }

    ctx->prims = static_cast<Prim*>(std::calloc(ctx->max_prims, sizeof(Prim)));
    if (!ctx->prims) {
        destroy_context(ctx);
        return nullptr;
    }

    if (!font_atlas_init(&ctx->atlas, desc.font_path)) {
        destroy_context(ctx);
        return nullptr;
    }

    // Initialize Clay layout arena cleanly.
    const uint32_t clay_min = Clay_MinMemorySize();
    ctx->active_ui->clay_mem = std::malloc(clay_min);
    if (!ctx->active_ui->clay_mem) {
        destroy_context(ctx);
        return nullptr;
    }
    const Clay_Arena clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_min, ctx->active_ui->clay_mem);
    Clay_Context* prev_clay = Clay_GetCurrentContext();
    ctx->active_ui->clay_ctx = Clay_Initialize(
        clay_arena,
        Clay_Dimensions{ 1280.0f, 720.0f },
        Clay_ErrorHandler{ etch_clay_error_handler, ctx });
    if (ctx->active_ui->clay_ctx) Clay_SetMeasureTextFunction(etch_measure_text, ctx);
    Clay_SetCurrentContext(prev_clay);
    if (!ctx->active_ui->clay_ctx) {
        destroy_context(ctx);
        return nullptr;
    }

    // Initialize SDL3 GPU device (DXIL + SPIR-V).
    const SDL_GPUShaderFormat want_formats = SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV;
    ctx->device = SDL_CreateGPUDevice(want_formats, desc.enable_gpu_debug, nullptr);
    if (!ctx->device && !SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            ctx->owns_video_init = true;
            ctx->device = SDL_CreateGPUDevice(want_formats, desc.enable_gpu_debug, nullptr);
        }
    }
    if (!ctx->device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUDevice failed: %s", SDL_GetError());
        destroy_context(ctx);
        return nullptr;
    }

    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(ctx->device);
    SDL_GPUShaderFormat chosen_fmt = SDL_GPU_SHADERFORMAT_INVALID;
    const Uint8* vs_code = nullptr;
    size_t vs_size = 0;
    const Uint8* ps_code = nullptr;
    size_t ps_size = 0;
    const Uint8* kawase_vs_code = nullptr;
    size_t kawase_vs_size = 0;
    const Uint8* kawase_ps_code = nullptr;
    size_t kawase_ps_size = 0;

    if ((supported & SDL_GPU_SHADERFORMAT_DXIL) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_DXIL;
        vs_code = g_etch_uber_vs_dxil;
        vs_size = sizeof(g_etch_uber_vs_dxil);
        ps_code = g_etch_uber_ps_dxil;
        ps_size = sizeof(g_etch_uber_ps_dxil);
        kawase_vs_code = g_etch_kawase_vs_dxil;
        kawase_vs_size = sizeof(g_etch_kawase_vs_dxil);
        kawase_ps_code = g_etch_kawase_ps_dxil;
        kawase_ps_size = sizeof(g_etch_kawase_ps_dxil);
    } else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_SPIRV;
        vs_code = g_etch_uber_vs_spv;
        vs_size = sizeof(g_etch_uber_vs_spv);
        ps_code = g_etch_uber_ps_spv;
        ps_size = sizeof(g_etch_uber_ps_spv);
        kawase_vs_code = g_etch_kawase_vs_spv;
        kawase_vs_size = sizeof(g_etch_kawase_vs_spv);
        kawase_ps_code = g_etch_kawase_ps_spv;
        kawase_ps_size = sizeof(g_etch_kawase_ps_spv);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No compatible shader format (DXIL/SPIR-V) supported by SDL_GPUDevice");
        destroy_context(ctx);
        return nullptr;
    }

    const SDL_GPUShaderCreateInfo vs_info = {
        .code_size = vs_size,
        .code = vs_code,
        .entrypoint = "vs_main",
        .format = chosen_fmt,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0u,
        .num_storage_textures = 0u,
        .num_storage_buffers = 1u,
        .num_uniform_buffers = 1u,
        .props = 0u,
    };
    ctx->vs_shader = SDL_CreateGPUShader(ctx->device, &vs_info);

    const SDL_GPUShaderCreateInfo ps_info = {
        .code_size = ps_size,
        .code = ps_code,
        .entrypoint = "ps_main",
        .format = chosen_fmt,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 4u,
        .num_storage_textures = 0u,
        .num_storage_buffers = 2u,
        .num_uniform_buffers = 0u,
        .props = 0u,
    };
    ctx->ps_shader = SDL_CreateGPUShader(ctx->device, &ps_info);

    const SDL_GPUShaderCreateInfo kawase_vs_info = {
        .code_size = kawase_vs_size,
        .code = kawase_vs_code,
        .entrypoint = "vs_main",
        .format = chosen_fmt,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0u,
        .num_storage_textures = 0u,
        .num_storage_buffers = 0u,
        .num_uniform_buffers = 0u,
        .props = 0u,
    };
    ctx->kawase_vs_shader = SDL_CreateGPUShader(ctx->device, &kawase_vs_info);

    const SDL_GPUShaderCreateInfo kawase_ps_info = {
        .code_size = kawase_ps_size,
        .code = kawase_ps_code,
        .entrypoint = "ps_main",
        .format = chosen_fmt,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1u,
        .num_storage_textures = 0u,
        .num_storage_buffers = 0u,
        .num_uniform_buffers = 1u,
        .props = 0u,
    };
    ctx->kawase_ps_shader = SDL_CreateGPUShader(ctx->device, &kawase_ps_info);

    if (!ctx->vs_shader || !ctx->ps_shader ||
        !ctx->kawase_vs_shader || !ctx->kawase_ps_shader)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUShader failed: %s", SDL_GetError());
        destroy_context(ctx);
        return nullptr;
    }

    const SDL_GPUSamplerCreateInfo sampler_info = {
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .mip_lod_bias = 0.0f,
        .max_anisotropy = 1.0f,
        .compare_op = SDL_GPU_COMPAREOP_INVALID,
        .min_lod = 0.0f,
        .max_lod = 0.0f,
        .enable_anisotropy = false,
        .enable_compare = false,
        .padding1 = 0u,
        .padding2 = 0u,
        .props = 0u,
    };
    ctx->linear_sampler = SDL_CreateGPUSampler(ctx->device, &sampler_info);
    if (!ctx->linear_sampler) {
        destroy_context(ctx);
        return nullptr;
    }

    const uint32_t prim_buf_bytes = ctx->max_prims * static_cast<uint32_t>(sizeof(Prim));
    const SDL_GPUBufferCreateInfo gpu_buf_info = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = prim_buf_bytes,
        .props = 0u,
    };
    ctx->prim_gpu_buf = SDL_CreateGPUBuffer(ctx->device, &gpu_buf_info);

    const SDL_GPUTransferBufferCreateInfo tbuf_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = prim_buf_bytes,
        .props = 0u,
    };
    ctx->prim_transfer_buf = SDL_CreateGPUTransferBuffer(ctx->device, &tbuf_info);

    const SDL_GPUTextureCreateInfo atlas_tex_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = kAtlasSize,
        .height = kAtlasSize,
        .layer_count_or_depth = 1u,
        .num_levels = 1u,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0u,
    };
    ctx->atlas.gpu_tex = SDL_CreateGPUTexture(ctx->device, &atlas_tex_info);

    const SDL_GPUTransferBufferCreateInfo atlas_tbuf_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = kAtlasSize * kAtlasSize * 4u,
        .props = 0u,
    };
    ctx->atlas_transfer_buf = SDL_CreateGPUTransferBuffer(ctx->device, &atlas_tbuf_info);

    ctx->slug_curve_gpu_cap = kInitialSlugCurveCap;
    const SDL_GPUBufferCreateInfo slug_curve_info = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = ctx->slug_curve_gpu_cap * static_cast<uint32_t>(sizeof(SlugCurveGpu)),
        .props = 0u,
    };
    ctx->slug_curve_gpu_buf = SDL_CreateGPUBuffer(ctx->device, &slug_curve_info);

    ctx->slug_band_gpu_cap = kInitialSlugBandCap;
    const SDL_GPUBufferCreateInfo slug_band_info = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = ctx->slug_band_gpu_cap * static_cast<uint32_t>(sizeof(SlugBandEntryGpu)),
        .props = 0u,
    };
    ctx->slug_band_gpu_buf = SDL_CreateGPUBuffer(ctx->device, &slug_band_info);

    if (!ctx->prim_gpu_buf || !ctx->prim_transfer_buf ||
        !ctx->atlas.gpu_tex || !ctx->atlas_transfer_buf ||
        !ctx->slug_curve_gpu_buf || !ctx->slug_band_gpu_buf)
    {
        destroy_context(ctx);
        return nullptr;
    }

    // Pre-create universal sRGB pipeline for offscreen/SDR_LINEAR rendering.
    get_or_create_pipeline(ctx, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);

    if (desc.window && !claim_window_swapchain(ctx, desc.window)) {
        destroy_context(ctx);
        return nullptr;
    }
    if (desc.window) {
        ctx->headless_ui.redraw_budget = 0;
        ctx->active_ui = window_ui(ctx, desc.window);
        ctx->active_window = desc.window;
        const float init_dpi = SDL_GetWindowDisplayScale(desc.window);
        if (init_dpi > 0.0f) {
            ctx->active_ui->dpi_scale = init_dpi;
        }
    }

    ctx->active_ui->redraw_budget = 2u;
    if (SDL_WasInit(SDL_INIT_EVENTS) || SDL_WasInit(SDL_INIT_VIDEO)) {
        ctx->wake_event_type = SDL_RegisterEvents(1);
    }

    set_current_context(ctx);
    return ctx;
}

void destroy_context(Context* ctx) noexcept {
    if (!ctx) {
        return;
    }
    if (current_context() == ctx) {
        set_current_context(nullptr);
        Clay_SetCurrentContext(nullptr);
    }

    ctx->in_frame = false;
    if (ctx->device) SDL_WaitForGPUIdle(ctx->device);
    for (uint32_t i = 0; i < kMaxClaimedWindows; ++i)
        if (ctx->claimed_windows[i].window) release_window(ctx, ctx->claimed_windows[i].window);
    ctx->active_ui = &ctx->headless_ui;
    if (ctx->device) {
        for (uint32_t i = 0u; i < ctx->pipeline_count; ++i) {
            if (ctx->pipelines[i].pipeline) {
                SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->pipelines[i].pipeline);
            }
        }
        for (uint32_t i = 0u; i < ctx->present_pipeline_count; ++i) {
            if (ctx->present_pipelines[i].pipeline) {
                SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->present_pipelines[i].pipeline);
            }
        }
        if (ctx->kawase_pipe) SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->kawase_pipe);
        if (ctx->vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->vs_shader);
        if (ctx->ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->ps_shader);
        if (ctx->kawase_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->kawase_vs_shader);
        if (ctx->kawase_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->kawase_ps_shader);
        if (ctx->linear_sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->linear_sampler);
        if (ctx->prim_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->prim_gpu_buf);
        if (ctx->prim_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf);
        if (ctx->slug_curve_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->slug_curve_gpu_buf);
        if (ctx->slug_band_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->slug_band_gpu_buf);
        if (ctx->slug_curve_tbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->slug_curve_tbuf);
        if (ctx->slug_band_tbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->slug_band_tbuf);
        if (ctx->atlas_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->atlas_transfer_buf);
        if (ctx->atlas.gpu_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->atlas.gpu_tex);
        release_blur_targets(ctx);
        if (ctx->headless_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->headless_tex);
        if (ctx->headless_dl_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->headless_dl_buf);
        SDL_DestroyGPUDevice(ctx->device);
        ctx->device = nullptr;
    }

    if (ctx->active_ui->mouse_captured) {
        SDL_CaptureMouse(false);
        ctx->active_ui->mouse_captured = false;
    }
    for (uint32_t i = 0u; i < kCursorCount; ++i) {
        if (ctx->system_cursors[i]) {
            SDL_DestroyCursor(ctx->system_cursors[i]);
            ctx->system_cursors[i] = nullptr;
        }
    }

    font_atlas_destroy(&ctx->atlas);
    std::free(ctx->clipboard_fallback);
    ctx->clipboard_fallback = nullptr;
    ctx->clipboard_fallback_cap = 0u;
    std::free(ctx->prims);
    std::free(ctx->active_ui->clay_mem);
    state_map_destroy(&ctx->active_ui->states);
    arena_destroy(&ctx->frame_arena);

    if (ctx->owns_video_init) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    std::free(ctx);
}

bool render_window(Context* ctx, Color clear_color, SDL_Window* window_override) noexcept {
    if (!ctx || !ctx->device) {
        return false;
    }
    SDL_Window* target_win = window_override ? window_override : (ctx->draw_list_window ? ctx->draw_list_window : ctx->window);
    if (ctx->in_frame || (ctx->draw_list_window && target_win != ctx->draw_list_window)) return false;
    if (!target_win || !claim_window_swapchain(ctx, target_win)) {
        return false;
    }

    const uint64_t t0 = SDL_GetPerformanceCounter();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        return false;
    }

    SDL_GPUTexture* swap_tex = nullptr;
    Uint32 swap_w = 0u;
    Uint32 swap_h = 0u;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, target_win, &swap_tex, &swap_w, &swap_h)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }

    if (!swap_tex || swap_w == 0u || swap_h == 0u) {
        // Window is minimized or hidden; submit empty command buffer cleanly.
        SDL_SubmitGPUCommandBuffer(cmd);
        return true;
    }

    const SDL_GPUTextureFormat swap_fmt = SDL_GetGPUSwapchainTextureFormat(ctx->device, target_win);
    if (!record_frame_commands(ctx, cmd, swap_tex, swap_fmt, swap_w, swap_h, clear_color)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return false;
    }

    const bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
    const uint64_t t1 = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    ctx->stats.draw_call_count = ctx->batch_count;
    ctx->stats.gpu_submit_ms = (freq > 0u)
        ? (static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(freq))
        : 0.0;
    return submitted;
}

bool render_headless_png(
    Context* ctx,
    uint32_t width,
    uint32_t height,
    const char* out_png_path,
    Color clear_color) noexcept
{
    if (!ctx || !ctx->device || width == 0u || height == 0u || !out_png_path || out_png_path[0] == '\0') {
        return false;
    }

    // Align row pitch to 64 pixels (256 bytes) to satisfy D3D12 copy pitch alignment without extra copies.
    const uint32_t aligned_pixels_per_row = (width + 63u) & ~63u;
    const uint32_t stride_bytes = aligned_pixels_per_row * 4u;
    if (!ensure_headless_resources(ctx, width, height, stride_bytes)) {
        return false;
    }

    const uint64_t t0 = SDL_GetPerformanceCounter();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        return false;
    }

    if (!record_frame_commands(
            ctx,
            cmd,
            ctx->headless_tex,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
            width,
            height,
            clear_color))
    {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }

    SDL_GPUCopyPass* dl_pass = SDL_BeginGPUCopyPass(cmd);
    if (!dl_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }

    const SDL_GPUTextureRegion src_reg = {
        .texture = ctx->headless_tex,
        .mip_level = 0u,
        .layer = 0u,
        .x = 0u,
        .y = 0u,
        .z = 0u,
        .w = width,
        .h = height,
        .d = 1u,
    };
    const SDL_GPUTextureTransferInfo dst_info = {
        .transfer_buffer = ctx->headless_dl_buf,
        .offset = 0u,
        .pixels_per_row = aligned_pixels_per_row,
        .rows_per_layer = height,
    };
    SDL_DownloadFromGPUTexture(dl_pass, &src_reg, &dst_info);
    SDL_EndGPUCopyPass(dl_pass);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) {
        return false;
    }
    const bool waited = SDL_WaitForGPUFences(ctx->device, true, &fence, 1u);
    SDL_ReleaseGPUFence(ctx->device, fence);
    if (!waited) {
        return false;
    }

    void* pixels = SDL_MapGPUTransferBuffer(ctx->device, ctx->headless_dl_buf, false);
    if (!pixels) {
        return false;
    }
    const int wrote = stbi_write_png(
        out_png_path,
        static_cast<int>(width),
        static_cast<int>(height),
        4,
        pixels,
        static_cast<int>(stride_bytes));
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->headless_dl_buf);

    const uint64_t t1 = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    ctx->stats.draw_call_count = ctx->batch_count;
    ctx->stats.gpu_submit_ms = (freq > 0u)
        ? (static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(freq))
        : 0.0;

    return wrote != 0;
}

namespace {

bool upload_rgba_rows(
    Context* ctx,
    SDL_GPUTexture* texture,
    uint32_t width,
    uint32_t height,
    const void* rgba_pixels,
    uint32_t row_pitch_bytes,
    bool cycle) noexcept
{
    const uint32_t tight_pitch = width * 4u;
    const uint32_t src_pitch = (row_pitch_bytes != 0u) ? row_pitch_bytes : tight_pitch;
    const uint32_t total_bytes = tight_pitch * height;

    const SDL_GPUTransferBufferCreateInfo tb_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = total_bytes,
        .props = 0u,
    };
    SDL_GPUTransferBuffer* tbuf = SDL_CreateGPUTransferBuffer(ctx->device, &tb_info);
    if (!tbuf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUTransferBuffer (upload) failed: %s", SDL_GetError());
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(ctx->device, tbuf, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(ctx->device, tbuf);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_MapGPUTransferBuffer (upload) failed: %s", SDL_GetError());
        return false;
    }

    if (src_pitch == tight_pitch) {
        std::memcpy(mapped, rgba_pixels, total_bytes);
    } else {
        uint8_t* dst = static_cast<uint8_t*>(mapped);
        const uint8_t* src = static_cast<const uint8_t*>(rgba_pixels);
        for (uint32_t y = 0u; y < height; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * tight_pitch,
                        src + static_cast<size_t>(y) * src_pitch,
                        tight_pitch);
        }
    }
    SDL_UnmapGPUTransferBuffer(ctx->device, tbuf);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(ctx->device, tbuf);
        return false;
    }

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    if (!copy) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(ctx->device, tbuf);
        return false;
    }

    const SDL_GPUTextureTransferInfo src_info = {
        .transfer_buffer = tbuf,
        .offset = 0u,
        .pixels_per_row = width,
        .rows_per_layer = height,
    };
    const SDL_GPUTextureRegion dst_reg = {
        .texture = texture,
        .mip_level = 0u,
        .layer = 0u,
        .x = 0u,
        .y = 0u,
        .z = 0u,
        .w = width,
        .h = height,
        .d = 1u,
    };
    SDL_UploadToGPUTexture(copy, &src_info, &dst_reg, cycle);
    SDL_EndGPUCopyPass(copy);

    const bool ok = SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(ctx->device, tbuf);
    return ok;
}

LoadedTexture upload_sdl_surface(Context* ctx, SDL_Surface* surf, bool srgb, const char* debug_name) noexcept {
    if (!surf || surf->w <= 0 || surf->h <= 0 || !surf->pixels) {
        return {};
    }

    SDL_Surface* rgba_surf = surf;
    if (surf->format != SDL_PIXELFORMAT_RGBA32) {
        rgba_surf = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        if (!rgba_surf) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_ConvertSurface RGBA32 failed: %s", SDL_GetError());
            return {};
        }
    }

    const LoadedTexture res = create_texture_rgba(ctx, TextureDesc{
        .rgba_pixels = rgba_surf->pixels,
        .width = static_cast<uint32_t>(rgba_surf->w),
        .height = static_cast<uint32_t>(rgba_surf->h),
        .row_pitch_bytes = static_cast<uint32_t>(rgba_surf->pitch),
        .srgb = srgb,
        .debug_name = debug_name,
    });

    if (rgba_surf != surf) {
        SDL_DestroySurface(rgba_surf);
    }
    return res;
}

} // namespace

void release_window(Context* ctx, SDL_Window* window) noexcept {
    if (!ctx || !window || ctx->in_frame) return;
    for (uint32_t i = 0; i < kMaxClaimedWindows; ++i) {
        ClaimedWindowSlot& slot = ctx->claimed_windows[i];
        if (slot.window != window) continue;
        if (ctx->ime_active_window == window) {
            SDL_StopTextInput(window); ctx->ime_active_window = nullptr;
        }
        if (ctx->mouse_capture_window == window) {
            SDL_CaptureMouse(false); ctx->mouse_capture_window = nullptr;
        }
        if (slot.hit_test_attached) SDL_SetWindowHitTest(window, nullptr, nullptr);
        WindowUiState* saved = ctx->active_ui;
        ctx->active_ui = &slot.ui;
        release_blur_targets(ctx);
        ctx->active_ui = saved == &slot.ui ? &ctx->headless_ui : saved;
        std::free(slot.ui.clay_mem);
        state_map_destroy(&slot.ui.states);
        SDL_ReleaseWindowFromGPUDevice(ctx->device, window);
        slot.window = nullptr;
        slot.hit_test_attached = false;
        slot.ui = WindowUiState{};
        --ctx->claimed_window_count;
        if (ctx->active_window == window) ctx->active_window = nullptr;
        if (ctx->draw_list_window == window) ctx->draw_list_window = nullptr;
        if (ctx->window == window) {
            ctx->window = nullptr;
            for (uint32_t j = 0; j < kMaxClaimedWindows; ++j)
                if (ctx->claimed_windows[j].window) { ctx->window = ctx->claimed_windows[j].window; break; }
        }
        if (current_context() == ctx) Clay_SetCurrentContext(ctx->active_ui->clay_ctx);
        return;
    }
}

uint32_t claimed_window_count(const Context* ctx) noexcept {
    return ctx ? ctx->claimed_window_count : 0u;
}

LoadedTexture create_texture_rgba(Context* ctx, const TextureDesc& desc) noexcept {
    if (!ctx || !ctx->device || desc.width == 0u || desc.height == 0u) {
        return {};
    }
    const uint32_t tight_pitch = desc.width * 4u;
    if (desc.rgba_pixels != nullptr && desc.row_pitch_bytes != 0u && desc.row_pitch_bytes < tight_pitch) {
        return {};
    }

    const SDL_GPUTextureCreateInfo tex_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = desc.srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = desc.width,
        .height = desc.height,
        .layer_count_or_depth = 1u,
        .num_levels = 1u,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0u,
    };
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(ctx->device, &tex_info);
    if (!tex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return {};
    }

    if (desc.rgba_pixels != nullptr) {
        if (!upload_rgba_rows(ctx, tex, desc.width, desc.height, desc.rgba_pixels, desc.row_pitch_bytes, false)) {
            SDL_ReleaseGPUTexture(ctx->device, tex);
            return {};
        }
    }

    return LoadedTexture{
        .texture = tex,
        .width = desc.width,
        .height = desc.height,
    };
}

bool update_texture_rgba(
    Context* ctx,
    SDL_GPUTexture* texture,
    uint32_t width,
    uint32_t height,
    const void* rgba_pixels,
    uint32_t row_pitch_bytes) noexcept
{
    if (!ctx || !ctx->device || !texture || width == 0u || height == 0u || !rgba_pixels) {
        return false;
    }
    const uint32_t tight_pitch = width * 4u;
    if (row_pitch_bytes != 0u && row_pitch_bytes < tight_pitch) {
        return false;
    }
    return upload_rgba_rows(ctx, texture, width, height, rgba_pixels, row_pitch_bytes, true);
}

LoadedTexture load_texture_file(Context* ctx, const char* path, bool srgb) noexcept {
    if (!ctx || !ctx->device || !path || path[0] == '\0') {
        return {};
    }
    SDL_Surface* surf = SDL_LoadPNG(path);
    if (!surf) {
        surf = SDL_LoadBMP(path);
    }
    if (!surf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "load_texture_file('%s') failed: %s", path, SDL_GetError());
        return {};
    }
    const LoadedTexture res = upload_sdl_surface(ctx, surf, srgb, path);
    SDL_DestroySurface(surf);
    return res;
}

LoadedTexture load_texture_memory(Context* ctx, const void* data, size_t size_bytes, bool srgb) noexcept {
    if (!ctx || !ctx->device || !data || size_bytes == 0u) {
        return {};
    }
    SDL_IOStream* io = SDL_IOFromConstMem(data, size_bytes);
    if (!io) {
        return {};
    }
    SDL_Surface* surf = SDL_LoadPNG_IO(io, false);
    if (!surf) {
        SDL_SeekIO(io, 0, SDL_IO_SEEK_SET);
        surf = SDL_LoadBMP_IO(io, false);
    }
    SDL_CloseIO(io);
    if (!surf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "load_texture_memory failed: %s", SDL_GetError());
        return {};
    }
    const LoadedTexture res = upload_sdl_surface(ctx, surf, srgb, nullptr);
    SDL_DestroySurface(surf);
    return res;
}

void destroy_texture(Context* ctx, LoadedTexture* tex) noexcept {
    if (!tex) {
        return;
    }
    if (ctx && ctx->device && tex->texture) {
        SDL_ReleaseGPUTexture(ctx->device, tex->texture);
    }
    tex->texture = nullptr;
    tex->width = 0u;
    tex->height = 0u;
}

void destroy_texture(Context* ctx, SDL_GPUTexture* tex) noexcept {
    if (ctx && ctx->device && tex) {
        SDL_ReleaseGPUTexture(ctx->device, tex);
    }
}

} // namespace etch
