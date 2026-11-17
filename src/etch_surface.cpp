// Persistent procedural material targets. Included after etch_gpu.cpp in the unity build.
namespace etch {
namespace {
struct alignas(16) SurfaceUniform {
    Vec4 viewport_time;
    Vec4 params[4];
};
static_assert(sizeof(SurfaceUniform) == 80 && sizeof(SurfaceUniform) % 16 == 0);

ShaderSurfaceSlot* surface_slot(Context* ctx, ShaderSurfaceId id) noexcept {
    const uint32_t index = id & 0xffffu;
    if (!index || index > 64) return nullptr;
    ShaderSurfaceSlot& s = ctx->shader_surfaces[index - 1];
    return s.shader && s.generation == (id >> 16) ? &s : nullptr;
}

bool surface_window_suspended(SDL_Window* window) noexcept {
    return window && (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
}

int32_t shader_surface_timeout(const Context* ctx, SDL_Window* window, bool all_windows, int32_t timeout) noexcept {
    if (ctx->time_scale == 0 || ctx->reduced_motion) return timeout;
    const uint64_t now = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    for (const ShaderSurfaceSlot& s : ctx->shader_surfaces) {
        if (!s.shader || !s.owner_bound || !s.eval.enabled || !s.continuous_time ||
            (!all_windows && s.window != window) || surface_window_suspended(s.window)) continue;
        const WindowUiState* ui = s.window ? window_ui(ctx, s.window) : &ctx->headless_ui;
        if (s.seen_frame != ui->frame_index) continue;
        const double remaining = s.initialized ? (double(s.last_render_tick) - double(now)) / freq + 1.0 / s.max_fps : 0;
        const int32_t ms = remaining > 0 ? static_cast<int32_t>(std::ceil(remaining * 1000)) : 0;
        if (timeout < 0 || ms < timeout) timeout = ms;
    }
    return timeout;
}

bool surface_texture_premultiplied(const Context* ctx, SDL_GPUTexture* texture) noexcept {
    for (const ShaderSurfaceSlot& s : ctx->shader_surfaces)
        if (s.shader && s.texture == texture) return true;
    return false;
}

bool record_shader_surfaces(Context* ctx, SDL_GPUCommandBuffer* cmd) noexcept {
    const uint64_t now = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    for (ShaderSurfaceSlot& s : ctx->shader_surfaces) {
        if (!s.shader || !s.owner_bound || s.window != ctx->active_window) continue;
        s.recorded = false;
        if (!s.texture || !s.eval.enabled || s.seen_frame != ctx->active_ui->frame_index || surface_window_suspended(s.window)) continue;
        const bool due = s.continuous_time && ctx->time_scale > 0 && !ctx->reduced_motion &&
            double(now - s.last_render_tick) >= freq / s.max_fps;
        if (s.initialized && !s.dirty && !due) continue;
        SDL_GPUColorTargetInfo target = {};
        target.texture = s.texture;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
        if (!pass) return false;
        SDL_BindGPUGraphicsPipeline(pass, s.pipeline);
        const SDL_GPUViewport viewport = {0, 0, float(s.eval.width), float(s.eval.height), 0, 1};
        const SDL_Rect scissor = {0, 0, int(s.eval.width), int(s.eval.height)};
        SDL_SetGPUViewport(pass, &viewport);
        SDL_SetGPUScissor(pass, &scissor);
        SurfaceUniform uniform = {};
        uniform.viewport_time = {float(s.eval.width), float(s.eval.height),
            s.continuous_time && !ctx->reduced_motion ? ctx->active_ui->elapsed_seconds : 0, float(s.seen_frame)};
        std::memcpy(uniform.params, s.eval.params, sizeof(uniform.params));
        SDL_PushGPUFragmentUniformData(cmd, 0, &uniform, sizeof(uniform));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        s.recorded = true;
    }
    return true;
}

void complete_shader_surfaces(Context* ctx, SDL_Window* window, bool submitted) noexcept {
    for (ShaderSurfaceSlot& s : ctx->shader_surfaces) {
        if (s.window != window || !s.recorded) continue;
        if (submitted) {
            s.initialized = true;
            s.dirty = false;
            s.last_render_tick = SDL_GetPerformanceCounter();
        }
        s.recorded = false;
    }
}

void detach_shader_surface_window(Context* ctx, SDL_Window* window) noexcept {
    for (ShaderSurfaceSlot& s : ctx->shader_surfaces) {
        if (!s.owner_bound || s.window != window) continue;
        s.window = nullptr;
        s.owner_bound = false;
        s.eval.enabled = false;
        s.seen_frame = 0;
    }
}

void release_surface_slot(Context* ctx, ShaderSurfaceSlot& s) noexcept {
    if (s.texture) SDL_ReleaseGPUTexture(ctx->device, s.texture);
    SDL_ReleaseGPUGraphicsPipeline(ctx->device, s.pipeline);
    SDL_ReleaseGPUShader(ctx->device, s.shader);
    const uint32_t next = (s.generation + 1) & 0xffffu;
    s = {};
    s.generation = next;
}

void release_shader_surfaces(Context* ctx) noexcept {
    for (ShaderSurfaceSlot& s : ctx->shader_surfaces) if (s.shader) release_surface_slot(ctx, s);
}
} // namespace

ShaderSurfaceId create_shader_surface(Context* ctx, const ShaderSurfaceCreateDesc& desc) noexcept {
    if (!ctx || ctx->in_frame || !std::isfinite(desc.max_fps) || desc.max_fps < 1 || desc.max_fps > 240 || !ensure_gpu_device(ctx)) return 0;
    uint32_t index = 0;
    while (index < 64 && ctx->shader_surfaces[index].shader) ++index;
    if (index == 64) return 0;
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(ctx->device);
    const ShaderSurfaceCode* code = nullptr;
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
    if ((formats & SDL_GPU_SHADERFORMAT_DXIL) && desc.dxil.data) { code = &desc.dxil; format = SDL_GPU_SHADERFORMAT_DXIL; }
    else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) && desc.spirv.data) { code = &desc.spirv; format = SDL_GPU_SHADERFORMAT_SPIRV; }
    else if ((formats & SDL_GPU_SHADERFORMAT_MSL) && desc.msl.data) { code = &desc.msl; format = SDL_GPU_SHADERFORMAT_MSL; }
    if (!code || !code->size || !code->entrypoint || !code->entrypoint[0]) return 0;
    const SDL_GPUShaderCreateInfo info = {code->size, static_cast<const Uint8*>(code->data), code->entrypoint,
        format, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 1, 0};
    SDL_GPUShader* shader = SDL_CreateGPUShader(ctx->device, &info);
    if (!shader) return 0;
    SDL_GPUGraphicsPipeline* pipe = create_graphics_pipeline(ctx->device, ctx->kawase_vs_shader, shader,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, false);
    if (!pipe) { SDL_ReleaseGPUShader(ctx->device, shader); return 0; }
    ShaderSurfaceSlot& s = ctx->shader_surfaces[index];
    s.shader = shader;
    s.pipeline = pipe;
    s.max_fps = desc.max_fps;
    s.continuous_time = desc.continuous_time;
    s.dirty = true;
    return (s.generation << 16) | (index + 1);
}

void destroy_shader_surface(Context* ctx, ShaderSurfaceId id) noexcept {
    if (!ctx || ctx->in_frame) return;
    if (ShaderSurfaceSlot* s = surface_slot(ctx, id)) release_surface_slot(ctx, *s);
}

SDL_GPUTexture* eval_shader_surface(Context* ctx, ShaderSurfaceId id, const ShaderSurfaceEvalDesc& desc) noexcept {
    if (!ctx || !ctx->in_frame || desc.width == 0 || desc.height == 0 || desc.width > 8192 || desc.height > 8192) return nullptr;
    ShaderSurfaceSlot* s = surface_slot(ctx, id);
    if (!s || (s->owner_bound && s->window != ctx->active_window)) return nullptr;
    // One evaluation per owner frame keeps earlier draw commands' borrowed texture alive.
    if (s->owner_bound && s->seen_frame == ctx->active_ui->frame_index) return s->eval.enabled ? s->texture : nullptr;
    const bool resize = !s->texture || s->eval.width != desc.width || s->eval.height != desc.height;
    if (resize) {
        const SDL_GPUTextureCreateInfo info = {SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, desc.width, desc.height, 1, 1, SDL_GPU_SAMPLECOUNT_1, 0};
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(ctx->device, &info);
        if (!texture) return nullptr;
        if (s->texture) SDL_ReleaseGPUTexture(ctx->device, s->texture);
        s->texture = texture;
        s->initialized = false;
    }
    s->dirty |= resize || s->eval.revision != desc.revision || std::memcmp(s->eval.params, desc.params, sizeof(desc.params)) != 0;
    s->eval = desc;
    s->seen_frame = ctx->active_ui->frame_index;
    s->window = ctx->active_window;
    s->owner_bound = true;
    return desc.enabled ? s->texture : nullptr;
}
} // namespace etch
