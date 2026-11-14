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

SDL_GPUGraphicsPipeline* get_or_create_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    if (!ctx || !ctx->device || !ctx->vs_shader || !ctx->ps_shader ||
        target_fmt == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        return nullptr;
    }

    for (uint32_t i = 0u; i < ctx->pipeline_count; ++i) {
        if (ctx->pipelines[i].format == target_fmt) {
            return ctx->pipelines[i].pipeline;
        }
    }
    if (ctx->pipeline_count >= kMaxCachedPipelines) {
        return nullptr;
    }

    SDL_GPUColorTargetBlendState blend = {};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
        SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    const SDL_GPUColorTargetDescription target_desc = {
        .format = target_fmt,
        .blend_state = blend,
    };

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = ctx->vs_shader;
    info.fragment_shader = ctx->ps_shader;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = false;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &target_desc;
    info.target_info.num_color_targets = 1u;
    info.target_info.has_depth_stencil_target = false;

    SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(ctx->device, &info);
    if (!pipe) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return nullptr;
    }

    ctx->pipelines[ctx->pipeline_count++] = CachedPipeline{
        .format = target_fmt,
        .pipeline = pipe,
    };
    return pipe;
}

bool claim_window_swapchain(Context* ctx, SDL_Window* win) noexcept {
    if (!ctx || !ctx->device || !win) {
        return false;
    }
    if (ctx->window == win) {
        return true;
    }
    if (ctx->window != nullptr) {
        SDL_ReleaseWindowFromGPUDevice(ctx->device, ctx->window);
        ctx->window = nullptr;
    }
    if (!SDL_ClaimWindowForGPUDevice(ctx->device, win)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    ctx->window = win;

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

    if (ctx->atlas.dirty || ctx->prim_count > 0u) {
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
        if (!copy_pass) {
            return false;
        }

        if (ctx->atlas.dirty && ctx->atlas.gpu_tex && ctx->atlas_transfer_buf) {
            const uint32_t y0 = ctx->atlas.dirty_y0;
            const uint32_t y1 = (ctx->atlas.dirty_y1 <= kAtlasSize) ? ctx->atlas.dirty_y1 : kAtlasSize;
            if (y1 > y0) {
                const uint32_t row_count = y1 - y0;
                const size_t row_stride = size_t(kAtlasSize) * 4u;
                void* mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->atlas_transfer_buf, true);
                if (mapped) {
                    std::memcpy(mapped, ctx->atlas.pixels + size_t(y0) * row_stride, size_t(row_count) * row_stride);
                    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->atlas_transfer_buf);

                    const SDL_GPUTextureTransferInfo src = {
                        .transfer_buffer = ctx->atlas_transfer_buf,
                        .offset = 0u,
                        .pixels_per_row = kAtlasSize,
                        .rows_per_layer = row_count,
                    };
                    const SDL_GPUTextureRegion dst = {
                        .texture = ctx->atlas.gpu_tex,
                        .mip_level = 0u,
                        .layer = 0u,
                        .x = 0u,
                        .y = y0,
                        .z = 0u,
                        .w = kAtlasSize,
                        .h = row_count,
                        .d = 1u,
                    };
                    SDL_UploadToGPUTexture(copy_pass, &src, &dst, false);
                }
            }
            ctx->atlas.dirty = false;
            ctx->atlas.dirty_y0 = 0u;
            ctx->atlas.dirty_y1 = 0u;
        }

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

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = target_tex;
    color_target.clear_color = make_clear_fcolor(clear_color, target_fmt);
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmd, &color_target, 1u, nullptr);
    if (!render_pass) {
        return false;
    }

    if (ctx->prim_count > 0u && ctx->batch_count > 0u) {
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

        const uint32_t manual_srgb_bit = is_srgb_gpu_format(target_fmt) ? 0u : (1u << 31);
        const float inv_w = 1.0f / static_cast<float>(target_w);
        const float inv_h = 1.0f / static_cast<float>(target_h);

        for (uint32_t b = 0u; b < ctx->batch_count; ++b) {
            const DrawBatch& batch = ctx->batches[b];
            if (batch.prim_count == 0u) {
                continue;
            }

            SDL_GPUTextureSamplerBinding bindings[4] = {
                { ctx->atlas.gpu_tex, ctx->linear_sampler },
                { batch.ext_textures[0] ? batch.ext_textures[0] : ctx->atlas.gpu_tex, ctx->linear_sampler },
                { batch.ext_textures[1] ? batch.ext_textures[1] : ctx->atlas.gpu_tex, ctx->linear_sampler },
                { batch.ext_textures[2] ? batch.ext_textures[2] : ctx->atlas.gpu_tex, ctx->linear_sampler },
            };
            SDL_BindGPUFragmentSamplers(render_pass, 0u, bindings, 4u);

            const GlobalsUniform globals = {
                .viewport_size = { static_cast<float>(target_w), static_cast<float>(target_h) },
                .inv_viewport_size = { inv_w, inv_h },
                .dpi_scale = ctx->dpi_scale,
                .time_seconds = ctx->elapsed_seconds,
                .frame_index = ctx->frame_index,
                .flags = (batch.first_prim & 0x00FFFFFFu) | manual_srgb_bit,
            };
            SDL_PushGPUVertexUniformData(cmd, 0u, &globals, static_cast<Uint32>(sizeof(globals)));

            // Portable across D3D12/Vulkan/Metal: always pass 0 for first_vertex & first_instance.
            SDL_DrawGPUPrimitives(render_pass, 6u, batch.prim_count, 0u, 0u);
        }
    }

    SDL_EndGPURenderPass(render_pass);
    return true;
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

    if (!state_map_init(&ctx->states, kDefaultStateCapacity)) {
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
    ctx->clay_mem = std::malloc(clay_min);
    if (!ctx->clay_mem) {
        destroy_context(ctx);
        return nullptr;
    }
    const Clay_Arena clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_min, ctx->clay_mem);
    ctx->clay_ctx = Clay_Initialize(
        clay_arena,
        Clay_Dimensions{ 1280.0f, 720.0f },
        Clay_ErrorHandler{ etch_clay_error_handler, ctx });
    if (!ctx->clay_ctx) {
        destroy_context(ctx);
        return nullptr;
    }
    Clay_SetCurrentContext(ctx->clay_ctx);
    Clay_SetMeasureTextFunction(etch_measure_text, ctx);

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

    if ((supported & SDL_GPU_SHADERFORMAT_DXIL) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_DXIL;
        vs_code = g_etch_uber_vs_dxil;
        vs_size = sizeof(g_etch_uber_vs_dxil);
        ps_code = g_etch_uber_ps_dxil;
        ps_size = sizeof(g_etch_uber_ps_dxil);
    } else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_SPIRV;
        vs_code = g_etch_uber_vs_spv;
        vs_size = sizeof(g_etch_uber_vs_spv);
        ps_code = g_etch_uber_ps_spv;
        ps_size = sizeof(g_etch_uber_ps_spv);
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
        .num_storage_buffers = 0u,
        .num_uniform_buffers = 0u,
        .props = 0u,
    };
    ctx->ps_shader = SDL_CreateGPUShader(ctx->device, &ps_info);

    if (!ctx->vs_shader || !ctx->ps_shader) {
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

    if (!ctx->prim_gpu_buf || !ctx->prim_transfer_buf ||
        !ctx->atlas.gpu_tex || !ctx->atlas_transfer_buf)
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
        const float init_dpi = SDL_GetWindowDisplayScale(desc.window);
        if (init_dpi > 0.0f) {
            ctx->dpi_scale = init_dpi;
        }
    }

    ctx->redraw_budget = 2u;
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
    }

    if (ctx->ime_input_started && !ctx->headless && ctx->window) {
        SDL_ClearComposition(ctx->window);
        SDL_SetTextInputArea(ctx->window, nullptr, 0);
        SDL_StopTextInput(ctx->window);
        ctx->ime_input_started = false;
    }

    if (ctx->device) {
        SDL_WaitForGPUIdle(ctx->device);

        for (uint32_t i = 0u; i < ctx->pipeline_count; ++i) {
            if (ctx->pipelines[i].pipeline) {
                SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->pipelines[i].pipeline);
            }
        }
        if (ctx->vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->vs_shader);
        if (ctx->ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->ps_shader);
        if (ctx->linear_sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->linear_sampler);
        if (ctx->prim_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->prim_gpu_buf);
        if (ctx->prim_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf);
        if (ctx->atlas_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->atlas_transfer_buf);
        if (ctx->atlas.gpu_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->atlas.gpu_tex);
        if (ctx->headless_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->headless_tex);
        if (ctx->headless_dl_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->headless_dl_buf);
        if (ctx->window) SDL_ReleaseWindowFromGPUDevice(ctx->device, ctx->window);

        SDL_DestroyGPUDevice(ctx->device);
        ctx->device = nullptr;
    }

    if (ctx->mouse_captured) {
        SDL_CaptureMouse(false);
        ctx->mouse_captured = false;
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
    std::free(ctx->clay_mem);
    state_map_destroy(&ctx->states);
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
    SDL_Window* target_win = window_override ? window_override : ctx->window;
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

} // namespace etch
