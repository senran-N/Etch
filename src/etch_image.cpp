// Shared static decoding and Slot 0 sprite staging; compiled by the unity TU.
namespace etch {
namespace {
bool looks_like_svg_bytes(const uint8_t* bytes, size_t size) noexcept {
    if (!bytes || size < 5u) return false;
    size_t i = 0u;
    if (size >= 3u && bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu) i = 3u;
    auto skip_ws = [&]() noexcept {
        while (i < size && (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\r' || bytes[i] == '\n')) ++i;
    };
    auto match_ci = [&](const char* lit, size_t len) noexcept -> bool {
        if (i + len > size) return false;
        for (size_t k = 0u; k < len; ++k) {
            char c = static_cast<char>(bytes[i + k]);
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != lit[k]) return false;
        }
        return true;
    };
    for (int iter = 0; iter < 8; ++iter) {
        skip_ws();
        if (i >= size) return false;
        if (match_ci("<svg", 4u)) {
            if (i + 4u == size) return true;
            const char next = static_cast<char>(bytes[i + 4u]);
            return next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '>' || next == '/';
        }
        if (match_ci("<?xml", 5u)) {
            while (i + 1u < size && !(bytes[i] == '?' && bytes[i + 1u] == '>')) ++i;
            if (i + 1u >= size) return false;
            i += 2u;
            continue;
        }
        if (match_ci("<!--", 4u)) {
            while (i + 2u < size && !(bytes[i] == '-' && bytes[i + 1u] == '-' && bytes[i + 2u] == '>')) ++i;
            if (i + 2u >= size) return false;
            i += 3u;
            continue;
        }
        if (match_ci("<!doctype", 9u)) {
            while (i < size && bytes[i] != '>') ++i;
            if (i >= size) return false;
            ++i;
            continue;
        }
        break;
    }
    return false;
}

bool decode_webp_memory(const uint8_t* bytes, size_t size, DecodedImage* out) noexcept {
    if (!bytes || size < 12u || std::memcmp(bytes, "RIFF", 4u) != 0 || std::memcmp(bytes + 8u, "WEBP", 4u) != 0) {
        return false;
    }
    simplewebp* swebp = nullptr;
    if (simplewebp_load_from_memory(const_cast<uint8_t*>(bytes), size, nullptr, &swebp) != SIMPLEWEBP_NO_ERROR || !swebp) {
        return false;
    }
    size_t w = 0u, h = 0u;
    simplewebp_get_dimensions(swebp, &w, &h);
    if (w == 0u || h == 0u || w > 8192u || h > 8192u) {
        simplewebp_unload(swebp);
        return false;
    }
    uint8_t* rgba = static_cast<uint8_t*>(std::malloc(w * h * 4u));
    if (!rgba) {
        simplewebp_unload(swebp);
        return false;
    }
    const simplewebp_error dec_err = simplewebp_decode(swebp, rgba, nullptr);
    simplewebp_unload(swebp);
    if (dec_err != SIMPLEWEBP_NO_ERROR) {
        std::free(rgba);
        return false;
    }
    *out = {rgba, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    return true;
}

bool decode_svg_memory(const void* data, size_t size, uint32_t target_w, uint32_t target_h, float scale, DecodedImage* out) noexcept {
    *out = {};
    if (!data || size == 0u) return false;
    uint8_t* rgba = nullptr;
    uint32_t w = 0u, h = 0u;
    if (!etch_svg_rasterize_memory(data, size, target_w, target_h, scale, &rgba, &w, &h, nullptr, nullptr)) {
        return false;
    }
    *out = {rgba, w, h};
    return true;
}

bool decode_image_file(const char* path, DecodedImage* out) noexcept {
    if (!path || !path[0])
        return false;
    size_t size = 0;
    void* raw = SDL_LoadFile(path, &size);
    if (!raw)
        return false;
    const bool ok = decode_image_memory(raw, size, out);
    SDL_free(raw);
    return ok;
}
SpriteSlot* find_atlas_sprite(FontAtlas* a, uint32_t id) noexcept {
    const uint32_t i = id & 0xFFFFFu;
    if (!a || !i || i >= a->sprite_used)
        return nullptr;
    auto* s = &a->sprites[i];
    return s->live && s->generation == (id >> 20) ? s : nullptr;
}
const SpriteSlot* find_sprite(const Context* ctx, uint32_t id) noexcept {
    return ctx ? find_atlas_sprite(const_cast<FontAtlas*>(&ctx->atlas), id) : nullptr;
}
SpriteSlot* find_sprite(Context* ctx, uint32_t id) noexcept {
    return ctx ? find_atlas_sprite(&ctx->atlas, id) : nullptr;
}
void release_atlas_sprite(FontAtlas* a, uint32_t id) noexcept {
    auto* s = find_atlas_sprite(a, id);
    if (!s)
        return;
    s->live = false;
    s->generation = uint16_t(s->generation == 4095 ? 1 : s->generation + 1);
    s->next_free = a->sprite_free;
    a->sprite_free = id & 0xFFFFFu;
    --a->sprite_live;
}
bool is_atlas_slot0_prim(const Prim& p) noexcept {
    const uint32_t kind = p.style.x & 0xFu;
    return kind == uint32_t(PrimKind::Texture) &&
           ((p.style.x >> 10) & 0xFu) == 0u;
}
void remap_atlas_prims_scale(Prim* prims, uint32_t count, float sx, float sy) noexcept {
    for (uint32_t i = 0; i < count; ++i) {
        auto& p = prims[i];
        if (!is_atlas_slot0_prim(p)) continue;
        p.payload.x = pack_unorm16x2(float(p.payload.x & 65535u) / 65535.0f * sx,
                                     float(p.payload.x >> 16) / 65535.0f * sy);
        p.payload.y = pack_unorm16x2(float(p.payload.y & 65535u) / 65535.0f * sx,
                                     float(p.payload.y >> 16) / 65535.0f * sy);
    }
}
void remap_atlas_prims_compact(Prim* prims, uint32_t count, const FontAtlas& a, const SpriteSlot* plan) noexcept {
    for (uint32_t i = 0; i < count; ++i) {
        auto& p = prims[i];
        if (!is_atlas_slot0_prim(p)) continue;
        const float u0 = float(p.payload.x & 65535u) / 65535.0f * float(a.width);
        const float v0 = float(p.payload.x >> 16) / 65535.0f * float(a.height);
        const float u1 = float(p.payload.y & 65535u) / 65535.0f * float(a.width);
        const float v1 = float(p.payload.y >> 16) / 65535.0f * float(a.height);
        const float mx = (u0 + u1) * 0.5f, my = (v0 + v1) * 0.5f;
        for (uint32_t j = 1; j < a.sprite_used; ++j) {
            const auto& old = a.sprites[j];
            if (!old.live || mx < float(old.x) || mx >= float(old.x + old.alloc_w) || my < float(old.y) || my >= float(old.y + old.alloc_h)) continue;
            const float dx = float(plan[j].x) - float(old.x), dy = float(plan[j].y) - float(old.y);
            p.payload.x = pack_unorm16x2((u0 + dx) / float(a.width), (v0 + dy) / float(a.height));
            p.payload.y = pack_unorm16x2((u1 + dx) / float(a.width), (v1 + dy) / float(a.height));
            break;
        }
    }
}
bool grow_sprite_atlas(Context* ctx) noexcept {
    auto& a = ctx->atlas;
    if (a.width == 4096)
        return false;
    const uint32_t n = a.width < 512 ? 512 : a.width * 2;
    auto* pixels = static_cast<uint8_t*>(std::calloc(size_t(n) * n, 4));
    if (!pixels)
        return false;
    for (uint32_t y = 0; y < a.height; ++y)
        std::memcpy(pixels + size_t(y) * n * 4, a.pixels + size_t(y) * a.width * 4, size_t(a.width) * 4);
    for (uint32_t y = 0; y < 8; ++y)
        std::memset(pixels + size_t(y) * n * 4, 255, 32);
    const float sx = float(a.width) / float(n), sy = float(a.height) / float(n);
    remap_atlas_prims_scale(ctx->prims, ctx->prim_count, sx, sy);
    if (ctx->gpu_prims && ctx->gpu_prims != ctx->prims)
        remap_atlas_prims_scale(ctx->gpu_prims, ctx->prim_count, sx, sy);
    remap_atlas_prims_scale(ctx->filter_prims, ctx->filter_prim_count, sx, sy);
    std::free(a.pixels);
    a.pixels = pixels;
    a.width = n;
    a.height = n;
    a.dirty = true;
    a.dirty_y0 = 0;
    a.dirty_y1 = n;
    ++a.uv_epoch;
    if (!ctx->in_frame)
        ctx->stats.sprite_atlas_size = n;
    return true;
}
bool compact_sprite_atlas(Context* ctx) noexcept {
    auto& a=ctx->atlas;
    auto* plan=static_cast<SpriteSlot*>(std::malloc(size_t(a.sprite_used)*sizeof(SpriteSlot)));
    auto* pixels=static_cast<uint8_t*>(std::calloc(size_t(a.width)*a.height,4));
    if(!plan || !pixels) {std::free(plan);std::free(pixels);return false;}
    std::memcpy(plan,a.sprites,size_t(a.sprite_used)*sizeof(SpriteSlot));
    uint32_t x=0,y=8,shelf=0;
    for(uint32_t i=1;i<a.sprite_used;++i) {
        auto& s=plan[i];if(!s.live){s.alloc_w=s.alloc_h=0;continue;}
        const uint32_t w=s.width+2,h=s.height+2;
        if(x+w>a.width){x=0;y+=shelf;shelf=0;}
        if(y+h>a.height){std::free(plan);std::free(pixels);return false;}
        for(uint32_t row=0;row<h;++row) std::memcpy(pixels+(size_t(y+row)*a.width+x)*4,a.pixels+(size_t(s.y+row)*a.width+s.x)*4,size_t(w)*4);
        s.x=x;s.y=y;s.alloc_w=w;s.alloc_h=h;x+=w;shelf=shelf>h?shelf:h;
    }
    remap_atlas_prims_compact(ctx->prims, ctx->prim_count, a, plan);
    if (ctx->gpu_prims && ctx->gpu_prims != ctx->prims)
        remap_atlas_prims_compact(ctx->gpu_prims, ctx->prim_count, a, plan);
    remap_atlas_prims_compact(ctx->filter_prims, ctx->filter_prim_count, a, plan);
    for(uint32_t row=0;row<8;++row)std::memset(pixels+size_t(row)*a.width*4,255,32);
    std::memcpy(a.sprites,plan,size_t(a.sprite_used)*sizeof(SpriteSlot));std::free(plan);std::free(a.pixels);a.pixels=pixels;
    a.shelf_x=x;a.shelf_y=y;a.shelf_h=shelf;a.dirty=true;a.dirty_y0=0;a.dirty_y1=a.height;++a.uv_epoch;
    if (ctx->trace_enabled) ++ctx->trace_atlas_evictions;
    return true;
}
bool allocate_sprite_rect(Context* ctx, uint32_t w, uint32_t h, SpriteSlot* s) noexcept {
    auto& a = ctx->atlas;
    if (a.width < 512 && !grow_sprite_atlas(ctx))
        return false;
    bool compacted=false;
    for (;;) {
        uint32_t x = a.shelf_x, y = a.shelf_y, sh = a.shelf_h;
        if (x + w > a.width) {
            x = 0;
            y += sh;
            sh = 0;
        }
        if (y + h <= a.height && w <= a.width) {
            s->x = x;
            s->y = y;
            s->alloc_w = w;
            s->alloc_h = h;
            a.shelf_x = x + w;
            a.shelf_y = y;
            a.shelf_h = sh > h ? sh : h;
            return true;
        }
        if (!grow_sprite_atlas(ctx)) {
            if(compacted || !compact_sprite_atlas(ctx))return false;
            compacted=true;
        }
    }
}
uint8_t encode_sprite_channel(float v) noexcept {
    const float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return uint8_t(s * 255.0f + 0.5f);
}
struct SpriteColorLut {
    float values[256];
};
const SpriteColorLut sprite_lut = []() noexcept {
    SpriteColorLut lut{};
    for (uint32_t i = 0; i < 256; ++i) {
        const float v = float(i) / 255.0f;
        lut.values[i] = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    }
    return lut;
}();
void write_sprite(FontAtlas* a, const SpriteSlot& s, const void* data, uint32_t pitch) noexcept {
    const auto* src = static_cast<const uint8_t*>(data);
    for (uint32_t y = 0; y < s.height; ++y)
        for (uint32_t x = 0; x < s.width; ++x) {
            const auto* p = src + size_t(y) * pitch + x * 4;
            auto* d = a->pixels + (size_t(s.y + 1 + y) * a->width + s.x + 1 + x) * 4;
            d[3] = p[3];
            for (uint32_t c = 0; c < 3; ++c) {
                if (s.premultiplied && s.srgb)
                    d[c] = p[c];
                else if (!s.premultiplied && !p[3])
                    d[c] = 0;
                else if (!s.premultiplied && p[3] == 255 && s.srgb)
                    d[c] = p[c];
                else {
                    float v = s.srgb ? sprite_lut.values[p[c]] : float(p[c]) / 255.0f;
                    if (!s.premultiplied)
                        v *= float(p[3]) / 255.0f;
                    d[c] = encode_sprite_channel(v);
                }
            }
        }
    const size_t stride = size_t(a->width) * 4;
    for (uint32_t y = 1; y <= s.height; ++y) {
        auto* row = a->pixels + (size_t(s.y + y) * a->width + s.x) * 4;
        std::memcpy(row, row + 4, 4);
        std::memcpy(row + (s.width + 1) * 4, row + s.width * 4, 4);
    }
    auto* top = a->pixels + (size_t(s.y) * a->width + s.x) * 4;
    std::memcpy(top, top + stride, size_t(s.width + 2) * 4);
    std::memcpy(top + stride * (s.height + 1), top + stride * s.height, size_t(s.width + 2) * 4);
    a->dirty = true;
    a->dirty_y0 = a->dirty_y0 < s.y ? a->dirty_y0 : s.y;
    const uint32_t end = s.y + s.height + 2;
    a->dirty_y1 = a->dirty_y1 > end ? a->dirty_y1 : end;
}
bool resolve_sprite_uv_rect(const Context* ctx, uint32_t id, Vec2 lo, Vec2 hi, Vec2* uv0,
                            Vec2* uv1) noexcept {
    const auto* s = find_sprite(ctx, id);
    if (!s)
        return false;
    *uv0 = {(float(s->x + 1) + lo.x * float(s->width)) / float(ctx->atlas.width),
            (float(s->y + 1) + lo.y * float(s->height)) / float(ctx->atlas.height)};
    *uv1 = {(float(s->x + 1) + hi.x * float(s->width)) / float(ctx->atlas.width),
            (float(s->y + 1) + hi.y * float(s->height)) / float(ctx->atlas.height)};
    return true;
}
} // namespace

void free_decoded_image(DecodedImage* image) noexcept {
    if (!image) return;
    std::free(image->rgba);
    *image = {};
}

bool decode_image_memory(const void* data, size_t size_bytes, DecodedImage* out_image) noexcept {
    if (!out_image) return false;
    *out_image = {};
    if (!data || size_bytes == 0u || size_bytes > 0x7FFFFFFFu) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    if (decode_webp_memory(bytes, size_bytes, out_image)) {
        return true;
    }
    if (looks_like_svg_bytes(bytes, size_bytes)) {
        return decode_svg_memory(data, size_bytes, 0u, 0u, 2.0f, out_image);
    }
    auto* io = SDL_IOFromConstMem(data, size_bytes);
    auto* surf = io ? SDL_LoadPNG_IO(io, false) : nullptr;
    if (!surf && io && size_bytes >= 2u && bytes[0] == 'B' && bytes[1] == 'M') {
        SDL_SeekIO(io, 0, SDL_IO_SEEK_SET);
        surf = SDL_LoadBMP_IO(io, false);
    }
    if (io) {
        SDL_CloseIO(io);
    }
    if (surf) {
        if (surf->w <= 0 || surf->h <= 0 || surf->w > 8192 || surf->h > 8192) {
            SDL_DestroySurface(surf);
            return false;
        }
        if (surf->format != SDL_PIXELFORMAT_RGBA32) {
            auto* converted = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(surf);
            surf = converted;
            if (!surf) return false;
        }
        const uint32_t w = static_cast<uint32_t>(surf->w);
        const uint32_t h = static_cast<uint32_t>(surf->h);
        auto* rgba = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(w) * h * 4u));
        if (rgba) {
            for (uint32_t y = 0u; y < h; ++y) {
                std::memcpy(rgba + static_cast<size_t>(y) * w * 4u,
                            static_cast<const uint8_t*>(surf->pixels) + static_cast<size_t>(y) * static_cast<size_t>(surf->pitch),
                            static_cast<size_t>(w) * 4u);
            }
        }
        SDL_DestroySurface(surf);
        if (!rgba) return false;
        *out_image = {rgba, w, h};
        return true;
    }
    int w = 0, h = 0, comp = 0;
    auto* rgba = stbi_load_from_memory(bytes, static_cast<int>(size_bytes), &w, &h, &comp, 4);
    if (!rgba) {
        return false;
    }
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        stbi_image_free(rgba);
        return false;
    }
    *out_image = {rgba, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    return true;
}

bool rasterize_svg_memory(
    const void* data,
    size_t size_bytes,
    const SvgRasterDesc& desc,
    DecodedImage* out_image) noexcept
{
    if (!out_image) return false;
    *out_image = {};
    if (!data || size_bytes == 0u || size_bytes > 0x7FFFFFFFu) {
        return false;
    }
    return decode_svg_memory(data, size_bytes, desc.width_px, desc.height_px, desc.scale, out_image);
}

uint32_t create_sprite_rgba(Context* ctx, const TextureDesc& d) noexcept {
    if (!ctx || !d.rgba_pixels || !d.width || !d.height || d.width > 2046 || d.height > 2046 ||
        (d.row_pitch_bytes && d.row_pitch_bytes < d.width * 4))
        return kInvalidSprite;
    auto& a = ctx->atlas;
    uint32_t idx = a.sprite_free;
    if (!idx) {
        if (a.sprite_used == 0x100000)
            return kInvalidSprite;
        if (a.sprite_used >= a.sprite_cap) {
            const uint32_t cap = a.sprite_cap ? a.sprite_cap * 2 : 64;
            auto* slots = static_cast<SpriteSlot*>(std::realloc(a.sprites, size_t(cap) * sizeof(SpriteSlot)));
            if (!slots)
                return kInvalidSprite;
            a.sprites = slots;
            a.sprite_cap = cap;
        }
        idx = a.sprite_used;
        a.sprites[idx] = {};
    }
    SpriteSlot next = a.sprites[idx];
    if (next.alloc_w < d.width + 2 || next.alloc_h < d.height + 2)
        if (!allocate_sprite_rect(ctx, d.width + 2, d.height + 2, &next))
            return kInvalidSprite;
    if (a.sprite_free)
        a.sprite_free = next.next_free;
    else
        ++a.sprite_used;
    next.width = d.width;
    next.height = d.height;
    next.live = true;
    next.srgb = d.srgb;
    next.premultiplied = d.premultiplied_alpha;
    a.sprites[idx] = next;
    ++a.sprite_live;
    if (!ctx->in_frame)
        ctx->stats.sprite_count = a.sprite_live;
    write_sprite(&a, next, d.rgba_pixels, d.row_pitch_bytes ? d.row_pitch_bytes : d.width * 4);
    return (uint32_t(next.generation) << 20) | idx;
}
bool update_sprite_rgba(Context* ctx, uint32_t id, uint32_t w, uint32_t h, const void* pixels,
                        uint32_t pitch) noexcept {
    auto* s = find_sprite(ctx, id);
    if (!s || !pixels || !w || !h || w > 2046 || h > 2046 || (pitch && pitch < w * 4))
        return false;
    SpriteSlot old = *s;
    SpriteSlot next = old;
    if (w + 2 > next.alloc_w || h + 2 > next.alloc_h)
        if (!allocate_sprite_rect(ctx, w + 2, h + 2, &next))
            return false;
    old=*s;
    next.width = w;
    next.height = h;
    *s = next;
    write_sprite(&ctx->atlas, next, pixels, pitch ? pitch : w * 4);
    if (ctx->prim_count && old.width && old.height &&
        (next.x != old.x || next.y != old.y || w != old.width || h != old.height)) {
        const auto& a = ctx->atlas;
        const float aw = float(a.width), ah = float(a.height);
        const float ox = float(old.x + 1), oy = float(old.y + 1);
        const float nx = float(next.x + 1), ny = float(next.y + 1);
        const float ow = float(old.width), oh = float(old.height);
        const float nw = float(w), nh = float(h);
        const auto remap = [](float u, float o, float old_dim, float n, float new_dim, float dim) noexcept {
            float t = (u * dim - o) / old_dim;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            return (n + t * new_dim) / dim;
        };
        const auto remap_list = [&](Prim* list, uint32_t count) noexcept {
            for (uint32_t i = 0; i < count; ++i) {
                auto& p = list[i];
                if ((p.style.x & 0xFu) != uint32_t(PrimKind::Texture) || ((p.style.x >> 10) & 0xFu) != 0)
                    continue;
                const float u0 = float(p.payload.x & 65535u) / 65535.0f;
                const float v0 = float(p.payload.x >> 16) / 65535.0f;
                const float u1 = float(p.payload.y & 65535u) / 65535.0f;
                const float v1 = float(p.payload.y >> 16) / 65535.0f;
                const float mx = 0.5f * (u0 + u1) * aw, my = 0.5f * (v0 + v1) * ah;
                if (mx < float(old.x) || mx >= float(old.x + old.alloc_w) ||
                    my < float(old.y) || my >= float(old.y + old.alloc_h))
                    continue;
                p.payload.x = pack_unorm16x2(remap(u0, ox, ow, nx, nw, aw),
                                             remap(v0, oy, oh, ny, nh, ah));
                p.payload.y = pack_unorm16x2(remap(u1, ox, ow, nx, nw, aw),
                                             remap(v1, oy, oh, ny, nh, ah));
            }
        };
        remap_list(ctx->prims, ctx->prim_count);
        if (ctx->gpu_prims && ctx->gpu_prims != ctx->prims)
            remap_list(ctx->gpu_prims, ctx->prim_count);
        remap_list(ctx->filter_prims, ctx->filter_prim_count);
        ++ctx->atlas.uv_epoch;
    }
    return true;
}
uint32_t load_sprite_memory(Context* ctx, const void* data, size_t size, bool srgb) noexcept {
    if (!ctx)
        return kInvalidSprite;
    DecodedImage image{};
    if (!decode_image_memory(data, size, &image))
        return kInvalidSprite;
    const uint32_t id = create_sprite_rgba(
        ctx, {.rgba_pixels = image.rgba, .width = image.width, .height = image.height, .srgb = srgb});
    free_decoded_image(&image);
    return id;
}
uint32_t load_sprite_file(Context* ctx, const char* path, bool srgb) noexcept {
    if (!ctx)
        return kInvalidSprite;
    DecodedImage image{};
    if (!decode_image_file(path, &image))
        return kInvalidSprite;
    const uint32_t id = create_sprite_rgba(
        ctx, {.rgba_pixels = image.rgba, .width = image.width, .height = image.height, .srgb = srgb});
    free_decoded_image(&image);
    return id;
}
void destroy_sprite(Context* ctx, uint32_t id) noexcept {
    if (!ctx)
        return;
    release_atlas_sprite(&ctx->atlas, id);
    if(!ctx->atlas.sprite_live && !ctx->in_frame) {
        auto& a=ctx->atlas;a.shelf_x=0;a.shelf_y=8;a.shelf_h=0;
        for(uint32_t i=1;i<a.sprite_used;++i)a.sprites[i].alloc_w=a.sprites[i].alloc_h=0;
    }
    if (!ctx->in_frame)
        ctx->stats.sprite_count = ctx->atlas.sprite_live;
}
Vec2 sprite_size(const Context* ctx, uint32_t id) noexcept {
    const auto* s = find_sprite(ctx, id);
    return s ? Vec2{float(s->width), float(s->height)} : Vec2{};
}
uint32_t sprite_count(const Context* ctx) noexcept { return ctx ? ctx->atlas.sprite_live : 0; }
} // namespace etch
