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
    if (is_srgb_gpu_format(target_fmt) || is_linear_float_format(target_fmt)) {
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

enum class GpuBlendMode : uint8_t {
    Opaque = 0,
    PremulAlpha = 1,
    SubpixelRgb = 2,
    PunchHole = 3,
    Additive = 4,
    Multiply = 5,
    Screen = 6,
};
enum class DepthMode : uint8_t { None = 0, WriteLess = 1, TestLess = 2 };
enum class GpuShaderKind : uint8_t {
    Quad = 0,
    Subpixel = 1,
    Slug = 2,
    Image = 3,
    Fx = 4,
    Kawase = 5,
};

constexpr GpuBlendMode to_gpu_blend_mode(uint8_t bm) noexcept {
    switch (static_cast<BlendMode>(bm & kPrimAuxBlendMask)) {
        case BlendMode::Additive: return GpuBlendMode::Additive;
        case BlendMode::Multiply: return GpuBlendMode::Multiply;
        case BlendMode::Screen:   return GpuBlendMode::Screen;
        case BlendMode::Normal:
        default:                  return GpuBlendMode::PremulAlpha;
    }
}

constexpr uint32_t make_pso_key(
    GpuShaderKind shader,
    GpuBlendMode blend,
    DepthMode depth,
    SDL_GPUTextureFormat fmt) noexcept
{
    return (static_cast<uint32_t>(shader) & 0xFu) |
           ((static_cast<uint32_t>(blend) & 0xFu) << 4) |
           ((static_cast<uint32_t>(depth) & 0x3u) << 8) |
           ((static_cast<uint32_t>(fmt) & 0x3FFFu) << 10);
}

SDL_GPUGraphicsPipeline* create_graphics_pipeline(
    SDL_GPUDevice* device,
    SDL_GPUShader* vs,
    SDL_GPUShader* ps,
    SDL_GPUTextureFormat target_fmt,
    GpuBlendMode blend_mode,
    DepthMode depth_mode = DepthMode::None) noexcept
{
    SDL_GPUColorTargetBlendState blend = {};
    blend.enable_blend = (blend_mode != GpuBlendMode::Opaque);
    switch (blend_mode) {
        case GpuBlendMode::Opaque:
            break;
        case GpuBlendMode::PremulAlpha:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
        case GpuBlendMode::SubpixelRgb:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
        case GpuBlendMode::PunchHole:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
        case GpuBlendMode::Additive:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
        case GpuBlendMode::Multiply:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
        case GpuBlendMode::Screen:
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
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

bool create_shader_pair_from_blobs(
    Context* ctx,
    SDL_GPUShader** out_vs,
    SDL_GPUShader** out_ps,
    const Uint8* vs_code,
    size_t vs_size,
    uint32_t vs_sbufs,
    uint32_t vs_ubufs,
    const Uint8* ps_code,
    size_t ps_size,
    uint32_t ps_samplers,
    uint32_t ps_sbufs,
    uint32_t ps_ubufs) noexcept
{
    if (!vs_code || !ps_code || vs_size == 0u || ps_size == 0u) return false;
    const SDL_GPUShaderCreateInfo vs_info = {
        .code_size = vs_size, .code = vs_code, .entrypoint = "vs_main",
        .format = ctx->gpu_shader_fmt, .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0u, .num_storage_textures = 0u,
        .num_storage_buffers = vs_sbufs,
        .num_uniform_buffers = vs_ubufs, .props = 0u,
    };
    const SDL_GPUShaderCreateInfo ps_info = {
        .code_size = ps_size, .code = ps_code, .entrypoint = "ps_main",
        .format = ctx->gpu_shader_fmt, .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = ps_samplers, .num_storage_textures = 0u,
        .num_storage_buffers = ps_sbufs,
        .num_uniform_buffers = ps_ubufs, .props = 0u,
    };
    if (!*out_vs) *out_vs = SDL_CreateGPUShader(ctx->device, &vs_info);
    if (!*out_ps) *out_ps = SDL_CreateGPUShader(ctx->device, &ps_info);
    return *out_vs != nullptr && *out_ps != nullptr;
}

#if defined(_WIN32)
#    define ETCH_SELECT_SHADER_PAIR(name, vs_code, vs_size, ps_code, ps_size)                                       \
        if (ctx->gpu_shader_fmt == SDL_GPU_SHADERFORMAT_DXIL) {                                                     \
            vs_code = g_##name##_vs_dxil; vs_size = sizeof(g_##name##_vs_dxil);                                     \
            ps_code = g_##name##_ps_dxil; ps_size = sizeof(g_##name##_ps_dxil);                                     \
        } else if (ctx->gpu_shader_fmt == SDL_GPU_SHADERFORMAT_SPIRV) {                                             \
            vs_code = g_##name##_vs_spv; vs_size = sizeof(g_##name##_vs_spv);                                       \
            ps_code = g_##name##_ps_spv; ps_size = sizeof(g_##name##_ps_spv);                                       \
        } else {                                                                                                    \
            return false;                                                                                           \
        }
#elif defined(__APPLE__)
#    define ETCH_SELECT_SHADER_PAIR(name, vs_code, vs_size, ps_code, ps_size)                                       \
        if (ctx->gpu_shader_fmt == SDL_GPU_SHADERFORMAT_MSL) {                                                      \
            vs_code = g_##name##_vs_msl; vs_size = sizeof(g_##name##_vs_msl) - 1u;                                  \
            ps_code = g_##name##_ps_msl; ps_size = sizeof(g_##name##_ps_msl) - 1u;                                  \
        } else {                                                                                                    \
            return false;                                                                                           \
        }
#else
#    define ETCH_SELECT_SHADER_PAIR(name, vs_code, vs_size, ps_code, ps_size)                                       \
        if (ctx->gpu_shader_fmt == SDL_GPU_SHADERFORMAT_SPIRV) {                                                    \
            vs_code = g_##name##_vs_spv; vs_size = sizeof(g_##name##_vs_spv);                                       \
            ps_code = g_##name##_ps_spv; ps_size = sizeof(g_##name##_ps_spv);                                       \
        } else {                                                                                                    \
            return false;                                                                                           \
        }
#endif

bool ensure_quad_shaders(Context* ctx) noexcept {
    if (ctx->quad_vs_shader && ctx->quad_ps_shader) return true;
    const Uint8* vs_code = nullptr; size_t vs_size = 0u;
    const Uint8* ps_code = nullptr; size_t ps_size = 0u;
    ETCH_SELECT_SHADER_PAIR(etch_quad, vs_code, vs_size, ps_code, ps_size)
    return create_shader_pair_from_blobs(ctx, &ctx->quad_vs_shader, &ctx->quad_ps_shader,
        vs_code, vs_size, 2u, 1u, ps_code, ps_size, 1u, 2u, 0u);
}

bool ensure_subpixel_shaders(Context* ctx) noexcept {
    if (ctx->subpixel_vs_shader && ctx->subpixel_ps_shader) return true;
    const Uint8* vs_code = nullptr; size_t vs_size = 0u;
    const Uint8* ps_code = nullptr; size_t ps_size = 0u;
    ETCH_SELECT_SHADER_PAIR(etch_subpixel, vs_code, vs_size, ps_code, ps_size)
    return create_shader_pair_from_blobs(ctx, &ctx->subpixel_vs_shader, &ctx->subpixel_ps_shader,
        vs_code, vs_size, 2u, 1u, ps_code, ps_size, 2u, 1u, 0u);
}

bool ensure_slug_shaders(Context* ctx) noexcept {
    if (ctx->slug_vs_shader && ctx->slug_ps_shader) return true;
    const Uint8* vs_code = nullptr; size_t vs_size = 0u;
    const Uint8* ps_code = nullptr; size_t ps_size = 0u;
    ETCH_SELECT_SHADER_PAIR(etch_slug, vs_code, vs_size, ps_code, ps_size)
    return create_shader_pair_from_blobs(ctx, &ctx->slug_vs_shader, &ctx->slug_ps_shader,
        vs_code, vs_size, 2u, 1u, ps_code, ps_size, 1u, 5u, 0u);
}

bool ensure_image_shaders(Context* ctx) noexcept {
    if (ctx->image_vs_shader && ctx->image_ps_shader) return true;
    const Uint8* vs_code = nullptr; size_t vs_size = 0u;
    const Uint8* ps_code = nullptr; size_t ps_size = 0u;
    ETCH_SELECT_SHADER_PAIR(etch_image, vs_code, vs_size, ps_code, ps_size)
    return create_shader_pair_from_blobs(ctx, &ctx->image_vs_shader, &ctx->image_ps_shader,
        vs_code, vs_size, 2u, 1u, ps_code, ps_size, 10u, 1u, 0u);
}

bool ensure_fx_shaders(Context* ctx) noexcept {
    if (ctx->fx_vs_shader && ctx->fx_ps_shader) return true;
    const Uint8* vs_code = nullptr; size_t vs_size = 0u;
    const Uint8* ps_code = nullptr; size_t ps_size = 0u;
    ETCH_SELECT_SHADER_PAIR(etch_fx, vs_code, vs_size, ps_code, ps_size)
    return create_shader_pair_from_blobs(ctx, &ctx->fx_vs_shader, &ctx->fx_ps_shader,
        vs_code, vs_size, 2u, 1u, ps_code, ps_size, 1u, 1u, 0u);
}

bool ensure_kawase_shaders(Context* ctx) noexcept {
    if (ctx->kawase_vs_shader && ctx->kawase_ps_shader) return true;
    const Uint8* vs_code = nullptr; size_t vs_size = 0u;
    const Uint8* ps_code = nullptr; size_t ps_size = 0u;
    ETCH_SELECT_SHADER_PAIR(etch_kawase, vs_code, vs_size, ps_code, ps_size)
    return create_shader_pair_from_blobs(ctx, &ctx->kawase_vs_shader, &ctx->kawase_ps_shader,
        vs_code, vs_size, 0u, 0u, ps_code, ps_size, 1u, 0u, 1u);
}

#undef ETCH_SELECT_SHADER_PAIR

SDL_GPUGraphicsPipeline* get_or_create_pso(
    Context* ctx,
    GpuShaderKind shader,
    GpuBlendMode blend,
    DepthMode depth,
    SDL_GPUTextureFormat target_fmt) noexcept
{
    if (!ctx || !ctx->device || target_fmt == SDL_GPU_TEXTUREFORMAT_INVALID) return nullptr;
    const uint32_t key = make_pso_key(shader, blend, depth, target_fmt);
    for (uint32_t i = 0u; i < ctx->pso_count; ++i) {
        if (ctx->pso_table[i].key == key) return ctx->pso_table[i].pipe;
    }

    SDL_GPUShader* vs = nullptr;
    SDL_GPUShader* ps = nullptr;
    switch (shader) {
        case GpuShaderKind::Quad:
            if (!ensure_quad_shaders(ctx)) return nullptr;
            vs = ctx->quad_vs_shader; ps = ctx->quad_ps_shader;
            break;
        case GpuShaderKind::Subpixel:
            if (!ensure_subpixel_shaders(ctx)) return nullptr;
            vs = ctx->subpixel_vs_shader; ps = ctx->subpixel_ps_shader;
            break;
        case GpuShaderKind::Slug:
            if (!ensure_slug_shaders(ctx)) return nullptr;
            vs = ctx->slug_vs_shader; ps = ctx->slug_ps_shader;
            break;
        case GpuShaderKind::Image:
            if (!ensure_image_shaders(ctx)) return nullptr;
            vs = ctx->image_vs_shader; ps = ctx->image_ps_shader;
            break;
        case GpuShaderKind::Fx:
            if (!ensure_fx_shaders(ctx)) return nullptr;
            vs = ctx->fx_vs_shader; ps = ctx->fx_ps_shader;
            break;
        case GpuShaderKind::Kawase:
            if (!ensure_kawase_shaders(ctx)) return nullptr;
            vs = ctx->kawase_vs_shader; ps = ctx->kawase_ps_shader;
            break;
        default:
            return nullptr;
    }

    if (ctx->pso_count == ctx->pso_cap) {
        const uint32_t next_cap = ctx->pso_cap ? (ctx->pso_cap * 2u) : 16u;
        auto* grown = static_cast<PsoEntry*>(std::realloc(ctx->pso_table, size_t(next_cap) * sizeof(PsoEntry)));
        if (!grown) return nullptr;
        ctx->pso_table = grown;
        ctx->pso_cap = next_cap;
    }

    SDL_GPUGraphicsPipeline* pipe = create_graphics_pipeline(ctx->device, vs, ps, target_fmt, blend, depth);
    if (!pipe) return nullptr;
    ctx->pso_table[ctx->pso_count++] = PsoEntry{ .key = key, .pipe = pipe };
    return pipe;
}

void release_pso_table(Context* ctx) noexcept {
    if (ctx->device && ctx->pso_table) {
        for (uint32_t i = 0u; i < ctx->pso_count; ++i) {
            if (ctx->pso_table[i].pipe) {
                SDL_ReleaseGPUGraphicsPipeline(ctx->device, ctx->pso_table[i].pipe);
            }
        }
    }
    std::free(ctx->pso_table);
    ctx->pso_table = nullptr;
    ctx->pso_count = 0u;
    ctx->pso_cap = 0u;
}

SDL_GPUGraphicsPipeline* get_batch_pipeline(
    Context* ctx,
    GpuPipelineKind kind,
    uint8_t blend_mode,
    SDL_GPUTextureFormat fmt,
    bool use_depth) noexcept
{
    const DepthMode dm = use_depth ? DepthMode::TestLess : DepthMode::None;
    switch (kind) {
        case GpuPipelineKind::QuadOpaque:
            return get_or_create_pso(ctx, GpuShaderKind::Quad, GpuBlendMode::Opaque, DepthMode::WriteLess, fmt);
        case GpuPipelineKind::QuadAlpha:
            return get_or_create_pso(ctx, GpuShaderKind::Quad, to_gpu_blend_mode(blend_mode), dm, fmt);
        case GpuPipelineKind::GrayscaleText:
            return get_or_create_pso(ctx, GpuShaderKind::Subpixel, to_gpu_blend_mode(blend_mode), dm, fmt);
        case GpuPipelineKind::SubpixelText:
            return get_or_create_pso(ctx, GpuShaderKind::Subpixel, GpuBlendMode::SubpixelRgb, dm, fmt);
        case GpuPipelineKind::Slug:
            return get_or_create_pso(ctx, GpuShaderKind::Slug, to_gpu_blend_mode(blend_mode), dm, fmt);
        case GpuPipelineKind::Image:
            return get_or_create_pso(ctx, GpuShaderKind::Image, to_gpu_blend_mode(blend_mode), dm, fmt);
        case GpuPipelineKind::ImagePunch:
            return get_or_create_pso(ctx, GpuShaderKind::Image, GpuBlendMode::PunchHole, DepthMode::None, fmt);
        case GpuPipelineKind::Fx:
            return get_or_create_pso(ctx, GpuShaderKind::Fx, to_gpu_blend_mode(blend_mode), dm, fmt);
        default:
            return nullptr;
    }
}

bool ensure_white_fallback_tex(Context* ctx) noexcept {
    if (ctx->surface_fallback_tex) return true;
    const SDL_GPUTextureCreateInfo tinfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = 1u, .height = 1u,
        .layer_count_or_depth = 1u, .num_levels = 1u,
        .sample_count = SDL_GPU_SAMPLECOUNT_1, .props = 0u,
    };
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(ctx->device, &tinfo);
    if (!tex) return false;
    const SDL_GPUTransferBufferCreateInfo tbinfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = 16u,
        .props = 0u,
    };
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(ctx->device, &tbinfo);
    if (!tb) { SDL_ReleaseGPUTexture(ctx->device, tex); return false; }
    void* mapped = SDL_MapGPUTransferBuffer(ctx->device, tb, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(ctx->device, tb);
        SDL_ReleaseGPUTexture(ctx->device, tex);
        return false;
    }
    static constexpr uint8_t kWhite[4] = {255u, 255u, 255u, 255u};
    std::memcpy(mapped, kWhite, 4u);
    SDL_UnmapGPUTransferBuffer(ctx->device, tb);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(ctx->device, tb);
        SDL_ReleaseGPUTexture(ctx->device, tex);
        return false;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    if (!cp) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(ctx->device, tb);
        SDL_ReleaseGPUTexture(ctx->device, tex);
        return false;
    }
    const SDL_GPUTextureTransferInfo src = { .transfer_buffer = tb, .offset = 0u, .pixels_per_row = 1u, .rows_per_layer = 1u };
    const SDL_GPUTextureRegion dst = { .texture = tex, .mip_level = 0u, .layer = 0u, .x = 0u, .y = 0u, .z = 0u, .w = 1u, .h = 1u, .d = 1u };
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    const bool ok = SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(ctx->device, tb);
    if (!ok) {
        SDL_ReleaseGPUTexture(ctx->device, tex);
        return false;
    }
    ctx->surface_fallback_tex = tex;
    return true;
}

void release_depth_target(SDL_GPUDevice* device, WindowUiState& ui) noexcept {
    if (ui.depth_tex) {
        SDL_ReleaseGPUTexture(device, ui.depth_tex);
        ui.depth_tex = nullptr;
    }
    ui.depth_w = 0u;
    ui.depth_h = 0u;
    ui.depth_fmt = SDL_GPU_TEXTUREFORMAT_INVALID;
    ui.depth_idle_frames = 0u;
}

bool ensure_depth_target(Context* ctx, uint32_t width, uint32_t height) noexcept {
    if (ctx->active_ui->depth_tex && ctx->active_ui->depth_w == width && ctx->active_ui->depth_h == height) {
        return true;
    }
    release_depth_target(ctx->device, *ctx->active_ui);
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

void release_filter_targets(SDL_GPUDevice* device, WindowUiState& ui) noexcept {
    for (uint32_t s = 0u; s < kMaxFilterSlices; ++s) {
        for (uint32_t l = 0u; l < kMaxFilterLevels; ++l) {
            if (ui.filter_slices[s].levels[l]) {
                SDL_ReleaseGPUTexture(device, ui.filter_slices[s].levels[l]);
                ui.filter_slices[s].levels[l] = nullptr;
            }
        }
    }
    ui.filter_rt_w = 0u;
    ui.filter_rt_h = 0u;
    ui.filter_idle_frames = 0u;
}

bool ensure_filter_slice_targets(Context* ctx, uint32_t width, uint32_t height, uint32_t slice, uint32_t max_level) noexcept {
    if (ctx->active_ui->filter_rt_w != width || ctx->active_ui->filter_rt_h != height) {
        release_filter_targets(ctx->device, *ctx->active_ui);
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

void release_blur_targets(SDL_GPUDevice* device, WindowUiState& ui) noexcept {
    if (ui.scene_tex) {
        SDL_ReleaseGPUTexture(device, ui.scene_tex);
        ui.scene_tex = nullptr;
    }
    if (ui.blur_ping_tex) {
        SDL_ReleaseGPUTexture(device, ui.blur_ping_tex);
        ui.blur_ping_tex = nullptr;
    }
    if (ui.blur_pong_tex) {
        SDL_ReleaseGPUTexture(device, ui.blur_pong_tex);
        ui.blur_pong_tex = nullptr;
    }
    ui.blur_rt_w = 0u;
    ui.blur_rt_h = 0u;
    ui.blur_rt_fmt = SDL_GPU_TEXTUREFORMAT_INVALID;
    ui.blur_idle_frames = 0u;
}

bool ensure_blur_targets(Context* ctx, uint32_t width, uint32_t height, SDL_GPUTextureFormat scene_fmt) noexcept {
    if (ctx->active_ui->scene_tex && ctx->active_ui->blur_ping_tex && ctx->active_ui->blur_pong_tex &&
        ctx->active_ui->blur_rt_w == width && ctx->active_ui->blur_rt_h == height &&
        ctx->active_ui->blur_rt_fmt == scene_fmt)
    {
        return true;
    }
    release_blur_targets(ctx->device, *ctx->active_ui);

    const uint32_t half_w = (width > 1u) ? (width / 2u) : 1u;
    const uint32_t half_h = (height > 1u) ? (height / 2u) : 1u;
    const uint32_t quarter_w = (width > 3u) ? (width / 4u) : 1u;
    const uint32_t quarter_h = (height > 3u) ? (height / 4u) : 1u;

    auto make_rt = [&](uint32_t w, uint32_t h, SDL_GPUTextureFormat fmt) noexcept -> SDL_GPUTexture* {
        const SDL_GPUTextureCreateInfo info = {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = fmt,
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

    ctx->active_ui->scene_tex = make_rt(width, height, scene_fmt);
    ctx->active_ui->blur_ping_tex = make_rt(half_w, half_h, kBlurTexFormat);
    ctx->active_ui->blur_pong_tex = make_rt(quarter_w, quarter_h, kBlurTexFormat);
    if (!ctx->active_ui->scene_tex || !ctx->active_ui->blur_ping_tex || !ctx->active_ui->blur_pong_tex) {
        release_blur_targets(ctx->device, *ctx->active_ui);
        return false;
    }
    ctx->active_ui->blur_rt_w = width;
    ctx->active_ui->blur_rt_h = height;
    ctx->active_ui->blur_rt_fmt = scene_fmt;
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
    const uint32_t want_tbuf = (needed_count > kMaxPrimHardCap / 2u) ? kMaxPrimHardCap : (needed_count * 2u);
    if (!ctx->prim_transfer_buf || ctx->prim_tbuf_cap < needed_count) {
        uint32_t cap = ctx->prim_tbuf_cap >= 2048u ? ctx->prim_tbuf_cap : 2048u;
        while (cap < want_tbuf) cap = cap > kMaxPrimHardCap / 2u ? kMaxPrimHardCap : cap * 2u;
        const SDL_GPUTransferBufferCreateInfo info = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = cap * static_cast<uint32_t>(sizeof(Prim)), .props = 0u };
        SDL_GPUTransferBuffer* next = SDL_CreateGPUTransferBuffer(ctx->device, &info);
        if (!next) return false;
        if (ctx->prim_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf);
        ctx->prim_transfer_buf = next;
        ctx->prim_tbuf_cap = cap;
        ctx->prim_tbuf_cursor = 0u;
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
    const uint32_t want_tbuf = (needed_count > kMaxPrimHardCap / 2u) ? kMaxPrimHardCap : (needed_count * 2u);
    if (!ctx->prim_depth_transfer_buf || ctx->prim_depth_tbuf_cap < needed_count) {
        uint32_t cap = ctx->prim_depth_tbuf_cap >= 2048u ? ctx->prim_depth_tbuf_cap : 2048u;
        while (cap < want_tbuf) cap = cap > kMaxPrimHardCap / 2u ? kMaxPrimHardCap : cap * 2u;
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
        ctx->prim_depth_tbuf_cursor = 0u;
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

    SDL_GPUBuffer* quad_frag_bufs[2] = { ctx->clip_shape_gpu, ctx->grad_stop_gpu };
    SDL_GPUBuffer* clip_only_buf[1] = { ctx->clip_shape_gpu };
    SDL_GPUBuffer* slug_frag_bufs[5] = {
        ctx->clip_shape_gpu,
        ctx->slug_curve_gpu_buf, ctx->slug_band_gpu_buf,
        ctx->dyn_slug_curve_gpu_buf, ctx->dyn_slug_band_gpu_buf,
    };

    const uint32_t manual_srgb_bit = (is_srgb_gpu_format(target_fmt) || is_linear_float_format(target_fmt)) ? 0u : (1u << 31);
    const uint32_t depth_bit = use_depth ? (1u << 30) : 0u;
    const uint32_t atlas_r8_bit = (ctx->atlas.subpixel_gpu_tex && ctx->atlas.subpixel_gpu_is_r8) ? (1u << 27) : 0u;
    const float inv_w = 1.0f / static_cast<float>(target_w);
    const float inv_h = 1.0f / static_cast<float>(target_h);
    SDL_GPUTexture* sentinel = blur_tex_sentinel();
    SDL_GPUTexture* white_tex = ctx->surface_fallback_tex ? ctx->surface_fallback_tex : ctx->atlas.gpu_tex;
    const uintptr_t filter_base = reinterpret_cast<uintptr_t>(filter_tex_sentinel(0u, 0u));
    const uintptr_t filter_end = filter_base + kMaxFilterSlices * kMaxFilterLevels;

    SDL_GPUGraphicsPipeline* bound_pipe = nullptr;
    uint8_t bound_frag_layout = 0u; // 1 = Quad (2 sbufs), 2 = ClipOnly (1 sbuf), 3 = Slug (5 sbufs)
    uint8_t bound_sampler_mode = 0u; // 1 = mask-only (1), 2 = text+mask (2), 3 = image+mask (10)
    SDL_GPUTexture* bound_mask_tex = nullptr;
    uint32_t bound_blend_rgb = 0xFFFFFFFFu;

    auto resolve_tex = [&](SDL_GPUTexture* t) noexcept -> SDL_GPUTexture* {
        if (t == sentinel) {
            return ctx->active_ui->blur_ping_tex ? ctx->active_ui->blur_ping_tex : ctx->atlas.gpu_tex;
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

        const GpuPipelineKind kind = batch.is_punch_hole ? GpuPipelineKind::ImagePunch : batch.pipeline;
        SDL_GPUGraphicsPipeline* pipe = get_batch_pipeline(ctx, kind, batch.blend_mode, target_fmt, use_depth);
        if (!pipe) continue;
        if (pipe != bound_pipe) {
            SDL_BindGPUGraphicsPipeline(render_pass, pipe);
            bound_pipe = pipe;
        }

        const uint8_t need_frag_layout =
            (kind == GpuPipelineKind::QuadOpaque || kind == GpuPipelineKind::QuadAlpha) ? 1u
            : ((kind == GpuPipelineKind::Slug) ? 3u : 2u);
        if (need_frag_layout != bound_frag_layout) {
            if (need_frag_layout == 1u) {
                SDL_BindGPUFragmentStorageBuffers(render_pass, 0u, quad_frag_bufs, 2u);
            } else if (need_frag_layout == 3u) {
                SDL_BindGPUFragmentStorageBuffers(render_pass, 0u, slug_frag_bufs, 5u);
            } else {
                SDL_BindGPUFragmentStorageBuffers(render_pass, 0u, clip_only_buf, 1u);
            }
            bound_frag_layout = need_frag_layout;
        }

        SDL_GPUTexture* mask_tex = batch.mask_tex ? resolve_tex(batch.mask_tex) : white_tex;
        if (kind == GpuPipelineKind::GrayscaleText || kind == GpuPipelineKind::SubpixelText) {
            if (bound_sampler_mode != 2u || bound_mask_tex != mask_tex) {
                const SDL_GPUTextureSamplerBinding sbs[2] = {
                    { ctx->atlas.subpixel_gpu_tex ? ctx->atlas.subpixel_gpu_tex : ctx->atlas.gpu_tex,
                      ctx->nearest_sampler ? ctx->nearest_sampler : ctx->linear_sampler },
                    { mask_tex, ctx->linear_sampler },
                };
                SDL_BindGPUFragmentSamplers(render_pass, 0u, sbs, 2u);
                bound_sampler_mode = 2u;
                bound_mask_tex = mask_tex;
            }
            if (kind == GpuPipelineKind::SubpixelText) {
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
            }
        } else if (kind == GpuPipelineKind::Image || kind == GpuPipelineKind::ImagePunch) {
            SDL_GPUTextureSamplerBinding bindings[10] = {};
            bindings[0] = { ctx->atlas.gpu_tex, ctx->linear_sampler };
            for (uint32_t i = 0u; i < 8u; ++i) {
                bindings[i + 1u] = { resolve_tex(batch.ext_textures[i]), ctx->linear_sampler };
            }
            bindings[9] = { mask_tex, ctx->linear_sampler };
            SDL_BindGPUFragmentSamplers(render_pass, 0u, bindings, 10u);
            bound_sampler_mode = 3u;
            bound_mask_tex = mask_tex;
        } else {
            // QuadOpaque, QuadAlpha, Slug, Fx: single mask sampler at slot 0.
            if (bound_sampler_mode != 1u || bound_mask_tex != mask_tex) {
                const SDL_GPUTextureSamplerBinding sb = { mask_tex, ctx->linear_sampler };
                SDL_BindGPUFragmentSamplers(render_pass, 0u, &sb, 1u);
                bound_sampler_mode = 1u;
                bound_mask_tex = mask_tex;
            }
        }

        const uint32_t opaque_bit = (kind == GpuPipelineKind::QuadOpaque) ? (1u << 29) : 0u;
        const uint32_t gs_flags = (kind == GpuPipelineKind::GrayscaleText) ? ((1u << 28) | atlas_r8_bit) : 0u;
        const GlobalsUniform globals = {
            .viewport_size = { static_cast<float>(target_w), static_cast<float>(target_h) },
            .inv_viewport_size = { inv_w, inv_h },
            .dpi_scale = ctx->active_ui->dpi_scale,
            .time_seconds = ctx->active_ui->elapsed_seconds,
            .frame_index = ctx->active_ui->frame_index,
            .flags = ((prim_base_offset + batch.first_prim) & 0x00FFFFFFu) | gs_flags | opaque_bit | depth_bit | manual_srgb_bit,
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
    Rect region = {},
    bool preserve_dst = false) noexcept
{
    SDL_GPUColorTargetInfo ct = {};
    ct.texture = dst_tex;
    ct.load_op = (mode == 4u || preserve_dst) ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
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
    free_window_ui_cold_arrays(ui);
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
    std::free(ui->pending_text_ops); std::free(ui->frame_text_ops);
    std::free(ui->pending_text_utf8); std::free(ui->frame_text_utf8);
    std::free(ui->dock_spaces); std::free(ui->dock_cursor.stack); std::free(ui->tables);
    std::free(ui->focus_order); std::free(ui->prev_focus_order);
    std::free(ui->span_hits); std::free(ui->drop_hits);
    layout_engine_destroy(ui->layout); ui->layout = nullptr;
    ui->hitboxes.destroy();
    ui->pointer_over_ids.destroy();
    subtree_cache_destroy_all(&ui->subtree_cache);
    state_map_destroy(&ui->states);
}

GpuPipelineKind classify_prim_pipeline(const Prim& p, bool is_punch, uint8_t blend_mode, bool allow_opaque, bool lcd_subpixel) noexcept;

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
    ctx->active_ui->transient_gpu_deadline = SDL_GetTicks() + 2000u;
    if (!flush_texture_uploads(ctx)) return false;
    if (!ensure_white_fallback_tex(ctx)) return false;
    if (!record_shader_surfaces(ctx, cmd)) return false;
    clear_color = resolve_theme_color(clear_color, active_theme(ctx).bg_canvas);
    if (ctx->prim_count > 0u && (ctx->gpu_batch_count == 0u || (ctx->gpu_use_early_z && !ctx->supports_d32_depth))) {
        optimize_render_batches(ctx);
        if (ctx->gpu_batch_count == 0u) return SDL_SetError("Etch could not build a complete GPU batch list");
    }

    const bool use_seg0_depth = ctx->gpu_use_early_z && ctx->supports_d32_depth;
    const bool bloom = ctx->active_ui->bloom_intensity > 0.0f;
    const bool target_srgb = is_srgb_gpu_format(target_fmt);
    const SDL_GPUTextureFormat scene_fmt =
        (target_srgb && SDL_GPUTextureSupportsFormat(ctx->device, target_fmt, SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER))
        ? target_fmt
        : kBlurTexFormat;

    auto prewarm_range = [&](uint32_t begin_b, uint32_t end_b, SDL_GPUTextureFormat fmt, bool depth) noexcept -> bool {
        for (uint32_t i = begin_b; i < end_b; ++i) {
            const DrawBatch& b = ctx->gpu_batches[i];
            if (b.prim_count == 0u) continue;
            const GpuPipelineKind kind = b.is_punch_hole ? GpuPipelineKind::ImagePunch : b.pipeline;
            if (!get_batch_pipeline(ctx, kind, b.blend_mode, fmt, depth)) return false;
        }
        return true;
    };

    if (ctx->gpu_blur_split_count == 0u && !bloom) {
        if (!prewarm_range(0u, ctx->gpu_batch_count, target_fmt, use_seg0_depth)) return false;
    } else {
        const uint32_t seg0_end = (ctx->gpu_blur_split_count > 0u) ? ctx->gpu_blur_splits[0].split_batch : ctx->gpu_batch_count;
        const uint32_t last_split_batch = (ctx->gpu_blur_split_count > 0u) ? ctx->gpu_blur_splits[ctx->gpu_blur_split_count - 1u].split_batch : 0u;
        if (!prewarm_range(0u, seg0_end, scene_fmt, use_seg0_depth)) return false;
        if (bloom || !target_srgb) {
            if (!prewarm_range(seg0_end, ctx->gpu_batch_count, scene_fmt, false)) return false;
        } else {
            if (!prewarm_range(seg0_end, last_split_batch, scene_fmt, false)) return false;
            if (!prewarm_range(last_split_batch, ctx->gpu_batch_count, target_fmt, false)) return false;
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
    const uint32_t grad_stop_upload_count = (ctx->grad_stops && ctx->grad_stop_count > 0u) ? ctx->grad_stop_count : 1u;
    const bool grad_stops_dirty = ctx->grad_stop_count > 1u || !ctx->grad_stop_sentinel_uploaded || !ctx->grad_stop_gpu;
    uint32_t grad_uploaded = 0u;
    if (grad_stops_dirty && !ensure_slug_storage_pair(ctx->device, grad_stop_upload_count,
        static_cast<uint32_t>(sizeof(GradientStopGpu)), &ctx->grad_stop_gpu, &ctx->grad_stop_gpu_capacity,
        &ctx->grad_stop_transfer, &ctx->grad_stop_transfer_capacity, &grad_uploaded)) return false;
    const GradientStopGpu empty_grad_stop = {};
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
        (use_seg0_depth && (!ctx->prim_depth_transfer_buf || needed_depths > ctx->prim_depth_tbuf_cap)))
    {
        if (!ensure_prim_depth_buffers(ctx, needed_depths)) return false;
    }
    auto& atlas = ctx->atlas;
    if (!atlas.gpu_tex || atlas.gpu_w != atlas.width || atlas.gpu_h != atlas.height) {
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
        const uint32_t sp_bpp = atlas.subpixel_is_r8 ? 1u : 4u;
        if (!atlas.subpixel_gpu_tex || atlas.subpixel_gpu_w != atlas.subpixel_w ||
            atlas.subpixel_gpu_h != atlas.subpixel_h || atlas.subpixel_gpu_is_r8 != atlas.subpixel_is_r8)
        {
            if (atlas.subpixel_gpu_tex) {
                SDL_ReleaseGPUTexture(ctx->device, atlas.subpixel_gpu_tex);
                atlas.subpixel_gpu_tex = nullptr;
            }
            const SDL_GPUTextureCreateInfo sp_info = {
                .type = SDL_GPU_TEXTURETYPE_2D,
                .format = atlas.subpixel_is_r8 ? SDL_GPU_TEXTUREFORMAT_R8_UNORM : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
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
            atlas.subpixel_gpu_is_r8 = atlas.subpixel_is_r8;
            atlas.subpixel_dirty = true;
            atlas.subpixel_dirty_y0 = 0u;
            atlas.subpixel_dirty_y1 = atlas.subpixel_h;
        }
        if (atlas.subpixel_dirty && atlas.subpixel_dirty_y1 > atlas.subpixel_dirty_y0) {
            const uint32_t bytes = atlas.subpixel_w * (atlas.subpixel_dirty_y1 - atlas.subpixel_dirty_y0) * sp_bpp;
            if (ctx->subpixel_tbuf_size < bytes) {
                if (ctx->subpixel_transfer_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->subpixel_transfer_buf);
                const SDL_GPUTransferBufferCreateInfo info = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = bytes, .props = 0u };
                ctx->subpixel_transfer_buf = SDL_CreateGPUTransferBuffer(ctx->device, &info);
                ctx->subpixel_tbuf_size = ctx->subpixel_transfer_buf ? bytes : 0u;
                if (!ctx->subpixel_transfer_buf) return false;
            }
        }
    } else if (atlas.subpixel_gpu_tex) {
        SDL_ReleaseGPUTexture(ctx->device, atlas.subpixel_gpu_tex);
        atlas.subpixel_gpu_tex = nullptr;
        atlas.subpixel_gpu_w = 0u;
        atlas.subpixel_gpu_h = 0u;
        atlas.subpixel_gpu_is_r8 = atlas.subpixel_is_r8;
    }
    const bool atlas_upload = atlas.dirty && atlas.dirty_y1 > atlas.dirty_y0;
    const bool subpixel_upload = atlas.subpixel_gpu_tex && atlas.subpixel_dirty && atlas.subpixel_dirty_y1 > atlas.subpixel_dirty_y0;
    if (clip_shapes_dirty || grad_stops_dirty || atlas_upload || subpixel_upload || total_prims > 0u || slug_curves_pending || slug_bands_pending ||
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

        if (grad_stops_dirty) {
            if (!upload_slug_delta(ctx->device, copy_pass, ctx->grad_stops ? ctx->grad_stops : &empty_grad_stop,
                static_cast<uint32_t>(sizeof(GradientStopGpu)), grad_stop_upload_count,
                &grad_uploaded, ctx->grad_stop_gpu, ctx->grad_stop_transfer, true))
            {
                SDL_EndGPUCopyPass(copy_pass);
                return false;
            }
            ctx->grad_stop_sentinel_uploaded = true;
        }

        if (atlas_upload) {
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
            ctx->atlas_clean_frames = 0u;
        }

        if (subpixel_upload) {
            const uint32_t sp_bpp = atlas.subpixel_gpu_is_r8 ? 1u : 4u;
            const uint32_t rows = atlas.subpixel_dirty_y1 - atlas.subpixel_dirty_y0;
            const bool full_upload = (atlas.subpixel_dirty_y0 == 0u && atlas.subpixel_dirty_y1 == atlas.subpixel_h);
            void* mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->subpixel_transfer_buf, true);
            if (!mapped) { SDL_EndGPUCopyPass(copy_pass); return false; }
            std::memcpy(mapped, atlas.subpixel_pixels + size_t(atlas.subpixel_dirty_y0) * atlas.subpixel_w * sp_bpp,
                        size_t(atlas.subpixel_w) * rows * sp_bpp);
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
            atlas.subpixel_clean_frames = 0u;
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
            const uint32_t tbuf_bytes = ctx->prim_tbuf_cap * static_cast<uint32_t>(sizeof(Prim));
            bool cycle_tbuf = (ctx->prim_tbuf_cursor == 0u);
            if (ctx->prim_tbuf_cursor + byte_size > tbuf_bytes) {
                ctx->prim_tbuf_cursor = 0u;
                cycle_tbuf = true;
            }
            const uint32_t slice_off = ctx->prim_tbuf_cursor;
            void* mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf, cycle_tbuf);
            if (mapped) {
                uint8_t* dst_ptr = static_cast<uint8_t*>(mapped) + slice_off;
                if (main_bytes > 0u) std::memcpy(dst_ptr, ctx->gpu_prims ? ctx->gpu_prims : ctx->prims, main_bytes);
                if (filter_bytes > 0u) std::memcpy(dst_ptr + main_bytes, ctx->filter_prims, filter_bytes);
                SDL_UnmapGPUTransferBuffer(ctx->device, ctx->prim_transfer_buf);

                const SDL_GPUTransferBufferLocation src = {
                    .transfer_buffer = ctx->prim_transfer_buf,
                    .offset = slice_off,
                };
                const SDL_GPUBufferRegion dst = {
                    .buffer = ctx->prim_gpu_buf,
                    .offset = 0u,
                    .size = byte_size,
                };
                SDL_UploadToGPUBuffer(copy_pass, &src, &dst, true);
                ctx->prim_tbuf_cursor = slice_off + byte_size;
            }
        }
        if (use_seg0_depth && ctx->prim_count > 0u && ctx->gpu_prim_depths && ctx->prim_depth_gpu_buf && ctx->prim_depth_transfer_buf) {
            const uint32_t dbytes = ctx->prim_count * static_cast<uint32_t>(sizeof(uint32_t));
            const uint32_t aligned_dbytes = (dbytes + 15u) & ~15u;
            const uint32_t dtbuf_bytes = ctx->prim_depth_tbuf_cap * static_cast<uint32_t>(sizeof(uint32_t));
            bool cycle_dtbuf = (ctx->prim_depth_tbuf_cursor == 0u);
            if (ctx->prim_depth_tbuf_cursor + aligned_dbytes > dtbuf_bytes) {
                ctx->prim_depth_tbuf_cursor = 0u;
                cycle_dtbuf = true;
            }
            const uint32_t dslice_off = ctx->prim_depth_tbuf_cursor;
            void* mapped = SDL_MapGPUTransferBuffer(ctx->device, ctx->prim_depth_transfer_buf, cycle_dtbuf);
            if (mapped) {
                std::memcpy(static_cast<uint8_t*>(mapped) + dslice_off, ctx->gpu_prim_depths, dbytes);
                SDL_UnmapGPUTransferBuffer(ctx->device, ctx->prim_depth_transfer_buf);
                const SDL_GPUTransferBufferLocation dsrc = { .transfer_buffer = ctx->prim_depth_transfer_buf, .offset = dslice_off };
                const SDL_GPUBufferRegion ddst = { .buffer = ctx->prim_depth_gpu_buf, .offset = 0u, .size = dbytes };
                SDL_UploadToGPUBuffer(copy_pass, &dsrc, &ddst, true);
                ctx->prim_depth_tbuf_cursor = dslice_off + aligned_dbytes;
            }
        }

        SDL_EndGPUCopyPass(copy_pass);
    }

    if (!atlas_upload && ctx->atlas_transfer_buf && ++ctx->atlas_clean_frames >= 60u) {
        SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->atlas_transfer_buf);
        ctx->atlas_transfer_buf = nullptr;
        ctx->atlas_tbuf_size = 0u;
        ctx->atlas_clean_frames = 0u;
    }
    if (!subpixel_upload && ctx->subpixel_transfer_buf && ++atlas.subpixel_clean_frames >= 60u) {
        SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->subpixel_transfer_buf);
        ctx->subpixel_transfer_buf = nullptr;
        ctx->subpixel_tbuf_size = 0u;
        atlas.subpixel_clean_frames = 0u;
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

        // Split mixed-PrimKind filter batches into homogeneous micro-shader batches without remapping tex_slots.
        uint32_t total_split_batches = 0u;
        for (uint32_t gi = 0u; gi < ctx->filter_group_count; ++gi) {
            const FilterBlurGroup& fg = ctx->filter_groups[gi];
            for (uint32_t bi = fg.first_batch; bi < fg.end_batch; ++bi) {
                const DrawBatch& sb = ctx->filter_batches[bi];
                if (sb.prim_count == 0u) continue;
                GpuPipelineKind prev_pk = GpuPipelineKind::QuadAlpha;
                for (uint32_t k = 0u; k < sb.prim_count; ++k) {
                    const uint32_t pi = sb.first_prim + k;
                    const GpuPipelineKind pk = classify_prim_pipeline(ctx->filter_prims[pi], sb.is_punch_hole, sb.blend_mode, false, false);
                    if (k == 0u || pk != prev_pk) {
                        ++total_split_batches;
                        prev_pk = pk;
                    }
                }
            }
        }

        const size_t filter_mark = ctx->frame_arena.mark();
        bool split_heap = false;
        DrawBatch* split_batches = total_split_batches > 0u
            ? ctx->frame_arena.alloc_uninit<DrawBatch>(total_split_batches) : nullptr;
        uint32_t* group_first = ctx->frame_arena.alloc_uninit<uint32_t>(ctx->filter_group_count);
        uint32_t* group_end = ctx->frame_arena.alloc_uninit<uint32_t>(ctx->filter_group_count);
        if ((total_split_batches > 0u && !split_batches) || !group_first || !group_end) {
            ctx->frame_arena.rewind(filter_mark);
            split_batches = total_split_batches > 0u
                ? static_cast<DrawBatch*>(std::malloc(size_t(total_split_batches) * sizeof(DrawBatch))) : nullptr;
            group_first = static_cast<uint32_t*>(std::malloc(size_t(ctx->filter_group_count) * sizeof(uint32_t)));
            group_end = static_cast<uint32_t*>(std::malloc(size_t(ctx->filter_group_count) * sizeof(uint32_t)));
            split_heap = true;
            if ((total_split_batches > 0u && !split_batches) || !group_first || !group_end) {
                std::free(split_batches);
                std::free(group_first);
                std::free(group_end);
                return false;
            }
        }
        const auto cleanup_filter_split = [&]() noexcept {
            if (split_heap) {
                std::free(split_batches);
                std::free(group_first);
                std::free(group_end);
            } else {
                ctx->frame_arena.rewind(filter_mark);
            }
        };

        uint32_t split_cursor = 0u;
        for (uint32_t gi = 0u; gi < ctx->filter_group_count; ++gi) {
            const FilterBlurGroup& fg = ctx->filter_groups[gi];
            group_first[gi] = split_cursor;
            for (uint32_t bi = fg.first_batch; bi < fg.end_batch; ++bi) {
                const DrawBatch& sb = ctx->filter_batches[bi];
                if (sb.prim_count == 0u) continue;
                for (uint32_t k = 0u; k < sb.prim_count; ++k) {
                    const uint32_t pi = sb.first_prim + k;
                    const GpuPipelineKind pk = classify_prim_pipeline(ctx->filter_prims[pi], sb.is_punch_hole, sb.blend_mode, false, false);
                    if (k == 0u || split_batches[split_cursor - 1u].pipeline != pk) {
                        DrawBatch sub = sb;
                        sub.first_prim = pi;
                        sub.prim_count = 1u;
                        sub.pipeline = pk;
                        sub.is_punch_hole = (pk == GpuPipelineKind::ImagePunch);
                        split_batches[split_cursor++] = sub;
                    } else {
                        ++split_batches[split_cursor - 1u].prim_count;
                    }
                }
            }
            group_end[gi] = split_cursor;
        }

        SDL_GPUGraphicsPipeline* filter_kawase_pipe = get_or_create_pso(
            ctx, GpuShaderKind::Kawase, GpuBlendMode::Opaque, DepthMode::None, kFilterTexFormat);
        if (!filter_kawase_pipe) {
            cleanup_filter_split();
            return false;
        }
        for (uint32_t bi = 0u; bi < split_cursor; ++bi) {
            const DrawBatch& b = split_batches[bi];
            const GpuPipelineKind kind = b.is_punch_hole ? GpuPipelineKind::ImagePunch : b.pipeline;
            if (!get_batch_pipeline(ctx, kind, b.blend_mode, kFilterTexFormat, false)) {
                cleanup_filter_split();
                return false;
            }
        }
        for (uint32_t s = 0u; s < kMaxFilterSlices; ++s) {
            if (!slice_used[s]) continue;
            if (!ensure_filter_slice_targets(ctx, target_w, target_h, s, slice_max_level[s])) {
                cleanup_filter_split();
                return false;
            }
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
            if (!fpass) {
                cleanup_filter_split();
                return false;
            }
            for (uint32_t gi = 0u; gi < ctx->filter_group_count; ++gi) {
                const FilterBlurGroup& fg = ctx->filter_groups[gi];
                if (fg.slice != s) continue;
                emit_gpu_batches(ctx, cmd, fpass, kFilterTexFormat, target_w, target_h,
                    split_batches, group_first[gi], group_end[gi], ctx->prim_count, false, &slice_scissor);
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
                    cleanup_filter_split();
                    return false;
                }
                ++ctx->stats.blur_pass_count;
            }
        }
        cleanup_filter_split();
    } else if (ctx->active_ui->filter_rt_w != 0u) {
        if (++ctx->active_ui->filter_idle_frames >= 120u) {
            release_filter_targets(ctx->device, *ctx->active_ui);
        }
    }

    if (use_seg0_depth) {
        ctx->active_ui->depth_idle_frames = 0u;
    } else if (ctx->active_ui->depth_tex && ++ctx->active_ui->depth_idle_frames >= 120u) {
        release_depth_target(ctx->device, *ctx->active_ui);
    }

    if (ctx->gpu_blur_split_count == 0u && !bloom) {
        if (ctx->active_ui->scene_tex && ++ctx->active_ui->blur_idle_frames >= 120u) {
            release_blur_targets(ctx->device, *ctx->active_ui);
        }
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
            use_seg0_depth);
    }
    ctx->active_ui->blur_idle_frames = 0u;

    if (!ensure_blur_targets(ctx, target_w, target_h, scene_fmt)) {
        return false;
    }
    SDL_GPUGraphicsPipeline* kawase_pipe = get_or_create_pso(
        ctx, GpuShaderKind::Kawase, GpuBlendMode::Opaque, DepthMode::None, kBlurTexFormat);
    SDL_GPUGraphicsPipeline* present_pipe = target_srgb
        ? nullptr
        : get_or_create_pso(ctx, GpuShaderKind::Kawase, GpuBlendMode::Opaque, DepthMode::None, target_fmt);
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
        const KawaseBlurParams kp = etch_kawase_blur_params(radius_px);
        const float off_down1 = kp.off_down1;
        const float off_down2 = kp.off_down2;
        const float off_up    = kp.off_up;
        const float margin_up    = kp.margin_up;
        const float margin_down2 = kp.margin_down2;
        const float margin_down1 = kp.margin_down1;
        const auto region=[&](uint32_t w,uint32_t h,float margin) noexcept -> Rect {
            const float sx=float(w)/float(target_w),sy=float(h)/float(target_h);
            return {(bounds.x-margin)*sx,(bounds.y-margin)*sy,(bounds.w+2*margin)*sx,(bounds.h+2*margin)*sy};
        };

        if (s == 0u || split_batch > prev_batch) {
            if (!run_gpu_render_pass(
                    ctx,
                    cmd,
                    ctx->active_ui->scene_tex,
                    scene_fmt,
                    target_w,
                    target_h,
                    (s == 0u) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
                    clear_color,
                    prev_batch,
                    split_batch,
                    (s == 0u) ? use_seg0_depth : false))
            {
                return false;
            }
        }

        const bool preserve_blur = ctx->blur_splits_need_load_preserve && s > 0u;
        if (!run_kawase_step(
                ctx, cmd, kawase_pipe,
                ctx->active_ui->scene_tex, ctx->active_ui->blur_ping_tex, half_w, half_h,
                1.0f / static_cast<float>(target_w), 1.0f / static_cast<float>(target_h),
                off_down1, 0u, region(half_w, half_h, margin_down1), preserve_blur))
        {
            return false;
        }
        if (!run_kawase_step(
                ctx, cmd, kawase_pipe,
                ctx->active_ui->blur_ping_tex, ctx->active_ui->blur_pong_tex, quarter_w, quarter_h,
                1.0f / static_cast<float>(half_w), 1.0f / static_cast<float>(half_h),
                off_down2, 0u, region(quarter_w, quarter_h, margin_down2), false))
        {
            return false;
        }
        if (!run_kawase_step(
                ctx, cmd, kawase_pipe,
                ctx->active_ui->blur_pong_tex, ctx->active_ui->blur_ping_tex, half_w, half_h,
                1.0f / static_cast<float>(quarter_w), 1.0f / static_cast<float>(quarter_h),
                off_up, 1u, region(half_w, half_h, margin_up), preserve_blur))
        {
            return false;
        }
    }

    const uint32_t last_batch = ctx->gpu_blur_split_count ? ctx->gpu_blur_splits[ctx->gpu_blur_split_count - 1u].split_batch : 0u;
    if (bloom) {
        if (!run_gpu_render_pass(ctx, cmd, ctx->active_ui->scene_tex, scene_fmt, target_w, target_h,
                ctx->gpu_blur_split_count ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR, clear_color,
                last_batch, ctx->gpu_batch_count, ctx->gpu_blur_split_count == 0u ? use_seg0_depth : false)) return false;
        SDL_GPUGraphicsPipeline* bloom_pipe = get_or_create_pso(
            ctx, GpuShaderKind::Kawase, GpuBlendMode::Additive, DepthMode::None, scene_fmt);
        if (!bloom_pipe) return false;
        if (!run_kawase_step(ctx,cmd,kawase_pipe,ctx->active_ui->scene_tex,ctx->active_ui->blur_ping_tex,
                half_w,half_h,1.f/float(target_w),1.f/float(target_h),1,3)) return false;
        if (!run_kawase_step(ctx,cmd,kawase_pipe,ctx->active_ui->blur_ping_tex,ctx->active_ui->blur_pong_tex,
                quarter_w,quarter_h,1.f/float(half_w),1.f/float(half_h),2,0)) return false;
        if (!run_kawase_step(ctx,cmd,kawase_pipe,ctx->active_ui->blur_pong_tex,ctx->active_ui->blur_ping_tex,
                half_w,half_h,1.f/float(quarter_w),1.f/float(quarter_h),1,1)) return false;
        if (!run_kawase_step(ctx,cmd,bloom_pipe,ctx->active_ui->blur_ping_tex,ctx->active_ui->scene_tex,
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
        if (bloom || last_batch >= ctx->gpu_batch_count) {
            return true;
        }
        return run_gpu_render_pass(
            ctx,
            cmd,
            target_tex,
            target_fmt,
            target_w,
            target_h,
            SDL_GPU_LOADOP_LOAD,
            clear_color,
            last_batch,
            ctx->gpu_batch_count,
            false);
    }

    if (!bloom && !run_gpu_render_pass(
            ctx,
            cmd,
            ctx->active_ui->scene_tex,
            scene_fmt,
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

bool ensure_headless_resources(Context* ctx, uint32_t width, uint32_t height, uint32_t needed_dl) noexcept {
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

bool ensure_gpu_device(Context* ctx) noexcept {
    if (!ctx || ctx->gpu_init_failed) return false;
    if (ctx->device) return true;

    const char* driver = ctx->gpu_driver;
    if (!driver[0]) driver = SDL_getenv("ETCH_GPU_DRIVER");
    if (!driver || !driver[0]) driver = SDL_getenv("SDL_GPU_DRIVER");
    if (driver && !driver[0]) driver = nullptr;

    const auto fail = [&]() noexcept -> bool {
        if (ctx->device) {
            release_pso_table(ctx);
            if (ctx->quad_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->quad_vs_shader);
            if (ctx->quad_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->quad_ps_shader);
            if (ctx->subpixel_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->subpixel_vs_shader);
            if (ctx->subpixel_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->subpixel_ps_shader);
            if (ctx->slug_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->slug_vs_shader);
            if (ctx->slug_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->slug_ps_shader);
            if (ctx->image_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->image_vs_shader);
            if (ctx->image_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->image_ps_shader);
            if (ctx->fx_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->fx_vs_shader);
            if (ctx->fx_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->fx_ps_shader);
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
            if (ctx->grad_stop_gpu) SDL_ReleaseGPUBuffer(ctx->device, ctx->grad_stop_gpu);
            if (ctx->grad_stop_transfer) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->grad_stop_transfer);
            if (ctx->texture_upload_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->texture_upload_buf);
            if (ctx->slug_curve_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->slug_curve_gpu_buf);
            if (ctx->slug_band_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->slug_band_gpu_buf);
            if (ctx->dyn_slug_curve_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->dyn_slug_curve_gpu_buf);
            if (ctx->dyn_slug_band_gpu_buf) SDL_ReleaseGPUBuffer(ctx->device, ctx->dyn_slug_band_gpu_buf);
            SDL_DestroyGPUDevice(ctx->device);
        }
        ctx->quad_vs_shader = ctx->quad_ps_shader = nullptr;
        ctx->subpixel_vs_shader = ctx->subpixel_ps_shader = nullptr;
        ctx->slug_vs_shader = ctx->slug_ps_shader = nullptr;
        ctx->image_vs_shader = ctx->image_ps_shader = nullptr;
        ctx->fx_vs_shader = ctx->fx_ps_shader = nullptr;
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
        ctx->grad_stop_gpu = nullptr; ctx->grad_stop_transfer = nullptr;
        ctx->grad_stop_gpu_capacity = ctx->grad_stop_transfer_capacity = 0u;
        ctx->grad_stop_sentinel_uploaded = false;
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

    // Request only shader formats embedded on this platform, preferring low-power iGPU unless opted out.
#if defined(_WIN32)
    const SDL_GPUShaderFormat want_formats = SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV;
#elif defined(__APPLE__)
    const SDL_GPUShaderFormat want_formats = SDL_GPU_SHADERFORMAT_MSL;
#else
    const SDL_GPUShaderFormat want_formats = SDL_GPU_SHADERFORMAT_SPIRV;
#endif
    auto create_gpu_dev = [&]() noexcept -> SDL_GPUDevice* {
        SDL_PropertiesID props = SDL_CreateProperties();
        if (!props) {
            return SDL_CreateGPUDevice(want_formats, ctx->enable_gpu_debug, driver);
        }
#if defined(_WIN32)
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
#elif defined(__APPLE__)
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
#else
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
#endif
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, ctx->enable_gpu_debug);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, !ctx->prefer_high_performance_gpu);
        if (driver) {
            SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, driver);
        }
        SDL_GPUDevice* dev = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
        return dev;
    };
    ctx->device = create_gpu_dev();
    if (!ctx->device && !SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            ctx->owns_video_init = true;
            if (ctx->wake_event_type == 0 && (SDL_WasInit(SDL_INIT_EVENTS) || SDL_WasInit(SDL_INIT_VIDEO)))
                ctx->wake_event_type = SDL_RegisterEvents(1);
            ctx->device = create_gpu_dev();
        }
    }
    if (!ctx->device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return fail();
    }

    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(ctx->device);
    SDL_GPUShaderFormat chosen_fmt = SDL_GPU_SHADERFORMAT_INVALID;
#if defined(_WIN32)
    if ((supported & SDL_GPU_SHADERFORMAT_DXIL) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_DXIL;
    } else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_SPIRV;
    }
#elif defined(__APPLE__)
    if ((supported & SDL_GPU_SHADERFORMAT_MSL) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_MSL;
    }
#else
    if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0u) {
        chosen_fmt = SDL_GPU_SHADERFORMAT_SPIRV;
    }
#endif
    if (chosen_fmt == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No compatible shader format supported by SDL_GPUDevice");
        return fail();
    }
    ctx->gpu_shader_fmt = chosen_fmt;

    ctx->supports_d32_depth = (chosen_fmt != SDL_GPU_SHADERFORMAT_MSL) &&
        SDL_GPUTextureSupportsFormat(ctx->device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                                     SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);

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
        .max_lod = 16.0f,
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
    nearest_info.max_lod = 0.0f;
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

    const uint32_t init_prim_cap = ctx->max_prims < 1024u ? ctx->max_prims : 1024u;
    if (!ensure_prim_buffers(ctx, init_prim_cap) || !ensure_prim_depth_buffers(ctx, 64u)) {
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
    ctx->atlas.gpu_w = ctx->atlas.width;
    ctx->atlas.gpu_h = ctx->atlas.height;
    ctx->atlas_tbuf_size = 0u;
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
        !ctx->atlas.gpu_tex ||
        !ctx->slug_curve_gpu_buf || !ctx->slug_band_gpu_buf || !ctx->dyn_slug_curve_gpu_buf || !ctx->dyn_slug_band_gpu_buf)
    {
        return fail();
    }

    return true;
}

struct OptPrimItem {
    uint32_t        orig_idx;
    float           x0, y0, x1, y1;     // Clipped screen AABB
    float           bx0, by0, bx1, by1; // Unclipped prim bounds (hollow border interior test)
    float           hollow_bw;          // > 0 when axis-aligned hollow border Rect
    float           hollow_max_r;       // Clamped max corner radius when hollow_bw > 0
    uint32_t        blend_rgb;          // Packed RGB for SubpixelText CONSTANT_COLOR
    SDL_GPUTexture* mask_tex;           // Active Texture AlphaMask texture (or nullptr)
    SDL_GPUTexture* ext1;
    SDL_GPUTexture* ext2;
    GpuPipelineKind pipe;
    uint8_t         blend_mode;         // static_cast<uint8_t>(BlendMode)
    uint8_t         rank;               // 0..6 spatial emission rank
};

struct SpatialGridNode {
    uint32_t item_i;
    int32_t  next;
};

constexpr uint32_t kNumSpatialRanks = 7u;

uint8_t compute_spatial_rank(GpuPipelineKind pipe, uint8_t blend_mode, SDL_GPUTexture* mask_tex) noexcept {
    if (blend_mode != 0u || mask_tex != nullptr) return 6u;
    switch (pipe) {
        case GpuPipelineKind::QuadAlpha:     return 0u;
        case GpuPipelineKind::Image:         return 1u;
        case GpuPipelineKind::GrayscaleText: return 2u;
        case GpuPipelineKind::SubpixelText:  return 3u;
        case GpuPipelineKind::Slug:          return 4u;
        case GpuPipelineKind::Fx:            return 5u;
        default:                             return 6u;
    }
}

bool box_strictly_inside_hollow(float ix0, float iy0, float ix1, float iy1, const OptPrimItem& h) noexcept {
    const float bw = h.hollow_bw;
    if (ix0 < h.bx0 + bw || ix1 > h.bx1 - bw || iy0 < h.by0 + bw || iy1 > h.by1 - bw) {
        return false;
    }
    const float r = h.hollow_max_r;
    if (r > bw) {
        const float safe_r = r - bw;
        const float safe_r2 = safe_r * safe_r;
        if (ix0 < h.bx0 + r && iy0 < h.by0 + r) {
            const float dx = (h.bx0 + r) - ix0;
            const float dy = (h.by0 + r) - iy0;
            if (dx * dx + dy * dy >= safe_r2) return false;
        }
        if (ix1 > h.bx1 - r && iy0 < h.by0 + r) {
            const float dx = ix1 - (h.bx1 - r);
            const float dy = (h.by0 + r) - iy0;
            if (dx * dx + dy * dy >= safe_r2) return false;
        }
        if (ix1 > h.bx1 - r && iy1 > h.by1 - r) {
            const float dx = ix1 - (h.bx1 - r);
            const float dy = iy1 - (h.by1 - r);
            if (dx * dx + dy * dy >= safe_r2) return false;
        }
        if (ix0 < h.bx0 + r && iy1 > h.by1 - r) {
            const float dx = (h.bx0 + r) - ix0;
            const float dy = iy1 - (h.by1 - r);
            if (dx * dx + dy * dy >= safe_r2) return false;
        }
    }
    return true;
}

bool prim_overlaps(const OptPrimItem& a, const OptPrimItem& b) noexcept {
    if (!(a.x1 > b.x0 && a.x0 < b.x1 && a.y1 > b.y0 && a.y0 < b.y1)) return false;
    if (a.hollow_bw > 0.0f && box_strictly_inside_hollow(b.x0, b.y0, b.x1, b.y1, a)) return false;
    if (b.hollow_bw > 0.0f && box_strictly_inside_hollow(a.x0, a.y0, a.x1, a.y1, b)) return false;
    return true;
}

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
        if (prim.style.z != 0u) {
            const float angle = std::bit_cast<float>(prim.style.z);
            const float hx = 0.5f * prim.bounds.z, hy = 0.5f * prim.bounds.w;
            const float cx = prim.bounds.x + hx, cy = prim.bounds.y + hy;
            const float ac = std::fabs(std::cos(angle)), as = std::fabs(std::sin(angle));
            const float ex = ac * hx + as * hy, ey = as * hx + ac * hy;
            min_x = cx - ex; min_y = cy - ey;
            max_x = cx + ex; max_y = cy + ey;
        }
        pad = ((((prim.style.x >> 4) & 0x3Fu) & PrimFlag_LinearGrad) != 0u)
            ? 1.0f
            : (std::fmax(unpack_f16_cpu(static_cast<uint16_t>(prim.style.w >> 16)), 1.0f) * 2.0f + 2.0f);
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

GpuPipelineKind classify_prim_pipeline(
    const Prim& p,
    bool is_punch,
    uint8_t blend_mode,
    bool allow_opaque,
    bool lcd_subpixel) noexcept
{
    const uint32_t header = p.style.x;
    const PrimKind kind = static_cast<PrimKind>(header & 0xFu);
    if (is_punch || kind == PrimKind::PunchHole) {
        return GpuPipelineKind::ImagePunch;
    }
    const uint32_t flags = (header >> 4) & 0x3Fu;
    switch (kind) {
        case PrimKind::Rect: {
            if (allow_opaque && blend_mode == 0u) {
                const bool no_radii = (p.payload.z == 0u && p.payload.w == 0u);
                const bool rect_clip = ((flags & PrimFlag_RoundedClip) == 0u) &&
                                       ((header & (1u << 18)) == 0u) &&
                                       (((header >> 20) & 0xFFFu) == 0u);
                const bool fill_opaque = (p.style.y >> 24) == 255u;
                const bool has_grad = (flags & (PrimFlag_LinearGrad | PrimFlag_RadialGrad)) != 0u;
                const bool is_multistop = (flags & (PrimFlag_LinearGrad | PrimFlag_RadialGrad)) == (PrimFlag_LinearGrad | PrimFlag_RadialGrad);
                const bool grad_opaque = !has_grad || (!is_multistop && ((p.payload.x >> 24) == 255u));
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
        case PrimKind::RotatedRect:
        case PrimKind::Shadow:
            return GpuPipelineKind::QuadAlpha;
        case PrimKind::Glyph:
            if (lcd_subpixel && blend_mode == 0u && (flags & PrimFlag_LinearGrad) == 0u) {
                return GpuPipelineKind::SubpixelText;
            }
            return GpuPipelineKind::GrayscaleText;
        case PrimKind::SlugGlyph:
        case PrimKind::SlugFill:
            return GpuPipelineKind::Slug;
        case PrimKind::Texture:
        case PrimKind::BlurGlass:
        case PrimKind::LayerBlur:
            return GpuPipelineKind::Image;
        case PrimKind::CanvasGrid:
        case PrimKind::BezierWire:
        case PrimKind::RingArc:
        case PrimKind::MeshGrad:
        case PrimKind::BorderBeam:
            return GpuPipelineKind::Fx;
        default:
            return GpuPipelineKind::QuadAlpha;
    }
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
    const uint8_t blend_mode = (pipe == GpuPipelineKind::ImagePunch || pipe == GpuPipelineKind::SubpixelText ||
                                pipe == GpuPipelineKind::QuadOpaque) ? 0u : item.blend_mode;
    const bool uses_ext_tex = (pipe == GpuPipelineKind::Image);

    auto make_batch = [&](uint32_t first) noexcept -> DrawBatch {
        return DrawBatch{
            .first_prim = first,
            .prim_count = 0u,
            .ext_textures = {},
            .ext_count = 0u,
            .blend_rgba8 = blend_rgb,
            .mask_tex = item.mask_tex,
            .pipeline = pipe,
            .blend_mode = blend_mode,
            .is_punch_hole = (pipe == GpuPipelineKind::ImagePunch),
        };
    };

    if (ctx->gpu_batch_count == 0u || force_new_batch) {
        if (!ensure_batch_cap_array(&ctx->gpu_batches, &ctx->gpu_batch_cap, ctx->gpu_batch_count)) return false;
        ctx->gpu_batches[ctx->gpu_batch_count++] = make_batch(*out_prim_cursor);
    }

    DrawBatch* cur = &ctx->gpu_batches[ctx->gpu_batch_count - 1u];
    if (cur->prim_count > 0u &&
        (cur->pipeline != pipe ||
         cur->blend_mode != blend_mode ||
         cur->mask_tex != item.mask_tex ||
         (pipe == GpuPipelineKind::SubpixelText && cur->blend_rgba8 != blend_rgb)))
    {
        if (!ensure_batch_cap_array(&ctx->gpu_batches, &ctx->gpu_batch_cap, ctx->gpu_batch_count)) return false;
        cur = &ctx->gpu_batches[ctx->gpu_batch_count++];
        *cur = make_batch(*out_prim_cursor);
    }
    cur->pipeline = pipe;
    cur->blend_rgba8 = blend_rgb;
    cur->mask_tex = item.mask_tex;
    cur->blend_mode = blend_mode;
    cur->is_punch_hole = (pipe == GpuPipelineKind::ImagePunch);

    if (uses_ext_tex && (item.ext1 || item.ext2)) {
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
            *cur = make_batch(*out_prim_cursor);
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
    Arena* scratch,
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

    const size_t seg_mark = scratch->mark();
    constexpr uint32_t kGridDim = 32u;
    constexpr uint32_t kGridCells = kGridDim * kGridDim;
    constexpr uint32_t kRankGridEntries = kGridCells * kNumSpatialRanks;
    const size_t min_base_bytes =
        size_t(count) * 8u +
        size_t(kGridCells) * (sizeof(int32_t) + sizeof(uint16_t)) +
        size_t(kRankGridEntries) * (2u * sizeof(int32_t)) + 256u;
    if (scratch->remaining() < min_base_bytes) {
        for (uint32_t i = 0u; i < count; ++i) {
            (void)append_gpu_opt_prim(ctx, out_prim_cursor, items[i], i == 0u && force_first_new_batch);
        }
        return;
    }
    uint32_t* layers = scratch->alloc_uninit<uint32_t>(count);
    uint32_t* sorted = scratch->alloc_uninit<uint32_t>(count);
    int32_t* cell_head = scratch->alloc_uninit<int32_t>(kGridCells);
    uint16_t* cell_len = scratch->alloc<uint16_t>(kGridCells);
    int32_t* cell_max_layer = scratch->alloc_uninit<int32_t>(kRankGridEntries);
    int32_t* cell_pruned_layer = scratch->alloc_uninit<int32_t>(kRankGridEntries);
    if (!layers || !sorted || !cell_head || !cell_len || !cell_max_layer || !cell_pruned_layer) {
        scratch->rewind(seg_mark);
        for (uint32_t i = 0u; i < count; ++i) {
            (void)append_gpu_opt_prim(ctx, out_prim_cursor, items[i], i == 0u && force_first_new_batch);
        }
        return;
    }

    const size_t avail_bytes = scratch->remaining();
    const uint32_t want_nodes = (count <= 65536u) ? (count * 8u) : (count * 2u);
    const size_t reserve_tail = size_t(count) * 12u + 8192u;
    const uint32_t max_fit_nodes = (avail_bytes > reserve_tail)
        ? static_cast<uint32_t>((avail_bytes - reserve_tail) / sizeof(SpatialGridNode)) : 0u;
    const uint32_t node_cap = (max_fit_nodes < want_nodes) ? max_fit_nodes : want_nodes;
    SpatialGridNode* nodes = (node_cap > 0u) ? scratch->alloc_uninit<SpatialGridNode>(node_cap) : nullptr;
    const uint32_t effective_node_cap = nodes ? node_cap : 0u;

    const float vw = static_cast<float>(ctx->active_ui->viewport_w > 0u ? ctx->active_ui->viewport_w : 1u);
    const float vh = static_cast<float>(ctx->active_ui->viewport_h > 0u ? ctx->active_ui->viewport_h : 1u);
    const float inv_cell_w = static_cast<float>(kGridDim) / vw;
    const float inv_cell_h = static_cast<float>(kGridDim) / vh;

    for (uint32_t c = 0u; c < kGridCells; ++c) {
        cell_head[c] = -1;
    }
    for (uint32_t e = 0u; e < kRankGridEntries; ++e) {
        cell_max_layer[e] = -1;
        cell_pruned_layer[e] = -1;
    }

    uint32_t global_items[64] = {};
    uint32_t global_count = 0u;
    int32_t global_pruned_layer[kNumSpatialRanks];
    for (uint32_t r = 0u; r < kNumSpatialRanks; ++r) global_pruned_layer[r] = -1;
    uint32_t node_used = 0u;
    uint32_t max_layer = 0u;

    auto same_layer_ok = [](const OptPrimItem& prev, const OptPrimItem& cur) noexcept -> bool {
        if (prev.rank < cur.rank) return true;
        if (prev.rank > cur.rank) return false;
        if (cur.rank == 3u) return prev.blend_rgb == cur.blend_rgb;
        if (cur.rank == 6u) {
            return prev.pipe == cur.pipe &&
                   prev.blend_mode == cur.blend_mode &&
                   prev.mask_tex == cur.mask_tex &&
                   prev.blend_rgb == cur.blend_rgb;
        }
        return true;
    };

    auto check_rank_layers = [](const int32_t* rank_layers, uint8_t cur_rank, uint32_t& L) noexcept {
        for (uint32_t r = 0u; r < kNumSpatialRanks; ++r) {
            const int32_t ml = rank_layers[r];
            if (ml < 0) continue;
            const bool same_ok = (r < cur_rank) || (r == cur_rank && cur_rank != 3u && cur_rank != 6u);
            const uint32_t need = same_ok ? static_cast<uint32_t>(ml) : (static_cast<uint32_t>(ml) + 1u);
            if (need > L) L = need;
        }
    };

    auto record_cell_exact = [&](uint32_t c, uint32_t item_idx, uint8_t rank, int32_t lv) noexcept {
        int32_t& mslot = cell_max_layer[c * kNumSpatialRanks + rank];
        if (lv > mslot) mslot = lv;
        if (cell_len[c] < 64u && node_used < effective_node_cap) {
            const int32_t ni = static_cast<int32_t>(node_used++);
            nodes[ni] = SpatialGridNode{ item_idx, cell_head[c] };
            cell_head[c] = ni;
            ++cell_len[c];
        } else {
            int32_t& pslot = cell_pruned_layer[c * kNumSpatialRanks + rank];
            if (lv > pslot) pslot = lv;
        }
    };

    uint16_t strip_cells[256];

    for (uint32_t i = 0u; i < count; ++i) {
        const OptPrimItem& it = items[i];
        uint32_t L = 0u;
        check_rank_layers(global_pruned_layer, it.rank, L);

        for (uint32_t gi = 0u; gi < global_count; ++gi) {
            const uint32_t j = global_items[gi];
            const OptPrimItem& ot = items[j];
            if (prim_overlaps(it, ot)) {
                const uint32_t need = same_layer_ok(ot, it) ? layers[j] : (layers[j] + 1u);
                if (need > L) L = need;
            }
        }

        // Hollow border rects use their exact 4-strip (+ corner arc) cell footprint instead of solid AABB.
        if (it.hollow_bw > 0.0f) {
            uint64_t seen[16] = {};
            uint32_t strip_count = 0u;
            const float bw = it.hollow_bw;
            auto add_rect_cells = [&](float rx0, float ry0, float rx1, float ry1) noexcept -> bool {
                const float ax0 = std::fmax(rx0, it.x0);
                const float ay0 = std::fmax(ry0, it.y0);
                const float ax1 = std::fmin(rx1, it.x1);
                const float ay1 = std::fmin(ry1, it.y1);
                if (ax1 <= ax0 || ay1 <= ay0) return true;
                const uint32_t gx0 = static_cast<uint32_t>(clamp_range(std::floor(ax0 * inv_cell_w), 0.0f, static_cast<float>(kGridDim - 1u)));
                const uint32_t gy0 = static_cast<uint32_t>(clamp_range(std::floor(ay0 * inv_cell_h), 0.0f, static_cast<float>(kGridDim - 1u)));
                const uint32_t gx1 = static_cast<uint32_t>(clamp_range(std::floor((ax1 - 1e-3f) * inv_cell_w), 0.0f, static_cast<float>(kGridDim - 1u)));
                const uint32_t gy1 = static_cast<uint32_t>(clamp_range(std::floor((ay1 - 1e-3f) * inv_cell_h), 0.0f, static_cast<float>(kGridDim - 1u)));
                for (uint32_t ty = gy0; ty <= gy1; ++ty) {
                    for (uint32_t tx = gx0; tx <= gx1; ++tx) {
                        const uint32_t c = ty * kGridDim + tx;
                        const uint64_t bit = 1ULL << (c & 63u);
                        if ((seen[c >> 6] & bit) == 0u) {
                            if (strip_count >= 256u) return false;
                            seen[c >> 6] |= bit;
                            strip_cells[strip_count++] = static_cast<uint16_t>(c);
                        }
                    }
                }
                return true;
            };
            bool fits_exact =
                add_rect_cells(it.x0, it.y0, it.x1, it.by0 + bw) &&
                add_rect_cells(it.x0, it.by1 - bw, it.x1, it.y1) &&
                add_rect_cells(it.x0, it.by0 + bw, it.bx0 + bw, it.by1 - bw) &&
                add_rect_cells(it.bx1 - bw, it.by0 + bw, it.x1, it.by1 - bw);
            if (fits_exact && it.hollow_max_r > bw) {
                const float r = it.hollow_max_r;
                fits_exact =
                    add_rect_cells(it.x0, it.y0, it.bx0 + r, it.by0 + r) &&
                    add_rect_cells(it.bx1 - r, it.y0, it.x1, it.by0 + r) &&
                    add_rect_cells(it.bx1 - r, it.by1 - r, it.x1, it.y1) &&
                    add_rect_cells(it.x0, it.by1 - r, it.bx0 + r, it.y1);
            }
            if (fits_exact) {
                for (uint32_t sc = 0u; sc < strip_count; ++sc) {
                    const uint32_t c = strip_cells[sc];
                    check_rank_layers(cell_pruned_layer + c * kNumSpatialRanks, it.rank, L);
                    for (int32_t ni = cell_head[c]; ni >= 0; ni = nodes[ni].next) {
                        const uint32_t j = nodes[ni].item_i;
                        const OptPrimItem& ot = items[j];
                        if (prim_overlaps(it, ot)) {
                            const uint32_t need = same_layer_ok(ot, it) ? layers[j] : (layers[j] + 1u);
                            if (need > L) L = need;
                        }
                    }
                }
                layers[i] = L;
                const int32_t lv = static_cast<int32_t>(L);
                for (uint32_t sc = 0u; sc < strip_count; ++sc) {
                    record_cell_exact(strip_cells[sc], i, it.rank, lv);
                }
                if (L > max_layer) max_layer = L;
                continue;
            }
        }

        const uint32_t cx0 = static_cast<uint32_t>(clamp_range(std::floor(it.x0 * inv_cell_w), 0.0f, static_cast<float>(kGridDim - 1u)));
        const uint32_t cy0 = static_cast<uint32_t>(clamp_range(std::floor(it.y0 * inv_cell_h), 0.0f, static_cast<float>(kGridDim - 1u)));
        const uint32_t cx1 = static_cast<uint32_t>(clamp_range(std::floor((it.x1 - 1e-3f) * inv_cell_w), 0.0f, static_cast<float>(kGridDim - 1u)));
        const uint32_t cy1 = static_cast<uint32_t>(clamp_range(std::floor((it.y1 - 1e-3f) * inv_cell_h), 0.0f, static_cast<float>(kGridDim - 1u)));
        const uint32_t ncells = (cx1 - cx0 + 1u) * (cy1 - cy0 + 1u);

        if (ncells > 256u) {
            for (uint32_t ty = cy0; ty <= cy1; ++ty) {
                for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                    const uint32_t c = ty * kGridDim + tx;
                    check_rank_layers(cell_max_layer + c * kNumSpatialRanks, it.rank, L);
                }
            }
            layers[i] = L;
            const int32_t lv = static_cast<int32_t>(L);
            for (uint32_t ty = cy0; ty <= cy1; ++ty) {
                for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                    const uint32_t c = ty * kGridDim + tx;
                    int32_t& mslot = cell_max_layer[c * kNumSpatialRanks + it.rank];
                    if (lv > mslot) mslot = lv;
                }
            }
            if (global_count < 64u) {
                global_items[global_count++] = i;
            } else if (lv > global_pruned_layer[it.rank]) {
                global_pruned_layer[it.rank] = lv;
            }
        } else {
            for (uint32_t ty = cy0; ty <= cy1; ++ty) {
                for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                    const uint32_t c = ty * kGridDim + tx;
                    check_rank_layers(cell_pruned_layer + c * kNumSpatialRanks, it.rank, L);
                    for (int32_t ni = cell_head[c]; ni >= 0; ni = nodes[ni].next) {
                        const uint32_t j = nodes[ni].item_i;
                        const OptPrimItem& ot = items[j];
                        if (prim_overlaps(it, ot)) {
                            const uint32_t need = same_layer_ok(ot, it) ? layers[j] : (layers[j] + 1u);
                            if (need > L) L = need;
                        }
                    }
                }
            }
            layers[i] = L;
            const int32_t lv = static_cast<int32_t>(L);
            for (uint32_t ty = cy0; ty <= cy1; ++ty) {
                for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                    record_cell_exact(ty * kGridDim + tx, i, it.rank, lv);
                }
            }
        }
        if (L > max_layer) max_layer = L;
    }

    const uint32_t num_layers = max_layer + 1u;
    uint32_t* counts = scratch->alloc<uint32_t>(num_layers + 1u);
    uint32_t* offsets = scratch->alloc_uninit<uint32_t>(num_layers);
    if (!counts || !offsets) {
        scratch->rewind(seg_mark);
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
    int32_t* next_in_run = (scratch->remaining() >= size_t(count) * sizeof(int32_t) + 32u)
        ? scratch->alloc_uninit<int32_t>(count) : nullptr;
    struct ColorRun { uint32_t rgb; int32_t head; int32_t tail; };
    struct SpecialRun {
        GpuPipelineKind pipe;
        uint8_t         blend_mode;
        SDL_GPUTexture* mask_tex;
        uint32_t        blend_rgb;
        int32_t         head;
        int32_t         tail;
    };
    ColorRun stack_runs[64];
    SpecialRun special_runs[32];
    bool first_emitted = true;

    for (uint32_t lv = 0u; lv < num_layers; ++lv) {
        const uint32_t lo = counts[lv];
        const uint32_t hi = counts[lv + 1u];
        if (lo >= hi) continue;

        uint8_t rank_mask = 0u;
        for (uint32_t s = lo; s < hi; ++s) {
            rank_mask |= static_cast<uint8_t>(1u << items[sorted[s]].rank);
        }

        auto emit_one = [&](const OptPrimItem& it) noexcept {
            (void)append_gpu_opt_prim(ctx, out_prim_cursor, it, first_emitted && force_first_new_batch);
            first_emitted = false;
        };

        // Rank 0: QuadAlpha
        if ((rank_mask & (1u << 0)) != 0u) {
            for (uint32_t s = lo; s < hi; ++s) {
                const OptPrimItem& it = items[sorted[s]];
                if (it.rank == 0u) emit_one(it);
            }
        }

        // Rank 1: Image (Texture / BlurGlass / LayerBlur)
        if ((rank_mask & (1u << 1)) != 0u) {
            for (uint32_t s = lo; s < hi; ++s) {
                const OptPrimItem& it = items[sorted[s]];
                if (it.rank == 1u) emit_one(it);
            }
        }

        // Rank 2: GrayscaleText (single-DC R8_UNORM across all colors)
        if ((rank_mask & (1u << 2)) != 0u) {
            for (uint32_t s = lo; s < hi; ++s) {
                const OptPrimItem& it = items[sorted[s]];
                if (it.rank == 2u) emit_one(it);
            }
        }

        // Rank 3: SubpixelText (opt-in ClearType RGB, stably grouped by RGB blend constant)
        if ((rank_mask & (1u << 3)) != 0u) {
            uint32_t run_count = 0u;
            bool overflow_colors = false;
            if (next_in_run) {
                for (uint32_t s = lo; s < hi; ++s) {
                    const OptPrimItem& it = items[sorted[s]];
                    if (it.rank != 3u) continue;
                    uint32_t found = run_count;
                    for (uint32_t r = 0u; r < run_count; ++r) {
                        if (stack_runs[r].rgb == it.blend_rgb) { found = r; break; }
                    }
                    if (found < run_count) {
                        next_in_run[stack_runs[found].tail] = static_cast<int32_t>(s);
                        next_in_run[s] = -1;
                        stack_runs[found].tail = static_cast<int32_t>(s);
                    } else if (run_count < 64u) {
                        stack_runs[run_count++] = ColorRun{ it.blend_rgb, static_cast<int32_t>(s), static_cast<int32_t>(s) };
                        next_in_run[s] = -1;
                    } else {
                        overflow_colors = true;
                    }
                }
                for (uint32_t r = 0u; r < run_count; ++r) {
                    for (int32_t idx = stack_runs[r].head; idx >= 0; idx = next_in_run[idx]) {
                        emit_one(items[sorted[idx]]);
                    }
                }
                if (overflow_colors) {
                    for (uint32_t s = lo; s < hi; ++s) {
                        const OptPrimItem& it = items[sorted[s]];
                        if (it.rank != 3u) continue;
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
                    if (it.rank == 3u) emit_one(it);
                }
            }
        }

        // Rank 4: Slug (GPU Bezier glyphs & dynamic vector fills)
        if ((rank_mask & (1u << 4)) != 0u) {
            for (uint32_t s = lo; s < hi; ++s) {
                const OptPrimItem& it = items[sorted[s]];
                if (it.rank == 4u) emit_one(it);
            }
        }

        // Rank 5: Fx (CanvasGrid / BezierWire / RingArc / MeshGrad / BorderBeam)
        if ((rank_mask & (1u << 5)) != 0u) {
            for (uint32_t s = lo; s < hi; ++s) {
                const OptPrimItem& it = items[sorted[s]];
                if (it.rank == 5u) emit_one(it);
            }
        }

        // Rank 6: Special (non-Normal BlendMode or Texture AlphaMask, stably grouped by exact state)
        if ((rank_mask & (1u << 6)) != 0u) {
            uint32_t srun_count = 0u;
            bool overflow_special = false;
            if (next_in_run) {
                for (uint32_t s = lo; s < hi; ++s) {
                    const OptPrimItem& it = items[sorted[s]];
                    if (it.rank != 6u) continue;
                    uint32_t found = srun_count;
                    for (uint32_t r = 0u; r < srun_count; ++r) {
                        if (special_runs[r].pipe == it.pipe &&
                            special_runs[r].blend_mode == it.blend_mode &&
                            special_runs[r].mask_tex == it.mask_tex &&
                            special_runs[r].blend_rgb == it.blend_rgb)
                        {
                            found = r;
                            break;
                        }
                    }
                    if (found < srun_count) {
                        next_in_run[special_runs[found].tail] = static_cast<int32_t>(s);
                        next_in_run[s] = -1;
                        special_runs[found].tail = static_cast<int32_t>(s);
                    } else if (srun_count < 32u) {
                        special_runs[srun_count++] = SpecialRun{
                            it.pipe, it.blend_mode, it.mask_tex, it.blend_rgb,
                            static_cast<int32_t>(s), static_cast<int32_t>(s)
                        };
                        next_in_run[s] = -1;
                    } else {
                        overflow_special = true;
                    }
                }
                for (uint32_t r = 0u; r < srun_count; ++r) {
                    for (int32_t idx = special_runs[r].head; idx >= 0; idx = next_in_run[idx]) {
                        emit_one(items[sorted[idx]]);
                    }
                }
                if (overflow_special) {
                    for (uint32_t s = lo; s < hi; ++s) {
                        const OptPrimItem& it = items[sorted[s]];
                        if (it.rank != 6u) continue;
                        bool in_runs = false;
                        for (uint32_t r = 0u; r < srun_count; ++r) {
                            if (special_runs[r].pipe == it.pipe &&
                                special_runs[r].blend_mode == it.blend_mode &&
                                special_runs[r].mask_tex == it.mask_tex &&
                                special_runs[r].blend_rgb == it.blend_rgb)
                            {
                                in_runs = true;
                                break;
                            }
                        }
                        if (!in_runs) emit_one(it);
                    }
                }
            } else {
                for (uint32_t s = lo; s < hi; ++s) {
                    const OptPrimItem& it = items[sorted[s]];
                    if (it.rank == 6u) emit_one(it);
                }
            }
        }
    }

    scratch->rewind(seg_mark);
}

void release_window_impl(Context* ctx, SDL_Window* window, bool destroying) noexcept;

int32_t idle_gpu_timeout(Context* ctx, uint64_t now, int32_t timeout) noexcept {
    if (!ctx->device) return timeout;
    auto check = [&](WindowUiState& ui) noexcept {
        if (!ui.depth_tex && !ui.scene_tex && !ui.filter_rt_w) return;
        if (now >= ui.transient_gpu_deadline) {
            release_depth_target(ctx->device, ui);
            release_blur_targets(ctx->device, ui);
            if (ui.filter_rt_w) release_filter_targets(ctx->device, ui);
        } else {
            const auto remaining = static_cast<int32_t>(ui.transient_gpu_deadline - now);
            if (timeout < 0 || remaining < timeout) timeout = remaining;
        }
    };
    if (ctx->headless_ui) check(*ctx->headless_ui);
    for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i) {
        if (ClaimedWindowSlot* slot = ctx->claimed_windows[i]) check(slot->ui);
    }
    return timeout;
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

    // Segment 0 (prims before the first BlurSplit) can use Early-Z opaque prepass even when glass overlays exist.
    const uint32_t seg0_barrier_prim = (ctx->blur_split_count > 0u) ? ctx->blur_splits[0].split_prim : ctx->prim_count;
    bool seg0_has_punch_hole = false;
    for (uint32_t b = 0u; b < ctx->batch_count; ++b) {
        const DrawBatch& sb = ctx->batches[b];
        if (sb.prim_count > 0u && sb.is_punch_hole && sb.first_prim < seg0_barrier_prim) {
            seg0_has_punch_hole = true;
            break;
        }
    }

    const bool allow_seg0_early_z = ctx->supports_d32_depth &&
                                    !seg0_has_punch_hole &&
                                    seg0_barrier_prim > 0u &&
                                    ctx->prim_count <= 4000000u;

    const size_t node_bytes_per_prim = (ctx->prim_count <= 65536u)
        ? (8u * sizeof(SpatialGridNode)) : (2u * sizeof(SpatialGridNode));
    const size_t total_scratch_bytes =
        73728u + size_t(ctx->prim_count) * (sizeof(OptPrimItem) + 20u + node_bytes_per_prim);
    Arena* scratch = &ctx->frame_arena;
    if (ctx->frame_arena.remaining() < total_scratch_bytes) {
        if (ctx->opt_scratch.capacity < total_scratch_bytes) {
            size_t next_cap = ctx->opt_scratch.capacity ? ctx->opt_scratch.capacity * 2u : (256u * 1024u);
            if (next_cap < total_scratch_bytes) {
                next_cap = (total_scratch_bytes + 65535u) & ~size_t(65535u);
            }
            Arena next = arena_create(next_cap);
            if (next.base) {
                arena_destroy(&ctx->opt_scratch);
                ctx->opt_scratch = next;
            }
        }
        if (ctx->opt_scratch.capacity >= total_scratch_bytes) {
            ctx->opt_scratch.reset();
            scratch = &ctx->opt_scratch;
        }
    }
    const size_t opt_mark = scratch->mark();
    const auto finish = [&](uint32_t emitted) noexcept {
        scratch->rewind(opt_mark);
        if (emitted != ctx->prim_count) {
            ctx->gpu_batch_count = 0u;
            ctx->gpu_blur_split_count = 0u;
            ctx->gpu_use_early_z = false;
        }
    };
    OptPrimItem* items = (scratch->remaining() >= size_t(ctx->prim_count) * sizeof(OptPrimItem) + 64u)
        ? scratch->alloc_uninit<OptPrimItem>(ctx->prim_count) : nullptr;
    const bool lcd_subpixel = !ctx->atlas.subpixel_is_r8;
    auto make_opt_item = [&](const DrawBatch& sb, uint32_t pi, bool allow_opaque) noexcept -> OptPrimItem {
        const Prim& p = ctx->prims[pi];
        const uint8_t aux = (ctx->prim_aux_flags && pi < ctx->prim_aux_cap) ? ctx->prim_aux_flags[pi] : 0u;
        const uint8_t blend_mode = sb.blend_mode;
        const GpuPipelineKind pipe = classify_prim_pipeline(p, sb.is_punch_hole, blend_mode, allow_opaque, lcd_subpixel);
        const uint32_t tex_slot = (p.style.x >> 10) & 0xFu;
        const PrimKind kind = static_cast<PrimKind>(p.style.x & 0xFu);
        SDL_GPUTexture* ext1 = (pipe == GpuPipelineKind::Image && tex_slot > 0u && tex_slot <= sb.ext_count)
            ? sb.ext_textures[tex_slot - 1u] : nullptr;
        SDL_GPUTexture* ext2 = (kind == PrimKind::LayerBlur && p.payload.y > 0u && p.payload.y <= sb.ext_count)
            ? sb.ext_textures[p.payload.y - 1u] : nullptr;
        const uint32_t blend_rgb = (pipe == GpuPipelineKind::SubpixelText) ? (p.style.y & 0x00FFFFFFu) : 0u;
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        compute_prim_screen_aabb(p, &x0, &y0, &x1, &y1);
        float hollow_bw = 0.0f;
        float hollow_max_r = 0.0f;
        if (kind == PrimKind::Rect && (aux & kPrimAuxHollowBorder) != 0u) {
            const float bw_raw = unpack_f16_cpu(static_cast<uint16_t>(p.style.w));
            const float soft = unpack_f16_cpu(static_cast<uint16_t>(p.style.w >> 16));
            const float bw = bw_raw + std::fmax(soft * 0.5f, 0.5f) + 1.0f;
            if (bw_raw > 0.0f && 2.0f * bw < p.bounds.z && 2.0f * bw < p.bounds.w) {
                hollow_bw = bw;
                const float r0 = unpack_f16_cpu(static_cast<uint16_t>(p.payload.z));
                const float r1 = unpack_f16_cpu(static_cast<uint16_t>(p.payload.z >> 16));
                const float r2 = unpack_f16_cpu(static_cast<uint16_t>(p.payload.w));
                const float r3 = unpack_f16_cpu(static_cast<uint16_t>(p.payload.w >> 16));
                const float max_r = std::fmax(std::fmax(r0, r1), std::fmax(r2, r3));
                const float half_min = 0.5f * std::fmin(p.bounds.z, p.bounds.w);
                hollow_max_r = std::fmin(std::fmax(max_r, 0.0f), half_min);
            }
        }
        return OptPrimItem{
            .orig_idx = pi,
            .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1,
            .bx0 = p.bounds.x, .by0 = p.bounds.y,
            .bx1 = p.bounds.x + p.bounds.z, .by1 = p.bounds.y + p.bounds.w,
            .hollow_bw = hollow_bw,
            .hollow_max_r = hollow_max_r,
            .blend_rgb = blend_rgb,
            .mask_tex = sb.mask_tex,
            .ext1 = ext1,
            .ext2 = ext2,
            .pipe = pipe,
            .blend_mode = blend_mode,
            .rank = compute_spatial_rank(pipe, blend_mode, sb.mask_tex),
        };
    };
    if (!items) {
        // Extreme OS OOM fallback: classify every primitive in-place into its micro-shader without Early-Z.
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
                const OptPrimItem it = make_opt_item(sb, pi, false);
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
        finish(out_prim_cursor);
        return;
    }

    uint32_t item_count = 0u;
    uint32_t seg0_item_count = 0u;
    uint32_t seg0_opaque_count = 0u;
    for (uint32_t b = 0u; b < ctx->batch_count; ++b) {
        const DrawBatch& sb = ctx->batches[b];
        const uint32_t end_p = sb.first_prim + sb.prim_count;
        for (uint32_t pi = sb.first_prim; pi < end_p && pi < ctx->prim_count; ++pi) {
            const bool in_seg0 = (pi < seg0_barrier_prim);
            const OptPrimItem it = make_opt_item(sb, pi, allow_seg0_early_z && in_seg0);
            items[item_count++] = it;
            if (in_seg0) {
                ++seg0_item_count;
                if (it.pipe == GpuPipelineKind::QuadOpaque) ++seg0_opaque_count;
            }
        }
    }

    uint32_t out_prim_cursor = 0u;
    uint32_t next_split = 0u;
    uint32_t seg_start = 0u;

    if (allow_seg0_early_z && seg0_opaque_count > 0u) {
        ctx->gpu_use_early_z = true;
        // Pass 0 (Segment 0): Front-to-back opaque Early-Z quad batch (writes depth, zero alpha blending).
        bool first_op = true;
        for (uint32_t i = seg0_item_count; i > 0u; --i) {
            const OptPrimItem& it = items[i - 1u];
            if (it.pipe == GpuPipelineKind::QuadOpaque) {
                (void)append_gpu_opt_prim(ctx, &out_prim_cursor, it, first_op);
                first_op = false;
            }
        }
        // Compact remaining non-opaque segment-0 items in original back-to-front order and spatially coalesce.
        uint32_t rem_count = 0u;
        for (uint32_t i = 0u; i < seg0_item_count; ++i) {
            if (items[i].pipe != GpuPipelineKind::QuadOpaque) {
                items[rem_count++] = items[i];
            }
        }
        if (rem_count > 0u) {
            emit_spatial_segment(ctx, scratch, items, rem_count, &out_prim_cursor, true);
        }
        seg_start = seg0_item_count;
    }

    // Remaining segments (all of frame if no Early-Z, or segments 1+ after first BlurSplit): partition by blur splits & punch holes.
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
        const bool is_punch = (items[seg_start].pipe == GpuPipelineKind::ImagePunch);
        uint32_t seg_end = seg_start + 1u;
        while (seg_end < item_count &&
               items[seg_end].orig_idx < barrier_prim &&
               (items[seg_end].pipe == GpuPipelineKind::ImagePunch) == is_punch)
        {
            ++seg_end;
        }
        if (is_punch) {
            for (uint32_t i = seg_start; i < seg_end; ++i) {
                (void)append_gpu_opt_prim(ctx, &out_prim_cursor, items[i], i == seg_start);
            }
        } else {
            emit_spatial_segment(ctx, scratch, items + seg_start, seg_end - seg_start, &out_prim_cursor, true);
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

    finish(out_prim_cursor);
}

Context* create_context(const ContextDesc& desc) noexcept {
    Context* ctx = static_cast<Context*>(std::calloc(1u, sizeof(Context)));
    if (!ctx) {
        return nullptr;
    }
    ctx->supports_d32_depth = true;
    ctx->grad_stop_count = 1u;

    ctx->max_layout_elements = desc.max_layout_elements < 4096u ? 4096u : (desc.max_layout_elements > (1u << 20) ? (1u << 20) : desc.max_layout_elements);
    if (!ensure_batch_cap(ctx)) { destroy_context(ctx); return nullptr; }
    ctx->arena_epoch = 1u;
    ctx->base_theme = Theme::dark();
    ctx->active_ui = nullptr;
    ctx->headless = desc.headless;
    ctx->enable_gpu_debug = desc.enable_gpu_debug;
    ctx->prefer_high_performance_gpu = desc.prefer_high_performance_gpu;
    ctx->time_scale=1;
#if defined(_WIN32)
    BOOL animation_enabled=TRUE;
    if(desc.respect_reduced_motion && SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation_enabled,0)) ctx->reduced_motion=!animation_enabled;
#elif defined(__APPLE__) && defined(__MACH__)
    if (desc.respect_reduced_motion) {
        if (Class ws_cls = objc_getClass("NSWorkspace")) {
            using SharedFn = ::id (*)(Class, SEL);
            using BoolFn = signed char (*)(::id, SEL);
            ::id ws = reinterpret_cast<SharedFn>(objc_msgSend)(ws_cls, sel_registerName("sharedWorkspace"));
            if (ws) {
                const SEL sel_rm = sel_registerName("accessibilityDisplayShouldReduceMotion");
                using RespFn = signed char (*)(::id, SEL, SEL);
                if (reinterpret_cast<RespFn>(objc_msgSend)(ws, sel_registerName("respondsToSelector:"), sel_rm)) {
                    ctx->reduced_motion = reinterpret_cast<BoolFn>(objc_msgSend)(ws, sel_rm) != 0;
                }
            }
        }
    }
#elif defined(__linux__)
    if (desc.respect_reduced_motion) {
        const char* gtk_anim = SDL_getenv("GTK_ENABLE_ANIMATIONS");
        const char* etch_rm = SDL_getenv("ETCH_REDUCED_MOTION");
        if ((gtk_anim && gtk_anim[0] == '0' && gtk_anim[1] == '\0') ||
            (etch_rm && (etch_rm[0] == '1' || etch_rm[0] == 't' || etch_rm[0] == 'T')))
        {
            ctx->reduced_motion = true;
        }
    }
#endif
    ctx->enable_os_a11y = desc.enable_os_a11y;
    ctx->refresh_fn = desc.on_window_refresh;
    ctx->refresh_user_data = desc.on_window_refresh ? desc.refresh_user_data : nullptr;
    if (desc.gpu_driver) {
        std::strncpy(ctx->gpu_driver, desc.gpu_driver, sizeof(ctx->gpu_driver) - 1u);
        ctx->gpu_driver[sizeof(ctx->gpu_driver) - 1u] = '\0';
    }
    ctx->max_prims = (desc.max_prims_per_frame > 0u) ? desc.max_prims_per_frame : 2048u;
    if (ctx->max_prims < kMinPrimCap) ctx->max_prims = kMinPrimCap;
    if (ctx->max_prims > kMaxPrimHardCap) ctx->max_prims = kMaxPrimHardCap;
    ctx->frame_arena = arena_create(desc.frame_arena_bytes ? desc.frame_arena_bytes : (512u * 1024u));
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
    if (desc.lcd_subpixel_text) {
        ctx->atlas.lcd_subpixel_text = true;
        ctx->atlas.subpixel_is_r8 = false;
        ctx->atlas.subpixel_gpu_is_r8 = false;
    }

    // Only allocate headless_ui lazily when no window is attached; windowed contexts own per-window
    // UI state via claim_window_swapchain() and keep headless_ui null unless headless rendering is invoked.
    if (desc.window == nullptr) {
        ctx->active_ui = ensure_headless_ui(ctx);
        if (!ctx->active_ui) {
            destroy_context(ctx);
            return nullptr;
        }
        if (desc.headless && !ensure_window_ui_clay(ctx, ctx->headless_ui, 1280.0f, 720.0f)) {
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
        ctx->active_ui = window_ui(ctx, desc.window);
        ctx->active_window = desc.window;
        const float init_dpi = SDL_GetWindowDisplayScale(desc.window);
        if (init_dpi > 0.0f) {
            ctx->active_ui->dpi_scale = init_dpi;
        }
    }

    if (ctx->active_ui) ctx->active_ui->redraw_budget = 2u;
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

GpuStatus gpu_status(const Context* ctx) noexcept {
    if (!ctx) return GpuStatus::Failed;
    if (ctx->device) return GpuStatus::Ready;
    return ctx->gpu_init_failed ? GpuStatus::Failed : GpuStatus::Uninitialized;
}

const char* gpu_driver_name(const Context* ctx) noexcept {
    if (!ctx || !ctx->device) return "";
    const char* name = SDL_GetGPUDeviceDriver(ctx->device);
    return name ? name : "";
}

bool retry_gpu_initialization(Context* ctx, const char* driver) noexcept {
    if (!ctx || !ctx->gpu_init_failed || ctx->in_frame) return false;
    if (driver) {
        const size_t len = std::strlen(driver);
        if (len >= sizeof(ctx->gpu_driver)) return false;
        std::memcpy(ctx->gpu_driver, driver, len + 1u);
    }
    ctx->gpu_init_failed = false;
    if (!ensure_gpu_device(ctx)) return false;
    request_redraw(ctx);
    return true;
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
        if (ctx->claimed_windows[i]) release_window_impl(ctx, ctx->claimed_windows[i]->window, true);
    ctx->active_ui = ctx->headless_ui;
    if (ctx->device) {
        release_shader_surfaces(ctx);
        release_pso_table(ctx);
        if (ctx->quad_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->quad_vs_shader);
        if (ctx->quad_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->quad_ps_shader);
        if (ctx->subpixel_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->subpixel_vs_shader);
        if (ctx->subpixel_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->subpixel_ps_shader);
        if (ctx->slug_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->slug_vs_shader);
        if (ctx->slug_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->slug_ps_shader);
        if (ctx->image_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->image_vs_shader);
        if (ctx->image_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->image_ps_shader);
        if (ctx->fx_vs_shader) SDL_ReleaseGPUShader(ctx->device, ctx->fx_vs_shader);
        if (ctx->fx_ps_shader) SDL_ReleaseGPUShader(ctx->device, ctx->fx_ps_shader);
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
        if (ctx->grad_stop_gpu) SDL_ReleaseGPUBuffer(ctx->device, ctx->grad_stop_gpu);
        if (ctx->grad_stop_transfer) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->grad_stop_transfer);
        ctx->grad_stop_gpu = nullptr; ctx->grad_stop_transfer = nullptr;
        ctx->grad_stop_gpu_capacity = ctx->grad_stop_transfer_capacity = 0u;
        if (ctx->texture_upload_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->texture_upload_buf);
        if (ctx->atlas.gpu_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->atlas.gpu_tex);
        if (ctx->atlas.subpixel_gpu_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->atlas.subpixel_gpu_tex);
        if (ctx->active_ui) {
            release_blur_targets(ctx->device, *ctx->active_ui);
            release_filter_targets(ctx->device, *ctx->active_ui);
            release_depth_target(ctx->device, *ctx->active_ui);
        }
        if (ctx->headless_tex) SDL_ReleaseGPUTexture(ctx->device, ctx->headless_tex);
        if (ctx->headless_dl_buf) SDL_ReleaseGPUTransferBuffer(ctx->device, ctx->headless_dl_buf);
        SDL_DestroyGPUDevice(ctx->device);
        ctx->device = nullptr;
    } else {
        release_pso_table(ctx);
    }

    if (ctx->active_ui && ctx->active_ui->mouse_captured) {
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
    std::free(ctx->clip_mask_textures);
    std::free(ctx->grad_stops);
    std::free(ctx->dyn_slug_curves);
    std::free(ctx->dyn_slug_bands);
    std::free(ctx->prims);
    std::free(ctx->prim_aux_flags);
    if (ctx->headless_ui) {
        destroy_window_ui_state(ctx, ctx->headless_ui);
        std::free(ctx->headless_ui);
        ctx->headless_ui = nullptr;
        ctx->active_ui = nullptr;
    }
    std::free(ctx->claimed_windows); std::free(ctx->batches); std::free(ctx->blur_splits);
    std::free(ctx->gpu_prims); std::free(ctx->gpu_prim_depths);
    std::free(ctx->gpu_batches); std::free(ctx->gpu_blur_splits);
    std::free(ctx->filter_prims); std::free(ctx->filter_prim_aux_flags); std::free(ctx->filter_batches); std::free(ctx->filter_groups);
    std::free(ctx->mipmapped_textures);
    ctx->mipmapped_textures = nullptr;
    ctx->mipmapped_texture_count = ctx->mipmapped_texture_cap = 0u;
    arena_destroy(&ctx->opt_scratch);
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

bool render_to_texture(Context* ctx, const RenderTargetDesc& desc) noexcept {
    if (!ctx || ctx->in_frame || !desc.texture || !desc.width || !desc.height) return false;
    switch (desc.sdl_texture_format) {
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
            break;
        default:
            return SDL_SetError("Unsupported Etch render target format");
    }
    if (!ensure_gpu_device(ctx)) return false;
    SDL_Window* draw_window = ctx->draw_list_window;
    WindowUiState* draw_ui = draw_window ? window_ui(ctx, draw_window) : ctx->headless_ui;
    if (!draw_ui) return false;
    const uint64_t start = SDL_GetPerformanceCounter();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->device);
    if (!cmd) return false;

    WindowUiState* saved_ui = ctx->active_ui;
    SDL_Window* saved_window = ctx->active_window;
    ctx->active_ui = draw_ui;
    ctx->active_window = draw_window;
    const bool recorded = record_frame_commands(ctx, cmd, desc.texture,
        static_cast<SDL_GPUTextureFormat>(desc.sdl_texture_format), desc.width, desc.height, desc.clear_color);
    ctx->active_ui = saved_ui;
    ctx->active_window = saved_window;
    if (!recorded) {
        SDL_CancelGPUCommandBuffer(cmd);
        complete_shader_surfaces(ctx, draw_window, false);
        return false;
    }
    const bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
    complete_shader_surfaces(ctx, draw_window, submitted);
    ctx->stats.draw_call_count = ctx->gpu_batch_count;
    ctx->stats.gpu_submit_ms = double(SDL_GetPerformanceCounter() - start) * 1000.0 /
        double(SDL_GetPerformanceFrequency());
    return submitted;
}

bool render_headless_pixels(Context* ctx, const ReadPixelsDesc& desc) noexcept {
    if (!ctx || ctx->in_frame || !desc.receive || desc.frame_width > 16384u || desc.frame_height > 16384u ||
        desc.x >= desc.frame_width || desc.y >= desc.frame_height) return false;
    const uint32_t remaining_w = desc.frame_width - desc.x, remaining_h = desc.frame_height - desc.y;
    const uint32_t width = desc.width ? desc.width : remaining_w;
    const uint32_t height = desc.height ? desc.height : remaining_h;
    if (width > remaining_w || height > remaining_h || !ensure_gpu_device(ctx)) {
        return false;
    }
    if (!ctx->active_ui) {
        ctx->active_ui = ensure_headless_ui(ctx);
        if (!ctx->active_ui) return false;
    }

    // Align row pitch to 64 pixels (256 bytes) to satisfy D3D12 copy pitch alignment without extra copies.
    const uint32_t aligned_pixels_per_row = (width + 63u) & ~63u;
    const uint32_t stride_bytes = aligned_pixels_per_row * 4u;
    if (!ensure_headless_resources(ctx, desc.frame_width, desc.frame_height, stride_bytes * height)) {
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
            desc.frame_width,
            desc.frame_height,
            desc.clear_color))
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
        .x = desc.x,
        .y = desc.y,
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
    const bool received = desc.receive(static_cast<const uint8_t*>(pixels), width, height, stride_bytes, desc.user_data);
    SDL_UnmapGPUTransferBuffer(ctx->device, ctx->headless_dl_buf);

    const uint64_t t1 = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    ctx->stats.draw_call_count = ctx->gpu_batch_count;
    ctx->stats.gpu_submit_ms = (freq > 0u)
        ? (static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(freq))
        : 0.0;

    return received;
}

bool render_headless_png(Context* ctx, uint32_t width, uint32_t height, const char* path, Color clear_color) noexcept {
    if (!path || !path[0]) return false;
    return render_headless_pixels(ctx, {
        .frame_width = width, .frame_height = height, .clear_color = clear_color,
        .receive = [](const uint8_t* rgba, uint32_t w, uint32_t h, uint32_t pitch, void* data) {
            return stbi_write_png(*static_cast<const char**>(data), static_cast<int>(w), static_cast<int>(h),
                                  4, rgba, static_cast<int>(pitch)) != 0;
        },
        .user_data = &path,
    });
}

namespace {

bool is_mipmapped_texture(const Context* ctx, SDL_GPUTexture* tex) noexcept {
    for (uint32_t i = 0u; i < ctx->mipmapped_texture_count; ++i) {
        if (ctx->mipmapped_textures[i] == tex) return true;
    }
    return false;
}

void track_mipmapped_texture(Context* ctx, SDL_GPUTexture* tex) noexcept {
    if (is_mipmapped_texture(ctx, tex)) return;
    if (ctx->mipmapped_texture_count == ctx->mipmapped_texture_cap) {
        const uint32_t next_cap = ctx->mipmapped_texture_cap ? (ctx->mipmapped_texture_cap * 2u) : 16u;
        void* p = std::realloc(ctx->mipmapped_textures, static_cast<size_t>(next_cap) * sizeof(SDL_GPUTexture*));
        if (!p) return;
        ctx->mipmapped_textures = static_cast<SDL_GPUTexture**>(p);
        ctx->mipmapped_texture_cap = next_cap;
    }
    ctx->mipmapped_textures[ctx->mipmapped_texture_count++] = tex;
}

void untrack_mipmapped_texture(Context* ctx, SDL_GPUTexture* tex) noexcept {
    for (uint32_t i = 0u; i < ctx->mipmapped_texture_count; ++i) {
        if (ctx->mipmapped_textures[i] == tex) {
            ctx->mipmapped_textures[i] = ctx->mipmapped_textures[--ctx->mipmapped_texture_count];
            return;
        }
    }
}

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


void release_window_impl(Context* ctx, SDL_Window* window, bool destroying) noexcept {
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
        const bool was_active_ui = (saved == &slot.ui);
        ctx->active_ui = &slot.ui;
        release_blur_targets(ctx->device, *ctx->active_ui);
        release_filter_targets(ctx->device, *ctx->active_ui);
        release_depth_target(ctx->device, *ctx->active_ui);
        destroy_window_ui_state(ctx, &slot.ui);
        SDL_ReleaseWindowFromGPUDevice(ctx->device, window);
        slot.window = nullptr;
        slot.hit_test_attached = false;
        std::free(ctx->claimed_windows[i]); ctx->claimed_windows[i] = nullptr;
        --ctx->claimed_window_count;
        if (ctx->active_window == window) ctx->active_window = nullptr;
        if (ctx->draw_list_window == window) ctx->draw_list_window = nullptr;
        ClaimedWindowSlot* fallback = nullptr;
        for (uint32_t j = 0; j < ctx->claimed_window_cap; ++j) {
            if (!ctx->claimed_windows[j]) continue;
            if (!fallback) fallback = ctx->claimed_windows[j];
            if (ctx->window != window && ctx->claimed_windows[j]->window == ctx->window) {
                fallback = ctx->claimed_windows[j];
                break;
            }
        }
        if (ctx->window == window) {
            ctx->window = fallback ? fallback->window : nullptr;
        }
        if (was_active_ui || !saved) {
            ctx->active_ui = destroying
                ? ctx->headless_ui
                : (fallback ? &fallback->ui : ensure_headless_ui(ctx));
        } else {
            ctx->active_ui = saved;
        }
        return;
    }
}

} // namespace

bool flush_texture_uploads(Context* ctx) noexcept {
    if(!ctx || !ctx->device) return false;
    if(!ctx->texture_upload_cmd) return true;
    SDL_GPUCommandBuffer* cmd=ctx->texture_upload_cmd;ctx->texture_upload_cmd=nullptr;
    return SDL_SubmitGPUCommandBuffer(cmd);
}

void release_window(Context* ctx, SDL_Window* window) noexcept {
    release_window_impl(ctx, window, false);
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

    const SDL_GPUTextureFormat fmt = desc.srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    uint32_t num_levels = 1u;
    if (desc.generate_mipmaps && (desc.width > 1u || desc.height > 1u)) {
        const SDL_GPUTextureUsageFlags mip_usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        if (SDL_GPUTextureSupportsFormat(ctx->device, fmt, SDL_GPU_TEXTURETYPE_2D, mip_usage)) {
            usage = mip_usage;
            uint32_t dim = desc.width > desc.height ? desc.width : desc.height;
            while (dim > 1u) {
                dim >>= 1u;
                ++num_levels;
            }
        }
    }

    const SDL_GPUTextureCreateInfo tex_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = fmt,
        .usage = usage,
        .width = desc.width,
        .height = desc.height,
        .layer_count_or_depth = 1u,
        .num_levels = num_levels,
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
        if (num_levels > 1u) {
            SDL_GenerateMipmapsForGPUTexture(ctx->texture_upload_cmd, tex);
        }
    }
    if (num_levels > 1u) {
        track_mipmapped_texture(ctx, tex);
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
    if (is_mipmapped_texture(ctx, texture) && ctx->texture_upload_cmd) {
        SDL_GenerateMipmapsForGPUTexture(ctx->texture_upload_cmd, texture);
    }
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
    if (is_mipmapped_texture(ctx, texture) && ctx->texture_upload_cmd) {
        SDL_GenerateMipmapsForGPUTexture(ctx->texture_upload_cmd, texture);
    }
    invalidate_surfaces_sampling_texture(ctx, texture);
    return true;
}

namespace {
void ensure_svg_fonts_registered(Context* ctx) noexcept {
    static bool s_svg_fonts_registered = false;
    if (s_svg_fonts_registered || !ctx) return;
    s_svg_fonts_registered = true;
    auto reg_slot = [&](uint16_t slot_idx, const char* family, bool bold, bool italic) noexcept {
        if (slot_idx >= kMaxFontSlots) return;
        const FontSlot& fs = ctx->atlas.slots[slot_idx];
        if (!fs.loaded) return;
        if (fs.font_file_data && fs.font_file_size > 0u) {
            etch_svg_register_font_memory(family, bold, italic, fs.font_file_data, fs.font_file_size);
        }
    };
    resolve_font_slot_idx(&ctx->atlas, kFontSans);
    resolve_font_slot_idx(&ctx->atlas, kFontBold);
    resolve_font_slot_idx(&ctx->atlas, kFontMono);
    resolve_font_slot_idx(&ctx->atlas, kFontItalic);
    // Register default fallback ("") and standard CSS generic families from Etch's loaded faces.
    reg_slot(kFontSans,   "",           false, false);
    reg_slot(kFontSans,   "sans-serif", false, false);
    reg_slot(kFontSans,   "system-ui",  false, false);
    reg_slot(kFontBold,   "",           true,  false);
    reg_slot(kFontBold,   "sans-serif", true,  false);
    reg_slot(kFontItalic, "",           false, true);
    reg_slot(kFontMono,   "monospace",  false, false);
}
} // namespace

LoadedTexture load_texture_file(Context* ctx, const char* path, bool srgb) noexcept {
    if (!ctx) return {};
    ensure_svg_fonts_registered(ctx);
    DecodedImage image{};
    if (!decode_image_file(path, &image)) return {};
    const auto result = create_texture_rgba(ctx, {.rgba_pixels=image.rgba, .width=image.width, .height=image.height, .srgb=srgb, .generate_mipmaps=true});
    free_decoded_image(&image); return result;
}
LoadedTexture load_texture_memory(Context* ctx, const void* data, size_t size_bytes, bool srgb) noexcept {
    if (!ctx) return {};
    if (looks_like_svg_bytes(static_cast<const uint8_t*>(data), size_bytes)) {
        ensure_svg_fonts_registered(ctx);
    }
    DecodedImage image{};
    if (!decode_image_memory(data, size_bytes, &image)) return {};
    const auto result = create_texture_rgba(ctx, {.rgba_pixels=image.rgba, .width=image.width, .height=image.height, .srgb=srgb, .generate_mipmaps=true});
    free_decoded_image(&image); return result;
}
LoadedTexture load_svg_memory(Context* ctx, const void* data, size_t size_bytes, const SvgRasterDesc& desc) noexcept {
    if (!ctx || !data || size_bytes == 0u) return {};
    ensure_svg_fonts_registered(ctx);
    DecodedImage image{};
    if (!decode_svg_memory(data, size_bytes, desc.width_px, desc.height_px, desc.scale, &image)) return {};
    const auto result = create_texture_rgba(ctx, {
        .rgba_pixels = image.rgba,
        .width = image.width,
        .height = image.height,
        .srgb = desc.srgb,
        .generate_mipmaps = desc.generate_mipmaps,
    });
    free_decoded_image(&image);
    return result;
}
LoadedTexture load_svg_file(Context* ctx, const char* path, const SvgRasterDesc& desc) noexcept {
    if (!ctx || !path || !path[0]) return {};
    size_t size = 0u;
    void* raw = SDL_LoadFile(path, &size);
    if (!raw) return {};
    const LoadedTexture result = load_svg_memory(ctx, raw, size, desc);
    SDL_free(raw);
    return result;
}
bool register_svg_font_file(const char* family, bool bold, bool italic, const char* path) noexcept {
    return etch_svg_register_font_file(family, bold, italic, path);
}
bool register_svg_font_memory(const char* family, bool bold, bool italic, const void* data, size_t size_bytes) noexcept {
    return etch_svg_register_font_memory(family, bold, italic, data, size_bytes);
}

void destroy_texture(Context* ctx, LoadedTexture* tex) noexcept {
    if (!tex) {
        return;
    }
    if (ctx && ctx->device && tex->texture) {
        untrack_mipmapped_texture(ctx, tex->texture);
        flush_texture_uploads(ctx);
        SDL_ReleaseGPUTexture(ctx->device, tex->texture);
    }
    tex->texture = nullptr;
    tex->width = 0u;
    tex->height = 0u;
}

void destroy_texture(Context* ctx, SDL_GPUTexture* tex) noexcept {
    if (ctx && ctx->device && tex) {
        untrack_mipmapped_texture(ctx, tex);
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

        uint32_t gray_glyphs = 0u;
        uint32_t misrouted_glyphs = 0u;
        uint32_t total_gpu_prims = 0u;
        for (uint32_t b = 0u; b < ctx->gpu_batch_count; ++b) {
            const DrawBatch& gb = ctx->gpu_batches[b];
            total_gpu_prims += gb.prim_count;
            for (uint32_t k = 0u; k < gb.prim_count; ++k) {
                const Prim& gp = ctx->gpu_prims[gb.first_prim + k];
                const PrimKind kind = static_cast<PrimKind>(gp.style.x & 0xFu);
                const uint32_t tex_slot = (gp.style.x >> 10) & 0xFu;
                if (kind == PrimKind::Glyph && tex_slot == 0u) {
                    if (gb.pipeline == GpuPipelineKind::GrayscaleText) {
                        ++gray_glyphs;
                    } else {
                        ++misrouted_glyphs;
                    }
                }
            }
        }
        // Also verify opt-in LCD mode routes PrimKind::Glyph to SubpixelText.
        ctx->atlas.lcd_subpixel_text = true;
        ctx->atlas.subpixel_is_r8 = false;
        ctx->frame_arena.offset = ctx->frame_arena.capacity;
        optimize_render_batches(ctx);
        ctx->frame_arena.offset = saved_offset;
        uint32_t lcd_glyphs = 0u;
        for (uint32_t b = 0u; b < ctx->gpu_batch_count; ++b) {
            const DrawBatch& gb = ctx->gpu_batches[b];
            for (uint32_t k = 0u; k < gb.prim_count; ++k) {
                const Prim& gp = ctx->gpu_prims[gb.first_prim + k];
                const PrimKind kind = static_cast<PrimKind>(gp.style.x & 0xFu);
                const uint32_t tex_slot = (gp.style.x >> 10) & 0xFu;
                if (kind == PrimKind::Glyph && tex_slot == 0u) {
                    if (gb.pipeline == GpuPipelineKind::SubpixelText) ++lcd_glyphs;
                    else ++misrouted_glyphs;
                }
            }
        }
        destroy_context(ctx);
        set_current_context(prev_ctx);
        if (gray_glyphs == 0u || lcd_glyphs == 0u || misrouted_glyphs != 0u || total_gpu_prims == 0u) {
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
        const uint32_t sv_sc = a.hinted_shelf_count;
        a.subpixel_w = 4096u;
        a.subpixel_h = 4096u;
        a.subpixel_shelf_x0 = 4096u;
        a.subpixel_shelf_x = 4096u;
        a.subpixel_shelf_y = 4096u;
        a.subpixel_shelf_h = 0u;
        a.hinted_shelf_count = 0u;
        const EmojiBitmapEntry he_fail = get_or_cache_hinted_glyph(ctx, kFontSans, static_cast<uint32_t>(gid_g), 14.0f, 0u);
        a.subpixel_w = sv_w;
        a.subpixel_h = sv_h;
        a.subpixel_shelf_x0 = sv_x0;
        a.subpixel_shelf_x = sv_x;
        a.subpixel_shelf_y = sv_y;
        a.subpixel_shelf_h = sv_sh;
        a.hinted_shelf_count = sv_sc;
        a.hinted_gc_pending = false;
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

    // 3. ensure_subpixel_atlas crossing 512x512 -> 512x1024 -> 1024x1024 -> 2048x1024 must never
    //    drop boundary-triggering glyphs or overlap any allocated tile, and LRU GC must reclaim cleanly.
    {
        Context* ctx = create_context({.headless = true});
        if (!ctx) return false;
        auto& a = ctx->atlas;
        constexpr uint32_t kTileW = 20u;
        constexpr uint32_t kTileH = 20u;
        bool saw_512x512 = false;
        bool saw_512x1024 = false;
        bool saw_1024x1024 = false;
        bool saw_2048x1024 = false;
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
            // Verify zero overlap with any prior tile, then mark sentinel byte (R8_UNORM default).
            for (uint32_t row = 0u; row < kTileH && ok; ++row) {
                uint8_t* dst = a.subpixel_pixels + size_t(ty + row) * a.subpixel_w + tx;
                for (uint32_t col = 0u; col < kTileW; ++col) {
                    if (dst[col] != 0u) {
                        ok = false;
                        break;
                    }
                    dst[col] = 0xA5u;
                }
            }
            if (!ok) break;
            if (a.subpixel_w == 512u && a.subpixel_h == 512u) saw_512x512 = true;
            else if (a.subpixel_w == 512u && a.subpixel_h == 1024u) saw_512x1024 = true;
            else if (a.subpixel_w == 1024u && a.subpixel_h == 1024u) saw_1024x1024 = true;
            else if (a.subpixel_w == 2048u && a.subpixel_h == 1024u) {
                saw_2048x1024 = true;
                break;
            }
        }
        const bool gc_flagged = a.hinted_gc_pending;
        const uint32_t prev_epoch = a.uv_epoch;
        // Verify real multi-frame begin_frame triggers GC automatically even when previous frame had prims.
        ctx->prim_count = 32u;
        Context* prev_ctx3 = current_context();
        begin_frame(ctx, {.viewport_w = 200, .viewport_h = 100});
        const bool gc_reclaimed = gc_flagged && !a.hinted_gc_pending && a.subpixel_pixels == nullptr &&
                                  a.subpixel_w == 0u && a.subpixel_h == 0u && a.uv_epoch == prev_epoch + 1u;
        end_frame(ctx);
        // Verify mid-frame set_lcd_subpixel_text defers format switch until next begin_frame without corrupting in-frame prims.
        begin_frame(ctx, {.viewport_w = 200, .viewport_h = 100});
        text(str_lit("Deferred LCD toggle"), {.color = Color::white(), .font_size_px = 14.0f});
        set_lcd_subpixel_text(ctx, true);
        const bool deferred_mid = a.lcd_subpixel_text && a.subpixel_is_r8 && a.hinted_gc_pending;
        end_frame(ctx);
        const bool deferred_ok = deferred_mid && a.subpixel_is_r8 && a.subpixel_pixels != nullptr && a.hinted_gc_pending;
        begin_frame(ctx, {.viewport_w = 200, .viewport_h = 100});
        const bool switched_next_frame = a.lcd_subpixel_text && !a.subpixel_is_r8 && !a.hinted_gc_pending && a.subpixel_pixels == nullptr;
        end_frame(ctx);
        destroy_context(ctx);
        set_current_context(prev_ctx3);
        if (!ok || !saw_512x512 || !saw_512x1024 || !saw_1024x1024 || !saw_2048x1024 ||
            !gc_reclaimed || !deferred_ok || !switched_next_frame)
        {
            std::fprintf(stderr, "atlas test3 failed: ok=%d 512=%d 512x1024=%d 1024=%d 2048=%d gc=%d def=%d sw=%d\n",
                         int(ok), int(saw_512x512), int(saw_512x1024), int(saw_1024x1024), int(saw_2048x1024),
                         int(gc_reclaimed), int(deferred_ok), int(switched_next_frame));
            return false;
        }
    }

    // 4. True LRU hinted atlas shelf reclamation, burst hot-glyph compaction, and 1024-entry ShapeCache.
    {
        Context* ctx = create_context({.headless = true});
        if (!ctx) return false;
        Context* prev_ctx4 = current_context();
        // Initialize headless active_ui once so frame_index tracking matches real frames.
        begin_frame(ctx, {.viewport_w = 400, .viewport_h = 200});
        end_frame(ctx);
        auto& a = ctx->atlas;
        auto insert_synthetic = [&](uint64_t key, uint32_t frame, uint8_t sentinel) noexcept -> bool {
            ctx->active_ui->frame_index = frame;
            if (!a.hinted_cap || (a.hinted_count + 1u) * 4u > a.hinted_cap * 3u) {
                if (!hinted_rehash(&a)) return false;
            }
            uint32_t tx = 0u, ty = 0u;
            uint16_t si = UINT16_MAX;
            if (!ensure_subpixel_atlas(ctx, 24u, 24u, &tx, &ty, &si)) return false;
            for (uint32_t r = 0u; r < 24u; ++r) {
                std::memset(a.subpixel_pixels + size_t(ty + r) * a.subpixel_w + tx, sentinel, 24u);
            }
            uint32_t idx = uint32_t(hash_u64(key)) & (a.hinted_cap - 1u);
            while (a.hinted_glyphs[idx].key) idx = (idx + 1u) & (a.hinted_cap - 1u);
            EmojiBitmapEntry e{};
            e.key = key;
            e.tile_x = tx;
            e.tile_y = ty;
            e.width_px = 24u;
            e.height_px = 24u;
            e.last_used_frame = frame;
            e.shelf_idx = si;
            e.has_bitmap = true;
            a.hinted_glyphs[idx] = e;
            ++a.hinted_count;
            return true;
        };
        auto find_synthetic = [&](uint64_t key) noexcept -> const EmojiBitmapEntry* {
            if (!a.hinted_glyphs || !a.hinted_cap) return nullptr;
            uint32_t idx = uint32_t(hash_u64(key)) & (a.hinted_cap - 1u);
            while (a.hinted_glyphs[idx].key) {
                if (a.hinted_glyphs[idx].key == key) return &a.hinted_glyphs[idx];
                idx = (idx + 1u) & (a.hinted_cap - 1u);
            }
            return nullptr;
        };

        bool ok = true;
        // Fill steady-state 1024x1024 with cold glyphs (frames 1..5), then hot glyphs (frame 20).
        for (uint32_t i = 0u; i < 2000u; ++i) {
            if (!insert_synthetic(0x0100000000000000ULL | uint64_t(i + 1u), 1u + (i / 300u), 0x33u)) {
                ok = false;
                break;
            }
            if (a.subpixel_w == 1024u && a.subpixel_h == 1024u && a.subpixel_shelf_y >= 880u) break;
        }
        constexpr uint32_t kHotCount = 16u;
        uint32_t hot_x[kHotCount] = {};
        uint32_t hot_y[kHotCount] = {};
        for (uint32_t i = 0u; i < kHotCount && ok; ++i) {
            const uint64_t hk = 0x0200000000000000ULL | uint64_t(i + 1u);
            ok &= insert_synthetic(hk, 20u, 0x7Au);
            if (const auto* e = find_synthetic(hk)) {
                hot_x[i] = e->tile_x;
                hot_y[i] = e->tile_y;
            } else {
                ok = false;
            }
        }
        const uint32_t before_gc_count = a.hinted_count;
        a.hinted_gc_pending = true;
        maybe_gc_hinted_atlas(ctx);
        // Steady-state LRU must keep 1024x1024 alive, evict cold shelves, and keep every hot glyph untouched in place.
        const bool steady_dim_ok = (a.subpixel_pixels != nullptr && a.subpixel_w == 1024u && a.subpixel_h == 1024u);
        const bool steady_cnt_ok = (a.hinted_count < before_gc_count && a.hinted_count >= kHotCount);
        bool steady_hot_ok = true;
        for (uint32_t i = 0u; i < kHotCount; ++i) {
            const auto* e = find_synthetic(0x0200000000000000ULL | uint64_t(i + 1u));
            steady_hot_ok &= (e != nullptr && e->tile_x == hot_x[i] && e->tile_y == hot_y[i] &&
                              a.subpixel_pixels && a.subpixel_pixels[size_t(e->tile_y) * a.subpixel_w + e->tile_x] == 0x7Au);
        }
        // New allocations reuse reclaimed shelves without bursting to 2048x1024.
        bool reuse_ok = true;
        for (uint32_t i = 0u; i < 64u && reuse_ok; ++i) {
            reuse_ok &= insert_synthetic(0x0300000000000000ULL | uint64_t(i + 1u), 21u, 0x55u);
        }
        reuse_ok &= (a.subpixel_w == 1024u && a.subpixel_h == 1024u);

        // Now force burst to 2048x1024, add ultra-hot frame-50 glyphs, and verify shrink-back compaction preserves them.
        for (uint32_t i = 0u; i < 2500u && ok && a.subpixel_w < 2048u; ++i) {
            ok &= insert_synthetic(0x0400000000000000ULL | uint64_t(i + 1u), 25u, 0x44u);
        }
        const bool burst_reached = (a.subpixel_w == 2048u && a.subpixel_h == 1024u && a.hinted_gc_pending);
        for (uint32_t i = 0u; i < kHotCount && ok; ++i) {
            ok &= insert_synthetic(0x0500000000000000ULL | uint64_t(i + 1u), 50u, 0xC3u);
        }
        const uint32_t burst_count = a.hinted_count;
        maybe_gc_hinted_atlas(ctx);
        const bool compact_dim_ok = (a.subpixel_pixels != nullptr && a.subpixel_w == 1024u && a.subpixel_h == 1024u && !a.hinted_gc_pending);
        const bool compact_cnt_ok = (a.hinted_count < burst_count && a.hinted_count >= kHotCount);
        bool compact_hot_ok = true;
        for (uint32_t i = 0u; i < kHotCount; ++i) {
            const auto* e = find_synthetic(0x0500000000000000ULL | uint64_t(i + 1u));
            compact_hot_ok &= (e != nullptr && e->tile_x + 24u <= 1024u && e->tile_y + 24u <= 1024u &&
                               a.subpixel_pixels && a.subpixel_pixels[size_t(e->tile_y) * a.subpixel_w + e->tile_x] == 0xC3u);
        }

        // Verify ShapeCache handles >512 distinct strings in one frame without saturating a bank.
        static_assert(Context::ShapeCache::kEntryCap >= 1024u, "ShapeCache bank must support high-density frames");
        begin_frame(ctx, {.viewport_w = 400, .viewport_h = 200});
        bool shape_ok = true;
        for (uint32_t i = 0u; i < 600u && shape_ok; ++i) {
            char buf[32];
            const int len = std::snprintf(buf, sizeof(buf), "cell_%04u_val", i);
            const Vec2 sz = measure_text(Str{buf, static_cast<uint32_t>(len)}, kFontSans, 13.0f);
            shape_ok &= (sz.x > 0.0f);
        }
        const uint32_t sc_entries = ctx->shape_cache.initialized ? ctx->shape_cache.banks[ctx->shape_cache.active_bank].entry_count : 0u;
        shape_ok &= (sc_entries >= 580u);
        end_frame(ctx);
        ok &= steady_dim_ok && steady_cnt_ok && steady_hot_ok && reuse_ok &&
              burst_reached && compact_dim_ok && compact_cnt_ok && compact_hot_ok && shape_ok;
        if (!ok) {
            std::fprintf(stderr, "atlas test4 failed: sdim=%d scnt=%d shot=%d reuse=%d brea=%d cdim=%d ccnt=%d chot=%d shape=%d(n=%u)\n",
                         int(steady_dim_ok), int(steady_cnt_ok), int(steady_hot_ok), int(reuse_ok),
                         int(burst_reached), int(compact_dim_ok), int(compact_cnt_ok), int(compact_hot_ok),
                         int(shape_ok), sc_entries);
        }
        destroy_context(ctx);
        set_current_context(prev_ctx4);
        if (!ok) return false;
    }

    return true;
}

} // namespace etch
