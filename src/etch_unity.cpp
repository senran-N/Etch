// Etch Unity Build aggregation unit: compiles the entire engine in one translation unit.

#include "etch/etch.h"
#include "etch_testing.h"

#include <cmath>
#include <cstdio>
#include <new>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#if defined(_MSC_VER)
#    pragma warning(push, 0)
#elif defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wall"
#    pragma clang diagnostic ignored "-Wextra"
#    pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wall"
#    pragma GCC diagnostic ignored "-Wextra"
#    pragma GCC diagnostic ignored "-Wpedantic"
#    pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#    pragma GCC diagnostic ignored "-Wunused-value"
#endif

#if defined(ETCH_MODULAR_BUILD)
#    include "etch_shaders_extern.h"
#else
#    include "etch_shaders_data.cpp"
#endif

#if !defined(ETCH_MODULAR_BUILD)
#    define STB_TRUETYPE_IMPLEMENTATION
#    define STB_IMAGE_WRITE_IMPLEMENTATION
#    define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_truetype.h"
#include "stb_image_write.h"

#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#include "stb_image.h"

#define SIMPLEWEBP_DISABLE_STDIO
#include "simplewebp.h"
#include "etch_svg_bridge.h"

#if defined(_MSC_VER)
#    pragma warning(pop)
#elif defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif

#include <hb.h>
#include <hb-ot.h>
#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <dwrite.h>
#else
#    include <dlfcn.h>
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#    if defined(__APPLE__) && defined(__MACH__)
#        include <objc/message.h>
#        include <objc/runtime.h>
#    endif
#endif

#include "etch_layout_engine.cpp"
#include "etch_arena.cpp"
#include "etch_theme.cpp"
#if !defined(ETCH_MODULAR_BUILD)
#    include "etch_color.cpp"
#endif
#include "etch_motion.cpp"
#include "etch_image.cpp"
#include "etch_slug.cpp"
#include "etch_emoji.cpp"
#include "etch_render_cmd.cpp"
#include "etch_text_input.cpp"
#include "etch_window.cpp"
#include "etch_layout.cpp"
#include "etch_containers.cpp"
#include "etch_frame.cpp"
#include "etch_widgets.cpp"
#include "etch_workbench.cpp"
#include "etch_editor.cpp"
#include "etch_a11y.cpp"
#include "etch_window_chrome.cpp"
#include "etch_a11y_os.cpp"
#include "etch_gpu.cpp"
#include "etch_surface.cpp"

#include "etch_c_internal.h"

namespace etch {
namespace {
void c_bridge_window_refresh_trampoline(Context* ctx, SDL_Window* win, float dt_seconds, void* user_data) noexcept {
    (void)user_data;
    if (ctx && ctx->c_refresh_fn) {
        ctx->c_refresh_fn(reinterpret_cast<EtchContext*>(ctx), win, dt_seconds, ctx->c_refresh_user_data);
    }
}
} // namespace

bool c_bridge_can_draw_after_layout(const Context* ctx) noexcept {
    return ctx && ctx->in_frame && !ctx->layout_draw_target;
}

void c_bridge_set_window_refresh(Context* ctx, EtchWindowRefreshFn fn, void* user_data) noexcept {
    if (!ctx) return;
    if (fn) {
        set_window_refresh_callback(ctx, c_bridge_window_refresh_trampoline, ctx);
        ctx->c_refresh_fn = fn;
        ctx->c_refresh_user_data = user_data;
    } else {
        set_window_refresh_callback(ctx, nullptr, nullptr);
    }
}

uint32_t c_bridge_tokenize_cpp_hlsl(Str line, void* out, uint32_t cap, CTokenEmitFn emit) noexcept {
    return tokenize_cpp_hlsl_emit(line, out, cap, active_theme(current_context()), emit);
}

void c_bridge_terminal_grid(const TerminalGridDesc& desc, const void* cells, size_t count, CTerminalCellReadFn read) noexcept {
    if (terminal_grid_valid(desc, count)) {
        terminal_grid_run(desc, cells, count, read);
    }
}
} // namespace etch

#if !defined(ETCH_MODULAR_BUILD)
#    include "etch_c.cpp"
#endif
