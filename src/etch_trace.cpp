// Structured per-frame timing, draw-call, OS memory & estimated VRAM ring trace.
// Compiled via src/etch_unity.cpp (immediately before etch_gpu.cpp).

#if defined(__APPLE__) && defined(__MACH__)
#    include <mach/mach.h>
#endif

namespace etch {

struct FrameTraceRing {
    static constexpr uint32_t kCap = 256u;
    FrameTraceSample samples[kCap] = {};
    uint32_t head = 0u;
    uint32_t count = 0u;
};

namespace {

#if defined(_WIN32)
// Exact binary layout of Windows SDK PROCESS_MEMORY_COUNTERS_EX (avoids <psapi.h> macro/link coupling).
struct EtchProcessMemoryCountersEx {
    DWORD  cb;
    DWORD  PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivateUsage;
};
using K32GetProcessMemoryInfoFn = BOOL (WINAPI *)(HANDLE, void*, DWORD);
#endif

uint64_t trace_format_bpp(SDL_GPUTextureFormat fmt) noexcept {
    switch (fmt) {
        case SDL_GPU_TEXTUREFORMAT_R8_UNORM:
        case SDL_GPU_TEXTUREFORMAT_A8_UNORM:
            return 1u;
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM:
            return 8u;
        case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
            return 16u;
        default:
            return 4u;
    }
}

uint64_t trace_window_vram_bytes(const WindowUiState& ui) noexcept {
    uint64_t bytes = 0u;
    if (ui.depth_tex && ui.depth_w > 0u && ui.depth_h > 0u) {
        bytes += uint64_t(ui.depth_w) * uint64_t(ui.depth_h) * 4u;
    }
    if (ui.blur_rt_w > 0u && ui.blur_rt_h > 0u) {
        if (ui.scene_tex) {
            bytes += uint64_t(ui.blur_rt_w) * uint64_t(ui.blur_rt_h) * trace_format_bpp(ui.blur_rt_fmt);
        }
        if (ui.blur_ping_tex) {
            const uint64_t hw = (ui.blur_rt_w > 1u) ? uint64_t(ui.blur_rt_w / 2u) : 1u;
            const uint64_t hh = (ui.blur_rt_h > 1u) ? uint64_t(ui.blur_rt_h / 2u) : 1u;
            bytes += hw * hh * 4u;
        }
        if (ui.blur_pong_tex) {
            const uint64_t qw = (ui.blur_rt_w > 3u) ? uint64_t(ui.blur_rt_w / 4u) : 1u;
            const uint64_t qh = (ui.blur_rt_h > 3u) ? uint64_t(ui.blur_rt_h / 4u) : 1u;
            bytes += qw * qh * 4u;
        }
    }
    if (ui.filter_rt_w > 0u && ui.filter_rt_h > 0u) {
        for (uint32_t s = 0u; s < kMaxFilterSlices; ++s) {
            for (uint32_t lvl = 0u; lvl < kMaxFilterLevels; ++lvl) {
                if (ui.filter_slices[s].levels[lvl]) {
                    const uint64_t lw = (ui.filter_rt_w >> lvl) > 0u ? uint64_t(ui.filter_rt_w >> lvl) : 1u;
                    const uint64_t lh = (ui.filter_rt_h >> lvl) > 0u ? uint64_t(ui.filter_rt_h >> lvl) : 1u;
                    bytes += lw * lh * 4u;
                }
            }
        }
    }
    return bytes;
}

void reset_trace_frame_counters(Context* ctx) noexcept {
    ctx->trace_text_shape_us = 0.0;
    ctx->trace_prim_build_us = 0.0;
    ctx->trace_gpu_upload_us = 0.0;
    ctx->trace_atlas_evictions = 0u;
    ctx->trace_shape_hits = 0u;
    ctx->trace_shape_misses = 0u;
    ctx->trace_filter_passes = 0u;
    ctx->trace_ui_draw_calls = 0u;
}

} // namespace

void trace_query_process_memory(uint64_t* out_ws, uint64_t* out_commit) noexcept {
    uint64_t ws = 0u;
    uint64_t commit = 0u;
#if defined(_WIN32)
    if (HMODULE k32 = GetModuleHandleA("kernel32.dll")) {
        const FARPROC raw = GetProcAddress(k32, "K32GetProcessMemoryInfo");
        if (raw) {
            K32GetProcessMemoryInfoFn fn = nullptr;
            static_assert(sizeof(fn) == sizeof(raw));
            std::memcpy(&fn, &raw, sizeof(fn));
            EtchProcessMemoryCountersEx pmc = {};
            pmc.cb = static_cast<DWORD>(sizeof(pmc));
            if (fn(GetCurrentProcess(), &pmc, pmc.cb)) {
                ws = static_cast<uint64_t>(pmc.WorkingSetSize);
                commit = static_cast<uint64_t>(pmc.PrivateUsage);
            }
        }
    }
#elif defined(__APPLE__) && defined(__MACH__)
    mach_task_basic_info_data_t info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
    {
        ws = static_cast<uint64_t>(info.resident_size);
        commit = static_cast<uint64_t>(info.resident_size);
    }
#elif defined(__linux__)
    const long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz > 0) {
        if (FILE* f = std::fopen("/proc/self/statm", "r")) {
            unsigned long long sz_pages = 0u;
            unsigned long long res_pages = 0u;
            unsigned long long sh_pages = 0u;
            unsigned long long txt_pages = 0u;
            unsigned long long lib_pages = 0u;
            unsigned long long data_pages = 0u;
            const int matched = std::fscanf(
                f, "%llu %llu %llu %llu %llu %llu",
                &sz_pages, &res_pages, &sh_pages, &txt_pages, &lib_pages, &data_pages);
            std::fclose(f);
            const uint64_t psz = static_cast<uint64_t>(page_sz);
            if (matched >= 2) {
                ws = static_cast<uint64_t>(res_pages) * psz;
            }
            if (matched >= 6 && data_pages > 0u) {
                commit = static_cast<uint64_t>(data_pages) * psz;
            } else if (matched >= 3 && res_pages > sh_pages) {
                commit = static_cast<uint64_t>(res_pages - sh_pages) * psz;
            } else {
                commit = ws;
            }
        }
    }
#endif
    if (out_ws) *out_ws = ws;
    if (out_commit) *out_commit = commit;
}

uint64_t trace_estimate_vram(const Context* ctx) noexcept {
    if (!ctx) return 0u;
    uint64_t total = 0u;

    // GPU storage buffers (prim, depth, Slug static/dynamic, auxiliary clip/gradient)
    if (ctx->prim_gpu_buf) total += uint64_t(ctx->prim_gpu_cap) * sizeof(Prim);
    if (ctx->prim_depth_gpu_buf) total += uint64_t(ctx->prim_depth_gpu_cap) * sizeof(uint32_t);
    if (ctx->slug_curve_gpu_buf) total += uint64_t(ctx->slug_curve_gpu_cap) * sizeof(SlugCurveGpu);
    if (ctx->slug_band_gpu_buf) total += uint64_t(ctx->slug_band_gpu_cap) * sizeof(SlugBandEntryGpu);
    if (ctx->dyn_slug_curve_gpu_buf) total += uint64_t(ctx->dyn_slug_curve_gpu_cap) * sizeof(SlugCurveGpu);
    if (ctx->dyn_slug_band_gpu_buf) total += uint64_t(ctx->dyn_slug_band_gpu_cap) * sizeof(SlugBandEntryGpu);
    if (ctx->clip_shape_gpu) total += uint64_t(ctx->clip_shape_gpu_capacity) * sizeof(ClipShapeGpu);
    if (ctx->grad_stop_gpu) total += uint64_t(ctx->grad_stop_gpu_capacity) * sizeof(GradientStopGpu);

    // Sprite/emoji & hinted/subpixel glyph atlases
    if (ctx->atlas.gpu_tex) {
        total += uint64_t(ctx->atlas.gpu_w) * uint64_t(ctx->atlas.gpu_h) * 4u;
    }
    if (ctx->atlas.subpixel_gpu_tex) {
        const uint64_t bpp = ctx->atlas.subpixel_gpu_is_r8 ? 1u : 4u;
        total += uint64_t(ctx->atlas.subpixel_gpu_w) * uint64_t(ctx->atlas.subpixel_gpu_h) * bpp;
    }
    if (ctx->surface_fallback_tex) {
        total += 4u;
    }

    // Persistent RGBA16F shader surfaces (8 bytes per pixel)
    for (const ShaderSurfaceSlot& s : ctx->shader_surfaces) {
        const uint64_t surf_bytes = uint64_t(s.eval.width) * uint64_t(s.eval.height) * 8u;
        if (s.texture) total += surf_bytes;
        if (s.prev_texture) total += surf_bytes;
        if (s.retired_tex) total += surf_bytes;
        if (s.retired_prev_tex) total += surf_bytes;
    }

    // Per-window transient targets (depth, blur scene/ping/pong, filter pyramid slices)
    if (ctx->headless_ui) {
        total += trace_window_vram_bytes(*ctx->headless_ui);
    }
    for (uint32_t i = 0u; i < ctx->claimed_window_cap; ++i) {
        if (const ClaimedWindowSlot* slot = ctx->claimed_windows[i]) {
            total += trace_window_vram_bytes(slot->ui);
        }
    }

    // Reusable headless offscreen render target
    if (ctx->headless_tex && ctx->headless_w > 0u && ctx->headless_h > 0u) {
        total += uint64_t(ctx->headless_w) * uint64_t(ctx->headless_h) * 4u;
    }

    return total;
}

void trace_record_frame(Context* ctx) noexcept {
    if (!ctx) return;
    if (!ctx->trace_enabled) {
        reset_trace_frame_counters(ctx);
        return;
    }
    if (!ctx->trace_ring) {
        ctx->trace_ring = static_cast<FrameTraceRing*>(std::calloc(1u, sizeof(FrameTraceRing)));
        if (!ctx->trace_ring) {
            reset_trace_frame_counters(ctx);
            return;
        }
    }

    uint64_t ws = 0u;
    uint64_t commit = 0u;
    trace_query_process_memory(&ws, &commit);

    const WindowUiState* ui = ctx->draw_list_window ? window_ui(ctx, ctx->draw_list_window) : ctx->active_ui;
    const uint32_t ui_dc = (ctx->trace_ui_draw_calls > 0u) ? ctx->trace_ui_draw_calls : ctx->stats.draw_call_count;
    const uint32_t bloom_passes = (ui && ui->bloom_intensity > 0.0f) ? 4u : 0u;
    const uint32_t blur_draws = ctx->gpu_blur_split_count * 3u + bloom_passes;

    FrameTraceSample sample = {};
    sample.frame_index = ui ? static_cast<uint64_t>(ui->frame_index) : 0u;
    sample.layout_us = ctx->stats.layout_ms * 1000.0;
    sample.text_shape_us = ctx->trace_text_shape_us;
    sample.prim_build_us = ctx->trace_prim_build_us;
    sample.gpu_upload_us = ctx->trace_gpu_upload_us;
    sample.gpu_submit_us = ctx->stats.gpu_submit_ms * 1000.0;
    sample.ui_draw_calls = ui_dc;
    sample.total_draw_calls = ui_dc + blur_draws + ctx->trace_filter_passes;
    sample.blur_passes = ctx->stats.blur_pass_count;
    sample.filter_passes = ctx->trace_filter_passes;
    sample.prim_count = ctx->stats.prim_count;
    sample.atlas_evictions = ctx->trace_atlas_evictions;
    sample.shape_cache_hits = ctx->trace_shape_hits;
    sample.shape_cache_misses = ctx->trace_shape_misses;
    sample.working_set_bytes = ws;
    sample.private_commit_bytes = commit;
    sample.estimated_vram_bytes = trace_estimate_vram(ctx);

    FrameTraceRing* ring = ctx->trace_ring;
    ring->samples[ring->head] = sample;
    ring->head = (ring->head + 1u) % FrameTraceRing::kCap;
    if (ring->count < FrameTraceRing::kCap) {
        ++ring->count;
    }

    reset_trace_frame_counters(ctx);
}

void free_trace_ring(Context* ctx) noexcept {
    if (!ctx) return;
    std::free(ctx->trace_ring);
    ctx->trace_ring = nullptr;
}

void set_trace_enabled(Context* ctx, bool enabled) noexcept {
    if (!ctx) return;
    ctx->trace_enabled = enabled;
}

bool trace_enabled(const Context* ctx) noexcept {
    return ctx && ctx->trace_enabled;
}

uint32_t trace_sample_count(const Context* ctx) noexcept {
    return (ctx && ctx->trace_ring) ? ctx->trace_ring->count : 0u;
}

bool trace_get_sample(const Context* ctx, uint32_t index, FrameTraceSample* out_sample) noexcept {
    if (!ctx || !ctx->trace_ring || !out_sample || index >= ctx->trace_ring->count) {
        return false;
    }
    const FrameTraceRing* ring = ctx->trace_ring;
    const uint32_t slot = (ring->head + FrameTraceRing::kCap - ring->count + index) % FrameTraceRing::kCap;
    *out_sample = ring->samples[slot];
    return true;
}

void trace_clear(Context* ctx) noexcept {
    if (!ctx || !ctx->trace_ring) return;
    ctx->trace_ring->head = 0u;
    ctx->trace_ring->count = 0u;
}

uint32_t trace_export_json(const Context* ctx, char* out_buf, uint32_t cap) noexcept {
    uint32_t total = 0u;
    const auto append = [&](const char* str, uint32_t len) noexcept {
        if (out_buf && cap > 1u && total < cap - 1u) {
            const uint32_t avail = (cap - 1u) - total;
            const uint32_t copy_n = (len < avail) ? len : avail;
            std::memcpy(out_buf + total, str, copy_n);
        }
        total += len;
    };
    const auto finite = [](double v) noexcept -> double {
        return std::isfinite(v) ? v : 0.0;
    };

    append("[", 1u);
    const uint32_t n = trace_sample_count(ctx);
    char entry[512];
    for (uint32_t i = 0u; i < n; ++i) {
        FrameTraceSample s = {};
        if (!trace_get_sample(ctx, i, &s)) continue;
        if (i > 0u) append(",", 1u);
        const int len = std::snprintf(
            entry,
            sizeof(entry),
            "{\"frame\":%llu,\"layout_us\":%.2f,\"gpu_upload_us\":%.2f,\"gpu_submit_us\":%.2f,"
            "\"ui_dc\":%u,\"total_dc\":%u,\"blur_passes\":%u,\"filter_passes\":%u,"
            "\"prims\":%u,\"working_set\":%llu,\"private_commit\":%llu,\"est_vram\":%llu}",
            static_cast<unsigned long long>(s.frame_index),
            finite(s.layout_us),
            finite(s.gpu_upload_us),
            finite(s.gpu_submit_us),
            s.ui_draw_calls,
            s.total_draw_calls,
            s.blur_passes,
            s.filter_passes,
            s.prim_count,
            static_cast<unsigned long long>(s.working_set_bytes),
            static_cast<unsigned long long>(s.private_commit_bytes),
            static_cast<unsigned long long>(s.estimated_vram_bytes));
        if (len > 0) {
            const uint32_t clamped = static_cast<uint32_t>(
                static_cast<size_t>(len) < sizeof(entry) ? static_cast<size_t>(len) : (sizeof(entry) - 1u));
            append(entry, clamped);
        }
    }
    append("]", 1u);

    if (out_buf && cap > 0u) {
        const uint32_t term = (total < cap - 1u) ? total : (cap - 1u);
        out_buf[term] = '\0';
    }
    return total;
}

bool trace_compute_percentiles(const Context* ctx, TracePercentiles* out_summary) noexcept {
    if (!out_summary) return false;
    *out_summary = {};
    if (!ctx || !ctx->trace_ring || ctx->trace_ring->count == 0u) return false;

    const FrameTraceRing* ring = ctx->trace_ring;
    const uint32_t n = ring->count <= FrameTraceRing::kCap ? ring->count : FrameTraceRing::kCap;
    double layout_vals[FrameTraceRing::kCap];
    double gpu_vals[FrameTraceRing::kCap];
    uint64_t peak_ws = 0u;
    uint64_t peak_vram = 0u;

    for (uint32_t i = 0u; i < n; ++i) {
        const uint32_t slot = (ring->head + FrameTraceRing::kCap - ring->count + i) % FrameTraceRing::kCap;
        const FrameTraceSample& s = ring->samples[slot];
        layout_vals[i] = std::isfinite(s.layout_us) ? s.layout_us : 0.0;
        gpu_vals[i] = std::isfinite(s.gpu_submit_us) ? s.gpu_submit_us : 0.0;
        if (s.working_set_bytes > peak_ws) peak_ws = s.working_set_bytes;
        if (s.estimated_vram_bytes > peak_vram) peak_vram = s.estimated_vram_bytes;
    }

    const auto sort_doubles = [](double* a, uint32_t count) noexcept {
        static constexpr uint32_t kGaps[] = {132u, 57u, 23u, 10u, 4u, 1u};
        for (uint32_t gap : kGaps) {
            for (uint32_t i = gap; i < count; ++i) {
                const double key = a[i];
                uint32_t j = i;
                while (j >= gap && a[j - gap] > key) {
                    a[j] = a[j - gap];
                    j -= gap;
                }
                a[j] = key;
            }
        }
    };
    const auto pick_pct = [](const double* sorted, uint32_t count, double pct) noexcept -> double {
        const double pos = static_cast<double>(count - 1u) * (pct * 0.01);
        const uint32_t lo = static_cast<uint32_t>(pos);
        const uint32_t hi = (lo + 1u < count) ? (lo + 1u) : lo;
        const double frac = pos - static_cast<double>(lo);
        return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
    };

    sort_doubles(layout_vals, n);
    sort_doubles(gpu_vals, n);

    out_summary->sample_count = n;
    out_summary->layout_p50_us = pick_pct(layout_vals, n, 50.0);
    out_summary->layout_p95_us = pick_pct(layout_vals, n, 95.0);
    out_summary->layout_p99_us = pick_pct(layout_vals, n, 99.0);
    out_summary->gpu_p50_us = pick_pct(gpu_vals, n, 50.0);
    out_summary->gpu_p95_us = pick_pct(gpu_vals, n, 95.0);
    out_summary->gpu_p99_us = pick_pct(gpu_vals, n, 99.0);
    out_summary->peak_working_set_bytes = peak_ws;
    out_summary->peak_vram_bytes = peak_vram;
    return true;
}

} // namespace etch
