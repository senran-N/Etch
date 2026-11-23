#ifndef ETCH_SVG_BRIDGE_H
#define ETCH_SVG_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register a TrueType/OpenType font face for SVG <text> rendering.
// Pass family = "" to register as the global fallback font.
bool etch_svg_register_font_file(const char* family, bool bold, bool italic, const char* path);
bool etch_svg_register_font_memory(const char* family, bool bold, bool italic, const void* data, size_t size);

// Rasterize an SVG document from memory into tightly packed plain RGBA8 pixels
// allocated via malloc() (caller must free(*out_rgba)).
// If target_w == 0 && target_h == 0, uses intrinsic SVG size * scale (clamped to 4096px).
// If one of target_w / target_h is 0, preserves aspect ratio.
bool etch_svg_rasterize_memory(
    const void* data,
    size_t size,
    uint32_t target_w,
    uint32_t target_h,
    float scale,
    uint8_t** out_rgba,
    uint32_t* out_w,
    uint32_t* out_h,
    float* out_intrinsic_w,
    float* out_intrinsic_h);

#ifdef __cplusplus
}
#endif

#endif // ETCH_SVG_BRIDGE_H
