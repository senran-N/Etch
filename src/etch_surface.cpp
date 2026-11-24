// Persistent procedural material targets. Included after etch_gpu.cpp in the unity build.
namespace etch {
namespace {
struct alignas(16) SurfaceUniform {
    Vec4 viewport_time;
    Vec4 params[4];
};
static_assert(sizeof(SurfaceUniform) == 80 && sizeof(SurfaceUniform) % 16 == 0);

struct SurfaceCodeCopy {
    uint8_t* bytes = nullptr;
    size_t   size = 0u;
    char     entrypoint[128] = {};
};

struct SurfaceCpuEntry {
    const Context*  ctx = nullptr;
    uint32_t        slot_index = 0u;
    SurfaceCodeCopy spirv = {};
    SurfaceCodeCopy dxil = {};
    SurfaceCodeCopy msl = {};
};

SurfaceCpuEntry* g_surface_cpu_entries = nullptr;
uint32_t         g_surface_cpu_count = 0u;
uint32_t         g_surface_cpu_cap = 0u;

void free_surface_code_copy(SurfaceCodeCopy* copy) noexcept {
    std::free(copy->bytes);
    *copy = {};
}

bool copy_surface_code(const ShaderSurfaceCode& src, SurfaceCodeCopy* dst) noexcept {
    *dst = {};
    if (!src.data || src.size == 0u || !src.entrypoint || !src.entrypoint[0]) {
        return true;
    }
    const size_t ep_len = std::strlen(src.entrypoint);
    if (ep_len >= sizeof(dst->entrypoint)) {
        return false;
    }
    auto* buf = static_cast<uint8_t*>(std::malloc(src.size));
    if (!buf) {
        return false;
    }
    std::memcpy(buf, src.data, src.size);
    dst->bytes = buf;
    dst->size = src.size;
    std::memcpy(dst->entrypoint, src.entrypoint, ep_len + 1u);
    return true;
}

SurfaceCpuEntry* find_surface_cpu(const Context* ctx, uint32_t slot_index) noexcept {
    for (uint32_t i = 0u; i < g_surface_cpu_count; ++i) {
        if (g_surface_cpu_entries[i].ctx == ctx && g_surface_cpu_entries[i].slot_index == slot_index) {
            return &g_surface_cpu_entries[i];
        }
    }
    return nullptr;
}

void remove_surface_cpu(const Context* ctx, uint32_t slot_index) noexcept {
    for (uint32_t i = 0u; i < g_surface_cpu_count; ++i) {
        if (g_surface_cpu_entries[i].ctx == ctx && g_surface_cpu_entries[i].slot_index == slot_index) {
            free_surface_code_copy(&g_surface_cpu_entries[i].spirv);
            free_surface_code_copy(&g_surface_cpu_entries[i].dxil);
            free_surface_code_copy(&g_surface_cpu_entries[i].msl);
            g_surface_cpu_entries[i] = g_surface_cpu_entries[--g_surface_cpu_count];
            if (g_surface_cpu_count == 0u) {
                std::free(g_surface_cpu_entries);
                g_surface_cpu_entries = nullptr;
                g_surface_cpu_cap = 0u;
            }
            return;
        }
    }
}

bool store_surface_cpu(const Context* ctx, uint32_t slot_index, const ShaderSurfaceCreateDesc& desc) noexcept {
    remove_surface_cpu(ctx, slot_index);
    if (g_surface_cpu_count == g_surface_cpu_cap) {
        const uint32_t next_cap = g_surface_cpu_cap ? (g_surface_cpu_cap * 2u) : 8u;
        void* p = std::realloc(g_surface_cpu_entries, size_t(next_cap) * sizeof(SurfaceCpuEntry));
        if (!p) return false;
        g_surface_cpu_entries = static_cast<SurfaceCpuEntry*>(p);
        g_surface_cpu_cap = next_cap;
    }
    SurfaceCpuEntry entry = {};
    entry.ctx = ctx;
    entry.slot_index = slot_index;
    if (!copy_surface_code(desc.spirv, &entry.spirv) ||
        !copy_surface_code(desc.dxil, &entry.dxil) ||
        !copy_surface_code(desc.msl, &entry.msl))
    {
        free_surface_code_copy(&entry.spirv);
        free_surface_code_copy(&entry.dxil);
        free_surface_code_copy(&entry.msl);
        return false;
    }
    g_surface_cpu_entries[g_surface_cpu_count++] = entry;
    return true;
}

bool surface_slot_live(const Context* ctx, uint32_t index) noexcept {
    return ctx->shader_surfaces[index].shader != nullptr || find_surface_cpu(ctx, index) != nullptr;
}

bool create_surface_shader_and_pipeline(
    Context* ctx,
    const ShaderSurfaceCode& spirv,
    const ShaderSurfaceCode& dxil,
    const ShaderSurfaceCode& msl,
    uint32_t num_samplers,
    SDL_GPUShader** out_shader,
    SDL_GPUGraphicsPipeline** out_pipe) noexcept
{
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(ctx->device);
    const ShaderSurfaceCode* code = nullptr;
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
    if ((formats & SDL_GPU_SHADERFORMAT_DXIL) && dxil.data) { code = &dxil; format = SDL_GPU_SHADERFORMAT_DXIL; }
    else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) && spirv.data) { code = &spirv; format = SDL_GPU_SHADERFORMAT_SPIRV; }
    else if ((formats & SDL_GPU_SHADERFORMAT_MSL) && msl.data) { code = &msl; format = SDL_GPU_SHADERFORMAT_MSL; }
    if (!code || !code->size || !code->entrypoint || !code->entrypoint[0]) return false;
    const SDL_GPUShaderCreateInfo info = {code->size, static_cast<const Uint8*>(code->data), code->entrypoint,
        format, SDL_GPU_SHADERSTAGE_FRAGMENT, num_samplers, 0, 0, 1, 0};
    SDL_GPUShader* shader = SDL_CreateGPUShader(ctx->device, &info);
    if (!shader) return false;
    if (!ensure_kawase_shaders(ctx)) { SDL_ReleaseGPUShader(ctx->device, shader); return false; }
    SDL_GPUGraphicsPipeline* pipe = create_graphics_pipeline(ctx->device, ctx->kawase_vs_shader, shader,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, GpuBlendMode::Opaque);
    if (!pipe) { SDL_ReleaseGPUShader(ctx->device, shader); return false; }
    *out_shader = shader;
    *out_pipe = pipe;
    return true;
}

bool ensure_surface_gpu_pipeline(Context* ctx, uint32_t index) noexcept {
    ShaderSurfaceSlot& s = ctx->shader_surfaces[index];
    if (s.shader && s.pipeline) return true;
    if (!ensure_gpu_device(ctx)) return false;
    const SurfaceCpuEntry* cpu = find_surface_cpu(ctx, index);
    if (!cpu) return false;
    const ShaderSurfaceCode spirv = {cpu->spirv.bytes, cpu->spirv.size, cpu->spirv.entrypoint};
    const ShaderSurfaceCode dxil = {cpu->dxil.bytes, cpu->dxil.size, cpu->dxil.entrypoint};
    const ShaderSurfaceCode msl = {cpu->msl.bytes, cpu->msl.size, cpu->msl.entrypoint};
    SDL_GPUShader* shader = nullptr;
    SDL_GPUGraphicsPipeline* pipe = nullptr;
    if (!create_surface_shader_and_pipeline(ctx, spirv, dxil, msl, s.num_samplers, &shader, &pipe)) {
        return false;
    }
    s.shader = shader;
    s.pipeline = pipe;
    s.dirty = true;
    return true;
}

ShaderSurfaceSlot* surface_slot(Context* ctx, ShaderSurfaceId id) noexcept {
    const uint32_t index = id & 0xffffu;
    if (!index || index > 64) return nullptr;
    ShaderSurfaceSlot& s = ctx->shader_surfaces[index - 1];
    return surface_slot_live(ctx, index - 1u) && s.generation == (id >> 16) ? &s : nullptr;
}

bool surface_window_suspended(SDL_Window* window) noexcept {
    return window && (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
}

int32_t shader_surface_timeout(const Context* ctx, SDL_Window* window, bool all_windows, int32_t timeout) noexcept {
    if (ctx->time_scale == 0 || ctx->reduced_motion) return timeout;
    const uint64_t now = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    for (uint32_t i = 0u; i < 64u; ++i) {
        const ShaderSurfaceSlot& s = ctx->shader_surfaces[i];
        if (!surface_slot_live(ctx, i) || !s.owner_bound || !s.eval.enabled || !s.continuous_time ||
            (!all_windows && s.window != window) || surface_window_suspended(s.window)) continue;
        const WindowUiState* ui = s.window ? window_ui(ctx, s.window) : ctx->headless_ui;
        if (!ui || s.seen_frame != ui->frame_index) continue;
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

SDL_GPUTexture* ensure_surface_fallback_tex(Context* ctx) noexcept {
    if (ctx->surface_fallback_tex) return ctx->surface_fallback_tex;
    static constexpr uint8_t kWhitePx[4] = {255u, 255u, 255u, 255u};
    const LoadedTexture fallback = create_texture_rgba(ctx, {.rgba_pixels = kWhitePx, .width = 1u, .height = 1u, .srgb = false});
    if (!fallback.texture || !flush_texture_uploads(ctx)) {
        if (fallback.texture) SDL_ReleaseGPUTexture(ctx->device, fallback.texture);
        return nullptr;
    }
    ctx->surface_fallback_tex = fallback.texture;
    return ctx->surface_fallback_tex;
}

bool record_shader_surfaces(Context* ctx, SDL_GPUCommandBuffer* cmd) noexcept {
    const uint64_t now = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    bool active[64] = {};
    bool visited[64] = {};
    uint32_t remaining = 0u;
    for (uint32_t i = 0u; i < 64u; ++i) {
        ShaderSurfaceSlot& s = ctx->shader_surfaces[i];
        if (!s.shader || !s.owner_bound || s.window != ctx->active_window) continue;
        s.recorded = false;
        if (!s.texture || !s.eval.enabled || s.seen_frame != ctx->active_ui->frame_index ||
            surface_window_suspended(s.window)) continue;
        active[i] = true;
        ++remaining;
    }
    while (remaining > 0u) {
        uint32_t next = 64u;
        for (uint32_t i = 0u; i < 64u && next == 64u; ++i) {
            if (!active[i] || visited[i]) continue;
            const ShaderSurfaceSlot& s = ctx->shader_surfaces[i];
            bool blocked = false;
            for (uint32_t k = 0u; k < s.num_samplers && !blocked; ++k) {
                SDL_GPUTexture* tex = s.eval.textures[k];
                if (!tex || tex == s.texture) continue;
                for (uint32_t u = 0u; u < 64u; ++u) {
                    if (u != i && active[u] && !visited[u] && ctx->shader_surfaces[u].texture == tex) {
                        blocked = true;
                        break;
                    }
                }
            }
            if (!blocked) next = i;
        }
        if (next == 64u) {
            for (uint32_t i = 0u; i < 64u; ++i) {
                if (active[i] && !visited[i]) { next = i; break; }
            }
        }
        visited[next] = true;
        --remaining;
        ShaderSurfaceSlot& s = ctx->shader_surfaces[next];
        bool upstream_updated = false;
        uint64_t up_epochs[4] = {};
        for (uint32_t k = 0u; k < s.num_samplers; ++k) {
            SDL_GPUTexture* tex = s.eval.textures[k];
            if (!tex || tex == s.texture) continue;
            for (uint32_t u = 0u; u < 64u; ++u) {
                if (u == next) continue;
                const ShaderSurfaceSlot& up = ctx->shader_surfaces[u];
                if (!up.shader || up.texture != tex) continue;
                if (active[u] && !visited[u]) continue; // Break cycle back-edge deterministically.
                const uint64_t up_epoch = up.recorded ? up.pending_output_epoch : up.output_epoch;
                up_epochs[k] = up_epoch;
                if (up.recorded || !up.initialized || s.sampled_epochs[k] != up_epoch) {
                    upstream_updated = true;
                }
                break;
            }
        }
        const bool due = upstream_updated || (s.continuous_time && ctx->time_scale > 0 && !ctx->reduced_motion &&
            double(now - s.last_render_tick) >= freq / s.max_fps);
        if (s.initialized && !s.dirty && !due) continue;
        SDL_GPUTexture* resolved_texs[4] = {};
        bool need_fallback = false;
        for (uint32_t k = 0u; k < s.num_samplers; ++k) {
            SDL_GPUTexture* tex = s.eval.textures[k];
            if (!tex) {
                tex = nullptr;
                need_fallback = true;
            } else if (tex == s.texture) {
                if (s.enable_feedback && s.initialized && s.prev_texture) {
                    tex = s.prev_texture;
                } else {
                    tex = nullptr;
                    need_fallback = true;
                }
            } else {
                for (uint32_t u = 0u; u < 64u; ++u) {
                    const ShaderSurfaceSlot& up = ctx->shader_surfaces[u];
                    if (up.shader && up.texture == tex && ((!up.initialized && !up.recorded) || (active[u] && !visited[u]))) {
                        tex = nullptr;
                        need_fallback = true;
                        break;
                    }
                }
            }
            resolved_texs[k] = tex;
        }
        if (need_fallback && !ensure_surface_fallback_tex(ctx)) return false;
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
        if (s.num_samplers > 0u) {
            SDL_GPUSampler* smp = s.sampler_mirror_repeat ? ctx->mirror_linear_sampler : ctx->linear_sampler;
            SDL_GPUTextureSamplerBinding bindings[4] = {};
            for (uint32_t k = 0u; k < s.num_samplers; ++k) {
                bindings[k] = {resolved_texs[k] ? resolved_texs[k] : ctx->surface_fallback_tex, smp};
            }
            SDL_BindGPUFragmentSamplers(pass, 0u, bindings, s.num_samplers);
        }
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        s.recorded = true;
        s.pending_output_epoch = ++ctx->shader_surface_epoch;
        for (uint32_t k = 0u; k < s.num_samplers; ++k) s.pending_sampled_epochs[k] = up_epochs[k];
    }
    return true;
}

void complete_shader_surfaces(Context* ctx, SDL_Window* window, bool submitted) noexcept {
    for (ShaderSurfaceSlot& s : ctx->shader_surfaces) {
        if (s.window != window) continue;
        if (s.retired_tex) {
            SDL_ReleaseGPUTexture(ctx->device, s.retired_tex);
            s.retired_tex = nullptr;
        }
        if (s.retired_prev_tex) {
            SDL_ReleaseGPUTexture(ctx->device, s.retired_prev_tex);
            s.retired_prev_tex = nullptr;
        }
        if (s.swapped_this_frame && !submitted && s.texture && s.prev_texture) {
            SDL_GPUTexture* tmp = s.texture;
            s.texture = s.prev_texture;
            s.prev_texture = tmp;
        }
        s.swapped_this_frame = false;
        if (!s.recorded) continue;
        if (submitted) {
            s.initialized = true;
            s.dirty = false;
            s.output_epoch = s.pending_output_epoch;
            for (uint32_t k = 0u; k < s.num_samplers; ++k) s.sampled_epochs[k] = s.pending_sampled_epochs[k];
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

void release_surface_gpu_handles(Context* ctx, ShaderSurfaceSlot& s) noexcept {
    if (ctx->device) {
        if (s.retired_tex) SDL_ReleaseGPUTexture(ctx->device, s.retired_tex);
        if (s.retired_prev_tex) SDL_ReleaseGPUTexture(ctx->device, s.retired_prev_tex);
        if (s.prev_texture) SDL_ReleaseGPUTexture(ctx->device, s.prev_texture);
        if (s.texture) SDL_ReleaseGPUTexture(ctx->device, s.texture);
        if (s.pipeline) SDL_ReleaseGPUGraphicsPipeline(ctx->device, s.pipeline);
        if (s.shader) SDL_ReleaseGPUShader(ctx->device, s.shader);
    }
    s.retired_tex = nullptr;
    s.retired_prev_tex = nullptr;
    s.prev_texture = nullptr;
    s.texture = nullptr;
    s.pipeline = nullptr;
    s.shader = nullptr;
}

void release_shader_surface_gpu_resources(Context* ctx) noexcept {
    if (!ctx) return;
    for (ShaderSurfaceSlot& s : ctx->shader_surfaces) {
        release_surface_gpu_handles(ctx, s);
        s.initialized = false;
        s.dirty = true;
        s.recorded = false;
        s.swapped_this_frame = false;
        s.seen_frame = 0u;
        for (uint32_t k = 0u; k < 4u; ++k) {
            s.eval.textures[k] = nullptr;
            s.sampled_epochs[k] = 0u;
            s.pending_sampled_epochs[k] = 0u;
        }
    }
}

void release_surface_slot(Context* ctx, uint32_t index) noexcept {
    ShaderSurfaceSlot& s = ctx->shader_surfaces[index];
    release_surface_gpu_handles(ctx, s);
    remove_surface_cpu(ctx, index);
    const uint32_t next = (s.generation + 1u) & 0xffffu;
    s = {};
    s.generation = next;
}

void release_shader_surfaces(Context* ctx) noexcept {
    if (!ctx) return;
    for (uint32_t i = 0u; i < 64u; ++i) {
        if (surface_slot_live(ctx, i)) release_surface_slot(ctx, i);
    }
}
} // namespace

ShaderSurfaceId create_shader_surface(Context* ctx, const ShaderSurfaceCreateDesc& desc) noexcept {
    if (!ctx || ctx->in_frame || !std::isfinite(desc.max_fps) || desc.max_fps < 1 || desc.max_fps > 240 ||
        desc.num_samplers > 4u || !ensure_gpu_device(ctx)) return 0;
    uint32_t index = 0;
    while (index < 64 && surface_slot_live(ctx, index)) ++index;
    if (index == 64) return 0;
    SDL_GPUShader* shader = nullptr;
    SDL_GPUGraphicsPipeline* pipe = nullptr;
    if (!create_surface_shader_and_pipeline(ctx, desc.spirv, desc.dxil, desc.msl, desc.num_samplers, &shader, &pipe)) {
        return 0;
    }
    if (!store_surface_cpu(ctx, index, desc)) {
        SDL_ReleaseGPUGraphicsPipeline(ctx->device, pipe);
        SDL_ReleaseGPUShader(ctx->device, shader);
        return 0;
    }
    ShaderSurfaceSlot& s = ctx->shader_surfaces[index];
    s.shader = shader;
    s.pipeline = pipe;
    s.num_samplers = desc.num_samplers;
    s.sampler_mirror_repeat = desc.sampler_mirror_repeat;
    s.enable_feedback = desc.enable_feedback;
    s.max_fps = desc.max_fps;
    s.continuous_time = desc.continuous_time;
    s.dirty = true;
    return (s.generation << 16) | (index + 1);
}

void destroy_shader_surface(Context* ctx, ShaderSurfaceId id) noexcept {
    if (!ctx || ctx->in_frame) return;
    if (ShaderSurfaceSlot* s = surface_slot(ctx, id)) {
        release_surface_slot(ctx, static_cast<uint32_t>(s - ctx->shader_surfaces));
    }
}

SDL_GPUTexture* eval_shader_surface(Context* ctx, ShaderSurfaceId id, const ShaderSurfaceEvalDesc& desc) noexcept {
    if (!ctx || !ctx->in_frame || desc.width == 0 || desc.height == 0 || desc.width > 8192 || desc.height > 8192) return nullptr;
    ShaderSurfaceSlot* s = surface_slot(ctx, id);
    if (!s || (s->owner_bound && s->window != ctx->active_window)) return nullptr;
    // One evaluation per owner frame keeps earlier draw commands' borrowed texture alive.
    if (s->owner_bound && s->seen_frame == ctx->active_ui->frame_index) return s->eval.enabled ? s->texture : nullptr;
    const uint32_t slot_index = static_cast<uint32_t>(s - ctx->shader_surfaces);
    if (!ctx->device && !ensure_gpu_device(ctx)) return nullptr;
    if ((!s->shader || !s->pipeline) && !ensure_surface_gpu_pipeline(ctx, slot_index)) return nullptr;
    const bool resize = !s->texture || s->eval.width != desc.width || s->eval.height != desc.height;
    SDL_GPUTexture* old_tex = nullptr;
    if (resize) {
        const SDL_GPUTextureCreateInfo info = {SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, desc.width, desc.height, 1, 1, SDL_GPU_SAMPLECOUNT_1, 0};
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(ctx->device, &info);
        if (!texture) return nullptr;
        SDL_GPUTexture* prev_texture = nullptr;
        if (s->enable_feedback) {
            prev_texture = SDL_CreateGPUTexture(ctx->device, &info);
            if (!prev_texture) {
                SDL_ReleaseGPUTexture(ctx->device, texture);
                return nullptr;
            }
        }
        old_tex = s->texture;
        if (old_tex) {
            if (s->retired_tex) SDL_ReleaseGPUTexture(ctx->device, s->retired_tex);
            s->retired_tex = old_tex;
            for (ShaderSurfaceSlot& other : ctx->shader_surfaces) {
                if (&other == s || !other.shader) continue;
                for (uint32_t k = 0u; k < other.num_samplers; ++k) {
                    if (other.eval.textures[k] == old_tex) {
                        other.eval.textures[k] = texture;
                        other.dirty = true;
                        other.sampled_epochs[k] = 0u;
                    }
                }
            }
        }
        if (s->prev_texture) {
            if (s->retired_prev_tex) SDL_ReleaseGPUTexture(ctx->device, s->retired_prev_tex);
            s->retired_prev_tex = s->prev_texture;
        }
        s->texture = texture;
        s->prev_texture = prev_texture;
        s->initialized = false;
        s->swapped_this_frame = false;
    }
    bool textures_changed = false;
    bool has_external_surface_input = false;
    for (uint32_t i = 0u; i < s->num_samplers; ++i) {
        if (s->eval.textures[i] != desc.textures[i]) {
            textures_changed = true;
            s->sampled_epochs[i] = 0u;
        }
        if (desc.textures[i] && desc.textures[i] != s->texture && desc.textures[i] != s->prev_texture) {
            for (const ShaderSurfaceSlot& other : ctx->shader_surfaces) {
                if (&other != s && other.shader && other.texture == desc.textures[i]) {
                    has_external_surface_input = true;
                    break;
                }
            }
        }
    }
    s->dirty |= resize || textures_changed || s->eval.revision != desc.revision ||
        std::memcmp(s->eval.params, desc.params, sizeof(desc.params)) != 0;
    s->eval = desc;
    if (old_tex) {
        for (uint32_t i = 0u; i < s->num_samplers; ++i) {
            if (s->eval.textures[i] == old_tex) {
                s->eval.textures[i] = s->texture;
                s->sampled_epochs[i] = 0u;
            }
        }
    }
    if (desc.enabled && s->enable_feedback && s->initialized && s->prev_texture && !s->swapped_this_frame) {
        bool will_render = s->dirty || has_external_surface_input;
        if (!will_render && s->continuous_time && ctx->time_scale > 0.0f && !ctx->reduced_motion) {
            const uint64_t now = SDL_GetPerformanceCounter();
            const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
            if (double(now - s->last_render_tick) >= freq / s->max_fps) {
                will_render = true;
            }
        }
        if (will_render) {
            SDL_GPUTexture* prev_front = s->texture;
            s->texture = s->prev_texture;
            s->prev_texture = prev_front;
            s->swapped_this_frame = true;
            s->dirty = true;
            for (uint32_t i = 0u; i < s->num_samplers; ++i) {
                if (s->eval.textures[i] == prev_front) {
                    s->eval.textures[i] = s->texture;
                }
            }
            for (ShaderSurfaceSlot& other : ctx->shader_surfaces) {
                if (&other == s || !other.shader) continue;
                for (uint32_t k = 0u; k < other.num_samplers; ++k) {
                    if (other.eval.textures[k] == prev_front) {
                        other.eval.textures[k] = s->texture;
                        other.dirty = true;
                        other.sampled_epochs[k] = 0u;
                    }
                }
            }
        }
    }
    s->seen_frame = ctx->active_ui->frame_index;
    s->window = ctx->active_window;
    s->owner_bound = true;
    return desc.enabled ? s->texture : nullptr;
}
} // namespace etch
