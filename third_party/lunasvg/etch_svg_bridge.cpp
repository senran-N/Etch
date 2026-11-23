#include "etch_svg_bridge.h"
#include "lunasvg.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint32_t kMaxSvgRasterDim = 4096u;

} // namespace

extern "C" bool etch_svg_register_font_file(const char* family, bool bold, bool italic, const char* path) {
    if (!path || !path[0]) return false;
    return lunasvg_add_font_face_from_file(family ? family : "", bold, italic, path);
}

extern "C" bool etch_svg_register_font_memory(const char* family, bool bold, bool italic, const void* data, size_t size) {
    if (!data || size < 12u || size > UINT32_MAX) return false;
    void* copy = std::malloc(size);
    if (!copy) return false;
    std::memcpy(copy, data, size);
    // PlutoVG takes ownership on both acceptance and rejection.
    return lunasvg_add_font_face_from_data(family ? family : "", bold, italic, copy, size, std::free, copy);
}

extern "C" bool etch_svg_rasterize_memory(
    const void* data,
    size_t size,
    uint32_t target_w,
    uint32_t target_h,
    float scale,
    uint8_t** out_rgba,
    uint32_t* out_w,
    uint32_t* out_h,
    float* out_intrinsic_w,
    float* out_intrinsic_h) {
    if (out_rgba) *out_rgba = nullptr;
    if (out_w) *out_w = 0u;
    if (out_h) *out_h = 0u;
    if (out_intrinsic_w) *out_intrinsic_w = 0.0f;
    if (out_intrinsic_h) *out_intrinsic_h = 0.0f;
    if (!data || size == 0u || !out_rgba || !out_w || !out_h) return false;

    auto doc = lunasvg::Document::loadFromData(static_cast<const char*>(data), size);
    if (!doc) return false;

    const float iw = doc->width(), ih = doc->height();
    if (!(iw > 0.f && ih > 0.f) || !std::isfinite(iw) || !std::isfinite(ih)) return false;
    if (out_intrinsic_w) *out_intrinsic_w = iw;
    if (out_intrinsic_h) *out_intrinsic_h = ih;

    const float eff_scale = (std::isfinite(scale) && scale > 0.0f) ? std::clamp(scale, 0.25f, 8.0f) : 1.0f;
    double rw = 0.0;
    double rh = 0.0;
    if (target_w > 0u && target_h > 0u) {
        rw = static_cast<float>(target_w);
        rh = static_cast<float>(target_h);
    } else if (target_w > 0u) {
        rw = static_cast<float>(target_w);
        rh = rw * ih / iw;
    } else if (target_h > 0u) {
        rh = static_cast<float>(target_h);
        rw = rh * iw / ih;
    } else {
        rw = double(iw) * eff_scale;
        rh = double(ih) * eff_scale;
    }

    const double max_side = std::max(rw, rh);
    if (max_side > kMaxSvgRasterDim) {
        const double shrink = kMaxSvgRasterDim / max_side;
        rw *= shrink;
        rh *= shrink;
    }

    const int w = std::max(1, std::min(static_cast<int>(kMaxSvgRasterDim), static_cast<int>(std::lround(rw))));
    const int h = std::max(1, std::min(static_cast<int>(kMaxSvgRasterDim), static_cast<int>(std::lround(rh))));

    auto* rgba = static_cast<uint8_t*>(std::calloc(static_cast<size_t>(w) * h, 4u));
    if (!rgba) return false;
    lunasvg::Bitmap bmp(rgba, w, h, w * 4);
    if (bmp.isNull()) {
        std::free(rgba);
        return false;
    }
    doc->render(bmp, lunasvg::Matrix(static_cast<float>(w) / iw, 0, 0, static_cast<float>(h) / ih, 0, 0));
    bmp.convertToRGBA();
    *out_rgba = rgba;
    *out_w = static_cast<uint32_t>(w);
    *out_h = static_cast<uint32_t>(h);
    return true;
}
