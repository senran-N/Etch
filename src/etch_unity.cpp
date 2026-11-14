// Etch Unity Build aggregation unit: compiles the entire engine in one translation unit.

#include "etch.h"

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
#endif

#include "etch_uber_vs_dxil.h"
#include "etch_uber_ps_dxil.h"
#include "etch_uber_vs_spv.h"
#include "etch_uber_ps_spv.h"
#include "etch_kawase_vs_dxil.h"
#include "etch_kawase_ps_dxil.h"
#include "etch_kawase_vs_spv.h"
#include "etch_kawase_ps_spv.h"

#define CLAY_IMPLEMENTATION
#include "clay.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(_MSC_VER)
#    pragma warning(pop)
#elif defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif

#include "etch_arena.cpp"
#include "etch_layout.cpp"
#include "etch_gpu.cpp"
