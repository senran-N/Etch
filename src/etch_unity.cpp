// Etch Unity Build aggregation unit: compiles the entire engine in one translation unit.

#include "etch/etch.h"
#include "etch_testing.h"

#include <cmath>
#include <cstdio>

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
#endif

#include "etch_uber_vs_dxil.h"
#include "etch_uber_ps_dxil.h"
#include "etch_uber_vs_spv.h"
#include "etch_uber_ps_spv.h"
#include "etch_quad_vs_dxil.h"
#include "etch_quad_ps_dxil.h"
#include "etch_quad_vs_spv.h"
#include "etch_quad_ps_spv.h"
#include "etch_subpixel_vs_dxil.h"
#include "etch_subpixel_ps_dxil.h"
#include "etch_subpixel_vs_spv.h"
#include "etch_subpixel_ps_spv.h"
#include "etch_kawase_vs_dxil.h"
#include "etch_kawase_ps_dxil.h"
#include "etch_kawase_vs_spv.h"
#include "etch_kawase_ps_spv.h"

#include "etch_uber_vs_msl.h"
#include "etch_uber_ps_msl.h"
#include "etch_quad_vs_msl.h"
#include "etch_quad_ps_msl.h"
#include "etch_subpixel_vs_msl.h"
#include "etch_subpixel_ps_msl.h"
#include "etch_kawase_vs_msl.h"
#include "etch_kawase_ps_msl.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#include "stb_image.h"

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
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

#include "etch_layout_engine.cpp"
#include "etch_arena.cpp"
#include "etch_theme.cpp"
#include "etch_color.cpp"
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
#include "etch_c.cpp"
