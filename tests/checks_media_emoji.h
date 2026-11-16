#pragma once
#include "etch/etch.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace media_emoji_checks {
struct Bytes {
    uint8_t data[65536] = {};
    uint32_t size = 0;
};
inline void put16(uint8_t* p, uint32_t v) noexcept {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}
inline void put32(uint8_t* p, uint32_t v) noexcept {
    put16(p, v >> 16);
    put16(p + 2, v);
}
inline Bytes png(uint32_t dim, uint32_t color) noexcept {
    Bytes b{};
    auto* s = SDL_CreateSurface(int(dim), int(dim), SDL_PIXELFORMAT_RGBA32);
    if (!s)
        return b;
    auto* p = static_cast<uint8_t*>(s->pixels);
    for (uint32_t y = 0; y < dim; ++y)
        for (uint32_t x = 0; x < dim; ++x) {
            auto* q = p + size_t(y) * size_t(s->pitch) + x * 4;
            q[0] = uint8_t(color >> 16);
            q[1] = uint8_t(color >> 8);
            q[2] = uint8_t(color);
            q[3] = 255;
        }
    auto* io = SDL_IOFromMem(b.data, sizeof(b.data));
    if (io) {
        if (SDL_SavePNG_IO(s, io, false))
            b.size = uint32_t(SDL_TellIO(io));
        SDL_CloseIO(io);
    }
    SDL_DestroySurface(s);
    return b;
}
// One-component, one-pixel JPEG with zero DC and AC coefficients. SOF2 uses separate DC/AC scans.
inline Bytes jpeg(bool progressive) noexcept {
    Bytes b{};
    auto byte = [&](uint32_t v) noexcept { b.data[b.size++] = uint8_t(v); };
    auto marker = [&](uint32_t tag, uint32_t len) noexcept {
        byte(255);
        byte(tag);
        byte(len >> 8);
        byte(len);
    };
    byte(255);
    byte(216);
    marker(219, 67);
    byte(0);
    for (uint32_t i = 0; i < 64; ++i)
        byte(1);
    marker(progressive ? 194 : 192, 11);
    byte(8);
    byte(0);
    byte(1);
    byte(0);
    byte(1);
    byte(1);
    byte(1);
    byte(17);
    byte(0);
    for (uint32_t t = 0; t < 2; ++t) {
        marker(196, 20);
        byte(t * 16);
        byte(1);
        for (uint32_t i = 1; i < 16; ++i)
            byte(0);
        byte(0);
    }
    marker(218, 8);
    byte(1);
    byte(1);
    byte(0);
    byte(0);
    byte(progressive ? 0 : 63);
    byte(0);
    byte(progressive ? 127 : 63);
    if (progressive) {
        marker(218, 8);
        byte(1);
        byte(1);
        byte(0);
        byte(1);
        byte(63);
        byte(0);
        byte(127);
    }
    byte(255);
    byte(217);
    return b;
}
inline Bytes font(bool cbdt) noexcept {
    Bytes b{};
    const uint32_t n = cbdt ? 9u : 8u;
    b.size = 12 + n * 16;
    put32(b.data, 0x10000);
    put16(b.data + 4, n);
    uint32_t table_index = 0;
    auto table = [&](const char* tag, uint32_t size) noexcept -> uint8_t* {
        b.size = (b.size + 3) & ~3u;
        auto* r = b.data + 12 + table_index++ * 16;
        std::memcpy(r, tag, 4);
        put32(r + 8, b.size);
        put32(r + 12, size);
        auto* p = b.data + b.size;
        b.size += size;
        return p;
    };
    auto* p = table("head", 54);
    put32(p, 0x10000);
    put32(p + 12, 0x5F0F3CF5);
    put16(p + 18, 16);
    p = table("hhea", 36);
    put32(p, 0x10000);
    put16(p + 4, 16);
    put16(p + 34, 4);
    p = table("maxp", 6);
    put32(p, 0x5000);
    put16(p + 4, 4);
    p = table("hmtx", 16);
    for (uint32_t i = 0; i < 4; ++i)
        put16(p + i * 4, 16);
    p = table("cmap", 40);
    put16(p + 2, 1);
    put16(p + 4, 3);
    put16(p + 6, 10);
    put32(p + 8, 12);
    put16(p + 12, 12);
    put32(p + 16, 28);
    put32(p + 24, 1);
    put32(p + 28, 0x1F600);
    put32(p + 32, 0x1F602);
    put32(p + 36, 1);
    p = table("name", 6);
    put16(p + 4, 6);
    p = table("post", 32);
    put32(p, 0x30000);
    if (!cbdt) {
        const Bytes cyan = png(16, 0x00FFFF), magenta = png(32, 0xFF00FF);
        const uint32_t len0 = 24 + 8 + cyan.size + 10, len1 = 24 + 8 + magenta.size + 10;
        p = table("sbix", 16 + len0 + len1);
        put16(p, 1);
        put16(p + 2, 1);
        put32(p + 4, 2);
        put32(p + 8, 16);
        put32(p + 12, 16 + len0);
        for (uint32_t i = 0; i < 2; ++i) {
            auto* s = p + 16 + (i ? len0 : 0);
            const Bytes& image = i ? magenta : cyan;
            put16(s, i ? 32 : 16);
            put16(s + 2, 72);
            put32(s + 4, 24);
            put32(s + 8, 24);
            put32(s + 12, 32 + image.size);
            put32(s + 16, 42 + image.size);
            put32(s + 20, 42 + image.size);
            std::memcpy(s + 28, "png ", 4);
            std::memcpy(s + 32, image.data, image.size);
            std::memcpy(s + 36 + image.size, "dupe", 4);
            put16(s + 40 + image.size, 1);
        }
    } else {
        const Bytes green = png(16, 0x00CC66);
        p = table("CBDT", 4 + 9 + green.size);
        put32(p, 0x30000);
        p[4] = 16;
        p[5] = 16;
        p[7] = 16;
        p[8] = 16;
        put32(p + 9, green.size);
        std::memcpy(p + 13, green.data, green.size);
        p = table("CBLC", 80);
        put32(p, 0x30000);
        put32(p + 4, 1);
        put32(p + 8, 56);
        put32(p + 12, 24);
        put32(p + 16, 1);
        put16(p + 48, 1);
        put16(p + 50, 1);
        p[52] = 16;
        p[53] = 16;
        p[54] = 32;
        p[55] = 1;
        put16(p + 56, 1);
        put16(p + 58, 1);
        put32(p + 60, 8);
        put16(p + 64, 1);
        put16(p + 66, 17);
        put32(p + 68, 4);
        put32(p + 72, 0);
        put32(p + 76, 9 + green.size);
    }
    return b;
}
inline bool pixel(SDL_Surface* s, int x, int y, uint32_t rgb) noexcept {
    if (!s || x < 0 || y < 0 || x >= s->w || y >= s->h)
        return false;
    uint8_t r = 0, g = 0, b = 0, a = 0;
    if (!SDL_ReadSurfacePixel(s, x, y, &r, &g, &b, &a))
        return false;

    return std::abs(int(r) - int((rgb >> 16) & 255)) <= 3 && std::abs(int(g) - int((rgb >> 8) & 255)) <= 3 &&
           std::abs(int(b) - int(rgb & 255)) <= 3;
}
} // namespace media_emoji_checks

inline bool verify_media_emoji_checks(const char* temp_prefix = "_etch_v060") noexcept {
    using namespace etch;
    using namespace media_emoji_checks;
    Context* previous = current_context();
    Context* c = create_context({.headless = true});
    if (!c)
        return false;
    bool ok = true;
    auto check = [&](bool pass, int line) noexcept {
        if (!pass) {
            ok = false;
            std::fprintf(stderr, "media/emoji check failed at %d: %s\n", line, SDL_GetError());
        }
    };
    char file[512], render[512];
    std::snprintf(file, sizeof(file), "%s.media", temp_prefix);
    std::snprintf(render, sizeof(render), "%s.png", temp_prefix);
    const uint8_t gif[] = {71, 73, 70, 56, 57, 97, 1, 0, 1, 0, 128, 0, 0, 255, 0, 0, 0, 0,
                           0,  44, 0,  0,  0,  0,  1, 0, 1, 0, 0,   2, 2, 68,  1, 0, 59};
    const uint8_t tga[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 32, 0, 0, 255};
    const Bytes baseline = jpeg(false), progressive = jpeg(true), image_png = png(8, 0xFF0000);
    Bytes bmp{};
    {
        auto* surf = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
        auto* io = SDL_IOFromMem(bmp.data, sizeof(bmp.data));
        if (surf && io && SDL_SaveBMP_IO(surf, io, false))
            bmp.size = uint32_t(SDL_TellIO(io));
        if (io)
            SDL_CloseIO(io);
        if (surf)
            SDL_DestroySurface(surf);
    }
    const void* data[] = {baseline.data, progressive.data, gif, tga, image_png.data, bmp.data};
    const size_t sizes[] = {baseline.size, progressive.size, sizeof(gif),
                            sizeof(tga),   image_png.size,   bmp.size};
    // All sprite decoding runs before the first GPU request.
    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t id = load_sprite_memory(c, data[i], sizes[i]);
        check(id != kInvalidSprite, __LINE__);
        destroy_sprite(c, id);
        check(SDL_SaveFile(file, data[i], sizes[i]), __LINE__);
        const uint32_t f = load_sprite_file(c, file);
        std::remove(file);
        check(f != kInvalidSprite, __LINE__);
        destroy_sprite(c, f);
    }
    const uint8_t bad[] = {0xFF, 0xD8, 0xFF, 0};
    check(load_sprite_memory(c, bad, sizeof(bad)) == kInvalidSprite, __LINE__);
    for (uint32_t i = 0; i < 6; ++i) {
        auto t = load_texture_memory(c, data[i], sizes[i]);
        check(bool(t), __LINE__);
        destroy_texture(c, &t);
    }
    check(!load_texture_memory(c, bad, sizeof(bad)), __LINE__);
    uint32_t sprites[32]{};
    uint8_t rgba[8 * 8 * 4];
    for (uint32_t i = 0; i < 32; ++i) {
        for (uint32_t k = 0; k < 64; ++k) {
            rgba[k * 4] = uint8_t(80 + i * 5);
            rgba[k * 4 + 1] = uint8_t(i * 7);
            rgba[k * 4 + 2] = uint8_t(255 - i * 5);
            rgba[k * 4 + 3] = 255;
        }
        sprites[i] = create_sprite_rgba(c, {.rgba_pixels = rgba, .width = 8, .height = 8});
        check(sprites[i] != kInvalidSprite, __LINE__);
    }
    check(sprite_count(c) == 32, __LINE__);
    check(sprite_size(c, sprites[0]).x == 8, __LINE__);
    check(update_sprite_rgba(c, sprites[31], 8, 8, rgba), __LINE__);
    const uint32_t old = sprites[30];
    destroy_sprite(c, old);
    sprites[30] = create_sprite_rgba(c, {.rgba_pixels = rgba, .width = 8, .height = 8});
    check(sprites[30] != old && (sprites[30] & 0xFFFFFu) == (old & 0xFFFFFu), __LINE__);
    check(!update_sprite_rgba(c, old, 8, 8, rgba), __LINE__);
    // Fill to the last shelf; first bitmap glyph forces 512 -> 1024 during lowering.
    auto* filler = static_cast<uint8_t*>(std::malloc(500 * 478 * 4));
    uint32_t fill_id = 0;
    if (filler) {
        std::memset(filler, 255, 500 * 478 * 4);
        fill_id = create_sprite_rgba(c, {.rgba_pixels = filler, .width = 500, .height = 478});
        std::free(filler);
    }
    check(fill_id != 0, __LINE__);
    const Bytes sbix = font(false), cbdt = font(true);
    const uint16_t fs = load_font_memory(c, sbix.data, sbix.size),
                   fc = load_font_memory(c, cbdt.data, cbdt.size);
    check(fs != kInvalidFont && fc != kInvalidFont, __LINE__);
    char input[8] = "\xF0\x9F\x98\x80";
    begin_frame(c, {.viewport_w = 640, .viewport_h = 240});
    begin_box({.axis = Axis::Col, .width = Sizing::fixed(640), .height = Sizing::fixed(240)});
    begin_box({.width = Sizing::fixed(640), .height = Sizing::fixed(140),
        .radius = Corners::all(1), .clip_children = true, .continuous_corners = true});
    for (uint32_t i = 0; i < 32; ++i)
        draw_image({.sprite_id = sprites[i], .rect = {float(i * 18), 0, 16, 16}});
    draw_text({.text = str_lit("\xF0\x9F\x98\x80"), .pos = {0, 40}, .font_id = fs, .size = 14});
    draw_text({.text = str_lit("\xF0\x9F\x98\x80"), .pos = {40, 40}, .font_id = fs, .size = 30});
    draw_text({.text = str_lit("\xF0\x9F\x98\x81"), .pos = {90, 40}, .font_id = fs, .size = 14});
    draw_text({.text = str_lit("\xF0\x9F\x98\x80"), .pos = {120, 40}, .font_id = fc, .size = 14});
    draw_text({.text = str_lit("\xF0\x9F\x98\x80"), .pos = {150, 40}, .size = 14});
    draw_image({.sprite_id = sprites[31], .rect = {200, 40, 16, 16}});
    draw_text({.text = str_lit("Slug text"), .pos = {250, 40}});
    end_box();
    text_input({.id = id("bitmap_input"),
                .buffer = input,
                .capacity = 8,
                .width = Sizing::fixed(120),
                .height = Sizing::fixed(40),
                .font_size_px = 14,
                .font_id = fs,
                .text_color = Color::white()});
    image({.id = id("natural_sprite"), .sprite_id = sprites[0]});
    box({.width = Sizing::fixed(30), .height = Sizing::fixed(10), .background = Color::white()});
    end_box();
    end_frame(c);
    check(element_bounds(id("natural_sprite")).w == 8 && element_bounds(id("natural_sprite")).h == 8,
          __LINE__);
    check(frame_stats(c).draw_call_count == 1, __LINE__);
    check(frame_stats(c).sprite_atlas_size == 1024, __LINE__);
    check(render_headless_png(c, 640, 240, render), __LINE__);
    auto* rendered = SDL_LoadPNG(render);
    check(rendered != nullptr, __LINE__);
    check(pixel(rendered, 8, 8, 0x5000FF), __LINE__);
    check(pixel(rendered, 208, 48, 0xEBD964), __LINE__);
    check(pixel(rendered, 5, 45, 0x00FFFF), __LINE__);
    check(pixel(rendered, 50, 50, 0xFF00FF), __LINE__);
    check(pixel(rendered, 95, 45, 0x00FFFF), __LINE__);
    check(pixel(rendered, 125, 45, 0x00CC66), __LINE__);
    check(pixel(rendered, 155, 45, 0x00FFFF), __LINE__);
    const Rect input_bounds = element_bounds(id("bitmap_input"));
    bool input_cyan = false;
    for (int y = int(input_bounds.y); rendered && y < int(input_bounds.y + input_bounds.h); ++y)
        for (int x = int(input_bounds.x); x < int(input_bounds.x + input_bounds.w); ++x)
            input_cyan = input_cyan || pixel(rendered, x, y, 0x00FFFF);
    check(input_cyan, __LINE__);
    if (rendered)
        SDL_DestroySurface(rendered);
    std::remove(render);
    std::remove(file);
    // Stage invalid caller inputs and an update after the full atlas upload.
    check(create_sprite_rgba(c, {.rgba_pixels = rgba, .width = 2047, .height = 1}) == kInvalidSprite,
          __LINE__);
    check(!update_sprite_rgba(c, sprites[0], 8, 8, rgba, 4), __LINE__);
    check(sprite_size(c, old).x == 0, __LINE__);
    check(update_sprite_rgba(c, sprites[0], 4, 4, rgba, 32), __LINE__);
    check(render_headless_png(c, 640, 240, render), __LINE__);
    rendered = SDL_LoadPNG(render);
    check(pixel(rendered, 8, 8, 0xEBD964), __LINE__);
    if (rendered)
        SDL_DestroySurface(rendered);
    std::remove(render);
    // Linear-light premultiplication and both caller color-space contracts.
    const uint8_t partial[] = {255, 0, 0, 128}, linear[] = {128, 0, 0, 255}, premul[] = {188, 0, 0, 128};
    const uint32_t alpha_ids[] = {
        create_sprite_rgba(c, {.rgba_pixels = partial, .width = 1, .height = 1}),
        create_sprite_rgba(c, {.rgba_pixels = linear, .width = 1, .height = 1, .srgb = false}),
        create_sprite_rgba(c, {.rgba_pixels = premul, .width = 1, .height = 1, .premultiplied_alpha = true}),
        create_sprite_rgba(
            c, {.rgba_pixels = linear, .width = 1, .height = 1, .srgb = false, .premultiplied_alpha = true})};
    begin_frame(c, {.viewport_w = 640, .viewport_h = 240});
    begin_box({.width = Sizing::fixed(640),
               .height = Sizing::fixed(240),
               .background = Color::rgba8(0, 0, 0, 255)});
    for (uint32_t i = 0; i < 4; ++i)
        draw_image({.sprite_id = alpha_ids[i], .rect = {float(i * 20), 0, 16, 16}});
    end_box();
    end_frame(c);
    check(frame_stats(c).draw_call_count == 1, __LINE__);
    // Grow the atlas and relocate a live sprite after end_frame before headless readout.
    auto* big = static_cast<uint8_t*>(std::malloc(600 * 600 * 4));
    uint32_t big_id = kInvalidSprite;
    if (big) {
        std::memset(big, 255, 600 * 600 * 4);
        big_id = create_sprite_rgba(c, {.rgba_pixels = big, .width = 600, .height = 600});
        std::free(big);
    }
    check(big_id != kInvalidSprite, __LINE__);
    uint8_t partial_4x4[4 * 4 * 4];
    for (uint32_t i = 0; i < 16; ++i) {
        partial_4x4[i * 4] = 255;
        partial_4x4[i * 4 + 1] = 0;
        partial_4x4[i * 4 + 2] = 0;
        partial_4x4[i * 4 + 3] = 128;
    }
    check(update_sprite_rgba(c, alpha_ids[0], 4, 4, partial_4x4), __LINE__);
    check(frame_stats(c).sprite_atlas_size == 2048, __LINE__);
    check(render_headless_png(c, 640, 240, render), __LINE__);
    rendered = SDL_LoadPNG(render);
    for (uint32_t i = 0; i < 4; ++i)
        check(pixel(rendered, int(i * 20 + 8), 8, 0xBC0000), __LINE__);
    if (rendered)
        SDL_DestroySurface(rendered);
    std::remove(render);
    // Reloading a font evicts only its own bitmap emoji sprites, even across generation wrap.
    const uint32_t before_reload = sprite_count(c);
    check(load_font_memory(c, sbix.data, sbix.size, fs) == fs, __LINE__);
    check(sprite_count(c) == before_reload - 3, __LINE__);
    for (uint32_t r = 0; r < 260; ++r) {
        check(load_font_memory(c, sbix.data, sbix.size, fs) == fs, __LINE__);
        begin_frame(c, {.viewport_w = 640, .viewport_h = 240});
        begin_box({.width = Sizing::fixed(640), .height = Sizing::fixed(240)});
        draw_text({.text = str_lit("\xF0\x9F\x98\x80"), .pos = {0, 40}, .font_id = fs, .size = 14});
        draw_text({.text = str_lit("\xF0\x9F\x98\x80"), .pos = {40, 40}, .font_id = fs, .size = 30});
        draw_text({.text = str_lit("\xF0\x9F\x98\x80"), .pos = {120, 40}, .font_id = fc, .size = 14});
        end_box();
        end_frame(c);
    }
    check(sprite_count(c) == before_reload - 1, __LINE__);
    check(frame_stats(c).sprite_atlas_size == 2048, __LINE__);
    check(render_headless_png(c, 640, 240, render), __LINE__);
    rendered = SDL_LoadPNG(render);
    check(pixel(rendered, 5, 45, 0x00FFFF), __LINE__);
    check(pixel(rendered, 50, 50, 0xFF00FF), __LINE__);
    check(pixel(rendered, 125, 45, 0x00CC66), __LINE__);
    if (rendered)
        SDL_DestroySurface(rendered);
    std::remove(render);
    destroy_context(c);
    set_current_context(previous);
    return ok;
}
