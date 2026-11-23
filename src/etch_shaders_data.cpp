// Isolated translation unit for compiled GPU shader byte arrays (platform-stripped).

#include "etch_shaders_extern.h"

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
#endif

#if defined(_WIN32)
#    include "etch_quad_vs_dxil.h"
#    include "etch_quad_ps_dxil.h"
#    include "etch_quad_vs_spv.h"
#    include "etch_quad_ps_spv.h"
#    include "etch_subpixel_vs_dxil.h"
#    include "etch_subpixel_ps_dxil.h"
#    include "etch_subpixel_vs_spv.h"
#    include "etch_subpixel_ps_spv.h"
#    include "etch_slug_vs_dxil.h"
#    include "etch_slug_ps_dxil.h"
#    include "etch_slug_vs_spv.h"
#    include "etch_slug_ps_spv.h"
#    include "etch_image_vs_dxil.h"
#    include "etch_image_ps_dxil.h"
#    include "etch_image_vs_spv.h"
#    include "etch_image_ps_spv.h"
#    include "etch_fx_vs_dxil.h"
#    include "etch_fx_ps_dxil.h"
#    include "etch_fx_vs_spv.h"
#    include "etch_fx_ps_spv.h"
#    include "etch_kawase_vs_dxil.h"
#    include "etch_kawase_ps_dxil.h"
#    include "etch_kawase_vs_spv.h"
#    include "etch_kawase_ps_spv.h"
#elif defined(__APPLE__)
#    include "etch_quad_vs_msl.h"
#    include "etch_quad_ps_msl.h"
#    include "etch_subpixel_vs_msl.h"
#    include "etch_subpixel_ps_msl.h"
#    include "etch_slug_vs_msl.h"
#    include "etch_slug_ps_msl.h"
#    include "etch_image_vs_msl.h"
#    include "etch_image_ps_msl.h"
#    include "etch_fx_vs_msl.h"
#    include "etch_fx_ps_msl.h"
#    include "etch_kawase_vs_msl.h"
#    include "etch_kawase_ps_msl.h"
#else
#    include "etch_quad_vs_spv.h"
#    include "etch_quad_ps_spv.h"
#    include "etch_subpixel_vs_spv.h"
#    include "etch_subpixel_ps_spv.h"
#    include "etch_slug_vs_spv.h"
#    include "etch_slug_ps_spv.h"
#    include "etch_image_vs_spv.h"
#    include "etch_image_ps_spv.h"
#    include "etch_fx_vs_spv.h"
#    include "etch_fx_ps_spv.h"
#    include "etch_kawase_vs_spv.h"
#    include "etch_kawase_ps_spv.h"
#endif

#if defined(_MSC_VER)
#    pragma warning(pop)
#elif defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif
