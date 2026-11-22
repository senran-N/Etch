// SDL3_GPU device lifecycle, pipeline cache, batched Prim submission, and PNG readback.
// Compiled via src/etch_unity.cpp.

#include <cmath>

namespace etch {

namespace {

bool ensure_gpu_device(Context* ctx) noexcept;

constexpr bool is_srgb_gpu_format(SDL_GPUTextureFormat fmt) noexcept {
    return fmt == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB ||
           fmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
}

constexpr bool is_linear_float_format(SDL_GPUTextureFormat fmt) noexcept {
    return fmt == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT ||
           fmt == SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
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
inline constexpr SDL_GPUTextureFormat kFilterTexFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;

struct alignas(16) KawaseUniform {
    float    texel_size[2];
    float    offset;
    uint32_t mode;
    Vec4 bloom;
};
static_assert(sizeof(KawaseUniform) == 32);
static_assert(sizeof(KawaseUniform) % 16 == 0);

inline float clampf_blur(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

enum class BlendMode : uint8_t { Opaque, PremulAlpha, SubpixelRgb, PunchHole, Additive };
enum class DepthMode : uint8_t { None, WriteLess, TestLess };

SDL_GPUGraphicsPipeline* create_graphics_pipeline(
    SDL_GPUDevice* device,
    SDL_GPUShader* vs,
    SDL_GPUShader* ps,
    SDL_GPUTextureFormat target_fmt,
    BlendMode blend_mode,
    DepthMode depth_mode = DepthMode::None) noexcept
{
    SDL_GPUColorTargetBlendState blend = {};
    blend.enable_blend = (blend_mode != BlendMode::Opaque);
    switch (blend_mode) {
        case BlendMode::Opaque:
            break;
        case BlendMode::PremulAlpha:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
        case BlendMode::SubpixelRgb:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
        case BlendMode::PunchHole:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
        case BlendMode::Additive:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
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
    if (depth_mode != DepthMode::None) {
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = (depth_mode == DepthMode::WriteLess);
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    } else {
        info.target_info.has_depth_stencil_target = false;
    }

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
    BlendMode blend_mode,
    CachedPipeline* cache,
    uint32_t* count,
    DepthMode depth_mode = DepthMode::None) noexcept
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
    SDL_GPUGraphicsPipeline* pipe = create_graphics_pipeline(device, vs, ps, target_fmt, blend_mode, depth_mode);
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
    if (!ctx) return nullptr;
    return get_or_create_cached_pipeline(
        ctx->device, ctx->vs_shader, ctx->ps_shader, target_fmt,
        BlendMode::PremulAlpha, ctx->pipelines, &ctx->pipeline_count);
}

SDL_GPUGraphicsPipeline* get_or_create_uber_depth_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    return get_or_create_cached_pipeline(
        ctx->device, ctx->vs_shader, ctx->ps_shader, target_fmt,
        BlendMode::PremulAlpha, ctx->uber_depth_pipelines, &ctx->uber_depth_pipeline_count, DepthMode::TestLess);
}

SDL_GPUGraphicsPipeline* get_or_create_quad_opaque_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    return get_or_create_cached_pipeline(
        ctx->device, ctx->quad_vs_shader, ctx->quad_ps_shader, target_fmt,
        BlendMode::Opaque, ctx->quad_opaque_pipelines, &ctx->quad_opaque_pipeline_count, DepthMode::WriteLess);
}

SDL_GPUGraphicsPipeline* get_or_create_quad_alpha_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    return get_or_create_cached_pipeline(
        ctx->device, ctx->quad_vs_shader, ctx->quad_ps_shader, target_fmt,
        BlendMode::PremulAlpha, ctx->quad_alpha_pipelines, &ctx->quad_alpha_pipeline_count);
}

SDL_GPUGraphicsPipeline* get_or_create_quad_alpha_depth_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    return get_or_create_cached_pipeline(
        ctx->device, ctx->quad_vs_shader, ctx->quad_ps_shader, target_fmt,
        BlendMode::PremulAlpha, ctx->quad_alpha_depth_pipelines, &ctx->quad_alpha_depth_pipeline_count, DepthMode::TestLess);
}

SDL_GPUGraphicsPipeline* get_or_create_subpixel_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    return get_or_create_cached_pipeline(
        ctx->device, ctx->subpixel_vs_shader, ctx->subpixel_ps_shader, target_fmt,
        BlendMode::SubpixelRgb, ctx->subpixel_pipelines, &ctx->subpixel_pipeline_count);
}

SDL_GPUGraphicsPipeline* get_or_create_subpixel_depth_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    return get_or_create_cached_pipeline(
        ctx->device, ctx->subpixel_vs_shader, ctx->subpixel_ps_shader, target_fmt,
        BlendMode::SubpixelRgb, ctx->subpixel_depth_pipelines, &ctx->subpixel_depth_pipeline_count, DepthMode::TestLess);
}

SDL_GPUGraphicsPipeline* get_or_create_punch_pipeline(Context* ctx, SDL_GPUTextureFormat format) noexcept {
    return get_or_create_cached_pipeline(
        ctx->device, ctx->vs_shader, ctx->ps_shader, format,
        BlendMode::PunchHole, ctx->punch_pipelines, &ctx->punch_pipeline_count);
}

SDL_GPUGraphicsPipeline* get_or_create_additive_pipeline(Context* ctx, SDL_GPUTextureFormat format) noexcept {
    return get_or_create_cached_pipeline(
        ctx->device, ctx->vs_shader, ctx->ps_shader, format,
        BlendMode::Additive, ctx->additive_pipelines, &ctx->additive_pipeline_count);
}

SDL_GPUGraphicsPipeline* get_or_create_additive_depth_pipeline(Context* ctx, SDL_GPUTextureFormat format) noexcept {
    return get_or_create_cached_pipeline(
        ctx->device, ctx->vs_shader, ctx->ps_shader, format,
        BlendMode::Additive, ctx->additive_depth_pipelines, &ctx->additive_depth_pipeline_count, DepthMode::TestLess);
}

SDL_GPUGraphicsPipeline* get_batch_pipeline(
    Context* ctx, GpuPipelineKind kind, SDL_GPUTextureFormat fmt, bool use_depth) noexcept
{
    switch (kind) {
        case GpuPipelineKind::QuadOpaque:
            return get_or_create_quad_opaque_pipeline(ctx, fmt);
        case GpuPipelineKind::QuadAlpha:
            return use_depth ? get_or_create_quad_alpha_depth_pipeline(ctx, fmt)
                             : get_or_create_quad_alpha_pipeline(ctx, fmt);
        case GpuPipelineKind::SubpixelText:
            return use_depth ? get_or_create_subpixel_depth_pipeline(ctx, fmt)
                             : get_or_create_subpixel_pipeline(ctx, fmt);
        case GpuPipelineKind::UberPunch:
            return get_or_create_punch_pipeline(ctx, fmt);
        case GpuPipelineKind::UberAdditive:
            return use_depth ? get_or_create_additive_depth_pipeline(ctx, fmt)
                             : get_or_create_additive_pipeline(ctx, fmt);
        case GpuPipelineKind::Uber:
        default:
            return use_depth ? get_or_create_uber_depth_pipeline(ctx, fmt)
                             : get_or_create_pipeline(ctx, fmt);
    }
}

SDL_GPUGraphicsPipeline* get_or_create_present_pipeline(Context* ctx, SDL_GPUTextureFormat target_fmt) noexcept {
    if (!ctx) return nullptr;
    return get_or_create_cached_pipeline(
        ctx->device, ctx->kawase_vs_shader, ctx->kawase_ps_shader, target_fmt,
        BlendMode::Opaque, ctx->present_pipelines, &ctx->present_pipeline_count);
}

SDL_GPUGraphicsPipeline* get_or_create_kawase_pipeline(Context* ctx) noexcept {
    if (!ctx->kawase_pipe) {
        ctx->kawase_pipe = create_graphics_pipeline(
            ctx->device, ctx->kawase_vs_shader, ctx->kawase_ps_shader, kBlurTexFormat, BlendMode::Opaque);
    }
    return ctx->kawase_pipe;
}

SDL_GPUGraphicsPipeline* get_or_create_filter_kawase_pipeline(Context* ctx) noexcept {
    if (!ctx->filter_kawase_pipe) {
        ctx->filter_kawase_pipe = create_graphics_pipeline(
            ctx->device, ctx->kawase_vs_shader, ctx->kawase_ps_shader, kFilterTexFormat, BlendMode::Opaque);
    }
    return ctx->filter_kawase_pipe;
}

void release_depth_target(Context* ctx) noexcept {
    if (ctx->active_ui->depth_tex) {
        SDL_ReleaseGPUTexture(ctx->device, ctx->active_ui->depth_tex);
        ctx->active_ui->depth_tex = nullptr;
    }
    ctx->active_ui->depth_w = 0u;
    ctx->active_ui->depth_h = 0u;
    ctx->active_ui->depth_fmt = SDL_GPU_TEXTUREFORMAT_INVALID;
}

bool ensure_depth_target(Context* ctx, uint32_t width, uint32_t height) noexcept {
    if (ctx->active_ui->depth_tex && ctx->active_ui->depth_w == width && ctx->active_ui->depth_h == height) {
        return true;
    }
    release_depth_target(ctx);
    const SDL_GPUTextureCreateInfo info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1u,
        .num_levels = 1u,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0u,
    };
    ctx->active_ui->depth_tex = SDL_CreateGPUTexture(ctx->device, &info);
    if (!ctx->active_ui->depth_tex) return false;
    ctx->active_ui->depth_w = width;
    ctx->active_ui->depth_h = height;
    ctx->active_ui->depth_fmt = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    return true;
}

void release_filter_targets(Context* ctx) noexcept {
    for (uint32_t s = 0u; s < kMaxFilterSlices; ++s) {
        for (uint32_t l = 0u; l < kMaxFilterLevels; ++l) {
            if (ctx->active_ui->filter_slices[s].levels[l]) {
                SDL_ReleaseGPUTexture(ctx->device, ctx->active_ui->filter_slices[s].levels[l]);
                ctx->active_ui->filter_slices[s].levels[l] = nullptr;
            }
        }
    }
    ctx->active_ui->filter_rt_w = 0u;
    ctx->active_ui->filter_rt_h = 0u;
    ctx->active_ui->filter_idle_frames = 0u;
}

bool ensure_filter_slice_targets(Context* ctx, uint32_t width, uint32_t height, uint32_t slice, uint32_t max_level) noexcept {
    if (ctx->active_ui->filter_rt_w != width || ctx->active_ui->filter_rt_h != height) {
        release_filter_targets(ctx);
        ctx->active_ui->filter_rt_w = width;
        ctx->active_ui->filter_rt_h = height;
    }
    FilterSliceTargets& st = ctx->active_ui->filter_slices[slice];
    for (uint32_t lvl = 0u; lvl <= max_level && lvl < kMaxFilterLevels; ++lvl) {
        if (st.levels[lvl]) continue;
        const uint32_t lw = (width >> lvl) > 0u ? (width >> lvl) : 1u;
        const uint32_t lh = (height >> lvl) > 0u ? (height >> lvl) : 1u;
        const SDL_GPUTextureCreateInfo info = {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = kFilterTexFormat,
            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = lw,
            .height = lh,
            .layer_count_or_depth = 1u,
            .num_levels = 1u,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0u,
        };
        st.levels[lvl] = SDL_CreateGPUTexture(ctx->device, &info);
        if (!st.levels[lvl]) return false;
    }
    return true;
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

bool ensure_prim_buffers(Context* ctx, uint32_t needed_count) noexcept {
    if (needed_count == 0u) return true;
    if (needed_count > kMaxPrimHardCap) return false;
    if (!ctx->prim_gpu_buf || ctx->prim_gpu_cap < needed_count) {
        uint32_t cap = ctx->prim_gpu_cap >= kMinPrimCap ? ctx->prim_gpu_cap : kMinPrimCap;
        while (cap < needed_count) cap = cap > kMaxPrimHardCap / 2u ? kMaxPrimHardCap : cap * 2u;
        const SDL_GPUBufferCreateInfo info = { .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, .size = cap * static_cast<uint32_t>(sizeof(Prim)), .props = 0u };
        SDL_GPUBuffer* next = SDL_CreateGPUBuffer(ctx->device, &info);
        if (!next) return false;
        if (ctx->prim_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->prim_gpu_buf);
        ctx->prim_gpu_buf = next;
        ctx->prim_gpu_cap = cap;
    }
    if (!ctx->prim_transfer_buf || ctx->prim_tbuf_cap < needed_count) {
        uint32_t cap = ctx->prim_tbuf_cap >= kMinPrimCap ? ctx->prim_tbuf_cap : kMinPrimCap;
        while (cap < needed_count) cap = cap > kMaxPrimHardCap / 2u ? kMaxPrimHardCap : cap * 2u;
        const SDL_GPUTransferBufferCreateInfo info = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = cap * static_cast<uint32_t>(sizeof(Prim)), .props = 0u };
        SDL_GPUTransferBuffer* next = SDL_CreateGPUTransferBuffer(ctx->device, &info);
        if (!next) return false;
        if (ctx->prim_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf);
        ctx->prim_transfer_buf = next;
        ctx->prim_tbuf_cap = cap;
    }
    return true;
}

bool ensure_prim_depth_buffers(Context* ctx, uint32_t needed_count) noexcept {
    if (needed_count == 0u) needed_count = 1u;
    if (needed_count > kMaxPrimHardCap) return false;
    if (!ctx->prim_depth_gpu_buf || ctx->prim_depth_gpu_cap < needed_count) {
        uint32_t cap = ctx->prim_depth_gpu_cap >= kMinPrimCap ? ctx->prim_depth_gpu_cap : kMinPrimCap;
        while (cap < needed_count) cap = cap > kMaxPrimHardCap / 2u ? kMaxPrimHardCap : cap * 2u;
        const SDL_GPUBufferCreateInfo info = {
            .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            .size = cap * static_cast<uint32_t>(sizeof(uint32_t)),
            .props = 0u,
        };
        SDL_GPUBuffer* next = SDL_CreateGPUBuffer(ctx->device, &info);
        if (!next) return false;
        if (ctx->prim_depth_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->prim_depth_gpu_buf);
        ctx->prim_depth_gpu_buf = next;
        ctx->prim_depth_gpu_cap = cap;
    }
    if (!ctx->prim_depth_transfer_buf || ctx->prim_depth_tbuf_cap < needed_count) {
        uint32_t cap = ctx->prim_depth_tbuf_cap >= kMinPrimCap ? ctx->prim_depth_tbuf_cap : kMinPrimCap;
        while (cap < needed_count) cap = cap > kMaxPrimHardCap / 2u ? kMaxPrimHardCap : cap * 2u;
        const SDL_GPUTransferBufferCreateInfo info = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = cap * static_cast<uint32_t>(sizeof(uint32_t)),
            .props = 0u,
        };
        SDL_GPUTransferBuffer* next = SDL_CreateGPUTransferBuffer(ctx->device, &info);
        if (!next) return false;
        if (ctx->prim_depth_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->prim_depth_transfer_buf);
        ctx->prim_depth_transfer_buf = next;
        ctx->prim_depth_tbuf_cap = cap;
    }
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

bool upload_slug_delta(
    SDL_GPUDevice* device,
    SDL_GPUCopyPass* copy_pass,
    const void* host_data,
    uint32_t elem_size,
    uint32_t total_count,
    uint32_t* uploaded_count,
    SDL_GPUBuffer* gpu_buf,
    SDL_GPUTransferBuffer* tbuf, bool cycle = false) noexcept
{
    if (*uploaded_count >= total_count) {
        return true;
    }
    const uint32_t first = *uploaded_count;
    const uint32_t count = total_count - first;
    const uint32_t byte_offset = first * elem_size;
    const uint32_t byte_size = count * elem_size;
    void* mapped = SDL_MapGPUTransferBuffer(device, tbuf, true);
    if (!mapped) {
        return false;
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
    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, cycle);
    *uploaded_count = total_count;
    return true;
}

void emit_gpu_batches(
    Context* ctx,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPURenderPass* render_pass,
    SDL_GPUTextureFormat target_fmt,
    uint32_t target_w,
    uint32_t target_h,
    const DrawBatch* batches,
    uint32_t begin_batch,
    uint32_t end_batch,
    uint32_t prim_base_offset = 0u,
    bool use_depth = false,
    const SDL_Rect* scissor_override = nullptr) noexcept
{
    if (begin_batch >= end_batch) {
        return;
    }

    const SDL_GPUViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .w = static_cast<float>(target_w),
        .h = static_cast<float>(target_h),
        .min_depth = 0.0f,
        .max_depth = 1.0f,
    };
    SDL_SetGPUViewport(render_pass, &viewport);

    const SDL_Rect full_scissor = {
        .x = 0,
        .y = 0,
        .w = static_cast<int>(target_w),
        .h = static_cast<int>(target_h),
    };
    SDL_SetGPUScissor(render_pass, scissor_override ? scissor_override : &full_scissor);

    SDL_GPUBuffer* vertex_bufs[2] = { ctx->prim_gpu_buf, ctx->prim_depth_gpu_buf };
    SDL_BindGPUVertexStorageBuffers(render_pass, 0u, vertex_bufs, 2u);

    SDL_GPUBuffer* uber_frag_bufs[5] = {
        ctx->slug_curve_gpu_buf, ctx->slug_band_gpu_buf,
        ctx->dyn_slug_curve_gpu_buf, ctx->dyn_slug_band_gpu_buf,
        ctx->clip_shape_gpu,
    };
    SDL_GPUBuffer* clip_only_buf[1] = { ctx->clip_shape_gpu };

    const uint32_t manual_srgb_bit = (is_srgb_gpu_format(target_fmt) || is_linear_float_format(target_fmt)) ? 0u : (1u << 31);
    const uint32_t depth_bit = use_depth ? (1u << 30) : 0u;
    const float inv_w = 1.0f / static_cast<float>(target_w);
    const float inv_h = 1.0f / static_cast<float>(target_h);
    SDL_GPUTexture* sentinel = blur_tex_sentinel();
    SDL_GPUTexture* subpixel_sentinel = subpixel_tex_sentinel();
    const uintptr_t filter_base = reinterpret_cast<uintptr_t>(filter_tex_sentinel(0u, 0u));
    const uintptr_t filter_end = filter_base + kMaxFilterSlices * kMaxFilterLevels;

    SDL_GPUGraphicsPipeline* bound_pipe = nullptr;
    uint8_t bound_frag_layout = 0u; // 1 = clip_only (Quad/Subpixel), 2 = uber_5 (Uber)
    uint32_t bound_blend_rgb = 0xFFFFFFFFu;

    auto resolve_tex = [&](SDL_GPUTexture* t) noexcept -> SDL_GPUTexture* {
        if (t == sentinel) {
            return ctx->active_ui->blur_ping_tex ? ctx->active_ui->blur_ping_tex : ctx->atlas.gpu_tex;
        }
        if (t == subpixel_sentinel) {
            return ctx->atlas.subpixel_gpu_tex ? ctx->atlas.subpixel_gpu_tex : ctx->atlas.gpu_tex;
        }
        const uintptr_t addr = reinterpret_cast<uintptr_t>(t);
        if (addr >= filter_base && addr < filter_end) {
            const size_t idx = static_cast<size_t>(addr - filter_base);
            SDL_GPUTexture* ftex = ctx->active_ui->filter_slices[idx / kMaxFilterLevels].levels[idx % kMaxFilterLevels];
            return ftex ? ftex : ctx->atlas.gpu_tex;
        }
        return t ? t : ctx->atlas.gpu_tex;
    };

    for (uint32_t b = begin_batch; b < end_batch; ++b) {
        const DrawBatch& batch = batches[b];
        if (batch.prim_count == 0u) {
            continue;
        }

        const GpuPipelineKind kind = (batch.pipeline == GpuPipelineKind::Uber)
            ? (batch.is_punch_hole ? GpuPipelineKind::UberPunch
                                   : (batch.is_additive ? GpuPipelineKind::UberAdditive : GpuPipelineKind::Uber))
            : batch.pipeline;

        SDL_GPUGraphicsPipeline* pipe = get_batch_pipeline(ctx, kind, target_fmt, use_depth);
        if (!pipe) continue;
        if (pipe != bound_pipe) {
            SDL_BindGPUGraphicsPipeline(render_pass, pipe);
            bound_pipe = pipe;
        }

        const uint8_t need_frag_layout = (kind == GpuPipelineKind::QuadOpaque ||
                                          kind == GpuPipelineKind::QuadAlpha ||
                                          kind == GpuPipelineKind::SubpixelText) ? 1u : 2u;
        if (need_frag_layout != bound_frag_layout) {
            if (need_frag_layout == 1u) {
                SDL_BindGPUFragmentStorageBuffers(render_pass, 0u, clip_only_buf, 1u);
            } else {
                SDL_BindGPUFragmentStorageBuffers(render_pass, 0u, uber_frag_bufs, 5u);
            }
            bound_frag_layout = need_frag_layout;
        }

        if (kind == GpuPipelineKind::SubpixelText) {
            const SDL_GPUTextureSamplerBinding sb = {
                ctx->atlas.subpixel_gpu_tex ? ctx->atlas.subpixel_gpu_tex : ctx->atlas.gpu_tex,
                ctx->nearest_sampler ? ctx->nearest_sampler : ctx->linear_sampler,
            };
            SDL_BindGPUFragmentSamplers(render_pass, 0u, &sb, 1u);
            const uint32_t rgb = batch.blend_rgba8 & 0x00FFFFFFu;
            if (rgb != bound_blend_rgb) {
                const uint8_t cr = static_cast<uint8_t>(rgb & 0xFFu);
                const uint8_t cg = static_cast<uint8_t>((rgb >> 8) & 0xFFu);
                const uint8_t cb = static_cast<uint8_t>((rgb >> 16) & 0xFFu);
                const SDL_FColor bc = (manual_srgb_bit == 0u)
                    ? SDL_FColor{ srgb_u8_to_linear(cr), srgb_u8_to_linear(cg), srgb_u8_to_linear(cb), 1.0f }
                    : SDL_FColor{ static_cast<float>(cr) * (1.0f / 255.0f),
                                  static_cast<float>(cg) * (1.0f / 255.0f),
                                  static_cast<float>(cb) * (1.0f / 255.0f), 1.0f };
                SDL_SetGPUBlendConstants(render_pass, bc);
                bound_blend_rgb = rgb;
            }
        } else if (need_frag_layout == 2u) {
            SDL_GPUTextureSamplerBinding bindings[9] = {};
            bindings[0] = { ctx->atlas.gpu_tex, ctx->linear_sampler };
            for (uint32_t i = 0u; i < 8u; ++i) {
                bindings[i + 1u] = { resolve_tex(batch.ext_textures[i]), ctx->linear_sampler };
            }
            SDL_BindGPUFragmentSamplers(render_pass, 0u, bindings, 9u);
        }

        const uint32_t opaque_bit = (kind == GpuPipelineKind::QuadOpaque) ? (1u << 29) : 0u;
        const GlobalsUniform globals = {
            .viewport_size = { static_cast<float>(target_w), static_cast<float>(target_h) },
            .inv_viewport_size = { inv_w, inv_h },
            .dpi_scale = ctx->active_ui->dpi_scale,
            .time_seconds = ctx->active_ui->elapsed_seconds,
            .frame_index = ctx->active_ui->frame_index,
            .flags = ((prim_base_offset + batch.first_prim) & 0x00FFFFFFu) | opaque_bit | depth_bit | manual_srgb_bit,
        };
        SDL_PushGPUVertexUniformData(cmd, 0u, &globals, static_cast<Uint32>(sizeof(globals)));

        // Portable across D3D12/Vulkan/Metal: always pass 0 for first_vertex & first_instance.
        SDL_DrawGPUPrimitives(render_pass, 6u, batch.prim_count, 0u, 0u);
    }
}

bool run_gpu_render_pass(
    Context* ctx,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUTexture* dst_tex,
    SDL_GPUTextureFormat dst_fmt,
    uint32_t dst_w,
    uint32_t dst_h,
    SDL_GPULoadOp load_op,
    Color clear_color,
    uint32_t begin_batch,
    uint32_t end_batch,
    bool use_depth = false) noexcept
{
    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = dst_tex;
    color_target.clear_color = make_clear_fcolor(clear_color, dst_fmt);
    color_target.load_op = load_op;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth_target = {};
    const SDL_GPUDepthStencilTargetInfo* depth_ptr = nullptr;
    if (use_depth) {
        if (!ensure_depth_target(ctx, dst_w, dst_h)) return false;
        depth_target.texture = ctx->active_ui->depth_tex;
        depth_target.clear_depth = 1.0f;
        depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
        depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth_target.cycle = true;
        depth_ptr = &depth_target;
    }

    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmd, &color_target, 1u, depth_ptr);
    if (!render_pass) {
        return false;
    }
    emit_gpu_batches(ctx, cmd, render_pass, dst_fmt, dst_w, dst_h,
                     ctx->gpu_batches, begin_batch, end_batch, 0u, use_depth);
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
    uint32_t mode,
    Rect region = {}) noexcept
{
    SDL_GPUColorTargetInfo ct = {};
    ct.texture = dst_tex;
    ct.load_op = mode == 4u ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
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

    const int x=region.w>0 ? static_cast<int>(std::floor(clamp_range(region.x,0,float(dst_w)))) : 0;
    const int y=region.h>0 ? static_cast<int>(std::floor(clamp_range(region.y,0,float(dst_h)))) : 0;
    const int right=region.w>0 ? static_cast<int>(std::ceil(clamp_range(region.x+region.w,0,float(dst_w)))) : static_cast<int>(dst_w);
    const int bottom=region.h>0 ? static_cast<int>(std::ceil(clamp_range(region.y+region.h,0,float(dst_h)))) : static_cast<int>(dst_h);
    const SDL_Rect scissor={x,y,right-x,bottom-y};
    SDL_SetGPUScissor(pass, &scissor);

    const SDL_GPUTextureSamplerBinding smp = { src_tex, ctx->linear_sampler };
    SDL_BindGPUFragmentSamplers(pass, 0u, &smp, 1u);

    const KawaseUniform u = {
        .texel_size = { texel_x, texel_y },
        .offset = offset,
        .mode = mode,
        .bloom = {ctx->active_ui->bloom_threshold,ctx->active_ui->bloom_intensity,0,0},
    };
    SDL_PushGPUFragmentUniformData(cmd, 0u, &u, static_cast<Uint32>(sizeof(u)));
    SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
    SDL_EndGPURenderPass(pass);
    return true;
}

void destroy_window_ui_state(Context*, WindowUiState* ui) noexcept {
    arena_destroy(&ui->a11y_arena);
    for (uint32_t i = 0; i < ui->vlist_count; ++i) free_vlist(&ui->vlists[i]);
    std::free(ui->vlists);
    for (uint32_t i = 0; i < ui->dock_space_count; ++i) {
        auto& s = ui->dock_spaces[i];
        for (uint32_t j = 0; j < s.tab_cap; ++j) std::free(s.tabs[j].title_heap);
        std::free(s.tabs); std::free(s.nodes);
    }
    for(uint32_t i=0;i<ui->undo_cap;++i) {std::free(ui->undo_pool[i].entries);std::free(ui->undo_pool[i].bytes);}
    std::free(ui->undo_pool);
    std::free(ui->drag_heap);
    std::free(ui->pending_drop_records);std::free(ui->pending_text_drop_records);std::free(ui->pending_drop_buf);
    std::free(ui->dock_spaces); std::free(ui->dock_cursor.stack); std::free(ui->tables);
    std::free(ui->focus_order); std::free(ui->prev_focus_order);
    std::free(ui->span_hits); std::free(ui->drop_hits);
    layout_engine_destroy(ui->layout); ui->layout = nullptr;
    ui->hitboxes.destroy();
    ui->pointer_over_ids.destroy();
    subtree_cache_destroy_all(&ui->subtree_cache);
    state_map_destroy(&ui->states);
}

bool claim_window_swapchain(Context* ctx, SDL_Window* win) noexcept {
    if (!ensure_gpu_device(ctx) || !win) {
        return false;
    }
    for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i)
        if (ctx->claimed_windows[i] && ctx->claimed_windows[i]->window == win) return true;
    if (ctx->claimed_window_count == ctx->claimed_window_cap) {
        const uint32_t cap = ctx->claimed_window_cap ? ctx->claimed_window_cap * 2u : 8u;
        void* p = std::realloc(ctx->claimed_windows, size_t(cap) * sizeof(ClaimedWindowSlot*));
        if (!p) return false;
        ctx->claimed_windows = static_cast<ClaimedWindowSlot**>(p);
        for (uint32_t i = ctx->claimed_window_cap; i < cap; ++i) ctx->claimed_windows[i] = nullptr;
        ctx->claimed_window_cap = cap;
    }
    uint32_t index = 0;
    while (ctx->claimed_windows[index]) ++index;
    auto* allocated = static_cast<ClaimedWindowSlot*>(std::calloc(1, sizeof(ClaimedWindowSlot)));
    if (!allocated) return false;
    ClaimedWindowSlot& slot = *allocated;
    WindowUiState& ui = slot.ui;
    ui = WindowUiState{};
    int init_pw = 0, init_ph = 0;
    if (SDL_GetWindowSizeInPixels(win, &init_pw, &init_ph) && init_pw > 0 && init_ph > 0) {
        ui.viewport_w = static_cast<uint32_t>(init_pw);
        ui.viewport_h = static_cast<uint32_t>(init_ph);
    }
    const float init_dpi = SDL_GetWindowDisplayScale(win);
    ui.dpi_scale = (init_dpi > 0.0f) ? init_dpi : 1.0f;
    ui.applied_dpi_scale = ui.dpi_scale;
    if (!ensure_window_ui_clay(
            ctx,
            &ui,
            static_cast<float>(ui.viewport_w) / ui.dpi_scale,
            static_cast<float>(ui.viewport_h) / ui.dpi_scale))
    {
        state_map_destroy(&ui.states);
        ui = WindowUiState{};
        std::free(allocated);
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(ctx->device, win)) {
        layout_engine_destroy(ui.layout); ui.layout = nullptr;
        ui.hitboxes.destroy(); ui.pointer_over_ids.destroy(); subtree_cache_destroy_all(&ui.subtree_cache);
        state_map_destroy(&ui.states); ui = WindowUiState{}; std::free(allocated); return false;
    }
    ctx->claimed_windows[index] = allocated;
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
    apply_platform_window_chrome(ctx, &slot);
    os_a11y_attach_window(ctx, &slot);
    sync_window_event_watch(ctx);
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
    if(!flush_texture_uploads(ctx)) return false;
    if (!record_shader_surfaces(ctx, cmd)) return false;
    clear_color = resolve_theme_color(clear_color, active_theme(ctx).bg_canvas);
    if (ctx->prim_count > 0u && (ctx->gpu_batch_count == 0u || (ctx->gpu_use_early_z && !ctx->supports_d32_depth))) {
        optimize_render_batches(ctx);
    }

    const bool use_depth = ctx->gpu_use_early_z && ctx->supports_d32_depth && ctx->gpu_blur_split_count == 0u;
    const bool bloom = ctx->active_ui->bloom_intensity > 0.0f;
    for (uint32_t i = 0u; i < ctx->gpu_batch_count; ++i) {
        if (!get_batch_pipeline(ctx, ctx->gpu_batches[i].pipeline, target_fmt, use_depth)) return false;
    }
    if (ctx->gpu_blur_split_count > 0u || bloom) {
        for (uint32_t i = 0u; i < ctx->gpu_batch_count; ++i) {
            if (!get_batch_pipeline(ctx, ctx->gpu_batches[i].pipeline, kBlurTexFormat, use_depth)) return false;
        }
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

    const bool clip_shapes_dirty = ctx->clip_shape_count > 1u || !ctx->clip_shape_sentinel_uploaded || !ctx->clip_shape_gpu;
    uint32_t clip_uploaded = 0;
    if (clip_shapes_dirty && !ensure_slug_storage_pair(ctx->device, ctx->clip_shape_count, sizeof(ClipShapeGpu), &ctx->clip_shape_gpu, &ctx->clip_shape_gpu_capacity,
        &ctx->clip_shape_transfer, &ctx->clip_shape_transfer_capacity, &clip_uploaded)) return false;
    const ClipShapeGpu empty_clip={};
    uint32_t dyn_curves_uploaded = 0u;
    uint32_t dyn_bands_uploaded = 0u;
    if (ctx->dyn_slug_curve_count > 0u && !ensure_slug_storage_pair(ctx->device,
        ctx->dyn_slug_curve_count, static_cast<uint32_t>(sizeof(SlugCurveGpu)), &ctx->dyn_slug_curve_gpu_buf,
        &ctx->dyn_slug_curve_gpu_cap, &ctx->dyn_slug_curve_tbuf, &ctx->dyn_slug_curve_tbuf_cap,
        &dyn_curves_uploaded)) return false;
    if (ctx->dyn_slug_band_count > 0u && !ensure_slug_storage_pair(ctx->device,
        ctx->dyn_slug_band_count, static_cast<uint32_t>(sizeof(SlugBandEntryGpu)), &ctx->dyn_slug_band_gpu_buf,
        &ctx->dyn_slug_band_gpu_cap, &ctx->dyn_slug_band_tbuf, &ctx->dyn_slug_band_tbuf_cap,
        &dyn_bands_uploaded)) return false;
    const uint32_t total_prims = ctx->prim_count + ctx->filter_prim_count;
    if (total_prims > ctx->prim_gpu_cap || total_prims > ctx->prim_tbuf_cap) {
        if (!ensure_prim_buffers(ctx, total_prims)) return false;
    }
    const uint32_t needed_depths = ctx->prim_count > 0u ? ctx->prim_count : 1u;
    if (!ctx->prim_depth_gpu_buf || needed_depths > ctx->prim_depth_gpu_cap ||
        (use_depth && (!ctx->prim_depth_transfer_buf || needed_depths > ctx->prim_depth_tbuf_cap)))
    {
        if (!ensure_prim_depth_buffers(ctx, needed_depths)) return false;
    }
    auto& atlas = ctx->atlas;
    if (!atlas.gpu_tex || atlas.gpu_w != atlas.width || atlas.gpu_h != atlas.height) {
        SDL_WaitForGPUIdle(ctx->device);
        if (atlas.gpu_tex) SDL_ReleaseGPUTexture(ctx->device, atlas.gpu_tex);
        const SDL_GPUTextureCreateInfo info = {
            .type=SDL_GPU_TEXTURETYPE_2D, .format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
            .usage=SDL_GPU_TEXTUREUSAGE_SAMPLER, .width=atlas.width, .height=atlas.height,
            .layer_count_or_depth=1u, .num_levels=1u, .sample_count=SDL_GPU_SAMPLECOUNT_1,
            .props=0u,
        };
        atlas.gpu_tex=SDL_CreateGPUTexture(ctx->device,&info);
        if (!atlas.gpu_tex) return false;
        atlas.gpu_w=atlas.width; atlas.gpu_h=atlas.height;
        atlas.dirty=true; atlas.dirty_y0=0; atlas.dirty_y1=atlas.height;
    }
    if (atlas.dirty && atlas.dirty_y1>atlas.dirty_y0) {
        const uint32_t bytes=atlas.width*(atlas.dirty_y1-atlas.dirty_y0)*4u;
        if (ctx->atlas_tbuf_size<bytes) {
            if (ctx->atlas_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device,ctx->atlas_transfer_buf);
            const SDL_GPUTransferBufferCreateInfo info={.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,.size=bytes,.props=0u};
            ctx->atlas_transfer_buf=SDL_CreateGPUTransferBuffer(ctx->device,&info);
            ctx->atlas_tbuf_size=ctx->atlas_transfer_buf?bytes:0;
            if (!ctx->atlas_transfer_buf) return false;
        }
    }
    if (atlas.subpixel_pixels && atlas.subpixel_w > 0u && atlas.subpixel_h > 0u) {
        if (!atlas.subpixel_gpu_tex || atlas.subpixel_gpu_w != atlas.subpixel_w || atlas.subpixel_gpu_h != atlas.subpixel_h) {
            if (atlas.subpixel_gpu_tex) {
                SDL_WaitForGPUIdle(ctx->device);
                SDL_ReleaseGPUTexture(ctx->device, atlas.subpixel_gpu_tex);
            }
            const SDL_GPUTextureCreateInfo sp_info = {
                .type = SDL_GPU_TEXTURETYPE_2D,
                .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
                .width = atlas.subpixel_w,
                .height = atlas.subpixel_h,
                .layer_count_or_depth = 1u,
                .num_levels = 1u,
                .sample_count = SDL_GPU_SAMPLECOUNT_1,
                .props = 0u,
            };
            atlas.subpixel_gpu_tex = SDL_CreateGPUTexture(ctx->device, &sp_info);
            if (!atlas.subpixel_gpu_tex) return false;
            atlas.subpixel_gpu_w = atlas.subpixel_w;
            atlas.subpixel_gpu_h = atlas.subpixel_h;
            atlas.subpixel_dirty = true;
            atlas.subpixel_dirty_y0 = 0u;
            atlas.subpixel_dirty_y1 = atlas.subpixel_h;
        }
        if (atlas.subpixel_dirty && atlas.subpixel_dirty_y1 > atlas.subpixel_dirty_y0) {
            const uint32_t bytes = atlas.subpixel_w * (atlas.subpixel_dirty_y1 - atlas.subpixel_dirty_y0) * 4u;
            if (ctx->subpixel_tbuf_size < bytes) {
                if (ctx->subpixel_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->subpixel_transfer_buf);
                const SDL_GPUTransferBufferCreateInfo info = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = bytes, .props = 0u };
                ctx->subpixel_transfer_buf = SDL_CreateGPUTransferBuffer(ctx->device, &info);
                ctx->subpixel_tbuf_size = ctx->subpixel_transfer_buf ? bytes : 0u;
                if (!ctx->subpixel_transfer_buf) return false;
            }
        }
    }
    const bool subpixel_upload = atlas.subpixel_gpu_tex && atlas.subpixel_dirty && atlas.subpixel_dirty_y1 > atlas.subpixel_dirty_y0;
    if (clip_shapes_dirty || ctx->atlas.dirty || subpixel_upload || total_prims > 0u || slug_curves_pending || slug_bands_pending ||
        ctx->dyn_slug_curve_count > 0u || ctx->dyn_slug_band_count > 0u) {
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
        if (!copy_pass) {
            return false;
        }

        if (clip_shapes_dirty) {
            if(!upload_slug_delta(ctx->device,copy_pass,ctx->clip_shapes ? ctx->clip_shapes : &empty_clip,sizeof(ClipShapeGpu),ctx->clip_shape_count,
                &clip_uploaded,ctx->clip_shape_gpu,ctx->clip_shape_transfer,true)) {SDL_EndGPUCopyPass(copy_pass);return false;}
            ctx->clip_shape_sentinel_uploaded = true;
        }

        if (atlas.dirty && atlas.dirty_y1>atlas.dirty_y0) {
            const uint32_t rows=atlas.dirty_y1-atlas.dirty_y0;
            const bool full_upload=(atlas.dirty_y0==0u && atlas.dirty_y1==atlas.height);
            void* mapped=SDL_MapGPUTransferBuffer(ctx->device,ctx->atlas_transfer_buf,true);
            if (!mapped) { SDL_EndGPUCopyPass(copy_pass); return false; }
            std::memcpy(mapped,atlas.pixels+size_t(atlas.dirty_y0)*atlas.width*4u,size_t(atlas.width)*rows*4u);
            SDL_UnmapGPUTransferBuffer(ctx->device,ctx->atlas_transfer_buf);
            const SDL_GPUTextureTransferInfo src={.transfer_buffer=ctx->atlas_transfer_buf,.offset=0u,.pixels_per_row=atlas.width,.rows_per_layer=rows};
            const SDL_GPUTextureRegion dst={.texture=atlas.gpu_tex,.mip_level=0u,.layer=0u,.x=0u,.y=atlas.dirty_y0,.z=0u,.w=atlas.width,.h=rows,.d=1u};
            SDL_UploadToGPUTexture(copy_pass,&src,&dst,full_upload);
            atlas.dirty=false;atlas.dirty_y0=UINT32_MAX;atlas.dirty_y1=0;
        }

        if (subpixel_upload) {
            const uint32_t rows = atlas.subpixel_dirty_y1 - atlas.subpixel_dirty_y0;
            const bool full_upload = (atlas.subpixel_dirty_y0 == 0u && atlas.subpixel_dirty_y1 == atlas.subpixel_h);
            void* mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->subpixel_transfer_buf, true);
            if (!mapped) { SDL_EndGPUCopyPass(copy_pass); return false; }
            std::memcpy(mapped, atlas.subpixel_pixels + size_t(atlas.subpixel_dirty_y0) * atlas.subpixel_w * 4u,
                        size_t(atlas.subpixel_w) * rows * 4u);
            SDL_UnmapGPUTransferBuffer(ctx->device, ctx->subpixel_transfer_buf);
            const SDL_GPUTextureTransferInfo src = {
                .transfer_buffer = ctx->subpixel_transfer_buf,
                .offset = 0u,
                .pixels_per_row = atlas.subpixel_w,
                .rows_per_layer = rows,
            };
            const SDL_GPUTextureRegion dst = {
                .texture = atlas.subpixel_gpu_tex,
                .mip_level = 0u,
                .layer = 0u,
                .x = 0u,
                .y = atlas.subpixel_dirty_y0,
                .z = 0u,
                .w = atlas.subpixel_w,
                .h = rows,
                .d = 1u,
            };
            SDL_UploadToGPUTexture(copy_pass, &src, &dst, full_upload);
            atlas.subpixel_dirty = false;
            atlas.subpixel_dirty_y0 = UINT32_MAX;
            atlas.subpixel_dirty_y1 = 0u;
        }

        if (!upload_slug_delta(
                ctx->device,
                copy_pass,
                ctx->atlas.slug_curves,
                static_cast<uint32_t>(sizeof(SlugCurveGpu)),
                ctx->atlas.slug_curve_count,
                &ctx->atlas.uploaded_slug_curves,
                ctx->slug_curve_gpu_buf,
                ctx->slug_curve_tbuf))
        {
            SDL_EndGPUCopyPass(copy_pass);
            return false;
        }

        if (!upload_slug_delta(
                ctx->device,
                copy_pass,
                ctx->atlas.slug_bands,
                static_cast<uint32_t>(sizeof(SlugBandEntryGpu)),
                ctx->atlas.slug_band_count,
                &ctx->atlas.uploaded_slug_bands,
                ctx->slug_band_gpu_buf,
                ctx->slug_band_tbuf))
        {
            SDL_EndGPUCopyPass(copy_pass);
            return false;
        }

        if (ctx->dyn_slug_curve_count > 0u && !upload_slug_delta(ctx->device, copy_pass,
            ctx->dyn_slug_curves, static_cast<uint32_t>(sizeof(SlugCurveGpu)), ctx->dyn_slug_curve_count,
            &dyn_curves_uploaded, ctx->dyn_slug_curve_gpu_buf, ctx->dyn_slug_curve_tbuf, true)) {
            SDL_EndGPUCopyPass(copy_pass); return false;
        }
        if (ctx->dyn_slug_band_count > 0u && !upload_slug_delta(ctx->device, copy_pass,
            ctx->dyn_slug_bands, static_cast<uint32_t>(sizeof(SlugBandEntryGpu)), ctx->dyn_slug_band_count,
            &dyn_bands_uploaded, ctx->dyn_slug_band_gpu_buf, ctx->dyn_slug_band_tbuf, true)) {
            SDL_EndGPUCopyPass(copy_pass); return false;
        }
        if (total_prims > 0u && ctx->prim_gpu_buf && ctx->prim_transfer_buf) {
            const uint32_t main_bytes = ctx->prim_count * static_cast<uint32_t>(sizeof(Prim));
            const uint32_t filter_bytes = ctx->filter_prim_count * static_cast<uint32_t>(sizeof(Prim));
            const uint32_t byte_size = main_bytes + filter_bytes;
            void* mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf, true);
            if (mapped) {
                if (main_bytes > 0u) std::memcpy(mapped, ctx->gpu_prims ? ctx->gpu_prims : ctx->prims, main_bytes);
                if (filter_bytes > 0u) std::memcpy(static_cast<uint8_t*>(mapped) + main_bytes, ctx->filter_prims, filter_bytes);
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
        if (use_depth && ctx->prim_count > 0u && ctx->gpu_prim_depths && ctx->prim_depth_gpu_buf && ctx->prim_depth_transfer_buf) {
            const uint32_t dbytes = ctx->prim_count * static_cast<uint32_t>(sizeof(uint32_t));
            void* mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->prim_depth_transfer_buf, true);
            if (mapped) {
                std::memcpy(mapped, ctx->gpu_prim_depths, dbytes);
                SDL_UnmapGPUTransferBuffer(ctx->device, ctx->prim_depth_transfer_buf);
                const SDL_GPUTransferBufferLocation dsrc = { .transfer_buffer = ctx->prim_depth_transfer_buf, .offset = 0u };
                const SDL_GPUBufferRegion ddst = { .buffer = ctx->prim_depth_gpu_buf, .offset = 0u, .size = dbytes };
                SDL_UploadToGPUBuffer(copy_pass, &dsrc, &ddst, true);
            }
        }

        SDL_EndGPUCopyPass(copy_pass);
    }

    if (ctx->filter_group_count > 0u) {
        ctx->active_ui->filter_idle_frames = 0u;
        bool slice_used[kMaxFilterSlices] = {};
        uint32_t slice_max_level[kMaxFilterSlices] = {};
        Rect slice_bounds[kMaxFilterSlices] = {};
        for (uint32_t gi = 0u; gi < ctx->filter_group_count; ++gi) {
            const FilterBlurGroup& fg = ctx->filter_groups[gi];
            if (!slice_used[fg.slice]) {
                slice_used[fg.slice] = true;
                slice_bounds[fg.slice] = fg.padded_bounds;
            } else {
                const float x0 = std::fmin(slice_bounds[fg.slice].x, fg.padded_bounds.x);
                const float y0 = std::fmin(slice_bounds[fg.slice].y, fg.padded_bounds.y);
                const float x1 = std::fmax(slice_bounds[fg.slice].x + slice_bounds[fg.slice].w, fg.padded_bounds.x + fg.padded_bounds.w);
                const float y1 = std::fmax(slice_bounds[fg.slice].y + slice_bounds[fg.slice].h, fg.padded_bounds.y + fg.padded_bounds.h);
                slice_bounds[fg.slice] = {x0, y0, x1 - x0, y1 - y0};
            }
            if (fg.max_level > slice_max_level[fg.slice]) slice_max_level[fg.slice] = fg.max_level;
        }
        SDL_GPUGraphicsPipeline* filter_uber_pipe = get_or_create_pipeline(ctx, kFilterTexFormat);
        SDL_GPUGraphicsPipeline* filter_kawase_pipe = get_or_create_filter_kawase_pipeline(ctx);
        if (!filter_uber_pipe || !filter_kawase_pipe) return false;
        for (uint32_t bi = 0u; bi < ctx->filter_batch_count; ++bi) {
            if (ctx->filter_batches[bi].is_punch_hole && !get_or_create_punch_pipeline(ctx, kFilterTexFormat)) return false;
            if (ctx->filter_batches[bi].is_additive && !get_or_create_additive_pipeline(ctx, kFilterTexFormat)) return false;
        }
        for (uint32_t s = 0u; s < kMaxFilterSlices; ++s) {
            if (!slice_used[s]) continue;
            if (!ensure_filter_slice_targets(ctx, target_w, target_h, s, slice_max_level[s])) return false;
            FilterSliceTargets& st = ctx->active_ui->filter_slices[s];
            const Rect sb = slice_bounds[s];
            const int sx0 = static_cast<int>(std::floor(clamp_range(sb.x, 0.0f, float(target_w))));
            const int sy0 = static_cast<int>(std::floor(clamp_range(sb.y, 0.0f, float(target_h))));
            const int sx1 = static_cast<int>(std::ceil(clamp_range(sb.x + sb.w, 0.0f, float(target_w))));
            const int sy1 = static_cast<int>(std::ceil(clamp_range(sb.y + sb.h, 0.0f, float(target_h))));
            const SDL_Rect slice_scissor = {
                sx0, sy0,
                (sx1 > sx0) ? (sx1 - sx0) : 0,
                (sy1 > sy0) ? (sy1 - sy0) : 0,
            };
            SDL_GPUColorTargetInfo ct = {};
            ct.texture = st.levels[0];
            ct.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
            ct.load_op = SDL_GPU_LOADOP_CLEAR;
            ct.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* fpass = SDL_BeginGPURenderPass(cmd, &ct, 1u, nullptr);
            if (!fpass) return false;
            for (uint32_t gi = 0u; gi < ctx->filter_group_count; ++gi) {
                const FilterBlurGroup& fg = ctx->filter_groups[gi];
                if (fg.slice != s) continue;
                emit_gpu_batches(ctx, cmd, fpass, kFilterTexFormat, target_w, target_h,
                    ctx->filter_batches, fg.first_batch, fg.end_batch, ctx->prim_count, false, &slice_scissor);
            }
            SDL_EndGPURenderPass(fpass);

            float pad_dst[kMaxFilterLevels] = {};
            if (slice_max_level[s] > 0u) {
                pad_dst[slice_max_level[s]] = 3.0f;
                for (int32_t l = static_cast<int32_t>(slice_max_level[s]) - 1; l >= 1; --l) {
                    pad_dst[l] = pad_dst[l + 1] * 2.0f + 3.0f;
                }
            }
            for (uint32_t lvl = 0u; lvl < slice_max_level[s]; ++lvl) {
                const uint32_t src_w = (target_w >> lvl) > 0u ? (target_w >> lvl) : 1u;
                const uint32_t src_h = (target_h >> lvl) > 0u ? (target_h >> lvl) : 1u;
                const uint32_t dst_w = (target_w >> (lvl + 1u)) > 0u ? (target_w >> (lvl + 1u)) : 1u;
                const uint32_t dst_h = (target_h >> (lvl + 1u)) > 0u ? (target_h >> (lvl + 1u)) : 1u;
                const float dsx = float(dst_w) / float(target_w);
                const float dsy = float(dst_h) / float(target_h);
                const float m = pad_dst[lvl + 1u];
                const Rect region = {
                    sb.x * dsx - m,
                    sb.y * dsy - m,
                    sb.w * dsx + 2.0f * m,
                    sb.h * dsy + 2.0f * m,
                };
                if (!run_kawase_step(ctx, cmd, filter_kawase_pipe, st.levels[lvl], st.levels[lvl + 1u],
                    dst_w, dst_h, 1.0f / float(src_w), 1.0f / float(src_h), 1.0f, 0u, region))
                {
                    return false;
                }
                ++ctx->stats.blur_pass_count;
            }
        }
    } else if (ctx->active_ui->filter_rt_w != 0u) {
        if (++ctx->active_ui->filter_idle_frames >= 120u) {
            release_filter_targets(ctx);
        }
    }

    if (ctx->gpu_blur_split_count == 0u && !bloom) {
        return run_gpu_render_pass(
            ctx,
            cmd,
            target_tex,
            target_fmt,
            target_w,
            target_h,
            SDL_GPU_LOADOP_CLEAR,
            clear_color,
            0u,
            ctx->gpu_batch_count,
            use_depth);
    }

    if (!ensure_blur_targets(ctx, target_w, target_h)) {
        return false;
    }
    SDL_GPUGraphicsPipeline* kawase_pipe = get_or_create_kawase_pipeline(ctx);
    const bool target_srgb = is_srgb_gpu_format(target_fmt);
    SDL_GPUGraphicsPipeline* present_pipe = target_srgb ? nullptr : get_or_create_present_pipeline(ctx, target_fmt);
    if (!kawase_pipe || (!target_srgb && !present_pipe)) {
        return false;
    }

    const uint32_t half_w = (target_w > 1u) ? (target_w / 2u) : 1u;
    const uint32_t half_h = (target_h > 1u) ? (target_h / 2u) : 1u;
    const uint32_t quarter_w = (target_w > 3u) ? (target_w / 4u) : 1u;
    const uint32_t quarter_h = (target_h > 3u) ? (target_h / 4u) : 1u;

    for (uint32_t s = 0u; s < ctx->gpu_blur_split_count; ++s) {
        const uint32_t prev_batch = (s == 0u) ? 0u : ctx->gpu_blur_splits[s - 1u].split_batch;
        const uint32_t split_batch = ctx->gpu_blur_splits[s].split_batch;
        const float radius_px = ctx->gpu_blur_splits[s].blur_radius_px;
        const Rect bounds = ctx->gpu_blur_splits[s].union_bounds;
        const float off_down1 = clampf_blur(1.0f + radius_px * 0.04f, 0.5f, 3.0f);
        const float off_down2 = clampf_blur(1.0f + radius_px * 0.06f, 0.5f, 4.0f);
        const float off_up    = clampf_blur(0.6f + radius_px * 0.03f, 0.5f, 2.5f);
        // Shrink scissor margins monotonically so every LOADOP_DONT_CARE pass samples strictly inside prior pass's written pixels.
        const float margin_up    = std::ceil(radius_px * 0.5f + 8.0f);
        const float margin_down2 = margin_up + std::ceil((off_up + 2.0f) * 4.0f);
        const float margin_down1 = margin_down2 + std::ceil((off_down2 + 2.0f) * 2.0f);
        const auto region=[&](uint32_t w,uint32_t h,float margin) noexcept -> Rect {
            const float sx=float(w)/float(target_w),sy=float(h)/float(target_h);
            return {(bounds.x-margin)*sx,(bounds.y-margin)*sy,(bounds.w+2*margin)*sx,(bounds.h+2*margin)*sy};
        };

        if (s == 0u || split_batch > prev_batch) {
            if (!run_gpu_render_pass(
                    ctx,
                    cmd,
                    ctx->active_ui->scene_tex,
                    kBlurTexFormat,
                    target_w,
                    target_h,
                    (s == 0u) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
                    clear_color,
                    prev_batch,
                    split_batch,
                    false))
            {
                return false;
            }
        }

        if (!run_kawase_step(
                ctx, cmd, kawase_pipe,
                ctx->active_ui->scene_tex, ctx->active_ui->blur_ping_tex, half_w, half_h,
                1.0f / static_cast<float>(target_w), 1.0f / static_cast<float>(target_h),
                off_down1, 0u, region(half_w, half_h, margin_down1)))
        {
            return false;
        }
        if (!run_kawase_step(
                ctx, cmd, kawase_pipe,
                ctx->active_ui->blur_ping_tex, ctx->active_ui->blur_pong_tex, quarter_w, quarter_h,
                1.0f / static_cast<float>(half_w), 1.0f / static_cast<float>(half_h),
                off_down2, 0u, region(quarter_w, quarter_h, margin_down2)))
        {
            return false;
        }
        if (!run_kawase_step(
                ctx, cmd, kawase_pipe,
                ctx->active_ui->blur_pong_tex, ctx->active_ui->blur_ping_tex, half_w, half_h,
                1.0f / static_cast<float>(quarter_w), 1.0f / static_cast<float>(quarter_h),
                off_up, 1u, region(half_w, half_h, margin_up)))
        {
            return false;
        }
    }

    const uint32_t last_batch = ctx->gpu_blur_split_count ? ctx->gpu_blur_splits[ctx->gpu_blur_split_count - 1u].split_batch : 0u;
    if (bloom) {
        if (!run_gpu_render_pass(ctx, cmd, ctx->active_ui->scene_tex, kBlurTexFormat, target_w, target_h,
                ctx->gpu_blur_split_count ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR, clear_color,
                last_batch, ctx->gpu_batch_count, use_depth)) return false;
        if (!ctx->bloom_pipe) ctx->bloom_pipe = create_graphics_pipeline(ctx->device, ctx->kawase_vs_shader, ctx->kawase_ps_shader, kBlurTexFormat, BlendMode::Additive);
        if (!ctx->bloom_pipe) return false;
        if (!run_kawase_step(ctx,cmd,kawase_pipe,ctx->active_ui->scene_tex,ctx->active_ui->blur_ping_tex,
                half_w,half_h,1.f/float(target_w),1.f/float(target_h),1,3)) return false;
        if (!run_kawase_step(ctx,cmd,kawase_pipe,ctx->active_ui->blur_ping_tex,ctx->active_ui->blur_pong_tex,
                quarter_w,quarter_h,1.f/float(half_w),1.f/float(half_h),2,0)) return false;
        if (!run_kawase_step(ctx,cmd,kawase_pipe,ctx->active_ui->blur_pong_tex,ctx->active_ui->blur_ping_tex,
                half_w,half_h,1.f/float(quarter_w),1.f/float(quarter_h),1,1)) return false;
        if (!run_kawase_step(ctx,cmd,ctx->bloom_pipe,ctx->active_ui->blur_ping_tex,ctx->active_ui->scene_tex,
                target_w,target_h,1.f/float(half_w),1.f/float(half_h),0,4)) return false;
        ctx->stats.blur_pass_count += 4;
    }
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
            .padding1 = 0u,
            .padding2 = 0u,
            .padding3 = 0u,
        };
        SDL_BlitGPUTexture(cmd, &blit);
        return run_gpu_render_pass(
            ctx,
            cmd,
            target_tex,
            target_fmt,
            target_w,
            target_h,
            SDL_GPU_LOADOP_LOAD,
            clear_color,
            bloom ? ctx->gpu_batch_count : last_batch,
            ctx->gpu_batch_count,
            false);
    }

    if (!bloom && !run_gpu_render_pass(
            ctx,
            cmd,
            ctx->active_ui->scene_tex,
            kBlurTexFormat,
            target_w,
            target_h,
            SDL_GPU_LOADOP_LOAD,
            clear_color,
            last_batch,
            ctx->gpu_batch_count,
            false))
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

void release_pipeline_cache(SDL_GPUDevice* dev, CachedPipeline* cache, uint32_t* count) noexcept {
    for (uint32_t i = 0u; i < *count; ++i) {
        if (cache[i].pipeline) SDL_ReleaseGPUGraphicsPipeline(dev, cache[i].pipeline);
        cache[i] = {};
    }
    *count = 0u;
}

bool ensure_gpu_device(Context* ctx) noexcept {
    if (!ctx || ctx->gpu_init_failed) return false;
    if (ctx->device) return true;

    const char* driver = ctx->gpu_driver;
    if (!driver[0]) driver = SDL_getenv("ETCH_GPU_DRIVER");
    if (!driver || !driver[0]) driver = SDL_getenv("SDL_GPU_DRIVER");
    if (driver && !driver[0]) driver = nullptr;

    const auto fail = [&]() noexcept -> bool {
        if (ctx->device) {
            release_pipeline_cache(ctx->device, ctx->additive_pipelines, &ctx->additive_pipeline_count);
            release_pipeline_cache(ctx->device, ctx->additive_depth_pipelines, &ctx->additive_depth_pipeline_count);
            release_pipeline_cache(ctx->device, ctx->punch_pipelines, &ctx->punch_pipeline_count);
            release_pipeline_cache(ctx->device, ctx->pipelines, &ctx->pipeline_count);
            release_pipeline_cache(ctx->device, ctx->uber_depth_pipelines, &ctx->uber_depth_pipeline_count);
            release_pipeline_cache(ctx->device, ctx->quad_opaque_pipelines, &ctx->quad_opaque_pipeline_count);
            release_pipeline_cache(ctx->device, ctx->quad_alpha_pipelines, &ctx->quad_alpha_pipeline_count);
            release_pipeline_cache(ctx->device, ctx->quad_alpha_depth_pipelines, &ctx->quad_alpha_depth_pipeline_count);
            release_pipeline_cache(ctx->device, ctx->subpixel_pipelines, &ctx->subpixel_pipeline_count);
            release_pipeline_cache(ctx->device, ctx->subpixel_depth_pipelines, &ctx->subpixel_depth_pipeline_count);
            if (ctx->vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->vs_shader);
            if (ctx->ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->ps_shader);
            if (ctx->quad_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->quad_vs_shader);
            if (ctx->quad_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->quad_ps_shader);
            if (ctx->subpixel_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->subpixel_vs_shader);
            if (ctx->subpixel_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->subpixel_ps_shader);
            if (ctx->kawase_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->kawase_vs_shader);
            if (ctx->kawase_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->kawase_ps_shader);
            if (ctx->linear_sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->linear_sampler);
            if (ctx->nearest_sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->nearest_sampler);
            if (ctx->mirror_linear_sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->mirror_linear_sampler);
            if (ctx->surface_fallback_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->surface_fallback_tex);
            if (ctx->prim_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->prim_gpu_buf);
            if (ctx->prim_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf);
            if (ctx->prim_depth_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->prim_depth_gpu_buf);
            if (ctx->prim_depth_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->prim_depth_transfer_buf);
            if (ctx->atlas.gpu_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->atlas.gpu_tex);
            if (ctx->atlas.subpixel_gpu_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->atlas.subpixel_gpu_tex);
            if (ctx->atlas_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->atlas_transfer_buf);
            if (ctx->subpixel_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->subpixel_transfer_buf);
            if (ctx->texture_upload_cmd) SDL_CancelGPUCommandBuffer(ctx->texture_upload_cmd);
            if (ctx->clip_shape_gpu) SDL_ReleaseGPUBuffer(ctx->device, ctx->clip_shape_gpu);
            if (ctx->clip_shape_transfer) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->clip_shape_transfer);
            if (ctx->texture_upload_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->texture_upload_buf);
            if (ctx->slug_curve_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->slug_curve_gpu_buf);
            if (ctx->slug_band_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->slug_band_gpu_buf);
            if (ctx->dyn_slug_curve_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->dyn_slug_curve_gpu_buf);
            if (ctx->dyn_slug_band_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->dyn_slug_band_gpu_buf);
            SDL_DestroyGPUDevice(ctx->device);
        }
        ctx->vs_shader = ctx->ps_shader = nullptr;
        ctx->quad_vs_shader = ctx->quad_ps_shader = nullptr;
        ctx->subpixel_vs_shader = ctx->subpixel_ps_shader = nullptr;
        ctx->kawase_vs_shader = ctx->kawase_ps_shader = nullptr;
        ctx->linear_sampler = ctx->nearest_sampler = ctx->mirror_linear_sampler = nullptr;
        ctx->surface_fallback_tex = nullptr;
        ctx->prim_gpu_buf = nullptr; ctx->prim_transfer_buf = nullptr;
        ctx->prim_depth_gpu_buf = nullptr; ctx->prim_depth_transfer_buf = nullptr;
        ctx->atlas.gpu_tex = nullptr; ctx->atlas.subpixel_gpu_tex = nullptr;
        ctx->atlas_transfer_buf = nullptr; ctx->subpixel_transfer_buf = nullptr; ctx->subpixel_tbuf_size = 0u;
        ctx->texture_upload_cmd = nullptr;
        ctx->clip_shape_gpu = nullptr; ctx->clip_shape_transfer = nullptr;
        ctx->clip_shape_gpu_capacity = ctx->clip_shape_transfer_capacity = 0u;
        ctx->texture_upload_buf = nullptr; ctx->texture_upload_cap = 0u;
        ctx->slug_curve_gpu_buf = ctx->slug_band_gpu_buf = nullptr;
        ctx->dyn_slug_curve_gpu_buf = ctx->dyn_slug_band_gpu_buf = nullptr;
        ctx->device = nullptr;
        ctx->prim_gpu_cap = ctx->prim_tbuf_cap = 0u;
        ctx->prim_depth_gpu_cap = ctx->prim_depth_tbuf_cap = 0u;
        ctx->slug_curve_gpu_cap = ctx->slug_band_gpu_cap = 0u;
        ctx->dyn_slug_curve_gpu_cap = ctx->dyn_slug_band_gpu_cap = 0u;
        ctx->gpu_init_failed = true;
        return false;
    };

    // Initialize SDL3 GPU device (DXIL + SPIR-V + MSL).
    const SDL_GPUShaderFormat want_formats = SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
    ctx->device = SDL_CreateGPUDevice(want_formats, ctx->enable_gpu_debug, driver);
    if (!ctx->device && !SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            ctx->owns_video_init = true;
            if (ctx->wake_event_type == 0 && (SDL_WasInit(SDL_INIT_EVENTS) || SDL_WasInit(SDL_INIT_VIDEO)))
                ctx->wake_event_type = SDL_RegisterEvents(1);
            ctx->device = SDL_CreateGPUDevice(want_formats, ctx->enable_gpu_debug, driver);
        }
    }
    if (!ctx->device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return fail();
    }

    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(ctx->device);
    SDL_GPUShaderFormat chosen_fmt = SDL_GPU_SHADERFORMAT_INVALID;
    const Uint8* vs_code = nullptr; size_t vs_size = 0;
    const Uint8* ps_code = nullptr; size_t ps_size = 0;
    const Uint8* quad_vs_code = nullptr; size_t quad_vs_size = 0;
    const Uint8* quad_ps_code = nullptr; size_t quad_ps_size = 0;
    const Uint8* subpixel_vs_code = nullptr; size_t subpixel_vs_size = 0;
    const Uint8* subpixel_ps_code = nullptr; size_t subpixel_ps_size = 0;
    const Uint8* kawase_vs_code = nullptr; size_t kawase_vs_size = 0;
    const Uint8* kawase_ps_code = nullptr; size_t kawase_ps_size = 0;

    if ((supported & SDL_GPU_SHADERFORMAT_DXIL) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_DXIL;
        vs_code = g_etch_uber_vs_dxil; vs_size = sizeof(g_etch_uber_vs_dxil);
        ps_code = g_etch_uber_ps_dxil; ps_size = sizeof(g_etch_uber_ps_dxil);
        quad_vs_code = g_etch_quad_vs_dxil; quad_vs_size = sizeof(g_etch_quad_vs_dxil);
        quad_ps_code = g_etch_quad_ps_dxil; quad_ps_size = sizeof(g_etch_quad_ps_dxil);
        subpixel_vs_code = g_etch_subpixel_vs_dxil; subpixel_vs_size = sizeof(g_etch_subpixel_vs_dxil);
        subpixel_ps_code = g_etch_subpixel_ps_dxil; subpixel_ps_size = sizeof(g_etch_subpixel_ps_dxil);
        kawase_vs_code = g_etch_kawase_vs_dxil; kawase_vs_size = sizeof(g_etch_kawase_vs_dxil);
        kawase_ps_code = g_etch_kawase_ps_dxil; kawase_ps_size = sizeof(g_etch_kawase_ps_dxil);
    } else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_SPIRV;
        vs_code = g_etch_uber_vs_spv; vs_size = sizeof(g_etch_uber_vs_spv);
        ps_code = g_etch_uber_ps_spv; ps_size = sizeof(g_etch_uber_ps_spv);
        quad_vs_code = g_etch_quad_vs_spv; quad_vs_size = sizeof(g_etch_quad_vs_spv);
        quad_ps_code = g_etch_quad_ps_spv; quad_ps_size = sizeof(g_etch_quad_ps_spv);
        subpixel_vs_code = g_etch_subpixel_vs_spv; subpixel_vs_size = sizeof(g_etch_subpixel_vs_spv);
        subpixel_ps_code = g_etch_subpixel_ps_spv; subpixel_ps_size = sizeof(g_etch_subpixel_ps_spv);
        kawase_vs_code = g_etch_kawase_vs_spv; kawase_vs_size = sizeof(g_etch_kawase_vs_spv);
        kawase_ps_code = g_etch_kawase_ps_spv; kawase_ps_size = sizeof(g_etch_kawase_ps_spv);
    } else if ((supported & SDL_GPU_SHADERFORMAT_MSL) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_MSL;
        vs_code = g_etch_uber_vs_msl; vs_size = sizeof(g_etch_uber_vs_msl) - 1u;
        ps_code = g_etch_uber_ps_msl; ps_size = sizeof(g_etch_uber_ps_msl) - 1u;
        quad_vs_code = g_etch_quad_vs_msl; quad_vs_size = sizeof(g_etch_quad_vs_msl) - 1u;
        quad_ps_code = g_etch_quad_ps_msl; quad_ps_size = sizeof(g_etch_quad_ps_msl) - 1u;
        subpixel_vs_code = g_etch_subpixel_vs_msl; subpixel_vs_size = sizeof(g_etch_subpixel_vs_msl) - 1u;
        subpixel_ps_code = g_etch_subpixel_ps_msl; subpixel_ps_size = sizeof(g_etch_subpixel_ps_msl) - 1u;
        kawase_vs_code = g_etch_kawase_vs_msl; kawase_vs_size = sizeof(g_etch_kawase_vs_msl) - 1u;
        kawase_ps_code = g_etch_kawase_ps_msl; kawase_ps_size = sizeof(g_etch_kawase_ps_msl) - 1u;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No compatible shader format (DXIL/SPIR-V/MSL) supported by SDL_GPUDevice");
        return fail();
    }

    ctx->supports_d32_depth = (chosen_fmt != SDL_GPU_SHADERFORMAT_MSL) &&
        SDL_GPUTextureSupportsFormat(ctx->device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                                     SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);

    const SDL_GPUShaderCreateInfo vs_info = {
        .code_size = vs_size,
        .code = vs_code,
        .entrypoint = "vs_main",
        .format = chosen_fmt,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0u,
        .num_storage_textures = 0u,
        .num_storage_buffers = (chosen_fmt == SDL_GPU_SHADERFORMAT_MSL) ? 1u : 2u,
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
        .num_samplers = 9u,
        .num_storage_textures = 0u,
        .num_storage_buffers = 5u,
        .num_uniform_buffers = 0u,
        .props = 0u,
    };
    ctx->ps_shader = SDL_CreateGPUShader(ctx->device, &ps_info);

    const SDL_GPUShaderCreateInfo quad_vs_info = {
        .code_size = quad_vs_size,
        .code = quad_vs_code,
        .entrypoint = "vs_main",
        .format = chosen_fmt,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0u,
        .num_storage_textures = 0u,
        .num_storage_buffers = 2u,
        .num_uniform_buffers = 1u,
        .props = 0u,
    };
    ctx->quad_vs_shader = SDL_CreateGPUShader(ctx->device, &quad_vs_info);

    const SDL_GPUShaderCreateInfo quad_ps_info = {
        .code_size = quad_ps_size,
        .code = quad_ps_code,
        .entrypoint = "ps_main",
        .format = chosen_fmt,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 0u,
        .num_storage_textures = 0u,
        .num_storage_buffers = 1u,
        .num_uniform_buffers = 0u,
        .props = 0u,
    };
    ctx->quad_ps_shader = SDL_CreateGPUShader(ctx->device, &quad_ps_info);

    const SDL_GPUShaderCreateInfo subpixel_vs_info = {
        .code_size = subpixel_vs_size,
        .code = subpixel_vs_code,
        .entrypoint = "vs_main",
        .format = chosen_fmt,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0u,
        .num_storage_textures = 0u,
        .num_storage_buffers = 2u,
        .num_uniform_buffers = 1u,
        .props = 0u,
    };
    ctx->subpixel_vs_shader = SDL_CreateGPUShader(ctx->device, &subpixel_vs_info);

    const SDL_GPUShaderCreateInfo subpixel_ps_info = {
        .code_size = subpixel_ps_size,
        .code = subpixel_ps_code,
        .entrypoint = "ps_main",
        .format = chosen_fmt,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1u,
        .num_storage_textures = 0u,
        .num_storage_buffers = 1u,
        .num_uniform_buffers = 0u,
        .props = 0u,
    };
    ctx->subpixel_ps_shader = SDL_CreateGPUShader(ctx->device, &subpixel_ps_info);

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
        !ctx->quad_vs_shader || !ctx->quad_ps_shader ||
        !ctx->subpixel_vs_shader || !ctx->subpixel_ps_shader ||
        !ctx->kawase_vs_shader || !ctx->kawase_ps_shader)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUShader failed: %s", SDL_GetError());
        return fail();
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
        return fail();
    }
    SDL_GPUSamplerCreateInfo nearest_info = sampler_info;
    nearest_info.min_filter = SDL_GPU_FILTER_NEAREST;
    nearest_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    nearest_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    ctx->nearest_sampler = SDL_CreateGPUSampler(ctx->device, &nearest_info);
    if (!ctx->nearest_sampler) {
        return fail();
    }
    SDL_GPUSamplerCreateInfo mirror_info = sampler_info;
    mirror_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    mirror_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    mirror_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    ctx->mirror_linear_sampler = SDL_CreateGPUSampler(ctx->device, &mirror_info);
    if (!ctx->mirror_linear_sampler) {
        return fail();
    }

    const uint32_t prim_buf_bytes = ctx->max_prims * static_cast<uint32_t>(sizeof(Prim));
    const SDL_GPUBufferCreateInfo gpu_buf_info = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = prim_buf_bytes,
        .props = 0u,
    };
    ctx->prim_gpu_buf = SDL_CreateGPUBuffer(ctx->device, &gpu_buf_info);
    ctx->prim_gpu_cap = ctx->max_prims;

    const SDL_GPUTransferBufferCreateInfo tbuf_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = prim_buf_bytes,
        .props = 0u,
    };
    ctx->prim_transfer_buf = SDL_CreateGPUTransferBuffer(ctx->device, &tbuf_info);
    ctx->prim_tbuf_cap = ctx->max_prims;

    if (!ensure_prim_depth_buffers(ctx, ctx->max_prims)) {
        return fail();
    }

    const SDL_GPUTextureCreateInfo atlas_tex_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = ctx->atlas.width,
        .height = ctx->atlas.height,
        .layer_count_or_depth = 1u,
        .num_levels = 1u,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0u,
    };
    ctx->atlas.gpu_tex = SDL_CreateGPUTexture(ctx->device, &atlas_tex_info);

    const SDL_GPUTransferBufferCreateInfo atlas_tbuf_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = ctx->atlas.width * ctx->atlas.height * 4u,
        .props = 0u,
    };
    ctx->atlas_transfer_buf = SDL_CreateGPUTransferBuffer(ctx->device, &atlas_tbuf_info);

    ctx->atlas.gpu_w = ctx->atlas.width; ctx->atlas.gpu_h = ctx->atlas.height;
    ctx->atlas_tbuf_size = ctx->atlas.width * ctx->atlas.height * 4u;
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

    ctx->dyn_slug_curve_gpu_cap = 256;
    const SDL_GPUBufferCreateInfo dyn_curve_info = {.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, .size = 256u * sizeof(SlugCurveGpu), .props = 0u};
    ctx->dyn_slug_curve_gpu_buf = SDL_CreateGPUBuffer(ctx->device, &dyn_curve_info);
    ctx->dyn_slug_band_gpu_cap = 256;
    const SDL_GPUBufferCreateInfo dyn_band_info = {.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, .size = 256u * sizeof(SlugBandEntryGpu), .props = 0u};
    ctx->dyn_slug_band_gpu_buf = SDL_CreateGPUBuffer(ctx->device, &dyn_band_info);
    if (!ctx->prim_gpu_buf || !ctx->prim_transfer_buf ||
        !ctx->atlas.gpu_tex || !ctx->atlas_transfer_buf ||
        !ctx->slug_curve_gpu_buf || !ctx->slug_band_gpu_buf || !ctx->dyn_slug_curve_gpu_buf || !ctx->dyn_slug_band_gpu_buf)
    {
        return fail();
    }

    // Pre-create universal sRGB pipeline for offscreen/SDR_LINEAR rendering.
    if (!get_or_create_pipeline(ctx, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB)) return fail();

    return true;
}

struct OptPrimItem {
    uint32_t        orig_idx;
    float           x0, y0, x1, y1;
    uint64_t        state_key;
    uint32_t        blend_rgb;
    GpuPipelineKind pipe;
    SDL_GPUTexture* ext1;
    SDL_GPUTexture* ext2;
};

struct SpatialGridNode {
    uint32_t item_i;
    int32_t  next;
};

bool ensure_gpu_opt_buffers(Context* ctx, uint32_t prim_count, uint32_t blur_count) noexcept {
    if (prim_count > ctx->gpu_prim_cap) {
        uint32_t cap = ctx->gpu_prim_cap >= kMinPrimCap ? ctx->gpu_prim_cap : kMinPrimCap;
        while (cap < prim_count) cap = cap > kMaxPrimHardCap / 2u ? kMaxPrimHardCap : cap * 2u;
        Prim* np = static_cast<Prim*>(std::realloc(ctx->gpu_prims, size_t(cap) * sizeof(Prim)));
        if (!np) return false;
        ctx->gpu_prims = np;
        uint32_t* nd = static_cast<uint32_t*>(std::realloc(ctx->gpu_prim_depths, size_t(cap) * sizeof(uint32_t)));
        if (!nd) return false;
        ctx->gpu_prim_depths = nd;
        ctx->gpu_prim_cap = cap;
    }
    if (blur_count > ctx->gpu_blur_split_cap) {
        uint32_t cap = ctx->gpu_blur_split_cap ? ctx->gpu_blur_split_cap : 8u;
        while (cap < blur_count) cap *= 2u;
        BlurSplit* nb = static_cast<BlurSplit*>(std::realloc(ctx->gpu_blur_splits, size_t(cap) * sizeof(BlurSplit)));
        if (!nb) return false;
        ctx->gpu_blur_splits = nb;
        ctx->gpu_blur_split_cap = cap;
    }
    return true;
}

void compute_prim_screen_aabb(const Prim& prim, float* ox0, float* oy0, float* ox1, float* oy1) noexcept {
    float min_x = prim.bounds.x, min_y = prim.bounds.y;
    float max_x = min_x + prim.bounds.z, max_y = min_y + prim.bounds.w;
    float pad = 0.0f;
    const PrimKind kind = static_cast<PrimKind>(prim.style.x & 0xFu);
    if (kind == PrimKind::Rect) {
        const float soft = unpack_f16_cpu(static_cast<uint16_t>(prim.style.w >> 16));
        const bool is_int = (prim.bounds.x == std::floor(prim.bounds.x) &&
                             prim.bounds.y == std::floor(prim.bounds.y) &&
                             prim.bounds.z == std::floor(prim.bounds.z) &&
                             prim.bounds.w == std::floor(prim.bounds.w));
        pad = (soft > 1.0f) ? (soft * 0.5f + 0.5f) : (is_int ? 0.0f : 0.5f);
    } else if (kind == PrimKind::Glyph || kind == PrimKind::Texture || kind == PrimKind::BlurGlass ||
               kind == PrimKind::CanvasGrid || kind == PrimKind::MeshGrad || kind == PrimKind::LayerBlur) {
        pad = 0.0f;
    } else if (kind == PrimKind::Shadow) {
        pad = std::fmax(unpack_f16_cpu(static_cast<uint16_t>(prim.style.w >> 16)), 1.0f) * 2.0f + 2.0f;
    } else if (kind == PrimKind::BorderBeam) {
        pad = std::fmax(std::fmax(unpack_f16_cpu(static_cast<uint16_t>(prim.payload.y >> 16)), 1.0f) + 2.0f,
                        unpack_f16_cpu(static_cast<uint16_t>(prim.style.w >> 16)) * 0.5f + 1.0f);
    } else if (kind == PrimKind::PunchHole) {
        pad = std::fmax(1.0f, unpack_f16_cpu(static_cast<uint16_t>(prim.style.w >> 16)) * 0.5f + 1.0f);
    } else if (kind == PrimKind::RotatedRect || (kind == PrimKind::SlugGlyph && prim.payload.z != 0u)) {
        const float angle = (kind == PrimKind::RotatedRect)
            ? unpack_f16_cpu(static_cast<uint16_t>(prim.style.w >> 16))
            : std::bit_cast<float>(prim.payload.z);
        const float hx = 0.5f * prim.bounds.z, hy = 0.5f * prim.bounds.w;
        const float cx = prim.bounds.x + hx, cy = prim.bounds.y + hy;
        const float ac = std::fabs(std::cos(angle)), as = std::fabs(std::sin(angle));
        const float ex = ac * hx + as * hy, ey = as * hx + ac * hy;
        min_x = cx - ex; min_y = cy - ey;
        max_x = cx + ex; max_y = cy + ey;
        pad = 1.0f;
    } else {
        pad = 0.5f;
    }
    *ox0 = std::fmax(min_x - pad, prim.clip.x);
    *oy0 = std::fmax(min_y - pad, prim.clip.y);
    *ox1 = std::fmin(max_x + pad, prim.clip.z);
    *oy1 = std::fmin(max_y + pad, prim.clip.w);
}

GpuPipelineKind classify_prim_pipeline(const Prim& p, bool is_punch, bool is_additive, bool allow_opaque) noexcept {
    if (is_punch) return GpuPipelineKind::UberPunch;
    if (is_additive) return GpuPipelineKind::UberAdditive;
    const uint32_t header = p.style.x;
    const PrimKind kind = static_cast<PrimKind>(header & 0xFu);
    const uint32_t flags = (header >> 4) & 0x3Fu;
    const uint32_t tex_slot = (header >> 10) & 0xFu;
    if (kind == PrimKind::Glyph) {
        if (tex_slot == 0u && (flags & PrimFlag_LinearGrad) == 0u) {
            return GpuPipelineKind::SubpixelText;
        }
        return GpuPipelineKind::Uber;
    }
    if (kind == PrimKind::Rect) {
        if (allow_opaque) {
            const bool no_radii = (p.payload.z == 0u && p.payload.w == 0u);
            const bool rect_clip = ((flags & PrimFlag_RoundedClip) == 0u) &&
                                   ((header & (1u << 18)) == 0u) &&
                                   (((header >> 20) & 0xFFFu) == 0u);
            const bool fill_opaque = (p.style.y >> 24) == 255u;
            const bool has_grad = (flags & (PrimFlag_LinearGrad | PrimFlag_RadialGrad)) != 0u;
            const bool grad_opaque = !has_grad || ((p.payload.x >> 24) == 255u);
            const bool int_bounds =
                p.bounds.z >= 1.0f && p.bounds.w >= 1.0f &&
                p.bounds.x == std::floor(p.bounds.x) && p.bounds.y == std::floor(p.bounds.y) &&
                p.bounds.z == std::floor(p.bounds.z) && p.bounds.w == std::floor(p.bounds.w) &&
                p.clip.x == std::floor(p.clip.x) && p.clip.y == std::floor(p.clip.y) &&
                p.clip.z == std::floor(p.clip.z) && p.clip.w == std::floor(p.clip.w);
            const bool crisp_aa = unpack_f16_cpu(static_cast<uint16_t>(p.style.w >> 16)) <= 1.0f;
            if (no_radii && rect_clip && fill_opaque && grad_opaque && int_bounds && crisp_aa) {
                return GpuPipelineKind::QuadOpaque;
            }
        }
        return GpuPipelineKind::QuadAlpha;
    }
    if (kind == PrimKind::RotatedRect || kind == PrimKind::Shadow) {
        return GpuPipelineKind::QuadAlpha;
    }
    return GpuPipelineKind::Uber;
}

bool append_gpu_opt_prim(
    Context* ctx,
    uint32_t* out_prim_cursor,
    const OptPrimItem& item,
    bool force_new_batch) noexcept
{
    Prim prim = ctx->prims[item.orig_idx];
    const GpuPipelineKind pipe = item.pipe;
    const uint32_t blend_rgb = (pipe == GpuPipelineKind::SubpixelText) ? item.blend_rgb : 0u;
    const bool is_uber = (pipe == GpuPipelineKind::Uber ||
                          pipe == GpuPipelineKind::UberPunch ||
                          pipe == GpuPipelineKind::UberAdditive);

    if (ctx->gpu_batch_count == 0u || force_new_batch) {
        if (!ensure_batch_cap_array(&ctx->gpu_batches, &ctx->gpu_batch_cap, ctx->gpu_batch_count)) return false;
        ctx->gpu_batches[ctx->gpu_batch_count++] = DrawBatch{
            .first_prim = *out_prim_cursor,
            .prim_count = 0u,
            .ext_textures = {},
            .ext_count = 0u,
            .blend_rgba8 = blend_rgb,
            .pipeline = pipe,
            .is_punch_hole = (pipe == GpuPipelineKind::UberPunch),
            .is_additive = (pipe == GpuPipelineKind::UberAdditive),
        };
    }

    DrawBatch* cur = &ctx->gpu_batches[ctx->gpu_batch_count - 1u];
    if (cur->prim_count > 0u &&
        (cur->pipeline != pipe || (pipe == GpuPipelineKind::SubpixelText && cur->blend_rgba8 != blend_rgb)))
    {
        if (!ensure_batch_cap_array(&ctx->gpu_batches, &ctx->gpu_batch_cap, ctx->gpu_batch_count)) return false;
        cur = &ctx->gpu_batches[ctx->gpu_batch_count++];
        *cur = DrawBatch{
            .first_prim = *out_prim_cursor,
            .prim_count = 0u,
            .ext_textures = {},
            .ext_count = 0u,
            .blend_rgba8 = blend_rgb,
            .pipeline = pipe,
            .is_punch_hole = (pipe == GpuPipelineKind::UberPunch),
            .is_additive = (pipe == GpuPipelineKind::UberAdditive),
        };
    }
    cur->pipeline = pipe;
    cur->blend_rgba8 = blend_rgb;
    cur->is_punch_hole = (pipe == GpuPipelineKind::UberPunch);
    cur->is_additive = (pipe == GpuPipelineKind::UberAdditive);

    if (is_uber && (item.ext1 || item.ext2)) {
        auto find_slot = [](const DrawBatch* b, SDL_GPUTexture* t) noexcept -> uint16_t {
            if (!t) return 0u;
            for (uint32_t i = 0u; i < b->ext_count; ++i) {
                if (b->ext_textures[i] == t) return static_cast<uint16_t>(i + 1u);
            }
            return 0u;
        };
        uint16_t s1 = find_slot(cur, item.ext1);
        uint16_t s2 = find_slot(cur, item.ext2);
        uint32_t need = 0u;
        if (item.ext1 && s1 == 0u) ++need;
        if (item.ext2 && item.ext2 != item.ext1 && s2 == 0u) ++need;
        if (cur->ext_count + need > 8u) {
            if (!ensure_batch_cap_array(&ctx->gpu_batches, &ctx->gpu_batch_cap, ctx->gpu_batch_count)) return false;
            cur = &ctx->gpu_batches[ctx->gpu_batch_count++];
            *cur = DrawBatch{
                .first_prim = *out_prim_cursor,
                .prim_count = 0u,
                .ext_textures = {},
                .ext_count = 0u,
                .blend_rgba8 = 0u,
                .pipeline = pipe,
                .is_punch_hole = (pipe == GpuPipelineKind::UberPunch),
                .is_additive = (pipe == GpuPipelineKind::UberAdditive),
            };
            s1 = 0u;
            s2 = 0u;
        }
        if (item.ext1 && s1 == 0u) {
            cur->ext_textures[cur->ext_count] = item.ext1;
            s1 = static_cast<uint16_t>(++cur->ext_count);
        }
        if (item.ext2) {
            s2 = find_slot(cur, item.ext2);
            if (s2 == 0u) {
                cur->ext_textures[cur->ext_count] = item.ext2;
                s2 = static_cast<uint16_t>(++cur->ext_count);
            }
        }
        prim.style.x = (prim.style.x & ~(0xFu << 10)) | ((static_cast<uint32_t>(s1) & 0xFu) << 10);
        if (item.ext2) {
            prim.payload.y = static_cast<uint32_t>(s2);
        }
    }

    const uint32_t idx = (*out_prim_cursor)++;
    ctx->gpu_prims[idx] = prim;
    ctx->gpu_prim_depths[idx] = item.orig_idx;
    ++cur->prim_count;
    return true;
}

void emit_spatial_segment(
    Context* ctx,
    const OptPrimItem* items,
    uint32_t count,
    uint32_t* out_prim_cursor,
    bool force_first_new_batch) noexcept
{
    if (count == 0u) return;
    if (count == 1u) {
        (void)append_gpu_opt_prim(ctx, out_prim_cursor, items[0], force_first_new_batch);
        return;
    }

    const size_t seg_mark = ctx->frame_arena.mark();
    constexpr uint32_t kGridDim = 32u;
    constexpr uint32_t kGridCells = kGridDim * kGridDim;
    uint32_t* layers = ctx->frame_arena.alloc_uninit<uint32_t>(count);
    uint32_t* sorted = ctx->frame_arena.alloc_uninit<uint32_t>(count);
    const uint32_t node_cap = count <= 65536u ? count * 8u : count * 2u;
    SpatialGridNode* nodes = ctx->frame_arena.alloc_uninit<SpatialGridNode>(node_cap);
    int32_t* cell_head = ctx->frame_arena.alloc_uninit<int32_t>(kGridCells);
    uint16_t* cell_len = ctx->frame_arena.alloc<uint16_t>(kGridCells);
    int32_t* cell_max_layer = ctx->frame_arena.alloc_uninit<int32_t>(kGridCells);
    uint64_t* cell_max_key = ctx->frame_arena.alloc_uninit<uint64_t>(kGridCells);
    int32_t* cell_pruned_layer = ctx->frame_arena.alloc_uninit<int32_t>(kGridCells);
    uint64_t* cell_pruned_key = ctx->frame_arena.alloc_uninit<uint64_t>(kGridCells);
    if (!layers || !sorted || !nodes || !cell_head || !cell_len ||
        !cell_max_layer || !cell_max_key || !cell_pruned_layer || !cell_pruned_key)
    {
        ctx->frame_arena.rewind(seg_mark);
        for (uint32_t i = 0u; i < count; ++i) {
            (void)append_gpu_opt_prim(ctx, out_prim_cursor, items[i], i == 0u && force_first_new_batch);
        }
        return;
    }

    const float vw = static_cast<float>(ctx->active_ui->viewport_w > 0u ? ctx->active_ui->viewport_w : 1u);
    const float vh = static_cast<float>(ctx->active_ui->viewport_h > 0u ? ctx->active_ui->viewport_h : 1u);
    const float inv_cell_w = static_cast<float>(kGridDim) / vw;
    const float inv_cell_h = static_cast<float>(kGridDim) / vh;
    constexpr uint64_t kMixedKey = UINT64_MAX;

    for (uint32_t c = 0u; c < kGridCells; ++c) {
        cell_head[c] = -1;
        cell_max_layer[c] = -1;
        cell_max_key[c] = kMixedKey;
        cell_pruned_layer[c] = -1;
        cell_pruned_key[c] = kMixedKey;
    }

    uint32_t global_items[64] = {};
    uint32_t global_count = 0u;
    int32_t global_pruned_layer = -1;
    uint32_t node_used = 0u;
    uint32_t max_layer = 0u;

    for (uint32_t i = 0u; i < count; ++i) {
        const OptPrimItem& it = items[i];
        uint32_t L = (global_pruned_layer >= 0) ? (static_cast<uint32_t>(global_pruned_layer) + 1u) : 0u;

        for (uint32_t gi = 0u; gi < global_count; ++gi) {
            const uint32_t j = global_items[gi];
            const OptPrimItem& ot = items[j];
            if (it.x1 > ot.x0 && it.x0 < ot.x1 && it.y1 > ot.y0 && it.y0 < ot.y1) {
                const uint32_t need = (ot.state_key == it.state_key) ? layers[j] : (layers[j] + 1u);
                if (need > L) L = need;
            }
        }

        const uint32_t cx0 = static_cast<uint32_t>(clamp_range(std::floor(it.x0 * inv_cell_w), 0.0f, static_cast<float>(kGridDim - 1u)));
        const uint32_t cy0 = static_cast<uint32_t>(clamp_range(std::floor(it.y0 * inv_cell_h), 0.0f, static_cast<float>(kGridDim - 1u)));
        const uint32_t cx1 = static_cast<uint32_t>(clamp_range(std::floor((it.x1 - 1e-3f) * inv_cell_w), 0.0f, static_cast<float>(kGridDim - 1u)));
        const uint32_t cy1 = static_cast<uint32_t>(clamp_range(std::floor((it.y1 - 1e-3f) * inv_cell_h), 0.0f, static_cast<float>(kGridDim - 1u)));
        const uint32_t ncells = (cx1 - cx0 + 1u) * (cy1 - cy0 + 1u);

        auto update_cell_max = [&](uint32_t c, int32_t lv, uint64_t key) noexcept {
            if (lv > cell_max_layer[c]) {
                cell_max_layer[c] = lv;
                cell_max_key[c] = key;
            } else if (lv == cell_max_layer[c] && cell_max_key[c] != key) {
                cell_max_key[c] = kMixedKey;
            }
        };

        if (ncells > 64u) {
            for (uint32_t ty = cy0; ty <= cy1; ++ty) {
                for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                    const uint32_t c = ty * kGridDim + tx;
                    const int32_t ml = cell_max_layer[c];
                    if (ml >= 0) {
                        const uint32_t need = (cell_max_key[c] == it.state_key)
                            ? static_cast<uint32_t>(ml)
                            : (static_cast<uint32_t>(ml) + 1u);
                        if (need > L) L = need;
                    }
                }
            }
            layers[i] = L;
            for (uint32_t ty = cy0; ty <= cy1; ++ty) {
                for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                    update_cell_max(ty * kGridDim + tx, static_cast<int32_t>(L), it.state_key);
                }
            }
            if (global_count < 64u) {
                global_items[global_count++] = i;
            } else if (static_cast<int32_t>(L) > global_pruned_layer) {
                global_pruned_layer = static_cast<int32_t>(L);
            }
        } else {
            for (uint32_t ty = cy0; ty <= cy1; ++ty) {
                for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                    const uint32_t c = ty * kGridDim + tx;
                    if (cell_pruned_layer[c] >= 0) {
                        const uint32_t need = (cell_pruned_key[c] == it.state_key)
                            ? static_cast<uint32_t>(cell_pruned_layer[c])
                            : (static_cast<uint32_t>(cell_pruned_layer[c]) + 1u);
                        if (need > L) L = need;
                    }
                    for (int32_t ni = cell_head[c]; ni >= 0; ni = nodes[ni].next) {
                        const uint32_t j = nodes[ni].item_i;
                        const OptPrimItem& ot = items[j];
                        if (it.x1 > ot.x0 && it.x0 < ot.x1 && it.y1 > ot.y0 && it.y0 < ot.y1) {
                            const uint32_t need = (ot.state_key == it.state_key) ? layers[j] : (layers[j] + 1u);
                            if (need > L) L = need;
                        }
                    }
                }
            }
            layers[i] = L;
            for (uint32_t ty = cy0; ty <= cy1; ++ty) {
                for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                    const uint32_t c = ty * kGridDim + tx;
                    update_cell_max(c, static_cast<int32_t>(L), it.state_key);
                    if (cell_len[c] < 64u && node_used < node_cap) {
                        const int32_t ni = static_cast<int32_t>(node_used++);
                        nodes[ni] = SpatialGridNode{ i, cell_head[c] };
                        cell_head[c] = ni;
                        ++cell_len[c];
                    } else {
                        const int32_t lv = static_cast<int32_t>(L);
                        if (lv > cell_pruned_layer[c]) {
                            cell_pruned_layer[c] = lv;
                            cell_pruned_key[c] = it.state_key;
                        } else if (lv == cell_pruned_layer[c] && cell_pruned_key[c] != it.state_key) {
                            cell_pruned_key[c] = kMixedKey;
                        }
                    }
                }
            }
        }
        if (L > max_layer) max_layer = L;
    }

    const uint32_t num_layers = max_layer + 1u;
    uint32_t* counts = ctx->frame_arena.alloc<uint32_t>(num_layers + 1u);
    uint32_t* offsets = ctx->frame_arena.alloc_uninit<uint32_t>(num_layers);
    if (!counts || !offsets) {
        ctx->frame_arena.rewind(seg_mark);
        for (uint32_t i = 0u; i < count; ++i) {
            (void)append_gpu_opt_prim(ctx, out_prim_cursor, items[i], i == 0u && force_first_new_batch);
        }
        return;
    }

    for (uint32_t i = 0u; i < count; ++i) ++counts[layers[i] + 1u];
    for (uint32_t lv = 0u; lv < num_layers; ++lv) {
        counts[lv + 1u] += counts[lv];
        offsets[lv] = counts[lv];
    }
    for (uint32_t i = 0u; i < count; ++i) {
        sorted[offsets[layers[i]]++] = i;
    }

    // Per-layer stable emission: non-overlapping pipelines coalesce into minimal draw calls.
    int32_t* next_same_color = ctx->frame_arena.alloc_uninit<int32_t>(count);
    struct ColorRun { uint32_t rgb; int32_t head; int32_t tail; };
    ColorRun stack_runs[64];
    bool first_emitted = true;

    for (uint32_t lv = 0u; lv < num_layers; ++lv) {
        const uint32_t lo = counts[lv];
        const uint32_t hi = counts[lv + 1u];
        if (lo >= hi) continue;

        auto emit_one = [&](const OptPrimItem& it) noexcept {
            (void)append_gpu_opt_prim(ctx, out_prim_cursor, it, first_emitted && force_first_new_batch);
            first_emitted = false;
        };

        // 1. QuadAlpha
        for (uint32_t s = lo; s < hi; ++s) {
            const OptPrimItem& it = items[sorted[s]];
            if (it.pipe == GpuPipelineKind::QuadAlpha) emit_one(it);
        }

        // 2. SubpixelText (stably grouped by RGB blend constant)
        uint32_t run_count = 0u;
        bool overflow_colors = false;
        if (next_same_color) {
            for (uint32_t s = lo; s < hi; ++s) {
                const OptPrimItem& it = items[sorted[s]];
                if (it.pipe != GpuPipelineKind::SubpixelText) continue;
                uint32_t found = run_count;
                for (uint32_t r = 0u; r < run_count; ++r) {
                    if (stack_runs[r].rgb == it.blend_rgb) { found = r; break; }
                }
                if (found < run_count) {
                    next_same_color[stack_runs[found].tail] = static_cast<int32_t>(s);
                    next_same_color[s] = -1;
                    stack_runs[found].tail = static_cast<int32_t>(s);
                } else if (run_count < 64u) {
                    stack_runs[run_count++] = ColorRun{ it.blend_rgb, static_cast<int32_t>(s), static_cast<int32_t>(s) };
                    next_same_color[s] = -1;
                } else {
                    overflow_colors = true;
                }
            }
            for (uint32_t r = 0u; r < run_count; ++r) {
                for (int32_t idx = stack_runs[r].head; idx >= 0; idx = next_same_color[idx]) {
                    emit_one(items[sorted[idx]]);
                }
            }
            if (overflow_colors) {
                for (uint32_t s = lo; s < hi; ++s) {
                    const OptPrimItem& it = items[sorted[s]];
                    if (it.pipe != GpuPipelineKind::SubpixelText) continue;
                    bool in_runs = false;
                    for (uint32_t r = 0u; r < run_count; ++r) {
                        if (stack_runs[r].rgb == it.blend_rgb) { in_runs = true; break; }
                    }
                    if (!in_runs) emit_one(it);
                }
            }
        } else {
            for (uint32_t s = lo; s < hi; ++s) {
                const OptPrimItem& it = items[sorted[s]];
                if (it.pipe == GpuPipelineKind::SubpixelText) emit_one(it);
            }
        }

        // 3. Uber
        for (uint32_t s = lo; s < hi; ++s) {
            const OptPrimItem& it = items[sorted[s]];
            if (it.pipe == GpuPipelineKind::Uber) emit_one(it);
        }

        // 4. UberAdditive
        for (uint32_t s = lo; s < hi; ++s) {
            const OptPrimItem& it = items[sorted[s]];
            if (it.pipe == GpuPipelineKind::UberAdditive) emit_one(it);
        }
    }

    ctx->frame_arena.rewind(seg_mark);
}

} // namespace

void optimize_render_batches(Context* ctx) noexcept {
    if (!ctx) return;
    ctx->gpu_batch_count = 0u;
    ctx->gpu_blur_split_count = 0u;
    ctx->gpu_use_early_z = false;
    if (ctx->prim_count == 0u) return;

    if (!ensure_gpu_opt_buffers(ctx, ctx->prim_count, ctx->blur_split_count)) {
        return;
    }

    bool has_punch_hole = false;
    for (uint32_t b = 0u; b < ctx->batch_count; ++b) {
        if (ctx->batches[b].prim_count > 0u && ctx->batches[b].is_punch_hole) {
            has_punch_hole = true;
            break;
        }
    }

    const bool allow_early_z = ctx->supports_d32_depth &&
                               ctx->blur_split_count == 0u &&
                               !has_punch_hole &&
                               ctx->prim_count <= 4000000u;

    const size_t opt_mark = ctx->frame_arena.mark();
    bool items_heap = false;
    OptPrimItem* items = ctx->frame_arena.alloc_uninit<OptPrimItem>(ctx->prim_count);
    if (!items) {
        items = static_cast<OptPrimItem*>(std::malloc(size_t(ctx->prim_count) * sizeof(OptPrimItem)));
        items_heap = (items != nullptr);
    }
    auto make_opt_item = [&](const DrawBatch& sb, uint32_t pi) noexcept -> OptPrimItem {
        const Prim& p = ctx->prims[pi];
        const GpuPipelineKind pipe = classify_prim_pipeline(p, sb.is_punch_hole, sb.is_additive, allow_early_z);
        const uint32_t tex_slot = (p.style.x >> 10) & 0xFu;
        const PrimKind kind = static_cast<PrimKind>(p.style.x & 0xFu);
        SDL_GPUTexture* ext1 = (tex_slot > 0u && tex_slot <= sb.ext_count) ? sb.ext_textures[tex_slot - 1u] : nullptr;
        SDL_GPUTexture* ext2 = (kind == PrimKind::LayerBlur && p.payload.y > 0u && p.payload.y <= sb.ext_count)
            ? sb.ext_textures[p.payload.y - 1u] : nullptr;
        const uint32_t blend_rgb = (pipe == GpuPipelineKind::SubpixelText) ? (p.style.y & 0x00FFFFFFu) : 0u;
        const uint64_t state_key = (static_cast<uint64_t>(pipe) << 32) | static_cast<uint64_t>(blend_rgb);
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        compute_prim_screen_aabb(p, &x0, &y0, &x1, &y1);
        return OptPrimItem{
            .orig_idx = pi,
            .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1,
            .state_key = state_key,
            .blend_rgb = blend_rgb,
            .pipe = pipe,
            .ext1 = ext1,
            .ext2 = ext2,
        };
    };
    if (!items) {
        // Extreme OS OOM fallback: still classify every primitive in-place so SubpixelText
        // glyphs are never misrouted into Uber with tex_slot == 0.
        uint32_t out_prim_cursor = 0u;
        uint32_t next_split = 0u;
        for (uint32_t b = 0u; b < ctx->batch_count; ++b) {
            const DrawBatch& sb = ctx->batches[b];
            const uint32_t end_p = sb.first_prim + sb.prim_count;
            for (uint32_t pi = sb.first_prim; pi < end_p && pi < ctx->prim_count; ++pi) {
                bool split_boundary = false;
                while (next_split < ctx->blur_split_count && ctx->blur_splits[next_split].split_prim <= pi) {
                    BlurSplit mapped = ctx->blur_splits[next_split];
                    mapped.split_batch = ctx->gpu_batch_count;
                    mapped.split_prim = out_prim_cursor;
                    ctx->gpu_blur_splits[ctx->gpu_blur_split_count++] = mapped;
                    ++next_split;
                    split_boundary = true;
                }
                const OptPrimItem it = make_opt_item(sb, pi);
                (void)append_gpu_opt_prim(ctx, &out_prim_cursor, it, split_boundary);
            }
        }
        while (next_split < ctx->blur_split_count) {
            BlurSplit mapped = ctx->blur_splits[next_split];
            mapped.split_batch = ctx->gpu_batch_count;
            mapped.split_prim = out_prim_cursor;
            ctx->gpu_blur_splits[ctx->gpu_blur_split_count++] = mapped;
            ++next_split;
        }
        ctx->frame_arena.rewind(opt_mark);
        return;
    }

    uint32_t item_count = 0u;
    uint32_t opaque_count = 0u;
    for (uint32_t b = 0u; b < ctx->batch_count; ++b) {
        const DrawBatch& sb = ctx->batches[b];
        const uint32_t end_p = sb.first_prim + sb.prim_count;
        for (uint32_t pi = sb.first_prim; pi < end_p && pi < ctx->prim_count; ++pi) {
            const OptPrimItem it = make_opt_item(sb, pi);
            items[item_count++] = it;
            if (it.pipe == GpuPipelineKind::QuadOpaque) ++opaque_count;
        }
    }

    uint32_t out_prim_cursor = 0u;
    if (allow_early_z && opaque_count > 0u) {
        ctx->gpu_use_early_z = true;
        // Pass 0: Front-to-back opaque Early-Z quad batch (writes depth, zero alpha blending).
        bool first_op = true;
        for (uint32_t i = item_count; i > 0u; --i) {
            const OptPrimItem& it = items[i - 1u];
            if (it.pipe == GpuPipelineKind::QuadOpaque) {
                (void)append_gpu_opt_prim(ctx, &out_prim_cursor, it, first_op);
                first_op = false;
            }
        }
        // Compact remaining non-opaque items in original back-to-front order and spatially coalesce.
        uint32_t rem_count = 0u;
        for (uint32_t i = 0u; i < item_count; ++i) {
            if (items[i].pipe != GpuPipelineKind::QuadOpaque) {
                items[rem_count++] = items[i];
            }
        }
        if (rem_count > 0u) {
            emit_spatial_segment(ctx, items, rem_count, &out_prim_cursor, true);
        }
        if (items_heap) std::free(items);
        ctx->frame_arena.rewind(opt_mark);
        return;
    }

    // No Early-Z (blur splits / punch-hole / no opaque rects): partition by blur splits & punch holes.
    uint32_t next_split = 0u;
    uint32_t seg_start = 0u;
    while (seg_start < item_count) {
        while (next_split < ctx->blur_split_count && ctx->blur_splits[next_split].split_prim <= items[seg_start].orig_idx) {
            BlurSplit mapped = ctx->blur_splits[next_split];
            mapped.split_batch = ctx->gpu_batch_count;
            mapped.split_prim = out_prim_cursor;
            ctx->gpu_blur_splits[ctx->gpu_blur_split_count++] = mapped;
            ++next_split;
        }
        const uint32_t barrier_prim = (next_split < ctx->blur_split_count)
            ? ctx->blur_splits[next_split].split_prim : UINT32_MAX;
        const bool is_punch = (items[seg_start].pipe == GpuPipelineKind::UberPunch);
        uint32_t seg_end = seg_start + 1u;
        while (seg_end < item_count &&
               items[seg_end].orig_idx < barrier_prim &&
               (items[seg_end].pipe == GpuPipelineKind::UberPunch) == is_punch)
        {
            ++seg_end;
        }
        if (is_punch) {
            for (uint32_t i = seg_start; i < seg_end; ++i) {
                (void)append_gpu_opt_prim(ctx, &out_prim_cursor, items[i], i == seg_start);
            }
        } else {
            emit_spatial_segment(ctx, items + seg_start, seg_end - seg_start, &out_prim_cursor, true);
        }
        seg_start = seg_end;
    }
    while (next_split < ctx->blur_split_count) {
        BlurSplit mapped = ctx->blur_splits[next_split];
        mapped.split_batch = ctx->gpu_batch_count;
        mapped.split_prim = out_prim_cursor;
        ctx->gpu_blur_splits[ctx->gpu_blur_split_count++] = mapped;
        ++next_split;
    }

    if (items_heap) std::free(items);
    ctx->frame_arena.rewind(opt_mark);
}

Context* create_context(const ContextDesc& desc) noexcept {
    Context* ctx = static_cast<Context*>(std::calloc(1u, sizeof(Context)));
    if (!ctx) {
        return nullptr;
    }
    ctx->supports_d32_depth = true;

    ctx->max_layout_elements = desc.max_layout_elements < 4096u ? 4096u : (desc.max_layout_elements > (1u << 20) ? (1u << 20) : desc.max_layout_elements);
    if (!ensure_batch_cap(ctx)) { destroy_context(ctx); return nullptr; }
    ctx->arena_epoch = 1u;
    ctx->base_theme = Theme::dark();
    ctx->active_ui = &ctx->headless_ui;
    ctx->headless = desc.headless;
    ctx->enable_gpu_debug = desc.enable_gpu_debug;
    ctx->time_scale=1;
#if defined(_WIN32)
    BOOL animation_enabled=TRUE;
    if(desc.respect_reduced_motion && SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation_enabled,0)) ctx->reduced_motion=!animation_enabled;
#endif
    ctx->enable_os_a11y = desc.enable_os_a11y;
    ctx->refresh_fn = desc.on_window_refresh;
    ctx->refresh_user_data = desc.on_window_refresh ? desc.refresh_user_data : nullptr;
    if (desc.gpu_driver) {
        std::strncpy(ctx->gpu_driver, desc.gpu_driver, sizeof(ctx->gpu_driver) - 1u);
        ctx->gpu_driver[sizeof(ctx->gpu_driver) - 1u] = '\0';
    }
    ctx->max_prims = (desc.max_prims_per_frame > 0u) ? desc.max_prims_per_frame : 65536u;
    if (ctx->max_prims < kMinPrimCap) ctx->max_prims = kMinPrimCap;
    if (ctx->max_prims > kMaxPrimHardCap) ctx->max_prims = kMaxPrimHardCap;
    ctx->frame_arena = arena_create(desc.frame_arena_bytes ? desc.frame_arena_bytes : (4u * 1024u * 1024u));
    if (!ctx->frame_arena.base) {
        destroy_context(ctx);
        return nullptr;
    }

    ctx->prims = static_cast<Prim*>(std::calloc(ctx->max_prims, sizeof(Prim)));
    if (!ctx->prims) {
        destroy_context(ctx);
        return nullptr;
    }

    ctx->atlas.scratch = &ctx->frame_arena;
    if (!font_atlas_init(&ctx->atlas, desc.font_path)) {
        destroy_context(ctx);
        return nullptr;
    }
    // font_atlas_init clears the atlas, including its scratch pointer.
    ctx->atlas.scratch = &ctx->frame_arena;

    // Only allocate the headless Clay arena eagerly for headless contexts; windowed contexts
    // own per-window arenas via claim_window_swapchain() and initialize headless_ui lazily if ever needed.
    if (desc.window == nullptr && desc.headless) {
        if (!ensure_window_ui_clay(ctx, &ctx->headless_ui, 1280.0f, 720.0f)) {
            destroy_context(ctx);
            return nullptr;
        }
    }

    if (desc.window != nullptr || !desc.headless) {
        if (!ensure_gpu_device(ctx)) { destroy_context(ctx); return nullptr; }
    }

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
    if (ctx->wake_event_type == 0 && (SDL_WasInit(SDL_INIT_EVENTS) || SDL_WasInit(SDL_INIT_VIDEO))) {
        ctx->wake_event_type = SDL_RegisterEvents(1);
    }

    set_os_a11y_enabled(ctx, desc.enable_os_a11y);
    sync_window_event_watch(ctx);
    set_current_context(ctx);
    return ctx;
}

SDL_GPUDevice* gpu_device(Context* ctx) noexcept {
    if (!ctx) return nullptr;
    (void)ensure_gpu_device(ctx);
    return ctx->device;
}

void destroy_context(Context* ctx) noexcept {
    if (!ctx) {
        return;
    }
    if (current_context() == ctx) {
        set_current_context(nullptr);
    }

    ctx->in_frame = false;
    detach_window_event_watch(ctx);
    if (ctx->device) SDL_WaitForGPUIdle(ctx->device);
    for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i)
        if (ctx->claimed_windows[i]) release_window(ctx, ctx->claimed_windows[i]->window);
    ctx->active_ui = &ctx->headless_ui;
    if (ctx->device) {
        release_shader_surfaces(ctx);
        release_pipeline_cache(ctx->device, ctx->additive_pipelines, &ctx->additive_pipeline_count);
        release_pipeline_cache(ctx->device, ctx->additive_depth_pipelines, &ctx->additive_depth_pipeline_count);
        release_pipeline_cache(ctx->device, ctx->punch_pipelines, &ctx->punch_pipeline_count);
        release_pipeline_cache(ctx->device, ctx->pipelines, &ctx->pipeline_count);
        release_pipeline_cache(ctx->device, ctx->uber_depth_pipelines, &ctx->uber_depth_pipeline_count);
        release_pipeline_cache(ctx->device, ctx->quad_opaque_pipelines, &ctx->quad_opaque_pipeline_count);
        release_pipeline_cache(ctx->device, ctx->quad_alpha_pipelines, &ctx->quad_alpha_pipeline_count);
        release_pipeline_cache(ctx->device, ctx->quad_alpha_depth_pipelines, &ctx->quad_alpha_depth_pipeline_count);
        release_pipeline_cache(ctx->device, ctx->subpixel_pipelines, &ctx->subpixel_pipeline_count);
        release_pipeline_cache(ctx->device, ctx->subpixel_depth_pipelines, &ctx->subpixel_depth_pipeline_count);
        release_pipeline_cache(ctx->device, ctx->present_pipelines, &ctx->present_pipeline_count);
        if (ctx->kawase_pipe) SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->kawase_pipe);
        if (ctx->filter_kawase_pipe) SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->filter_kawase_pipe);
        if (ctx->bloom_pipe) SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->bloom_pipe);
        if (ctx->vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->vs_shader);
        if (ctx->ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->ps_shader);
        if (ctx->quad_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->quad_vs_shader);
        if (ctx->quad_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->quad_ps_shader);
        if (ctx->subpixel_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->subpixel_vs_shader);
        if (ctx->subpixel_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->subpixel_ps_shader);
        if (ctx->kawase_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->kawase_vs_shader);
        if (ctx->kawase_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->kawase_ps_shader);
        if (ctx->linear_sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->linear_sampler);
        if (ctx->nearest_sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->nearest_sampler);
        if (ctx->mirror_linear_sampler) SDL_ReleaseGPUSampler(ctx->device, ctx->mirror_linear_sampler);
        if (ctx->surface_fallback_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->surface_fallback_tex);
        if (ctx->prim_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->prim_gpu_buf);
        if (ctx->prim_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf);
        if (ctx->prim_depth_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->prim_depth_gpu_buf);
        if (ctx->prim_depth_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->prim_depth_transfer_buf);
        if (ctx->dyn_slug_curve_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->dyn_slug_curve_gpu_buf);
        if (ctx->dyn_slug_curve_tbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->dyn_slug_curve_tbuf);
        if (ctx->dyn_slug_band_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->dyn_slug_band_gpu_buf);
        if (ctx->dyn_slug_band_tbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->dyn_slug_band_tbuf);
        if (ctx->slug_curve_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->slug_curve_gpu_buf);
        if (ctx->slug_band_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->slug_band_gpu_buf);
        if (ctx->slug_curve_tbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->slug_curve_tbuf);
        if (ctx->slug_band_tbuf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->slug_band_tbuf);
        if (ctx->atlas_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->atlas_transfer_buf);
        if (ctx->subpixel_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->subpixel_transfer_buf);
        if(ctx->texture_upload_cmd) {SDL_CancelGPUCommandBuffer(ctx->texture_upload_cmd);ctx->texture_upload_cmd=nullptr;}
        if(ctx->clip_shape_gpu) SDL_ReleaseGPUBuffer(ctx->device,ctx->clip_shape_gpu);
        if(ctx->clip_shape_transfer) SDL_ReleaseGPUTransferBuffer(ctx->device,ctx->clip_shape_transfer);
        ctx->clip_shape_gpu=nullptr;ctx->clip_shape_transfer=nullptr;ctx->clip_shape_gpu_capacity=ctx->clip_shape_transfer_capacity=0;
        if (ctx->texture_upload_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->texture_upload_buf);
        if (ctx->atlas.gpu_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->atlas.gpu_tex);
        if (ctx->atlas.subpixel_gpu_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->atlas.subpixel_gpu_tex);
        release_blur_targets(ctx);
        release_filter_targets(ctx);
        release_depth_target(ctx);
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
    for (uint32_t bi = 0u; bi < 2u; ++bi) {
        std::free(ctx->shape_cache.banks[bi].entries);
        std::free(ctx->shape_cache.banks[bi].glyphs);
        std::free(ctx->shape_cache.banks[bi].text);
        ctx->shape_cache.banks[bi] = {};
    }
    ctx->shape_cache.initialized = false;
    shutdown_dialog_queue(ctx);
    std::free(ctx->clipboard_fallback);
    ctx->clipboard_fallback = nullptr;
    ctx->clipboard_fallback_cap = 0u;
    std::free(ctx->clip_shapes);
    std::free(ctx->dyn_slug_curves);
    std::free(ctx->dyn_slug_bands);
    std::free(ctx->prims);
    destroy_window_ui_state(ctx, &ctx->headless_ui);
    std::free(ctx->claimed_windows); std::free(ctx->batches); std::free(ctx->blur_splits);
    std::free(ctx->gpu_prims); std::free(ctx->gpu_prim_depths);
    std::free(ctx->gpu_batches); std::free(ctx->gpu_blur_splits);
    std::free(ctx->filter_prims); std::free(ctx->filter_batches); std::free(ctx->filter_groups);
    arena_destroy(&ctx->frame_arena);

    if (ctx->owns_video_init) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    std::free(ctx);
}

bool render_window(Context* ctx, Color clear_color, SDL_Window* window_override) noexcept {
    if (!ensure_gpu_device(ctx)) {
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

    WindowUiState* target_ui = window_ui(ctx, target_win);
    if (target_ui && (swap_w != target_ui->viewport_w || swap_h != target_ui->viewport_h)) {
        target_ui->viewport_w = swap_w;
        target_ui->viewport_h = swap_h;
        if (target_ui->redraw_budget < 2u) {
            target_ui->redraw_budget = 2u;
        }
    }
    WindowUiState* saved_ui = ctx->active_ui;
    SDL_Window* saved_win = ctx->active_window;
    if (target_ui) ctx->active_ui = target_ui;
    ctx->active_window = target_win;
    const SDL_GPUTextureFormat swap_fmt = SDL_GetGPUSwapchainTextureFormat(ctx->device, target_win);
    const bool recorded = record_frame_commands(ctx, cmd, swap_tex, swap_fmt, swap_w, swap_h, clear_color);
    ctx->active_ui = saved_ui;
    ctx->active_window = saved_win;
    if (!recorded) {
        const bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
        complete_shader_surfaces(ctx, target_win, submitted);
        return false;
    }

    const bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
    complete_shader_surfaces(ctx, target_win, submitted);
    const uint64_t t1 = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    ctx->stats.draw_call_count = ctx->gpu_batch_count;
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
    if (!ensure_gpu_device(ctx) || width == 0u || height == 0u || !out_png_path || out_png_path[0] == '\0') {
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
        complete_shader_surfaces(ctx, ctx->active_window, false);
        return false;
    }

    SDL_GPUCopyPass* dl_pass = SDL_BeginGPUCopyPass(cmd);
    if (!dl_pass) {
        SDL_CancelGPUCommandBuffer(cmd);
        complete_shader_surfaces(ctx, ctx->active_window, false);
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
    complete_shader_surfaces(ctx, ctx->active_window, fence != nullptr);
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
    ctx->stats.draw_call_count = ctx->gpu_batch_count;
    ctx->stats.gpu_submit_ms = (freq > 0u)
        ? (static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(freq))
        : 0.0;

    return wrote != 0;
}

namespace {

void invalidate_surfaces_sampling_texture(Context* ctx, SDL_GPUTexture* texture) noexcept {
    for (ShaderSurfaceSlot& s : ctx->shader_surfaces) {
        if (!s.shader) continue;
        for (uint32_t i = 0u; i < s.num_samplers; ++i) {
            if (s.eval.textures[i] == texture) { s.dirty = true; break; }
        }
    }
}

bool upload_rgba_rows(
    Context* ctx,
    SDL_GPUTexture* texture,
    uint32_t width,
    uint32_t height,
    const void* rgba_pixels,
    uint32_t row_pitch_bytes,
    bool cycle, uint32_t dst_x = 0, uint32_t dst_y = 0) noexcept
{
    const uint32_t tight_pitch = width * 4u;
    const uint32_t src_pitch = (row_pitch_bytes != 0u) ? row_pitch_bytes : tight_pitch;
    const uint32_t total_bytes = tight_pitch * height;

    if (ctx->texture_upload_cap < total_bytes) {
        uint32_t capacity=ctx->texture_upload_cap ? ctx->texture_upload_cap : 4096;
        while(capacity<total_bytes) capacity*=2;
        const SDL_GPUTransferBufferCreateInfo info={.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,.size=capacity,.props=0u};
        auto* next=SDL_CreateGPUTransferBuffer(ctx->device,&info);
        if(!next) return false;
        if(ctx->texture_upload_buf) SDL_ReleaseGPUTransferBuffer(ctx->device,ctx->texture_upload_buf);
        ctx->texture_upload_buf=next;ctx->texture_upload_cap=capacity;
    }
    SDL_GPUTransferBuffer* tbuf=ctx->texture_upload_buf;
    void* mapped=SDL_MapGPUTransferBuffer(ctx->device,tbuf,true);
    if(!mapped) return false;

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

    if(!ctx->texture_upload_cmd) ctx->texture_upload_cmd=SDL_AcquireGPUCommandBuffer(ctx->device);
    if(!ctx->texture_upload_cmd) return false;
    SDL_GPUCopyPass* copy=SDL_BeginGPUCopyPass(ctx->texture_upload_cmd);
    if(!copy) return false;

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
        .x = dst_x,
        .y = dst_y,
        .z = 0u,
        .w = width,
        .h = height,
        .d = 1u,
    };
    SDL_UploadToGPUTexture(copy, &src_info, &dst_reg, cycle);
    SDL_EndGPUCopyPass(copy);

    return true;
}


} // namespace

bool flush_texture_uploads(Context* ctx) noexcept {
    if(!ctx || !ctx->device) return false;
    if(!ctx->texture_upload_cmd) return true;
    SDL_GPUCommandBuffer* cmd=ctx->texture_upload_cmd;ctx->texture_upload_cmd=nullptr;
    return SDL_SubmitGPUCommandBuffer(cmd);
}

void release_window(Context* ctx, SDL_Window* window) noexcept {
    if (!ctx || !window || ctx->in_frame) return;
    for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i) {
        if (!ctx->claimed_windows[i]) continue;
        ClaimedWindowSlot& slot = *ctx->claimed_windows[i];
        if (slot.window != window) continue;
        detach_shader_surface_window(ctx, window);
        if (ctx->ime_active_window == window) {
            SDL_StopTextInput(window); ctx->ime_active_window = nullptr;
        }
        if (ctx->mouse_capture_window == window) {
            SDL_CaptureMouse(false); ctx->mouse_capture_window = nullptr;
        }
        if (slot.hit_test_attached) SDL_SetWindowHitTest(window, nullptr, nullptr);
        os_a11y_detach_window(ctx, &slot);
        detach_win32_chrome(&slot);
        WindowUiState* saved = ctx->active_ui;
        ctx->active_ui = &slot.ui;
        release_blur_targets(ctx);
        release_filter_targets(ctx);
        release_depth_target(ctx);
        ctx->active_ui = saved == &slot.ui ? &ctx->headless_ui : saved;
        destroy_window_ui_state(ctx, &slot.ui);
        SDL_ReleaseWindowFromGPUDevice(ctx->device, window);
        slot.window = nullptr;
        slot.hit_test_attached = false;
        std::free(ctx->claimed_windows[i]); ctx->claimed_windows[i] = nullptr;
        --ctx->claimed_window_count;
        if (ctx->active_window == window) ctx->active_window = nullptr;
        if (ctx->draw_list_window == window) ctx->draw_list_window = nullptr;
        if (ctx->window == window) {
            ctx->window = nullptr;
            for (uint32_t j = 0; j < ctx->claimed_window_cap; ++j)
                if (ctx->claimed_windows[j]) { ctx->window = ctx->claimed_windows[j]->window; break; }
        }
        return;
    }
}

uint32_t claimed_window_count(const Context* ctx) noexcept {
    return ctx ? ctx->claimed_window_count : 0u;
}

LoadedTexture create_texture_rgba(Context* ctx, const TextureDesc& desc) noexcept {
    if (!ensure_gpu_device(ctx) || desc.width == 0u || desc.height == 0u) {
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
    if (!ctx || !ctx->device || !texture || width == 0u || height == 0u || width>16384 || height>16384 || !rgba_pixels) {
        return false;
    }
    const uint32_t tight_pitch = width * 4u;
    if (row_pitch_bytes != 0u && row_pitch_bytes < tight_pitch) {
        return false;
    }
    if (!upload_rgba_rows(ctx, texture, width, height, rgba_pixels, row_pitch_bytes, true)) return false;
    invalidate_surfaces_sampling_texture(ctx, texture);
    return true;
}

bool update_texture_region_rgba(Context* ctx, SDL_GPUTexture* texture,
    uint32_t x, uint32_t y, uint32_t width, uint32_t height,
    const void* rgba_pixels, uint32_t row_pitch_bytes) noexcept {
    if(!ctx || !ctx->device || !texture || !rgba_pixels || !width || !height ||
       x>=16384 || y>=16384 || width>16384-x || height>16384-y ||
       (row_pitch_bytes && row_pitch_bytes<width*4)) return false;
    // Cycling the destination would discard pixels outside this rectangle.
    if (!upload_rgba_rows(ctx,texture,width,height,rgba_pixels,row_pitch_bytes,false,x,y)) return false;
    invalidate_surfaces_sampling_texture(ctx, texture);
    return true;
}

LoadedTexture load_texture_file(Context* ctx, const char* path, bool srgb) noexcept {
    if (!ctx) return {};
    DecodedImage image{};
    if (!decode_image_file(path, &image)) return {};
    const auto result = create_texture_rgba(ctx, {.rgba_pixels=image.rgba, .width=image.width, .height=image.height, .srgb=srgb});
    free_decoded_image(&image); return result;
}
LoadedTexture load_texture_memory(Context* ctx, const void* data, size_t size_bytes, bool srgb) noexcept {
    if (!ctx) return {};
    DecodedImage image{};
    if (!decode_image_memory(data, size_bytes, &image)) return {};
    const auto result = create_texture_rgba(ctx, {.rgba_pixels=image.rgba, .width=image.width, .height=image.height, .srgb=srgb});
    free_decoded_image(&image); return result;
}

void destroy_texture(Context* ctx, LoadedTexture* tex) noexcept {
    if (!tex) {
        return;
    }
    if (ctx && ctx->device && tex->texture) {
        flush_texture_uploads(ctx);
        SDL_ReleaseGPUTexture(ctx->device, tex->texture);
    }
    tex->texture = nullptr;
    tex->width = 0u;
    tex->height = 0u;
}

void destroy_texture(Context* ctx, SDL_GPUTexture* tex) noexcept {
    if (ctx && ctx->device && tex) {
        flush_texture_uploads(ctx);
        SDL_ReleaseGPUTexture(ctx->device, tex);
    }
}

void rebuild_gpu_batches_for_test(Context* ctx) noexcept {
    if (!ctx) return;
    optimize_render_batches(ctx);
    ctx->stats.draw_call_count = ctx->gpu_batch_count;
}

bool verify_arena_exhausted_glyph_pipeline_and_caches() noexcept {
    // 1. Exhausted frame_arena during optimize_render_batches must still classify PrimKind::Glyph
    //    into GpuPipelineKind::SubpixelText (never Uber with tex_slot == 0).
    {
        Context* ctx = create_context({.headless = true});
        if (!ctx) return false;
        Context* prev_ctx = current_context();
        set_current_context(ctx);
        set_hinted_small_text(ctx, true);

        begin_frame(ctx, {.viewport_w = 400, .viewport_h = 200, .dpi_scale = 1.0f});
        begin_box({.axis = Axis::Col, .width = Sizing::fixed(400), .height = Sizing::fixed(200),
                   .background = Color::rgb_hex(0x16181D)});
        text(str_lit("Hello World 12345 small text"), {.color = Color::rgb_hex(0xE8ECF4), .font_size_px = 14.0f});
        end_box();
        end_frame(ctx);

        // Ensure at least one hinted PrimKind::Glyph (tex_slot == 0) exists on all platforms.
        bool has_hinted_glyph = false;
        for (uint32_t i = 0u; i < ctx->prim_count; ++i) {
            const Prim& p = ctx->prims[i];
            if (static_cast<PrimKind>(p.style.x & 0xFu) == PrimKind::Glyph &&
                ((p.style.x >> 10) & 0xFu) == 0u)
            {
                has_hinted_glyph = true;
                break;
            }
        }
        if (!has_hinted_glyph) {
            const Prim synthetic = Prim::make_glyph(
                Rect{10.0f, 10.0f, 8.0f, 12.0f},
                Rect{0.0f, 0.0f, 400.0f, 200.0f},
                Color::rgb_hex(0xE8ECF4),
                Vec2{0.01f, 0.01f},
                Vec2{0.03f, 0.04f},
                0u,
                0.0f,
                PrimFlag_StemDarken);
            push_prim(ctx, synthetic, nullptr);
        }

        // Exhaust frame_arena completely before running optimize_render_batches.
        const size_t saved_offset = ctx->frame_arena.offset;
        ctx->frame_arena.offset = ctx->frame_arena.capacity;
        optimize_render_batches(ctx);
        ctx->frame_arena.offset = saved_offset;

        uint32_t subpixel_glyphs = 0u;
        uint32_t misrouted_uber_glyphs = 0u;
        uint32_t total_gpu_prims = 0u;
        for (uint32_t b = 0u; b < ctx->gpu_batch_count; ++b) {
            const DrawBatch& gb = ctx->gpu_batches[b];
            total_gpu_prims += gb.prim_count;
            for (uint32_t k = 0u; k < gb.prim_count; ++k) {
                const Prim& gp = ctx->gpu_prims[gb.first_prim + k];
                const PrimKind kind = static_cast<PrimKind>(gp.style.x & 0xFu);
                const uint32_t tex_slot = (gp.style.x >> 10) & 0xFu;
                if (kind == PrimKind::Glyph && tex_slot == 0u) {
                    if (gb.pipeline == GpuPipelineKind::SubpixelText) {
                        ++subpixel_glyphs;
                    } else if (gb.pipeline == GpuPipelineKind::Uber) {
                        ++misrouted_uber_glyphs;
                    }
                }
            }
        }
        destroy_context(ctx);
        set_current_context(prev_ctx);
        if (subpixel_glyphs == 0u || misrouted_uber_glyphs != 0u || total_gpu_prims == 0u) {
            return false;
        }
    }

    // 2. Exhausted frame_arena during get_or_cache_hinted_glyph & slug_get_glyph must still produce
    //    valid glyphs and never poison caches on transient failure.
    {
        Context* ctx = create_context({.headless = true});
        if (!ctx) return false;
        Context* prev_ctx = current_context();
        set_current_context(ctx);

        const FontSlot& fs = ctx->atlas.slots[kFontSans];
        if (!fs.loaded || !fs.has_outlines) {
            destroy_context(ctx);
            set_current_context(prev_ctx);
            return false;
        }
        const int gid_w = stbtt_FindGlyphIndex(&fs.font_info, 'W');
        const int gid_m = stbtt_FindGlyphIndex(&fs.font_info, 'M');
        const int gid_g = stbtt_FindGlyphIndex(&fs.font_info, 'g');
        const int gid_sp = stbtt_FindGlyphIndex(&fs.font_info, ' ');
        if (gid_w <= 0 || gid_m <= 0 || gid_g <= 0 || gid_sp <= 0) {
            destroy_context(ctx);
            set_current_context(prev_ctx);
            return false;
        }

        const size_t saved_offset = ctx->frame_arena.offset;
        ctx->frame_arena.offset = ctx->frame_arena.capacity;

        // Slug glyph compilation with 100% exhausted frame_arena must succeed via stack buffer.
        const SlugGlyphEntry ge = slug_get_glyph(&ctx->atlas, kFontSans, static_cast<uint32_t>(gid_w));
        if (!ge.has_outline || ge.num_hbands == 0u || ge.num_vbands == 0u) {
            ctx->frame_arena.offset = saved_offset;
            destroy_context(ctx);
            set_current_context(prev_ctx);
            return false;
        }

#if defined(_WIN32)
        // Hinted DirectWrite glyph with 100% exhausted frame_arena must succeed via stack buffer.
        const EmojiBitmapEntry he_m = get_or_cache_hinted_glyph(ctx, kFontSans, static_cast<uint32_t>(gid_m), 14.0f, 0u);
        if (!he_m.has_bitmap || he_m.width_px == 0u || he_m.height_px == 0u) {
            ctx->frame_arena.offset = saved_offset;
            destroy_context(ctx);
            set_current_context(prev_ctx);
            return false;
        }
        // Space glyph genuinely reports empty bounds and may cache negative safely.
        const EmojiBitmapEntry he_sp = get_or_cache_hinted_glyph(ctx, kFontSans, static_cast<uint32_t>(gid_sp), 14.0f, 0u);
        if (he_sp.has_bitmap || he_sp.key == 0u) {
            ctx->frame_arena.offset = saved_offset;
            destroy_context(ctx);
            set_current_context(prev_ctx);
            return false;
        }
        // Transient atlas failure must NOT poison a.hinted_glyphs so a subsequent lookup succeeds.
        auto& a = ctx->atlas;
        const uint32_t sv_w = a.subpixel_w, sv_h = a.subpixel_h;
        const uint32_t sv_x0 = a.subpixel_shelf_x0, sv_x = a.subpixel_shelf_x, sv_y = a.subpixel_shelf_y, sv_sh = a.subpixel_shelf_h;
        a.subpixel_w = 4096u;
        a.subpixel_h = 4096u;
        a.subpixel_shelf_x0 = 4096u;
        a.subpixel_shelf_x = 4096u;
        a.subpixel_shelf_y = 4096u;
        a.subpixel_shelf_h = 0u;
        const EmojiBitmapEntry he_fail = get_or_cache_hinted_glyph(ctx, kFontSans, static_cast<uint32_t>(gid_g), 14.0f, 0u);
        a.subpixel_w = sv_w;
        a.subpixel_h = sv_h;
        a.subpixel_shelf_x0 = sv_x0;
        a.subpixel_shelf_x = sv_x;
        a.subpixel_shelf_y = sv_y;
        a.subpixel_shelf_h = sv_sh;
        if (he_fail.has_bitmap) {
            ctx->frame_arena.offset = saved_offset;
            destroy_context(ctx);
            set_current_context(prev_ctx);
            return false;
        }
        const EmojiBitmapEntry he_retry = get_or_cache_hinted_glyph(ctx, kFontSans, static_cast<uint32_t>(gid_g), 14.0f, 0u);
        if (!he_retry.has_bitmap || he_retry.width_px == 0u || he_retry.height_px == 0u) {
            ctx->frame_arena.offset = saved_offset;
            destroy_context(ctx);
            set_current_context(prev_ctx);
            return false;
        }
#endif

        ctx->frame_arena.offset = saved_offset;
        destroy_context(ctx);
        set_current_context(prev_ctx);
    }

    // 3. ensure_subpixel_atlas crossing 512x512 -> 512x1024 -> 1024x1024 -> 1024x2048 must never
    //    drop boundary-triggering glyphs or overlap any allocated tile.
    {
        Context* ctx = create_context({.headless = true});
        if (!ctx) return false;
        auto& a = ctx->atlas;
        constexpr uint32_t kTileW = 20u;
        constexpr uint32_t kTileH = 20u;
        bool saw_512x512 = false;
        bool saw_512x1024 = false;
        bool saw_1024x1024 = false;
        bool saw_1024x2048 = false;
        bool ok = true;
        for (uint32_t n = 0u; n < 4000u; ++n) {
            uint32_t tx = 0u, ty = 0u;
            if (!ensure_subpixel_atlas(ctx, kTileW, kTileH, &tx, &ty)) {
                ok = false;
                break;
            }
            if (tx + kTileW > a.subpixel_w || ty + kTileH > a.subpixel_h) {
                ok = false;
                break;
            }
            // Verify zero overlap with any prior tile, then mark sentinel alpha.
            for (uint32_t row = 0u; row < kTileH && ok; ++row) {
                uint8_t* dst = a.subpixel_pixels + (size_t(ty + row) * a.subpixel_w + tx) * 4u;
                for (uint32_t col = 0u; col < kTileW; ++col) {
                    if (dst[col * 4u + 3u] != 0u) {
                        ok = false;
                        break;
                    }
                    dst[col * 4u + 3u] = 0xA5u;
                }
            }
            if (!ok) break;
            if (a.subpixel_w == 512u && a.subpixel_h == 512u) saw_512x512 = true;
            else if (a.subpixel_w == 512u && a.subpixel_h == 1024u) saw_512x1024 = true;
            else if (a.subpixel_w == 1024u && a.subpixel_h == 1024u) saw_1024x1024 = true;
            else if (a.subpixel_w == 1024u && a.subpixel_h == 2048u) {
                saw_1024x2048 = true;
                break;
            }
        }
        destroy_context(ctx);
        if (!ok || !saw_512x512 || !saw_512x1024 || !saw_1024x1024 || !saw_1024x2048) {
            return false;
        }
    }

    return true;
}

} // namespace etch
