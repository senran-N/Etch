// HarfBuzz OpenType text shaping & Slug quadratic Bezier outline/band extraction.
// Compiled via src/etch_unity.cpp.

namespace etch {

namespace {

inline float unpack_f16_cpu(uint16_t h) noexcept {
    const uint32_t sign_bit = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    const uint32_t exp_bits = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    const uint32_t mant_bits = static_cast<uint32_t>(h) & 0x03FFu;
    uint32_t bits = 0u;
    if (exp_bits == 0u) {
        if (mant_bits == 0u) {
            bits = sign_bit;
        } else {
            const float val = static_cast<float>(mant_bits) * (1.0f / 16777216.0f);
            return (sign_bit != 0u) ? -val : val;
        }
    } else if (exp_bits == 31u) {
        bits = sign_bit | 0x7F800000u | (mant_bits << 13);
    } else {
        bits = sign_bit | ((exp_bits + 112u) << 23) | (mant_bits << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

template <typename T>
bool ensure_array_cap(T** ptr, uint32_t* cap, uint32_t needed) noexcept {
    if (needed <= *cap) {
        return true;
    }
    uint32_t new_cap = (*cap > 0u) ? *cap : 256u;
    while (new_cap < needed) {
        if (new_cap > (0x7FFFFFFFu / 2u)) {
            new_cap = needed;
            break;
        }
        new_cap *= 2u;
    }
    T* grown = static_cast<T*>(std::realloc(*ptr, size_t(new_cap) * sizeof(T)));
    if (!grown) {
        return false;
    }
    *ptr = grown;
    *cap = new_cap;
    return true;
}

void destroy_font_slot(FontSlot* slot) noexcept {
    if (slot->hb_font) hb_font_destroy(slot->hb_font);
    if (slot->hb_face) hb_face_destroy(slot->hb_face);
    if (slot->hb_blob) hb_blob_destroy(slot->hb_blob);
    if (slot->sbix_blob) hb_blob_destroy(slot->sbix_blob);
    if (slot->cblc_blob) hb_blob_destroy(slot->cblc_blob);
    if (slot->cbdt_blob) hb_blob_destroy(slot->cbdt_blob);
    if (slot->colr_blob) hb_blob_destroy(slot->colr_blob);
    if (slot->cpal_blob) hb_blob_destroy(slot->cpal_blob);
    if (slot->font_file_data) SDL_free(slot->font_file_data);
    *slot = {};
}

void ensure_bold_loaded(FontAtlas* atlas) noexcept;
void ensure_mono_loaded(FontAtlas* atlas) noexcept;
void ensure_weighted_loaded(FontAtlas* atlas, uint16_t slot) noexcept;
void evict_font_slot_emoji_bitmaps(FontAtlas* atlas, uint16_t slot_idx) noexcept;

uint16_t resolve_font_slot_idx(FontAtlas* atlas, uint16_t font_id) noexcept {
    if (font_id == kFontBold && !atlas->slots[kFontBold].loaded && !atlas->bold_probed)
        ensure_bold_loaded(atlas);
    if (font_id == kFontMono && !atlas->slots[kFontMono].loaded && !atlas->mono_probed)
        ensure_mono_loaded(atlas);
    if (font_id == kFontMedium || font_id == kFontSemiBold || font_id == kFontItalic) ensure_weighted_loaded(atlas, font_id);
    if (font_id < kMaxFontSlots && atlas->slots[font_id].loaded) {
        return font_id;
    }
    if (atlas->slots[kFontSans].loaded) {
        return kFontSans;
    }
    for (uint16_t i = 0u; i < kMaxFontSlots; ++i) {
        if (atlas->slots[i].loaded) {
            return i;
        }
    }
    return kInvalidFont;
}

uint16_t resolve_target_font_slot(const FontAtlas* atlas, uint16_t slot_hint) noexcept {
    if (slot_hint < kMaxFontSlots) {
        return slot_hint;
    }
    // Keep the built-in Sans/Bold/Mono slots available for lazy loading.
    if (slot_hint == kInvalidFont) {
        for (uint16_t i = 3u; i < kMaxFontSlots; ++i) {
            if (i != kFontMedium && i != kFontSemiBold && i != kFontItalic && !atlas->slots[i].loaded) {
                return i;
            }
        }
    }
    return kInvalidFont;
}

void ensure_fallback_registered(FontAtlas* atlas, uint16_t slot_idx) noexcept {
    for (uint32_t i = 0u; i < atlas->fallback_count; ++i) {
        if (atlas->fallback_order[i] == slot_idx) {
            return;
        }
    }
    if (atlas->fallback_count < kMaxFontSlots) {
        atlas->fallback_order[atlas->fallback_count++] = slot_idx;
    }
}

uint16_t ot_u16(const uint8_t* p) noexcept { return static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1]); }
uint32_t ot_u32(const uint8_t* p) noexcept { return (uint32_t(ot_u16(p)) << 16) | ot_u16(p+2); }
void init_colr_tables(FontSlot* s) noexcept {
    s->colr_blob = hb_face_reference_table(s->hb_face, HB_TAG('C','O','L','R'));
    s->cpal_blob = hb_face_reference_table(s->hb_face, HB_TAG('C','P','A','L'));
    s->colr_data = reinterpret_cast<const uint8_t*>(hb_blob_get_data(s->colr_blob, &s->colr_len));
    s->cpal_data = reinterpret_cast<const uint8_t*>(hb_blob_get_data(s->cpal_blob, &s->cpal_len));
    if (s->colr_len < 14 || s->cpal_len < 14) return;
    const auto* c = s->colr_data; const auto* p = s->cpal_data;
    if (ot_u16(c) > 1 || ot_u16(p) > 1 || !ot_u16(p+4)) return;
    s->colr_num_base = ot_u16(c+2); s->colr_base_off = ot_u32(c+4);
    s->colr_layer_off = ot_u32(c+8); s->colr_num_layers = ot_u16(c+12);
    s->cpal_num_palette_entries = ot_u16(p+2);
    const uint32_t records = ot_u32(p+8), first = ot_u16(p+12);
    if (s->colr_base_off > s->colr_len || uint32_t(s->colr_num_base)*6u > s->colr_len-s->colr_base_off ||
        s->colr_layer_off > s->colr_len || uint32_t(s->colr_num_layers)*4u > s->colr_len-s->colr_layer_off ||
        records > s->cpal_len || (first+uint32_t(s->cpal_num_palette_entries))*4u > s->cpal_len-records) return;
    s->cpal_color_records_off = records + first*4u; s->has_colr = true;
}
void init_color_tables(FontSlot* s) noexcept {
    init_colr_tables(s);
    s->sbix_blob=hb_face_reference_table(s->hb_face,HB_TAG('s','b','i','x'));
    s->cblc_blob=hb_face_reference_table(s->hb_face,HB_TAG('C','B','L','C'));
    s->cbdt_blob=hb_face_reference_table(s->hb_face,HB_TAG('C','B','D','T'));
    s->sbix_data=reinterpret_cast<const uint8_t*>(hb_blob_get_data(s->sbix_blob,&s->sbix_len));
    s->cblc_data=reinterpret_cast<const uint8_t*>(hb_blob_get_data(s->cblc_blob,&s->cblc_len));
    s->cbdt_data=reinterpret_cast<const uint8_t*>(hb_blob_get_data(s->cbdt_blob,&s->cbdt_len));
    if(s->sbix_len>=8 && ot_u16(s->sbix_data)>=1) {
        const uint32_t n=ot_u32(s->sbix_data+4);
        if(n && n<=(s->sbix_len-8)/4)for(uint32_t i=0;i<n;++i) {
            const uint32_t off=ot_u32(s->sbix_data+8+i*4);
            if(uint64_t(off)+4+(uint64_t(s->num_glyphs)+1)*4<=s->sbix_len){s->has_sbix=true;break;}
        }
    }
    if(s->cblc_len>=8 && s->cbdt_len>=4 && (ot_u16(s->cblc_data)==2 || ot_u16(s->cblc_data)==3)) {
        const uint32_t n=ot_u32(s->cblc_data+4);s->has_cbdt=n && n<=(s->cblc_len-8)/48;
    }
    s->has_color_emoji=s->has_colr || s->has_sbix || s->has_cbdt;
}
struct SlugColrLayers { uint32_t first = 0, count = 0; };
SlugColrLayers slug_get_colr_layers(const FontSlot& s, uint32_t glyph) noexcept {
    if (!s.has_colr) return {};
    uint32_t lo=0, hi=s.colr_num_base;
    while(lo<hi) {const uint32_t m=(lo+hi)/2;const auto* p=s.colr_data+s.colr_base_off+m*6u;if(ot_u16(p)<glyph)lo=m+1;else hi=m;}
    if(lo==s.colr_num_base)return {};
    const auto* p=s.colr_data+s.colr_base_off+lo*6u;
    if(ot_u16(p)!=glyph)return {};
    const uint32_t first=ot_u16(p+2),count=ot_u16(p+4);
    return first+count<=s.colr_num_layers ? SlugColrLayers{first,count} : SlugColrLayers{};
}
bool init_font_slot_from_owned_bytes(
    FontAtlas* atlas,
    uint16_t slot_idx,
    void* raw,
    size_t sz,
    uint64_t source_hash,
    int32_t face_index) noexcept
{
    const unsigned char* bytes = static_cast<const unsigned char*>(raw);
    stbtt_fontinfo new_info = {};
    hb_blob_t* blob = hb_blob_create(
        reinterpret_cast<const char*>(raw),
        static_cast<unsigned int>(sz),
        HB_MEMORY_MODE_READONLY,
        nullptr,
        nullptr);
    hb_face_t* face = blob ? hb_face_create(blob, static_cast<unsigned int>(face_index)) : nullptr;
    hb_font_t* font = face ? hb_font_create(face) : nullptr;
    const unsigned int upem = face ? hb_face_get_upem(face) : 0u;
    if (!blob || !face || !font || upem == 0u) {
        if (font) hb_font_destroy(font);
        if (face) hb_face_destroy(face);
        if (blob) hb_blob_destroy(blob);
        SDL_free(raw);
        return false;
    }

    hb_ot_font_set_funcs(font);
    hb_font_set_scale(font, static_cast<int>(upem), static_cast<int>(upem));

    FontSlot next = {};
    next.font_file_data = static_cast<uint8_t*>(raw);
    next.font_file_size = sz;
    next.hb_blob=blob;next.hb_face=face;next.hb_font=font;
    next.upem=upem;next.num_glyphs=hb_face_get_glyph_count(face);
    init_color_tables(&next);
    const int offset=stbtt_GetFontOffsetForIndex(bytes,face_index);
    next.has_outlines=offset>=0 && stbtt_InitFont(&new_info,bytes,offset)!=0;
    if(next.has_outlines) {
        next.font_info=new_info;
        stbtt_GetFontVMetrics(&next.font_info,&next.font_ascent,&next.font_descent,&next.font_line_gap);
        next.scale_1px=stbtt_ScaleForPixelHeight(&next.font_info,1.0f);
    } else {
        if(!next.has_color_emoji){destroy_font_slot(&next);return false;}
        hb_font_extents_t ext{};
        if(hb_font_get_h_extents(font,&ext) && ext.ascender>ext.descender) {
            next.font_ascent=ext.ascender;next.font_descent=ext.descender;next.font_line_gap=ext.line_gap;
        } else {next.font_ascent=int32_t(upem*4/5);next.font_descent=-int32_t(upem/5);}
        next.scale_1px=1.0f/float(next.font_ascent-next.font_descent);
    }
    if(next.scale_1px<=0)next.scale_1px=1.0f/float(upem);
    next.ascent_em=float(next.font_ascent)*next.scale_1px;
    next.descent_em=float(next.font_descent)*next.scale_1px;
    next.line_height_em=float(next.font_ascent-next.font_descent+next.font_line_gap)*next.scale_1px;
    next.source_hash = source_hash;
    next.loaded = true;

    hb_codepoint_t gid = 0u;
    if (!hb_font_get_nominal_glyph(font, 0x2022u, &gid)) {
        hb_font_get_nominal_glyph(font, static_cast<hb_codepoint_t>('*'), &gid);
    }
    next.bullet_glyph_id = gid;
    next.bullet_advance_em = static_cast<float>(hb_font_get_glyph_h_advance(font, gid)) * next.scale_1px;

    hb_codepoint_t sp_gid = 0u;
    if (hb_font_get_nominal_glyph(font, static_cast<hb_codepoint_t>(' '), &sp_gid)) {
        next.space_advance_em = static_cast<float>(hb_font_get_glyph_h_advance(font, sp_gid)) * next.scale_1px;
    } else {
        next.space_advance_em = static_cast<float>(upem) * 0.35f * next.scale_1px;
    }

    FontSlot* dst = &atlas->slots[slot_idx];
    const uint8_t prev_gen = dst->generation;
    if (prev_gen != 0u) {
        evict_font_slot_emoji_bitmaps(atlas, slot_idx);
    }
    destroy_font_slot(dst);
    if (prev_gen == 255u && atlas->slug_glyphs && atlas->slug_glyph_cap > 0u) {
        // Rare 255th slot reload: flush glyph hash table so wrapped generation 1 cannot hit stale glyphs.
        for (uint32_t i = 0u; i < atlas->slug_glyph_cap; ++i) { atlas->slug_glyphs[i] = {}; }
        atlas->slug_glyph_count = 0u;
    }
    next.generation = (prev_gen == 255u) ? 1u : static_cast<uint8_t>(prev_gen + 1u);

    uint32_t mh = 2166136261u;
    mh = fnv1a_32_step(mh, &slot_idx, sizeof(slot_idx));
    mh = fnv1a_32_step(mh, &next.generation, sizeof(next.generation));
    mh = fnv1a_32_step(mh, &upem, sizeof(upem));
    mh = fnv1a_32_step(mh, &next.font_ascent, sizeof(next.font_ascent));
    mh = fnv1a_32_step(mh, &next.font_descent, sizeof(next.font_descent));
    mh = fnv1a_32_step(mh, &next.font_line_gap, sizeof(next.font_line_gap));
    mh = fnv1a_32_step(mh, &next.space_advance_em, sizeof(next.space_advance_em));
    next.metric_hash = mh;

    *dst = next;
    ensure_fallback_registered(atlas, slot_idx);
    ++atlas->font_revision;
    atlas->font_valid = true;
    return true;
}

uint64_t hash_font_path(const char* path, int32_t face_index) noexcept {
    const uint64_t h = fnv1a_64(path, std::strlen(path));
    return h ^ (static_cast<uint64_t>(face_index) * 0x9E3779B97F4A7C15ULL);
}

bool try_load_font_file_into_slot(
    FontAtlas* atlas,
    uint16_t slot_idx,
    const char* path,
    int32_t face_index = 0, bool prefer_italic = false) noexcept
{
    size_t sz = 0;
    void* raw = SDL_LoadFile(path, &sz);
    if (!raw || sz < 16u || sz > 0x7FFFFFFFu) {
        if (raw) {
            SDL_free(raw);
        }
        return false;
    }
#if defined(__APPLE__)
    if (prefer_italic && std::memcmp(raw, "ttcf", 4) == 0) {
        hb_blob_t* blob = hb_blob_create(static_cast<const char*>(raw), static_cast<unsigned int>(sz),
                                          HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        const unsigned int faces = hb_face_count(blob);
        face_index = -1;
        for (unsigned int i = 0; i < faces; ++i) {
            hb_face_t* face = hb_face_create(blob, i);
            hb_blob_t* head = hb_face_reference_table(face, HB_TAG('h','e','a','d'));
            hb_blob_t* os2 = hb_face_reference_table(face, HB_TAG('O','S','/','2'));
            unsigned int head_len = 0, os2_len = 0;
            const auto* head_data = reinterpret_cast<const uint8_t*>(hb_blob_get_data(head, &head_len));
            const auto* os2_data = reinterpret_cast<const uint8_t*>(hb_blob_get_data(os2, &os2_len));
            // head.macStyle bit 1 and OS/2.fsSelection bits 0 and 9 identify italic/oblique.
            const bool italic = (head_len >= 46u && (ot_u16(head_data + 44u) & 2u)) ||
                                (os2_len >= 64u && (ot_u16(os2_data + 62u) & 0x201u));
            const bool regular_weight = os2_len >= 6u && ot_u16(os2_data + 4u) == 400u;
            hb_blob_destroy(os2);
            hb_blob_destroy(head);
            hb_face_destroy(face);
            if (!italic) continue;
            if (face_index < 0 || regular_weight) face_index = static_cast<int32_t>(i);
            if (regular_weight) break;
        }
        hb_blob_destroy(blob);
        if (face_index < 0) {
            SDL_free(raw);
            return false;
        }
    }
#else
    (void)prefer_italic;
#endif
    return init_font_slot_from_owned_bytes(
        atlas,
        slot_idx,
        raw,
        sz,
        hash_font_path(path, face_index),
        face_index);
}

bool font_has_source_hash(const FontAtlas* atlas, uint64_t source_hash) noexcept {
    for (uint16_t i = 0u; i < kMaxFontSlots; ++i) {
        if (atlas->slots[i].loaded && atlas->slots[i].source_hash == source_hash) {
            return true;
        }
    }
    return false;
}

bool try_load_system_font(FontAtlas* atlas, uint16_t slot_idx, const char* filename) noexcept {
    if (!filename || !filename[0]) return false;
    char buf[512];
    const auto probe = [&](const char* root, const char* directory) noexcept -> bool {
        if (!root || !root[0]) return false;
        const int n = std::snprintf(buf, sizeof(buf), "%s/%s%s", root, directory, filename);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) return false;
        if (slot_idx >= 3u && slot_idx < kFontMedium && font_has_source_hash(atlas, hash_font_path(buf, 0))) return true;
        return try_load_font_file_into_slot(atlas, slot_idx, buf, 0, slot_idx == kFontItalic);
    };
#if defined(_WIN32)
    if (probe(SDL_getenv("SystemRoot"), "Fonts/")) return true;
    if (probe(SDL_getenv("WINDIR"), "Fonts/")) return true;
    if (probe(SDL_getenv("LOCALAPPDATA"), "Microsoft/Windows/Fonts/")) return true;
    return probe("C:/Windows", "Fonts/");
#elif defined(__APPLE__)
    if (probe(SDL_getenv("HOME"), "Library/Fonts/")) return true;
    const char* directories[] = { "/Library/Fonts", "/System/Library/Fonts", "/System/Library/Fonts/Supplemental" };
#else
    if (probe(SDL_getenv("XDG_DATA_HOME"), "fonts/")) return true;
    if (probe(SDL_getenv("HOME"), ".local/share/fonts/")) return true;
    const char* directories[] = {
        "/usr/share/fonts/TTF", "/usr/share/fonts/opentype", "/usr/share/fonts/truetype",
        "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/truetype/noto",
        "/usr/share/fonts/truetype/liberation", "/usr/share/fonts/noto", "/usr/share/fonts/noto-cjk",
        "/usr/share/fonts/dejavu", "/usr/share/fonts/liberation", "/usr/local/share/fonts",
        "/run/current-system/sw/share/fonts"
    };
#endif
#if !defined(_WIN32)
    for (const char* directory : directories) {
        if (probe(directory, "")) return true;
    }
    return false;
#endif
}

void rebuild_fallback_order(FontAtlas* atlas) noexcept {
    // Prioritize Sans -> CJK/Symbols -> Bold -> Mono when resolving missing glyphs.
    uint16_t ordered[kMaxFontSlots] = {};
    uint32_t oc = 0u;
    if (atlas->slots[kFontSans].loaded) {
        ordered[oc++] = kFontSans;
    }
    for (uint16_t s = 3u; s < kMaxFontSlots; ++s) {
        if (atlas->slots[s].loaded) {
            ordered[oc++] = s;
        }
    }
    if (atlas->slots[kFontBold].loaded) {
        ordered[oc++] = kFontBold;
    }
    if (atlas->slots[kFontMono].loaded) {
        ordered[oc++] = kFontMono;
    }
    for (uint32_t i = 0u; i < oc; ++i) {
        atlas->fallback_order[i] = ordered[i];
    }
    atlas->fallback_count = oc;
}

void ensure_weighted_loaded(FontAtlas* atlas, uint16_t slot) noexcept {
    const uint8_t bit = static_cast<uint8_t>(1u << (slot - kFontMedium));
    if (atlas->slots[slot].loaded || (atlas->weighted_probed & bit)) return;
    atlas->weighted_probed |= bit;
    const char* medium[] = {
        "Inter-Medium.ttf", "Roboto-Medium.ttf", "NotoSans-Medium.ttf",
#if defined(_WIN32)
        "seguisb.ttf",
#endif
    };
    const char* semibold[] = {"seguisb.ttf", "Inter-SemiBold.ttf", "NotoSans-SemiBold.ttf"};
    const char* italic[] = {
#if defined(_WIN32)
        "segoeuii.ttf", "Inter-Italic.ttf", "ariali.ttf",
#elif defined(__APPLE__)
        "Arial Italic.ttf", "HelveticaNeue.ttc",
#else
        "DejaVuSans-Oblique.ttf", "NotoSans-Italic.ttf", "Ubuntu-Italic.ttf",
#endif
    };
    const std::span<const char* const> candidates = slot == kFontItalic
        ? std::span<const char* const>(italic)
        : (slot == kFontMedium ? std::span<const char* const>(medium) : std::span<const char* const>(semibold));
    for (const char* path : candidates)
        if (try_load_system_font(atlas, slot, path)) break;
    rebuild_fallback_order(atlas);
}

void ensure_bold_loaded(FontAtlas* atlas) noexcept {
    atlas->bold_probed = true;
    const char* bold_candidates[] = {
#if defined(_WIN32)
        "segoeuib.ttf",
        "msyhbd.ttc",
        "arialbd.ttf",
#elif defined(__APPLE__)
        "HelveticaNeue.ttc",
        "PingFang.ttc",
#else
        "DejaVuSans-Bold.ttf",
        "NotoSans-Bold.ttf",
        "NotoSansCJK-Bold.ttc",
#endif
    };
    for (const char* path : bold_candidates) {
        if (try_load_system_font(atlas, kFontBold, path)) {
            break;
        }
    }

    rebuild_fallback_order(atlas);
}

void ensure_mono_loaded(FontAtlas* atlas) noexcept {
    atlas->mono_probed = true;
    const char* mono_candidates[] = {
#if defined(_WIN32)
        "consola.ttf",
        "lucon.ttf",
#elif defined(__APPLE__)
        "Menlo.ttc",
        "Monaco.ttf",
#else
        "DejaVuSansMono.ttf",
        "LiberationMono-Regular.ttf",
#endif
    };
    for (const char* path : mono_candidates) {
        if (try_load_system_font(atlas, kFontMono, path)) {
            break;
        }
    }

    rebuild_fallback_order(atlas);
}

void ensure_system_fallbacks_loaded(FontAtlas* atlas) noexcept {
    atlas->fallbacks_probed = true;
    const char* system_fallbacks[] = {
#if defined(_WIN32)
        "msyh.ttc",
        "seguiemj.ttf",
        "seguisym.ttf",
#elif defined(__APPLE__)
        "PingFang.ttc",
        "Apple Color Emoji.ttc",
        "Apple Symbols.ttf",
#else
        "NotoSansCJK-Regular.ttc",
        "NotoColorEmoji.ttf",
        "NotoSansSymbols-Regular.ttf",
#endif
    };
    for (const char* path : system_fallbacks) {
        const uint16_t free_slot = resolve_target_font_slot(atlas, kInvalidFont);
        if (free_slot == kInvalidFont) {
            break;
        }
        try_load_system_font(atlas, free_slot, path);
    }

    rebuild_fallback_order(atlas);
}

bool font_atlas_init(FontAtlas* atlas, const char* preferred_font_path) noexcept {
    if (!atlas) {
        return false;
    }
    *atlas = {};
    atlas->slug_glyph_cap = kSlugGlyphTableCapacity;
    atlas->slug_glyphs = static_cast<SlugGlyphEntry*>(std::calloc(atlas->slug_glyph_cap, sizeof(SlugGlyphEntry)));
    atlas->slug_curves = static_cast<SlugCurveGpu*>(std::malloc(size_t(kInitialSlugCurveCap) * sizeof(SlugCurveGpu)));
    atlas->slug_curve_cap = atlas->slug_curves ? kInitialSlugCurveCap : 0u;
    atlas->slug_bands = static_cast<SlugBandEntryGpu*>(std::malloc(size_t(kInitialSlugBandCap) * sizeof(SlugBandEntryGpu)));
    atlas->slug_band_cap = atlas->slug_bands ? kInitialSlugBandCap : 0u;
    atlas->pixels = static_cast<uint8_t*>(std::malloc(size_t(kAtlasSize) * kAtlasSize * 4u));
    atlas->hb_buf = hb_buffer_create();

    if (!atlas->slug_glyphs || !atlas->slug_curves || !atlas->slug_bands || !atlas->pixels || !atlas->hb_buf) {
        return false;
    }

    // Solid white 16x16 fallback texture on sampler slot 0.
    std::memset(atlas->pixels, 255, size_t(kAtlasSize) * kAtlasSize * 4u);
    atlas->dirty = true;

    if (!preferred_font_path || !preferred_font_path[0] ||
        !try_load_font_file_into_slot(atlas, kFontSans, preferred_font_path)) {
        const char* sans_candidates[] = {
#if defined(_WIN32)
            "segoeui.ttf", "msyh.ttc", "arial.ttf",
#elif defined(__APPLE__)
            "Helvetica.ttc", "PingFang.ttc",
#else
            "DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf",
#endif
        };
        for (const char* filename : sans_candidates) {
            if (try_load_system_font(atlas, kFontSans, filename)) break;
        }
    }
    rebuild_fallback_order(atlas);
    return true;
}

void font_atlas_destroy(FontAtlas* atlas) noexcept {
    if (!atlas) {
        return;
    }
    if (atlas->hb_buf) hb_buffer_destroy(atlas->hb_buf);
    for (uint16_t i = 0u; i < kMaxFontSlots; ++i) {
        destroy_font_slot(&atlas->slots[i]);
    }
    std::free(atlas->sprites);
    std::free(atlas->emoji_bitmaps);
    std::free(atlas->pixels);
    std::free(atlas->slug_bands);
    std::free(atlas->slug_curves);
    std::free(atlas->slug_glyphs);
    *atlas = {};
}

bool slug_glyph_rehash(FontAtlas* atlas) noexcept {
    const uint32_t new_cap = (atlas->slug_glyph_cap > 0u) ? (atlas->slug_glyph_cap * 2u) : kSlugGlyphTableCapacity;
    SlugGlyphEntry* next = static_cast<SlugGlyphEntry*>(std::calloc(new_cap, sizeof(SlugGlyphEntry)));
    if (!next) {
        return false;
    }
    const uint32_t mask = new_cap - 1u;
    for (uint32_t i = 0u; i < atlas->slug_glyph_cap; ++i) {
        const SlugGlyphEntry& e = atlas->slug_glyphs[i];
        if (e.key == 0u) {
            continue;
        }
        uint32_t idx = static_cast<uint32_t>(hash_u64(e.key)) & mask;
        while (next[idx].key != 0u) {
            idx = (idx + 1u) & mask;
        }
        next[idx] = e;
    }
    std::free(atlas->slug_glyphs);
    atlas->slug_glyphs = next;
    atlas->slug_glyph_cap = new_cap;
    return true;
}

constexpr uint32_t kStackRawCap = 512u;

struct SlugRawCurve {
    float p1x, p1y;
    float p2x, p2y;
    float p3x, p3y;
    bool implicit_close = false;
};

struct SlugCurveSortItem {
    uint32_t curve_idx;
    float    max_coord;
};

struct CompiledSlugShape {
    uint32_t band_offset = 0;
    uint16_t num_hbands = 0;
    uint16_t num_vbands = 0;
    bool     has_outline = false;
};

void sort_band_desc(SlugCurveSortItem* items, uint32_t count) noexcept {
    for (uint32_t i = 1u; i < count; ++i) {
        const SlugCurveSortItem key = items[i];
        uint32_t j = i;
        while (j > 0u && items[j - 1u].max_coord < key.max_coord) {
            items[j] = items[j - 1u];
            --j;
        }
        items[j] = key;
    }
}

CompiledSlugShape compile_slug_contours(
    FontAtlas* atlas,
    const SlugRawCurve* raw,
    uint32_t raw_count,
    float norm_min_x,
    float norm_min_y,
    float norm_span_x,
    float norm_span_y, Context* dynamic = nullptr) noexcept
{
    CompiledSlugShape out = {};
    if (raw_count == 0u || norm_span_x < 1e-3f || norm_span_y < 1e-3f) {
        return out;
    }

    const size_t scratch_mark = atlas->scratch->mark();
    SlugCurveGpu*& curves = dynamic ? dynamic->dyn_slug_curves : atlas->slug_curves;
    uint32_t& curve_count = dynamic ? dynamic->dyn_slug_curve_count : atlas->slug_curve_count;
    uint32_t& curve_cap = dynamic ? dynamic->dyn_slug_curve_cap : atlas->slug_curve_cap;
    SlugBandEntryGpu*& bands = dynamic ? dynamic->dyn_slug_bands : atlas->slug_bands;
    uint32_t& band_count = dynamic ? dynamic->dyn_slug_band_count : atlas->slug_band_count;
    uint32_t& band_cap = dynamic ? dynamic->dyn_slug_band_cap : atlas->slug_band_cap;
    const float inv_w = 1.0f / norm_span_x;
    const float inv_h = 1.0f / norm_span_y;

    struct DecodedCurve {
        float p1x, p1y, p2x, p2y, p3x, p3y;
        float min_x, min_y, max_x, max_y;
        uint32_t p1_xy, p2_xy, p3_xy;
    };
    DecodedCurve* dec = atlas->scratch->alloc_uninit<DecodedCurve>(raw_count);
    uint32_t kept = 0u;

    if (!dec) {
        return out;
    }

    for (uint32_t i = 0u; i < raw_count; ++i) {
        const SlugRawCurve& c = raw[i];
        const uint16_t h1x = pack_f16((c.p1x - norm_min_x) * inv_w);
        const uint16_t h1y = pack_f16((c.p1y - norm_min_y) * inv_h);
        const uint16_t h2x = pack_f16((c.p2x - norm_min_x) * inv_w);
        const uint16_t h2y = pack_f16((c.p2y - norm_min_y) * inv_h);
        const uint16_t h3x = pack_f16((c.p3x - norm_min_x) * inv_w);
        const uint16_t h3y = pack_f16((c.p3y - norm_min_y) * inv_h);

        // Decode quantized f16 back on CPU so band classification and sorting match GPU bit-for-bit.
        const float x1 = unpack_f16_cpu(h1x), y1 = unpack_f16_cpu(h1y);
        const float x2 = unpack_f16_cpu(h2x), y2 = unpack_f16_cpu(h2y);
        const float x3 = unpack_f16_cpu(h3x), y3 = unpack_f16_cpu(h3y);

        if (x1 == x2 && x2 == x3 && y1 == y2 && y2 == y3) {
            continue;
        }
        const float lo_x = (x1 < x2) ? ((x1 < x3) ? x1 : x3) : ((x2 < x3) ? x2 : x3);
        const float hi_x = (x1 > x2) ? ((x1 > x3) ? x1 : x3) : ((x2 > x3) ? x2 : x3);
        const float lo_y = (y1 < y2) ? ((y1 < y3) ? y1 : y3) : ((y2 < y3) ? y2 : y3);
        const float hi_y = (y1 > y2) ? ((y1 > y3) ? y1 : y3) : ((y2 > y3) ? y2 : y3);

        dec[kept++] = DecodedCurve{
            x1, y1, x2, y2, x3, y3,
            lo_x, lo_y, hi_x, hi_y,
            static_cast<uint32_t>(h1x) | (static_cast<uint32_t>(h1y) << 16),
            static_cast<uint32_t>(h2x) | (static_cast<uint32_t>(h2y) << 16),
            static_cast<uint32_t>(h3x) | (static_cast<uint32_t>(h3y) << 16),
        };
    }

    if (kept > 0u && ensure_array_cap(&curves, &curve_cap, curve_count + kept)) {
        const uint32_t base_curve_idx = curve_count;
        for (uint32_t i = 0u; i < kept; ++i) {
            curves[curve_count++] = SlugCurveGpu{
                .p1_xy = dec[i].p1_xy,
                .p2_xy = dec[i].p2_xy,
                .p3_xy = dec[i].p3_xy,
                .pad0 = 0u,
            };
        }

        const uint32_t desired_bands = (kept + 3u) / 4u;
        const uint32_t nb = desired_bands > kMaxSlugBands ? kMaxSlugBands : desired_bands;
        const uint32_t num_hb = nb;
        const uint32_t num_vb = nb;

        SlugCurveSortItem* sort_buf = atlas->scratch->alloc_uninit<SlugCurveSortItem>(kept);

        // Conservative worst-case band storage: headers + (nb + nb) * kept
        const uint32_t worst_bands = num_hb + num_vb + (num_hb + num_vb) * kept;
        if (sort_buf && ensure_array_cap(&bands, &band_cap, band_count + worst_bands)) {
            const uint32_t band_offset = band_count;
            band_count += num_hb + num_vb;
            constexpr float kBandEps = 1e-4f;

            for (uint32_t b = 0u; b < num_hb; ++b) {
                const float y0 = (b == 0u) ? -1.0f : (static_cast<float>(b) / static_cast<float>(num_hb) - kBandEps);
                const float y1 = (b + 1u == num_hb) ? 2.0f : (static_cast<float>(b + 1u) / static_cast<float>(num_hb) + kBandEps);
                uint32_t cnt = 0u;
                for (uint32_t k = 0u; k < kept; ++k) {
                    const DecodedCurve& dc = dec[k];
                    if (dc.p1y == dc.p2y && dc.p2y == dc.p3y) {
                        continue;
                    }
                    if (dc.max_y >= y0 && dc.min_y <= y1) {
                        sort_buf[cnt++] = SlugCurveSortItem{ base_curve_idx + k, dc.max_x };
                    }
                }
                sort_band_desc(sort_buf, cnt);
                const uint32_t rel_off = band_count - band_offset;
                bands[band_offset + b] = SlugBandEntryGpu{ cnt, rel_off };
                for (uint32_t m = 0u; m < cnt; ++m) {
                    bands[band_count++] = SlugBandEntryGpu{ sort_buf[m].curve_idx, 0u };
                }
            }

            for (uint32_t b = 0u; b < num_vb; ++b) {
                const float x0 = (b == 0u) ? -1.0f : (static_cast<float>(b) / static_cast<float>(num_vb) - kBandEps);
                const float x1 = (b + 1u == num_vb) ? 2.0f : (static_cast<float>(b + 1u) / static_cast<float>(num_vb) + kBandEps);
                uint32_t cnt = 0u;
                for (uint32_t k = 0u; k < kept; ++k) {
                    const DecodedCurve& dc = dec[k];
                    if (dc.p1x == dc.p2x && dc.p2x == dc.p3x) {
                        continue;
                    }
                    if (dc.max_x >= x0 && dc.min_x <= x1) {
                        sort_buf[cnt++] = SlugCurveSortItem{ base_curve_idx + k, dc.max_y };
                    }
                }
                sort_band_desc(sort_buf, cnt);
                const uint32_t rel_off = band_count - band_offset;
                bands[band_offset + num_hb + b] = SlugBandEntryGpu{ cnt, rel_off };
                for (uint32_t m = 0u; m < cnt; ++m) {
                    bands[band_count++] = SlugBandEntryGpu{ sort_buf[m].curve_idx, 0u };
                }
            }

            out.band_offset = band_offset;
            out.num_hbands = static_cast<uint16_t>(num_hb);
            out.num_vbands = static_cast<uint16_t>(num_vb);
            out.has_outline = true;
        }

    }
    atlas->scratch->rewind(scratch_mark);
    return out;
}

SlugGlyphEntry slug_get_glyph(FontAtlas* atlas, uint16_t slot_idx, uint32_t glyph_id) noexcept {
    if (!atlas || !atlas->font_valid || !atlas->slug_glyphs || atlas->slug_glyph_cap == 0u ||
        slot_idx >= kMaxFontSlots || !atlas->slots[slot_idx].loaded) {
        return {};
    }

    FontSlot& slot = atlas->slots[slot_idx];
    if (!slot.has_outlines) return {};
    const uint64_t key =
        (static_cast<uint64_t>(slot_idx + 1u) << 40) |
        (static_cast<uint64_t>(slot.generation) << 32) |
        static_cast<uint64_t>(glyph_id);
    const uint32_t mask = atlas->slug_glyph_cap - 1u;
    uint32_t idx = static_cast<uint32_t>(hash_u64(key)) & mask;
    while (atlas->slug_glyphs[idx].key != 0u) {
        if (atlas->slug_glyphs[idx].key == key) {
            return atlas->slug_glyphs[idx];
        }
        idx = (idx + 1u) & mask;
    }

    SlugGlyphEntry entry = { .key = key };
    stbtt_vertex* verts = nullptr;
    const int nverts = stbtt_GetGlyphShape(&slot.font_info, static_cast<int>(glyph_id), &verts);
    if (nverts <= 0 || !verts) {
        if (verts) stbtt_FreeShape(&slot.font_info, verts);
        if ((atlas->slug_glyph_count + 1u) * 4u >= atlas->slug_glyph_cap * 3u && slug_glyph_rehash(atlas)) {
            const uint32_t nmask = atlas->slug_glyph_cap - 1u;
            idx = static_cast<uint32_t>(hash_u64(key)) & nmask;
            while (atlas->slug_glyphs[idx].key != 0u) idx = (idx + 1u) & nmask;
        }
        atlas->slug_glyphs[idx] = entry;
        ++atlas->slug_glyph_count;
        return entry;
    }

    const size_t raw_mark = atlas->scratch->mark();
    SlugRawCurve stack_raw[kStackRawCap];
    SlugRawCurve* raw = stack_raw;
    uint32_t raw_cap = kStackRawCap;
    uint32_t raw_count = 0u;

    auto push_raw = [&](SlugRawCurve c) noexcept -> bool {
        if (raw_count >= raw_cap) {
            const uint32_t new_cap = raw_cap * 2u;
            SlugRawCurve* next = atlas->scratch->alloc_uninit<SlugRawCurve>(new_cap);
            if (!next) {
                return false;
            }
            std::memcpy(next, raw, size_t(raw_count) * sizeof(SlugRawCurve));
            raw = next;
            raw_cap = new_cap;
        }
        raw[raw_count++] = c;
        return true;
    };

    float cur_x = 0.0f;
    float cur_y = 0.0f;
    for (int i = 0; i < nverts; ++i) {
        const stbtt_vertex& v = verts[i];
        const float vx = static_cast<float>(v.x);
        const float vy = static_cast<float>(v.y);
        if (v.type == STBTT_vmove) {
            cur_x = vx;
            cur_y = vy;
        } else if (v.type == STBTT_vline) {
            push_raw(SlugRawCurve{
                cur_x, cur_y,
                0.5f * (cur_x + vx), 0.5f * (cur_y + vy),
                vx, vy,
            });
            cur_x = vx;
            cur_y = vy;
        } else if (v.type == STBTT_vcurve) {
            push_raw(SlugRawCurve{
                cur_x, cur_y,
                static_cast<float>(v.cx), static_cast<float>(v.cy),
                vx, vy,
            });
            cur_x = vx;
            cur_y = vy;
        } else if (v.type == STBTT_vcubic) {
            const float c1x = static_cast<float>(v.cx);
            const float c1y = static_cast<float>(v.cy);
            const float c2x = static_cast<float>(v.cx1);
            const float c2y = static_cast<float>(v.cy1);
            const float l1x = 0.5f * (cur_x + c1x), l1y = 0.5f * (cur_y + c1y);
            const float hx  = 0.5f * (c1x + c2x),   hy  = 0.5f * (c1y + c2y);
            const float l2x = 0.5f * (l1x + hx),    l2y = 0.5f * (l1y + hy);
            const float r2x = 0.5f * (c2x + vx),    r2y = 0.5f * (c2y + vy);
            const float r1x = 0.5f * (hx + r2x),    r1y = 0.5f * (hy + r2y);
            const float mx  = 0.5f * (l2x + r1x),   my  = 0.5f * (l2y + r1y);
            push_raw(SlugRawCurve{
                cur_x, cur_y,
                0.75f * (l1x + l2x) - 0.25f * (cur_x + mx),
                0.75f * (l1y + l2y) - 0.25f * (cur_y + my),
                mx, my,
            });
            push_raw(SlugRawCurve{
                mx, my,
                0.75f * (r1x + r2x) - 0.25f * (mx + vx),
                0.75f * (r1y + r2y) - 0.25f * (my + vy),
                vx, vy,
            });
            cur_x = vx;
            cur_y = vy;
        }
    }
    stbtt_FreeShape(&slot.font_info, verts);

    float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
    if (raw_count > 0u) {
        min_x = max_x = raw[0].p1x;
        min_y = max_y = raw[0].p1y;
        auto accum_pt = [&](float x, float y) noexcept {
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        };
        for (uint32_t i = 0u; i < raw_count; ++i) {
            accum_pt(raw[i].p1x, raw[i].p1y);
            accum_pt(raw[i].p2x, raw[i].p2y);
            accum_pt(raw[i].p3x, raw[i].p3y);
        }
    }

    const float span_x = max_x - min_x;
    const float span_y = max_y - min_y;
    const CompiledSlugShape compiled = compile_slug_contours(atlas, raw, raw_count, min_x, min_y, span_x, span_y);
    if (compiled.has_outline) {
        entry.min_x_em = min_x;
        entry.min_y_em = min_y;
        entry.max_x_em = max_x;
        entry.max_y_em = max_y;
        entry.band_offset = compiled.band_offset;
        entry.num_hbands = compiled.num_hbands;
        entry.num_vbands = compiled.num_vbands;
        entry.has_outline = true;
    }
    atlas->scratch->rewind(raw_mark);

    if ((atlas->slug_glyph_count + 1u) * 4u >= atlas->slug_glyph_cap * 3u && slug_glyph_rehash(atlas)) {
        const uint32_t nmask = atlas->slug_glyph_cap - 1u;
        idx = static_cast<uint32_t>(hash_u64(key)) & nmask;
        while (atlas->slug_glyphs[idx].key != 0u) {
            idx = (idx + 1u) & nmask;
        }
    }
    atlas->slug_glyphs[idx] = entry;
    ++atlas->slug_glyph_count;
    return entry;
}

bool wants_emoji(const char* utf8, uint32_t len) noexcept {
    const char* p=utf8;const char* end=utf8+len;bool emoji=false;
    while(p<end) {const uint32_t cp=next_utf8_codepoint(p,end);if(cp==0xFE0E)return false;if(cp==0xFE0F||cp==0x20E3||cp>=0x1F000||(cp>=0x2600&&cp<=0x27BF))emoji=true;}
    return emoji;
}
uint16_t pick_fallback_slot_for_cluster(
    const FontAtlas* atlas,
    uint16_t primary_slot,
    const char* utf8,
    uint32_t len) noexcept
{
    const char* p = utf8;
    const char* end = utf8 + len;
    uint32_t* cps = atlas->scratch->alloc_uninit<uint32_t>(len);
    if (!cps) return primary_slot;
    uint32_t cp_count = 0u;
    while (p < end) {
        cps[cp_count++] = next_utf8_codepoint(p, end);
    }
    if (cp_count == 0u) {
        return primary_slot;
    }
    bool emoji = false, text_presentation = false;
    for (uint32_t i=0;i<cp_count;++i) { const uint32_t cp=cps[i]; if(cp==0xFE0E)text_presentation=true; if(cp==0xFE0F||cp==0x20E3||cp>=0x1F000||(cp>=0x2600&&cp<=0x27BF))emoji=true; }
    if (emoji && !text_presentation) for (uint32_t fi=0;fi<atlas->fallback_count;++fi) {
        const uint16_t idx=atlas->fallback_order[fi]; const auto& slot=atlas->slots[idx];
        if(!slot.has_color_emoji)continue;
        bool covers=true; hb_codepoint_t glyph=0;
        for(uint32_t i=0;i<cp_count;++i) if(cps[i]!=0xFE0F&&cps[i]!=0x200D&&!hb_font_get_nominal_glyph(slot.hb_font,cps[i],&glyph)){covers=false;break;}
        if(covers)return idx;
    }
    hb_codepoint_t dummy = 0u;
    for (uint32_t fi = 0u; fi < atlas->fallback_count; ++fi) {
        const uint16_t s = atlas->fallback_order[fi];
        if (s == primary_slot || !atlas->slots[s].loaded) {
            continue;
        }
        bool covers_all = true;
        for (uint32_t ci = 0u; ci < cp_count; ++ci) {
            if (!hb_font_get_nominal_glyph(atlas->slots[s].hb_font, cps[ci], &dummy)) {
                covers_all = false;
                break;
            }
        }
        if (covers_all) {
            return s;
        }
    }
    for (uint32_t fi = 0u; fi < atlas->fallback_count; ++fi) {
        const uint16_t s = atlas->fallback_order[fi];
        if (s == primary_slot || !atlas->slots[s].loaded) {
            continue;
        }
        if (hb_font_get_nominal_glyph(atlas->slots[s].hb_font, cps[0], &dummy)) {
            return s;
        }
    }
    return primary_slot;
}

std::span<const ShapedGlyph> store_shaped_glyphs(
    Context* ctx,
    const ShapedGlyph* src,
    uint32_t count) noexcept
{
    if (count == 0u) {
        return {};
    }
    ShapedGlyph* out = ctx->frame_arena.alloc_uninit<ShapedGlyph>(count);
    if (!out) {
        return {};
    }
    std::memcpy(out, src, size_t(count) * sizeof(ShapedGlyph));
    return std::span<const ShapedGlyph>(out, count);
}

std::span<const ShapedGlyph> shape_directional_run(
    Context* ctx,
    uint16_t font_id,
    const char* utf8,
    uint32_t byte_len,
    bool* out_is_rtl, hb_direction_t direction, hb_script_t script, uint32_t item_start = 0, uint32_t item_end = 0) noexcept
{
    if (out_is_rtl) {
        *out_is_rtl = false;
    }
    if (!ctx || !ctx->atlas.font_valid || !ctx->atlas.hb_buf || !utf8 || byte_len == 0u) {
        return {};
    }
    const uint16_t primary_slot = resolve_font_slot_idx(&ctx->atlas, font_id);
    if (primary_slot == kInvalidFont) {
        return {};
    }

    if (!item_end) item_end = byte_len;
    const FontSlot& primary = ctx->atlas.slots[primary_slot];
    hb_buffer_t* buf = ctx->atlas.hb_buf;
    hb_buffer_clear_contents(buf);
    hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_buffer_add_utf8(buf, utf8, static_cast<int>(byte_len), item_start, static_cast<int>(item_end - item_start));
    hb_buffer_set_direction(buf, direction);
    hb_buffer_set_script(buf, script);

    const hb_feature_t kFeatures[4] = {
        { HB_TAG('k', 'e', 'r', 'n'), 1u, 0u, static_cast<unsigned int>(-1) },
        { HB_TAG('l', 'i', 'g', 'a'), 1u, 0u, static_cast<unsigned int>(-1) },
        { HB_TAG('c', 'a', 'l', 't'), 1u, 0u, static_cast<unsigned int>(-1) },
        { HB_TAG('t', 'n', 'u', 'm'), 1u, 0u, static_cast<unsigned int>(-1) },
    };
    hb_shape(primary.hb_font, buf, kFeatures, primary.tabular_numbers ? 4u : 3u);

    hb_segment_properties_t seg_props = {};
    hb_buffer_get_segment_properties(buf, &seg_props);
    const bool is_rtl = HB_DIRECTION_IS_BACKWARD(seg_props.direction);
    if (out_is_rtl) {
        *out_is_rtl = is_rtl;
    }

    unsigned int count = 0u;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, &count);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &count);
    if (count == 0u || !infos || !pos) {
        return {};
    }

    bool has_missing = false;
    for (unsigned int i = 0u; i < count; ++i) {
        if (infos[i].codepoint == 0u) {
            has_missing = true;
            break;
        }
    }

    has_missing = has_missing || (!primary.has_color_emoji && wants_emoji(utf8 + item_start, item_end - item_start));
    if (has_missing && !ctx->atlas.fallbacks_probed) ensure_system_fallbacks_loaded(&ctx->atlas);
    if (!has_missing || ctx->atlas.fallback_count <= 1u) {
        ShapedGlyph* out = ctx->frame_arena.alloc_uninit<ShapedGlyph>(count);
        if (!out) {
            return {};
        }
        for (unsigned int i = 0u; i < count; ++i) {
            out[i] = ShapedGlyph{
                .glyph_id = infos[i].codepoint,
                .cluster = infos[i].cluster,
                .font_slot = primary_slot,
                .is_rtl = static_cast<uint8_t>(is_rtl),
                .cluster_end = item_end,
                .x_advance_1px = static_cast<float>(pos[i].x_advance) * primary.scale_1px,
                .x_offset_em = static_cast<float>(pos[i].x_offset),
                .y_offset_em = static_cast<float>(pos[i].y_offset),
            };
        }
        for (uint32_t i = 0; i < count;) {
            uint32_t j = i + 1u; while (j < count && out[j].cluster == out[i].cluster) ++j;
            const uint32_t end = is_rtl ? (i ? out[i-1].cluster : item_end) : (j < count ? out[j].cluster : item_end);
            for (uint32_t k = i; k < j; ++k) out[k].cluster_end = end;
            i = j;
        }
        return std::span<const ShapedGlyph>(out, count);
    }

    // Extract cluster -> fallback-slot sub-runs before hb_shape overwrites ctx->atlas.hb_buf.
    struct FontSubRun {
        uint32_t byte_start;
        uint32_t byte_end;
        uint16_t slot_idx;
    };
    const size_t scratch_mark = ctx->frame_arena.mark();
    constexpr uint32_t kStackSubCap = 64u;
    FontSubRun stack_subs[kStackSubCap];
    FontSubRun* subs = (count <= kStackSubCap)
        ? stack_subs
        : ctx->frame_arena.alloc_uninit<FontSubRun>(count);
    if (!subs) {
        return {};
    }
    uint32_t sub_count = 0u;

    const auto logical_idx = [&](unsigned int step) noexcept -> unsigned int {
        return is_rtl ? (count - 1u - step) : step;
    };

    unsigned int step = 0u;
    while (step < count) {
        const uint32_t c_start = infos[logical_idx(step)].cluster;
        bool cluster_missing = false;
        unsigned int step_end = step;
        while (step_end < count && infos[logical_idx(step_end)].cluster == c_start) {
            if (infos[logical_idx(step_end)].codepoint == 0u) {
                cluster_missing = true;
            }
            ++step_end;
        }
        const uint32_t c_end = (step_end < count && infos[logical_idx(step_end)].cluster > c_start)
            ? infos[logical_idx(step_end)].cluster
            : item_end;

        const uint16_t chosen_slot = (cluster_missing || (!primary.has_color_emoji && wants_emoji(utf8 + c_start, c_end - c_start)))
            ? pick_fallback_slot_for_cluster(&ctx->atlas, primary_slot, utf8 + c_start, c_end - c_start)
            : primary_slot;

        if (sub_count > 0u && subs[sub_count - 1u].slot_idx == chosen_slot && subs[sub_count - 1u].byte_end == c_start) {
            subs[sub_count - 1u].byte_end = c_end;
        } else {
            subs[sub_count++] = FontSubRun{ c_start, c_end, chosen_slot };
        }
        step = step_end;
    }

    constexpr uint32_t kStackOutCap = 256u;
    ShapedGlyph stack_out[kStackOutCap];
    ShapedGlyph* collected = stack_out;
    uint32_t out_cap = kStackOutCap;
    uint32_t out_count = 0u;

    for (uint32_t s_i = 0u; s_i < sub_count; ++s_i) {
        const FontSubRun& sr = subs[is_rtl ? (sub_count - 1u - s_i) : s_i];
        const FontSlot& slot = ctx->atlas.slots[sr.slot_idx];
        hb_buffer_clear_contents(buf);
        hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
        hb_buffer_add_utf8(
            buf,
            utf8,
            static_cast<int>(byte_len),
            sr.byte_start,
            static_cast<int>(sr.byte_end - sr.byte_start));
        hb_buffer_set_segment_properties(buf, &seg_props);
        hb_shape(slot.hb_font, buf, kFeatures, primary.tabular_numbers ? 4u : 3u);

        unsigned int sub_gcount = 0u;
        const hb_glyph_info_t* sub_infos = hb_buffer_get_glyph_infos(buf, &sub_gcount);
        const hb_glyph_position_t* sub_pos = hb_buffer_get_glyph_positions(buf, &sub_gcount);
        if (sub_gcount == 0u || !sub_infos || !sub_pos) {
            continue;
        }
        if (out_count + sub_gcount > out_cap) {
            uint32_t new_cap = out_cap * 2u;
            while (new_cap < out_count + sub_gcount) {
                new_cap *= 2u;
            }
            ShapedGlyph* grown = ctx->frame_arena.alloc_uninit<ShapedGlyph>(new_cap);
            if (!grown) {
                break;
            }
            std::memcpy(grown, collected, size_t(out_count) * sizeof(ShapedGlyph));
            collected = grown;
            out_cap = new_cap;
        }
        const uint32_t first_out = out_count;
        for (unsigned int j = 0u; j < sub_gcount; ++j) {
            collected[out_count++] = ShapedGlyph{
                .glyph_id = sub_infos[j].codepoint,
                .cluster = sub_infos[j].cluster,
                .font_slot = sr.slot_idx,
                .is_rtl = static_cast<uint8_t>(is_rtl),
                .cluster_end = sr.byte_end,
                .x_advance_1px = static_cast<float>(sub_pos[j].x_advance) * slot.scale_1px,
                .x_offset_em = static_cast<float>(sub_pos[j].x_offset),
                .y_offset_em = static_cast<float>(sub_pos[j].y_offset),
            };
        }
        for (uint32_t i = first_out; i < out_count;) {
            uint32_t j = i + 1u; while (j < out_count && collected[j].cluster == collected[i].cluster) ++j;
            const uint32_t end = is_rtl ? (i > first_out ? collected[i-1].cluster : sr.byte_end) : (j < out_count ? collected[j].cluster : sr.byte_end);
            for (uint32_t k = i; k < j; ++k) collected[k].cluster_end = end;
            i = j;
        }
    }

    if (collected == stack_out) {
        ctx->frame_arena.rewind(scratch_mark);
        return store_shaped_glyphs(ctx, stack_out, out_count);
    }
    return std::span<const ShapedGlyph>(collected, out_count);
}

#include "etch_bidi.cpp"

const char* svg_skip_ws_comma(const char* p, const char* end) noexcept {
    while (p < end) {
        const char ch = *p;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',') {
            ++p;
        } else {
            break;
        }
    }
    return p;
}

bool svg_parse_number(const char*& p, const char* end, float* out) noexcept {
    p = svg_skip_ws_comma(p, end);
    if (p >= end) {
        return false;
    }
    const char* s = p;
    bool neg = false;
    if (*s == '-' || *s == '+') {
        neg = (*s == '-');
        ++s;
    }
    double val = 0.0;
    uint32_t digits = 0u;
    while (s < end && *s >= '0' && *s <= '9') {
        val = val * 10.0 + static_cast<double>(*s - '0');
        ++digits;
        ++s;
    }
    if (s < end && *s == '.') {
        ++s;
        double frac = 0.1;
        while (s < end && *s >= '0' && *s <= '9') {
            val += static_cast<double>(*s - '0') * frac;
            frac *= 0.1;
            ++digits;
            ++s;
        }
    }
    if (digits == 0u) {
        return false;
    }
    if (s < end && (*s == 'e' || *s == 'E')) {
        const char* exp_start = s;
        ++s;
        bool exp_neg = false;
        if (s < end && (*s == '-' || *s == '+')) {
            exp_neg = (*s == '-');
            ++s;
        }
        int exp_val = 0;
        uint32_t exp_digits = 0u;
        while (s < end && *s >= '0' && *s <= '9') {
            if (exp_val <= 38) {
                exp_val = exp_val * 10 + (*s - '0');
            }
            ++exp_digits;
            ++s;
        }
        if (exp_digits > 0u) {
            if (exp_val > 38) exp_val = 38;
            double scale = 1.0;
            while (exp_val-- > 0) {
                scale *= 10.0;
            }
            val = exp_neg ? (val / scale) : (val * scale);
        } else {
            s = exp_start;
        }
    }
    const float fout = static_cast<float>(neg ? -val : val);
    if (!std::isfinite(fout)) {
        return false;
    }
    *out = fout;
    p = s;
    return true;
}

bool svg_parse_flag(const char*& p, const char* end, bool* out) noexcept {
    p = svg_skip_ws_comma(p, end);
    if (p >= end || (*p != '0' && *p != '1')) {
        return false;
    }
    *out = (*p == '1');
    ++p;
    return true;
}

float svg_vec_angle(float ux, float uy, float vx, float vy) noexcept {
    const float dot = ux * vx + uy * vy;
    const float cross = ux * vy - uy * vx;
    return std::atan2(cross, dot);
}

bool svg_is_command_char(char ch) noexcept {
    switch (ch) {
        case 'M': case 'm':
        case 'L': case 'l':
        case 'H': case 'h':
        case 'V': case 'v':
        case 'Q': case 'q':
        case 'T': case 't':
        case 'C': case 'c':
        case 'S': case 's':
        case 'A': case 'a':
        case 'Z': case 'z':
            return true;
        default:
            return false;
    }
}

} // namespace

namespace {
bool ascii_iequals(Str a, const char* b) noexcept {
    if (!a.chars || !b) return false;
    const size_t blen = std::strlen(b);
    if (a.length != blen) return false;
    for (size_t i = 0u; i < blen; ++i) {
        char ca = a.chars[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + ('a' - 'A'));
        if (ca != cb) return false;
    }
    return true;
}

uint16_t try_load_system_filename(Context* ctx, const char* filename, uint16_t slot_hint) noexcept {
    if (!filename || !filename[0]) return kInvalidFont;
    char buf[512];
    const auto probe = [&](const char* root, const char* directory) noexcept -> uint16_t {
        if (!root || !root[0]) return kInvalidFont;
        const int n = std::snprintf(buf, sizeof(buf), "%s/%s%s", root, directory, filename);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) return kInvalidFont;
        return load_font_file(ctx, buf, slot_hint, 0);
    };
#if defined(_WIN32)
    if (const uint16_t s = probe(SDL_getenv("SystemRoot"), "Fonts/"); s != kInvalidFont) return s;
    if (const uint16_t s = probe(SDL_getenv("WINDIR"), "Fonts/"); s != kInvalidFont) return s;
    if (const uint16_t s = probe(SDL_getenv("LOCALAPPDATA"), "Microsoft/Windows/Fonts/"); s != kInvalidFont) return s;
    return probe("C:/Windows", "Fonts/");
#elif defined(__APPLE__)
    if (const uint16_t s = probe(SDL_getenv("HOME"), "Library/Fonts/"); s != kInvalidFont) return s;
    const char* directories[] = { "/Library/Fonts", "/System/Library/Fonts", "/System/Library/Fonts/Supplemental" };
#else
    if (const uint16_t s = probe(SDL_getenv("XDG_DATA_HOME"), "fonts/"); s != kInvalidFont) return s;
    if (const uint16_t s = probe(SDL_getenv("HOME"), ".local/share/fonts/"); s != kInvalidFont) return s;
    const char* directories[] = {
        "/usr/share/fonts/TTF", "/usr/share/fonts/opentype", "/usr/share/fonts/truetype",
        "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/truetype/noto",
        "/usr/share/fonts/truetype/liberation", "/usr/share/fonts/noto", "/usr/share/fonts/noto-cjk",
        "/usr/share/fonts/dejavu", "/usr/share/fonts/liberation", "/usr/local/share/fonts",
        "/run/current-system/sw/share/fonts"
    };
#endif
#if !defined(_WIN32)
    for (const char* directory : directories) {
        if (const uint16_t s = probe(directory, ""); s != kInvalidFont) return s;
    }
    return kInvalidFont;
#endif
}

uint16_t try_load_alias_font(Context* ctx, Str family, FontWeight weight, uint16_t slot_hint) noexcept {
    struct AliasRow { const char* name; const char* files[6]; };
    static constexpr AliasRow kAliases[] = {
        {"Segoe UI",      {"segoeui.ttf", "seguisb.ttf", "seguisb.ttf", "segoeuib.ttf", "segoeuii.ttf", "consola.ttf"}},
        {"SegoeUI",       {"segoeui.ttf", "seguisb.ttf", "seguisb.ttf", "segoeuib.ttf", "segoeuii.ttf", "consola.ttf"}},
        {"Microsoft YaHei", {"msyh.ttc", "msyh.ttc", "msyhbd.ttc", "msyhbd.ttc", "msyh.ttc", "msyh.ttc"}},
        {"Microsoft YaHei UI", {"msyh.ttc", "msyh.ttc", "msyhbd.ttc", "msyhbd.ttc", "msyh.ttc", "msyh.ttc"}},
        {"\xE5\xBE\xAE\xE8\xBD\xAF\xE9\x9B\x85\xE9\xBB\x91", {"msyh.ttc", "msyh.ttc", "msyhbd.ttc", "msyhbd.ttc", "msyh.ttc", "msyh.ttc"}},
        {"msyh",          {"msyh.ttc", "msyh.ttc", "msyhbd.ttc", "msyhbd.ttc", "msyh.ttc", "msyh.ttc"}},
        {"Yu Gothic",     {"YuGothR.ttc", "YuGothM.ttc", "YuGothB.ttc", "YuGothB.ttc", "YuGothR.ttc", "YuGothR.ttc"}},
        {"Yu Gothic UI",  {"YuGothR.ttc", "YuGothM.ttc", "YuGothB.ttc", "YuGothB.ttc", "YuGothR.ttc", "YuGothR.ttc"}},
        {"\xE6\xB8\xB8\xE3\x82\xB4\xE3\x82\xB7\xE3\x83\x83\xE3\x82\xAF", {"YuGothR.ttc", "YuGothM.ttc", "YuGothB.ttc", "YuGothB.ttc", "YuGothR.ttc", "YuGothR.ttc"}},
        {"Cascadia Mono", {"CascadiaMono.ttf", "CascadiaMono.ttf", "CascadiaMono.ttf", "CascadiaMono.ttf", "CascadiaMono.ttf", "CascadiaMono.ttf"}},
        {"Cascadia Code", {"CascadiaCode.ttf", "CascadiaCode.ttf", "CascadiaCode.ttf", "CascadiaCode.ttf", "CascadiaCode.ttf", "CascadiaCode.ttf"}},
        {"Consolas",      {"consola.ttf", "consola.ttf", "consolab.ttf", "consolab.ttf", "consolai.ttf", "consola.ttf"}},
        {"Arial",         {"arial.ttf", "arial.ttf", "arialbd.ttf", "arialbd.ttf", "ariali.ttf", "consola.ttf"}},
        {"PingFang",      {"PingFang.ttc", "PingFang.ttc", "PingFang.ttc", "PingFang.ttc", "PingFang.ttc", "Menlo.ttc"}},
        {"PingFang SC",   {"PingFang.ttc", "PingFang.ttc", "PingFang.ttc", "PingFang.ttc", "PingFang.ttc", "Menlo.ttc"}},
        {"Noto Sans CJK", {"NotoSansCJK-Regular.ttc", "NotoSansCJK-Medium.ttc", "NotoSansCJK-Bold.ttc", "NotoSansCJK-Bold.ttc", "NotoSansCJK-Regular.ttc", "NotoSansMonoCJK-Regular.ttc"}},
        {"Inter",         {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf", "Inter-Italic.ttf", "Inter-Regular.ttf"}},
    };
    const uint32_t widx = static_cast<uint32_t>(weight) < 6u ? static_cast<uint32_t>(weight) : 0u;
    for (const AliasRow& row : kAliases) {
        if (ascii_iequals(family, row.name)) {
            if (const uint16_t s = try_load_system_filename(ctx, row.files[widx], slot_hint); s != kInvalidFont) return s;
            if (const uint16_t s = try_load_system_filename(ctx, row.files[0], slot_hint); s != kInvalidFont) return s;
        }
    }
    return kInvalidFont;
}
} // namespace

uint16_t load_system_font(
    Context* ctx,
    Str family_name,
    FontWeight weight,
    uint16_t slot_hint) noexcept
{
    if (!ctx || resolve_target_font_slot(&ctx->atlas, slot_hint) == kInvalidFont) {
        return kInvalidFont;
    }

    if (family_name.chars && family_name.length > 0u) {
        if (const uint16_t s = try_load_alias_font(ctx, family_name, weight, slot_hint); s != kInvalidFont) {
            return s;
        }
        char reg_path[512] = {};
        if (win32_find_system_font_path(family_name, weight, reg_path, sizeof(reg_path))) {
            const bool is_abs = (reg_path[0] && reg_path[1] == ':') || reg_path[0] == '\\' || reg_path[0] == '/';
            const uint16_t s = is_abs
                ? load_font_file(ctx, reg_path, slot_hint, 0)
                : try_load_system_filename(ctx, reg_path, slot_hint);
            if (s != kInvalidFont) return s;
        }
        char stem[96] = {};
        char compact[96] = {};
        uint32_t slen = 0u, clen = 0u;
        for (uint32_t i = 0u; i < family_name.length && slen + 1u < sizeof(stem); ++i) {
            const char ch = family_name.chars[i];
            if (ch == '/' || ch == '\\' || ch == '\0') continue;
            stem[slen++] = ch;
            if (ch != ' ' && clen + 1u < sizeof(compact)) compact[clen++] = ch;
        }
        static constexpr const char* kWeightSuffixes[6] = {"", "-Medium", "-SemiBold", "-Bold", "-Italic", "-Mono"};
        static constexpr const char* kExts[3] = {".ttf", ".ttc", ".otf"};
        const uint32_t widx = static_cast<uint32_t>(weight) < 6u ? static_cast<uint32_t>(weight) : 0u;
        const char* stems[2] = {stem, compact};
        for (const char* st : stems) {
            if (!st[0]) continue;
            const char* suffixes[2] = {kWeightSuffixes[widx], ""};
            for (int si = 0; si < (widx == 0u ? 1 : 2); ++si) {
                for (const char* ext : kExts) {
                    char candidate[160] = {};
                    const int n = std::snprintf(candidate, sizeof(candidate), "%s%s%s", st, suffixes[si], ext);
                    if (n > 0 && static_cast<size_t>(n) < sizeof(candidate)) {
                        if (const uint16_t s = try_load_system_filename(ctx, candidate, slot_hint); s != kInvalidFont) return s;
                    }
                }
            }
        }
    }

    static constexpr const char* kFallbackMono[] = {
#if defined(_WIN32)
        "consola.ttf", "lucon.ttf",
#elif defined(__APPLE__)
        "Menlo.ttc", "Monaco.ttf",
#else
        "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf",
#endif
    };
    static constexpr const char* kFallbackBold[] = {
#if defined(_WIN32)
        "segoeuib.ttf", "msyhbd.ttc", "arialbd.ttf",
#elif defined(__APPLE__)
        "HelveticaNeue.ttc", "PingFang.ttc",
#else
        "DejaVuSans-Bold.ttf", "NotoSans-Bold.ttf", "NotoSansCJK-Bold.ttc",
#endif
    };
    static constexpr const char* kFallbackSemiBold[] = {
#if defined(_WIN32)
        "seguisb.ttf", "segoeuib.ttf", "msyhbd.ttc",
#elif defined(__APPLE__)
        "HelveticaNeue.ttc", "PingFang.ttc",
#else
        "NotoSans-SemiBold.ttf", "DejaVuSans-Bold.ttf",
#endif
    };
    static constexpr const char* kFallbackMedium[] = {
#if defined(_WIN32)
        "seguisb.ttf", "segoeui.ttf",
#elif defined(__APPLE__)
        "HelveticaNeue.ttc", "PingFang.ttc",
#else
        "NotoSans-Medium.ttf", "DejaVuSans.ttf",
#endif
    };
    static constexpr const char* kFallbackItalic[] = {
#if defined(_WIN32)
        "segoeuii.ttf", "ariali.ttf",
#elif defined(__APPLE__)
        "Arial Italic.ttf", "HelveticaNeue.ttc",
#else
        "DejaVuSans-Oblique.ttf", "NotoSans-Italic.ttf", "Ubuntu-Italic.ttf",
#endif
    };
    static constexpr const char* kFallbackRegular[] = {
#if defined(_WIN32)
        "segoeui.ttf", "msyh.ttc", "arial.ttf",
#elif defined(__APPLE__)
        "Helvetica.ttc", "PingFang.ttc",
#else
        "DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf", "NotoSansCJK-Regular.ttc",
#endif
    };
    std::span<const char* const> fb = kFallbackRegular;
    switch (weight) {
        case FontWeight::Mono:     fb = kFallbackMono; break;
        case FontWeight::Bold:     fb = kFallbackBold; break;
        case FontWeight::SemiBold: fb = kFallbackSemiBold; break;
        case FontWeight::Medium:   fb = kFallbackMedium; break;
        case FontWeight::Italic:   fb = kFallbackItalic; break;
        case FontWeight::Regular:
        default:                   fb = kFallbackRegular; break;
    }
    for (const char* f : fb) {
        if (const uint16_t s = try_load_system_filename(ctx, f, slot_hint); s != kInvalidFont) return s;
    }
    if (weight != FontWeight::Regular) {
        for (const char* f : kFallbackRegular) {
            if (const uint16_t s = try_load_system_filename(ctx, f, slot_hint); s != kInvalidFont) return s;
        }
    }
    return kInvalidFont;
}

uint16_t load_font_file(
    Context* ctx,
    const char* path,
    uint16_t slot_hint,
    int32_t face_index) noexcept
{
    if (!ctx || !path || path[0] == '\0' || face_index < 0) {
        return kInvalidFont;
    }
    const uint16_t target_slot = resolve_target_font_slot(&ctx->atlas, slot_hint);
    if (target_slot == kInvalidFont) {
        return kInvalidFont;
    }
    const bool loaded = try_load_font_file_into_slot(&ctx->atlas, target_slot, path, face_index);
    if (loaded) {
        ctx->headless_ui.redraw_budget = 2u;
        for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i)
            if (ctx->claimed_windows[i]) ctx->claimed_windows[i]->ui.redraw_budget = 2u;
    }
    return loaded ? target_slot : kInvalidFont;
}

uint16_t load_font_memory(
    Context* ctx,
    const void* data,
    size_t size_bytes,
    uint16_t slot_hint,
    int32_t face_index) noexcept
{
    if (!ctx || !data || size_bytes < 16u || size_bytes > 0x7FFFFFFFu || face_index < 0) {
        return kInvalidFont;
    }
    const uint16_t target_slot = resolve_target_font_slot(&ctx->atlas, slot_hint);
    if (target_slot == kInvalidFont) {
        return kInvalidFont;
    }
    void* copy = SDL_malloc(size_bytes);
    if (!copy) {
        return kInvalidFont;
    }
    std::memcpy(copy, data, size_bytes);
    const uint64_t source_hash = fnv1a_64(data, size_bytes) ^
        (static_cast<uint64_t>(face_index) * 0x9E3779B97F4A7C15ULL);
    const bool loaded = init_font_slot_from_owned_bytes(&ctx->atlas, target_slot, copy, size_bytes, source_hash, face_index);
    if (loaded) {
        ctx->headless_ui.redraw_budget = 2u;
        for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i)
            if (ctx->claimed_windows[i]) ctx->claimed_windows[i]->ui.redraw_budget = 2u;
    }
    return loaded ? target_slot : kInvalidFont;
}

bool set_font_tabular_numbers(Context* ctx, uint16_t font_id, bool enabled) noexcept {
    if (!ctx || ctx->in_frame || !is_font_loaded(ctx, font_id)) return false;
    FontSlot& slot = ctx->atlas.slots[font_id];
    if (slot.tabular_numbers == enabled) return true;
    slot.tabular_numbers = enabled;
    slot.metric_hash ^= 0x746E756Du;
    ++ctx->atlas.font_revision;
    ctx->headless_ui.redraw_budget = 2;
    for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i)
        if (ctx->claimed_windows[i]) ctx->claimed_windows[i]->ui.redraw_budget = 2;
    return true;
}

bool is_font_loaded(const Context* ctx, uint16_t font_id) noexcept {
    if (!ctx || font_id >= kMaxFontSlots) return false;
    FontAtlas& atlas = const_cast<Context*>(ctx)->atlas;
    if (font_id == kFontBold && !atlas.slots[kFontBold].loaded && !atlas.bold_probed)
        ensure_bold_loaded(&atlas);
    if (font_id == kFontMono && !atlas.slots[kFontMono].loaded && !atlas.mono_probed)
        ensure_mono_loaded(&atlas);
    if (font_id == kFontMedium || font_id == kFontSemiBold || font_id == kFontItalic) ensure_weighted_loaded(&atlas, font_id);
    return atlas.slots[font_id].loaded;
}

// Shared SVG and native path decoder. Caller owns the frame-arena mark.
static SlugRawCurve* decode_vector_path(Context* ctx, Str svg_path_d,
    std::span<const PathCmd> commands, Vec4 view_box, uint32_t* count,
    void (*stroke_subpath)(void*, std::span<const SlugRawCurve>, Vec2, bool) noexcept = nullptr,
    void* stroke_data = nullptr) noexcept {
    SlugRawCurve* raw = ctx->frame_arena.alloc_uninit<SlugRawCurve>(kStackRawCap);
    if (!raw) { *count = 0; return nullptr; }
    uint32_t raw_cap = kStackRawCap;
    uint32_t raw_count = 0u;
    bool complete = true;

    auto push_raw = [&](SlugRawCurve c) noexcept -> bool {
        if (raw_count >= raw_cap) {
            const uint32_t new_cap = raw_cap * 2u;
            SlugRawCurve* next = ctx->frame_arena.alloc_uninit<SlugRawCurve>(new_cap);
            if (!next) {
                complete = false;
                return false;
            }
            std::memcpy(next, raw, size_t(raw_count) * sizeof(SlugRawCurve));
            raw = next;
            raw_cap = new_cap;
        }
        raw[raw_count++] = c;
        return true;
    };

    const float vx = view_box.x;
    const float vy = view_box.y;
    const float vh = view_box.w;
    auto map_svg = [&](Vec2 p) noexcept -> Vec2 {
        return Vec2{ p.x - vx, (vy + vh) - p.y };
    };
    const auto valid_stroke_point = [&](Vec2 p) noexcept {
        return std::fabs(p.x) < std::fmin(view_box.z * 32768.0f, 1e16f) &&
               std::fabs(p.y) < std::fmin(view_box.w * 32768.0f, 1e16f);
    };
    auto emit_quad = [&](Vec2 p1, Vec2 p2, Vec2 p3, bool is_implicit = false) noexcept {
        const Vec2 a = map_svg(p1);
        const Vec2 b = map_svg(p2);
        const Vec2 c = map_svg(p3);
        if (stroke_subpath && !(valid_stroke_point(a) && valid_stroke_point(b) && valid_stroke_point(c))) {
            complete = false;
            return;
        }
        push_raw(SlugRawCurve{ a.x, a.y, b.x, b.y, c.x, c.y, is_implicit });
    };
    auto emit_line = [&](Vec2 p1, Vec2 p2, bool is_implicit = false) noexcept {
        if (p1.x != p2.x || p1.y != p2.y) {
            emit_quad(p1, (p1 + p2) * 0.5f, p2, is_implicit);
        }
    };
    auto emit_half_cubic = [&](Vec2 q0, Vec2 h1, Vec2 h2, Vec2 q3) noexcept {
        const Vec2 l1 = (q0 + h1) * 0.5f;
        const Vec2 hx = (h1 + h2) * 0.5f;
        const Vec2 l2 = (l1 + hx) * 0.5f;
        const Vec2 r2 = (h2 + q3) * 0.5f;
        const Vec2 r1 = (hx + r2) * 0.5f;
        const Vec2 m  = (l2 + r1) * 0.5f;
        emit_quad(q0, (l1 + l2) * 0.75f - (q0 + m) * 0.25f, m);
        emit_quad(m,  (r1 + r2) * 0.75f - (m + q3) * 0.25f, q3);
    };
    auto emit_cubic = [&](Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3) noexcept {
        const Vec2 ab = (p0 + c1) * 0.5f;
        const Vec2 bc = (c1 + c2) * 0.5f;
        const Vec2 cd = (c2 + p3) * 0.5f;
        const Vec2 abc = (ab + bc) * 0.5f;
        const Vec2 bcd = (bc + cd) * 0.5f;
        const Vec2 mid = (abc + bcd) * 0.5f;
        emit_half_cubic(p0, ab, abc, mid);
        emit_half_cubic(mid, bcd, cd, p3);
    };
    auto emit_arc = [&](Vec2 p0, float rx_in, float ry_in, float phi_deg, bool large_arc, bool sweep, Vec2 p1) noexcept {
        if (p0.x == p1.x && p0.y == p1.y) {
            return;
        }
        float rx = std::fabs(rx_in);
        float ry = std::fabs(ry_in);
        if (rx <= 1e-6f || ry <= 1e-6f) {
            emit_line(p0, p1);
            return;
        }
        constexpr float kPi = 3.14159265358979323846f;
        const float phi = phi_deg * (kPi / 180.0f);
        const float cos_phi = std::cos(phi);
        const float sin_phi = std::sin(phi);
        const float dx2 = 0.5f * (p0.x - p1.x);
        const float dy2 = 0.5f * (p0.y - p1.y);
        const float x1p = cos_phi * dx2 + sin_phi * dy2;
        const float y1p = -sin_phi * dx2 + cos_phi * dy2;

        float rx_sq = rx * rx;
        float ry_sq = ry * ry;
        const float x1p_sq = x1p * x1p;
        const float y1p_sq = y1p * y1p;
        const float lambda = x1p_sq / rx_sq + y1p_sq / ry_sq;
        if (lambda > 1.0f) {
            const float s = std::sqrt(lambda);
            rx *= s;
            ry *= s;
            rx_sq = rx * rx;
            ry_sq = ry * ry;
        }

        const float num = rx_sq * ry_sq - rx_sq * y1p_sq - ry_sq * x1p_sq;
        const float den = rx_sq * y1p_sq + ry_sq * x1p_sq;
        float coef = (den > 1e-12f) ? std::sqrt(std::fmax(0.0f, num / den)) : 0.0f;
        if (large_arc == sweep) {
            coef = -coef;
        }
        const float cxp = coef * (rx * y1p) / ry;
        const float cyp = coef * -(ry * x1p) / rx;
        const float cx = cos_phi * cxp - sin_phi * cyp + 0.5f * (p0.x + p1.x);
        const float cy = sin_phi * cxp + cos_phi * cyp + 0.5f * (p0.y + p1.y);

        const float ux = (x1p - cxp) / rx;
        const float uy = (y1p - cyp) / ry;
        const float theta1 = svg_vec_angle(1.0f, 0.0f, ux, uy);
        float dtheta = svg_vec_angle(ux, uy, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
        if (!sweep && dtheta > 0.0f) {
            dtheta -= 2.0f * kPi;
        } else if (sweep && dtheta < 0.0f) {
            dtheta += 2.0f * kPi;
        }

        if (!std::isfinite(dtheta)) { complete = false; return; }
        const int segs = static_cast<int>(std::ceil(std::fabs(dtheta) / (0.5f * kPi)));
        const int n_segs = (segs > 1) ? segs : 1;
        const float seg_dt = dtheta / static_cast<float>(n_segs);
        const float k = (4.0f / 3.0f) * std::tan(0.25f * seg_dt);

        Vec2 seg_start = p0;
        for (int s = 0; s < n_segs; ++s) {
            const float t0 = theta1 + static_cast<float>(s) * seg_dt;
            const float t1 = theta1 + static_cast<float>(s + 1) * seg_dt;
            const float ct0 = std::cos(t0), st0 = std::sin(t0);
            const float ct1 = std::cos(t1), st1 = std::sin(t1);
            const float ex0 = rx * (ct0 - k * st0);
            const float ey0 = ry * (st0 + k * ct0);
            const float ex1 = rx * (ct1 + k * st1);
            const float ey1 = ry * (st1 - k * ct1);
            const Vec2 c1 = { cx + cos_phi * ex0 - sin_phi * ey0, cy + sin_phi * ex0 + cos_phi * ey0 };
            const Vec2 c2 = { cx + cos_phi * ex1 - sin_phi * ey1, cy + sin_phi * ex1 + cos_phi * ey1 };
            const Vec2 seg_end = (s + 1 == n_segs)
                ? p1
                : Vec2{
                    cx + cos_phi * (rx * ct1) - sin_phi * (ry * st1),
                    cy + sin_phi * (rx * ct1) + cos_phi * (ry * st1),
                };
            emit_cubic(seg_start, c1, c2, seg_end);
            seg_start = seg_end;
        }
    };

    const char* p = svg_path_d.chars;
    const char* end = svg_path_d.chars ? svg_path_d.chars + svg_path_d.length : nullptr;
    char cmd = '\0';
    Vec2 cur = {};
    Vec2 subpath_start = {};
    Vec2 prev_quad_ctrl = {};
    Vec2 prev_cubic_ctrl = {};
    bool has_quad_ctrl = false;
    bool has_cubic_ctrl = false;
    bool subpath_has_segments = false;

    auto close_subpath = [&](bool is_explicit) noexcept {
        if (subpath_has_segments) {
            if (stroke_subpath) {
                const Vec2 start = map_svg(subpath_start);
                complete = complete && valid_stroke_point(start) && valid_stroke_point(map_svg(cur));
                if (complete) stroke_subpath(stroke_data, {raw, raw_count}, start, is_explicit);
                raw_count = 0;
            } else {
                emit_line(cur, subpath_start, !is_explicit);
            }
            subpath_has_segments = false;
        }
        cur = subpath_start;
    };

    for (const PathCmd& c : commands) {
        switch (c.verb) {
            case PathVerb::MoveTo: close_subpath(false); cur = c.p1; subpath_start = cur; break;
            case PathVerb::LineTo: emit_line(cur, c.p1); cur = c.p1; subpath_has_segments = true; break;
            case PathVerb::QuadTo: emit_quad(cur, c.p1, c.p2); cur = c.p2; subpath_has_segments = true; break;
            case PathVerb::CubicTo: emit_cubic(cur, c.p1, c.p2, c.p3); cur = c.p3; subpath_has_segments = true; break;
            case PathVerb::Close: close_subpath(true); break;
        }
    }
    bool svg_started = false;
    while (complete && p && p < end) {
        p = svg_skip_ws_comma(p, end);
        if (p >= end) {
            break;
        }
        if (svg_is_command_char(*p)) {
            cmd = *p++;
        } else if (cmd == '\0') {
            if (stroke_subpath) complete = false;
            break;
        }
        if (stroke_subpath && !svg_started && cmd != 'M' && cmd != 'm') {
            complete = false;
            break;
        }
        svg_started = true;

        if (cmd == 'Z' || cmd == 'z') {
            close_subpath(true);
            has_quad_ctrl = false;
            has_cubic_ctrl = false;
            cmd = '\0';
            continue;
        }

        const bool rel = (cmd >= 'a' && cmd <= 'z');
        switch (cmd) {
            case 'M':
            case 'm': {
                float x = 0.0f, y = 0.0f;
                if (!svg_parse_number(p, end, &x) || !svg_parse_number(p, end, &y)) {
                    if (stroke_subpath) complete = false;
                    p = end;
                    break;
                }
                close_subpath(false);
                cur = rel ? Vec2{ cur.x + x, cur.y + y } : Vec2{ x, y };
                subpath_start = cur;
                has_quad_ctrl = false;
                has_cubic_ctrl = false;
                cmd = rel ? 'l' : 'L';
                break;
            }
            case 'L':
            case 'l': {
                float x = 0.0f, y = 0.0f;
                if (!svg_parse_number(p, end, &x) || !svg_parse_number(p, end, &y)) {
                    if (stroke_subpath) complete = false;
                    p = end;
                    break;
                }
                const Vec2 nxt = rel ? Vec2{ cur.x + x, cur.y + y } : Vec2{ x, y };
                emit_line(cur, nxt);
                cur = nxt;
                subpath_has_segments = true;
                has_quad_ctrl = false;
                has_cubic_ctrl = false;
                break;
            }
            case 'H':
            case 'h': {
                float x = 0.0f;
                if (!svg_parse_number(p, end, &x)) {
                    if (stroke_subpath) complete = false;
                    p = end;
                    break;
                }
                const Vec2 nxt = { rel ? (cur.x + x) : x, cur.y };
                emit_line(cur, nxt);
                cur = nxt;
                subpath_has_segments = true;
                has_quad_ctrl = false;
                has_cubic_ctrl = false;
                break;
            }
            case 'V':
            case 'v': {
                float y = 0.0f;
                if (!svg_parse_number(p, end, &y)) {
                    if (stroke_subpath) complete = false;
                    p = end;
                    break;
                }
                const Vec2 nxt = { cur.x, rel ? (cur.y + y) : y };
                emit_line(cur, nxt);
                cur = nxt;
                subpath_has_segments = true;
                has_quad_ctrl = false;
                has_cubic_ctrl = false;
                break;
            }
            case 'Q':
            case 'q': {
                float cx = 0.0f, cy = 0.0f, x = 0.0f, y = 0.0f;
                if (!svg_parse_number(p, end, &cx) || !svg_parse_number(p, end, &cy) ||
                    !svg_parse_number(p, end, &x) || !svg_parse_number(p, end, &y))
                {
                    if (stroke_subpath) complete = false;
                    p = end;
                    break;
                }
                const Vec2 ctrl = rel ? Vec2{ cur.x + cx, cur.y + cy } : Vec2{ cx, cy };
                const Vec2 nxt  = rel ? Vec2{ cur.x + x, cur.y + y } : Vec2{ x, y };
                emit_quad(cur, ctrl, nxt);
                cur = nxt;
                prev_quad_ctrl = ctrl;
                has_quad_ctrl = true;
                has_cubic_ctrl = false;
                subpath_has_segments = true;
                break;
            }
            case 'T':
            case 't': {
                float x = 0.0f, y = 0.0f;
                if (!svg_parse_number(p, end, &x) || !svg_parse_number(p, end, &y)) {
                    if (stroke_subpath) complete = false;
                    p = end;
                    break;
                }
                const Vec2 ctrl = has_quad_ctrl ? (2.0f * cur - prev_quad_ctrl) : cur;
                const Vec2 nxt  = rel ? Vec2{ cur.x + x, cur.y + y } : Vec2{ x, y };
                emit_quad(cur, ctrl, nxt);
                cur = nxt;
                prev_quad_ctrl = ctrl;
                has_quad_ctrl = true;
                has_cubic_ctrl = false;
                subpath_has_segments = true;
                break;
            }
            case 'C':
            case 'c': {
                float c1x = 0.0f, c1y = 0.0f, c2x = 0.0f, c2y = 0.0f, x = 0.0f, y = 0.0f;
                if (!svg_parse_number(p, end, &c1x) || !svg_parse_number(p, end, &c1y) ||
                    !svg_parse_number(p, end, &c2x) || !svg_parse_number(p, end, &c2y) ||
                    !svg_parse_number(p, end, &x) || !svg_parse_number(p, end, &y))
                {
                    if (stroke_subpath) complete = false;
                    p = end;
                    break;
                }
                const Vec2 c1  = rel ? Vec2{ cur.x + c1x, cur.y + c1y } : Vec2{ c1x, c1y };
                const Vec2 c2  = rel ? Vec2{ cur.x + c2x, cur.y + c2y } : Vec2{ c2x, c2y };
                const Vec2 nxt = rel ? Vec2{ cur.x + x, cur.y + y } : Vec2{ x, y };
                emit_cubic(cur, c1, c2, nxt);
                cur = nxt;
                prev_cubic_ctrl = c2;
                has_cubic_ctrl = true;
                has_quad_ctrl = false;
                subpath_has_segments = true;
                break;
            }
            case 'S':
            case 's': {
                float c2x = 0.0f, c2y = 0.0f, x = 0.0f, y = 0.0f;
                if (!svg_parse_number(p, end, &c2x) || !svg_parse_number(p, end, &c2y) ||
                    !svg_parse_number(p, end, &x) || !svg_parse_number(p, end, &y))
                {
                    if (stroke_subpath) complete = false;
                    p = end;
                    break;
                }
                const Vec2 c1  = has_cubic_ctrl ? (2.0f * cur - prev_cubic_ctrl) : cur;
                const Vec2 c2  = rel ? Vec2{ cur.x + c2x, cur.y + c2y } : Vec2{ c2x, c2y };
                const Vec2 nxt = rel ? Vec2{ cur.x + x, cur.y + y } : Vec2{ x, y };
                emit_cubic(cur, c1, c2, nxt);
                cur = nxt;
                prev_cubic_ctrl = c2;
                has_cubic_ctrl = true;
                has_quad_ctrl = false;
                subpath_has_segments = true;
                break;
            }
            case 'A':
            case 'a': {
                float rx = 0.0f, ry = 0.0f, phi = 0.0f, x = 0.0f, y = 0.0f;
                bool large_arc = false, sweep = false;
                if (!svg_parse_number(p, end, &rx) || !svg_parse_number(p, end, &ry) ||
                    !svg_parse_number(p, end, &phi) || !svg_parse_flag(p, end, &large_arc) ||
                    !svg_parse_flag(p, end, &sweep) || !svg_parse_number(p, end, &x) ||
                    !svg_parse_number(p, end, &y))
                {
                    if (stroke_subpath) complete = false;
                    p = end;
                    break;
                }
                const Vec2 nxt = rel ? Vec2{ cur.x + x, cur.y + y } : Vec2{ x, y };
                emit_arc(cur, rx, ry, phi, large_arc, sweep, nxt);
                cur = nxt;
                has_quad_ctrl = false;
                has_cubic_ctrl = false;
                subpath_has_segments = true;
                break;
            }
            default:
                if (stroke_subpath) complete = false;
                p = end;
                break;
        }
    }
    close_subpath(false);

    *count = complete ? raw_count : 0;
    if (stroke_subpath && !complete) return nullptr;
    return raw;
}

namespace {

struct StrokeOutline {
    Arena* arena;
    const SvgStrokeDesc* desc;
    float tolerance = 0.0f;
    float dot_tolerance = 0.0f;
    SlugRawCurve* curves = nullptr;
    uint32_t curve_count = 0;
    uint32_t curve_cap = 0;
    Vec2* points = nullptr;
    uint32_t point_count = 0;
    uint32_t point_cap = 0;
    bool complete = true;
};

template<class T>
bool grow_stroke_buffer(Arena* arena, T*& data, uint32_t& capacity) noexcept {
    const uint32_t grown = capacity ? capacity * 2u : 64u;
    T* next = arena->alloc_uninit<T>(grown);
    if (!next) return false;
    if (capacity) std::memcpy(next, data, size_t(capacity) * sizeof(T));
    data = next;
    capacity = grown;
    return true;
}

float stroke_cross(Vec2 a, Vec2 b) noexcept { return a.x * b.y - a.y * b.x; }
float stroke_dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }

void push_stroke_point(StrokeOutline& out, Vec2 p) noexcept {
    if (!out.complete) return;
    if (out.point_count && stroke_dot(p - out.points[out.point_count - 1], p - out.points[out.point_count - 1]) <= 1e-12f)
        return;
    if (out.point_count == out.point_cap && !grow_stroke_buffer(out.arena, out.points, out.point_cap)) {
        out.complete = false;
        return;
    }
    out.points[out.point_count++] = p;
}

void flatten_stroke_quad(StrokeOutline& out, Vec2 a, Vec2 b, Vec2 c, uint32_t depth) noexcept {
    const Vec2 l = (a + b) * 0.5f, r = (b + c) * 0.5f;
    const Vec2 mid = (l + r) * 0.5f;
    const Vec2 error = mid - (a + c) * 0.5f;
    if (depth == 10u || stroke_dot(error, error) <= out.tolerance * out.tolerance) {
        push_stroke_point(out, c);
        return;
    }
    flatten_stroke_quad(out, a, l, mid, depth + 1u);
    flatten_stroke_quad(out, mid, r, c, depth + 1u);
}

void stroke_subpath(void* data, std::span<const SlugRawCurve> source, Vec2 start, bool closed) noexcept {
    auto& out = *static_cast<StrokeOutline*>(data);
    if (!out.complete) return;
    out.point_count = 0;
    push_stroke_point(out, start);
    for (const SlugRawCurve& c : source)
        flatten_stroke_quad(out, {c.p1x, c.p1y}, {c.p2x, c.p2y}, {c.p3x, c.p3y}, 0);
    if (!out.complete) return;

    const auto& desc = *out.desc;
    const float half = desc.stroke_width * 0.5f;
    constexpr float pi = 3.14159265358979323846f;
    const auto right = [](Vec2 d) noexcept -> Vec2 { return {d.y, -d.x}; };
    const auto unit = [](Vec2 d) noexcept { return d / std::hypot(d.x, d.y); };
    Vec2 pen{}, contour_start{};
    bool has_pen = false;
    const auto quad = [&](Vec2 control, Vec2 end) noexcept {
        if (!out.complete) return;
        if (out.curve_count == out.curve_cap && !grow_stroke_buffer(out.arena, out.curves, out.curve_cap)) {
            out.complete = false;
            return;
        }
        out.curves[out.curve_count++] = {pen.x, pen.y, control.x, control.y, end.x, end.y};
        pen = end;
    };
    const auto line = [&](Vec2 end) noexcept {
        if (!has_pen) {
            contour_start = pen = end;
            has_pen = true;
        } else if (pen.x != end.x || pen.y != end.y) {
            quad((pen + end) * 0.5f, end);
        }
    };
    const auto arc = [&](Vec2 center, Vec2 from, Vec2 to, float sweep) noexcept {
        const int steps = static_cast<int>(std::ceil(sweep / (pi * 0.25f)));
        const float step = sweep / static_cast<float>(steps);
        Vec2 u = from;
        for (int i = 0; i < steps; ++i) {
            const Vec2 v = i + 1 == steps ? to : u * std::cos(step) - right(u) * std::sin(step);
            quad(center + (u + v) * (half / (1.0f + stroke_dot(u, v))), center + v * half);
            u = v;
        }
    };

    float path_length = 0.0f;
    for (uint32_t i = 1; i < out.point_count; ++i) {
        const Vec2 d = out.points[i] - out.points[i - 1];
        path_length += std::hypot(d.x, d.y);
    }
    if (path_length <= out.dot_tolerance) {
        if (desc.cap == StrokeCap::Round) {
            line(start + Vec2{half, 0});
            arc(start, {1, 0}, {1, 0}, 2.0f * pi);
        } else if (desc.cap == StrokeCap::Square) {
            line(start + Vec2{-half, -half});
            line(start + Vec2{half, -half});
            line(start + Vec2{half, half});
            line(start + Vec2{-half, half});
            line(start + Vec2{-half, -half});
        }
        return;
    }

    const Vec2* p = out.points;
    uint32_t n = out.point_count;
    if (closed && stroke_dot(p[n - 1] - p[0], p[n - 1] - p[0]) <= 1e-12f) --n;
    const auto join = [&](Vec2 a, Vec2 b, Vec2 c) noexcept {
        const Vec2 incoming = unit(b - a), outgoing = unit(c - b);
        const Vec2 u = right(incoming), v = right(outgoing);
        const float turn = stroke_cross(incoming, outgoing);
        const float cosine = stroke_dot(incoming, outgoing);
        const float denom = 1.0f + cosine;
        if (denom > 1e-5f) {
            const Vec2 bisector = (u + v) / denom;
            const float ratio_sq = stroke_dot(bisector, bisector);
            if ((turn <= 0.0f && ratio_sq <= 16.0f) ||
                (turn > 0.0f && desc.join == StrokeJoin::Miter && ratio_sq <= desc.miter_limit * desc.miter_limit)) {
                line(b + bisector * half);
                return;
            }
        }
        line(b + u * half);
        if (desc.join == StrokeJoin::Round && (turn > 1e-6f || (denom <= 1e-5f && turn == 0.0f)))
            arc(b, u, v, turn == 0.0f ? pi : std::atan2(turn, cosine));
        else
            line(b + v * half);
    };

    const auto trim = [&](Vec2 a, Vec2 b) noexcept {
        const float turn = stroke_cross(a, b);
        const float denom = 1.0f + stroke_dot(a, b);
        if (denom <= 1e-5f) return INFINITY;
        return turn * half / denom;
    };
    const uint32_t edges = closed ? n : n - 1u;
    bool folded = false;
    for (uint32_t i = 0; i < edges; ++i) {
        const uint32_t j = (i + 1u) % n;
        const Vec2 delta = p[j] - p[i];
        const float length = std::hypot(delta.x, delta.y);
        const Vec2 direction = delta / length;
        const float from = i > 0 || closed ? trim(unit(p[i] - p[(i + n - 1u) % n]), direction) : 0.0f;
        const float to = j < n - 1u || closed ? trim(direction, unit(p[(j + 1u) % n] - p[j])) : 0.0f;
        if (std::fmax(from, 0.0f) + std::fmax(to, 0.0f) >= length ||
            std::fmin(from, 0.0f) + std::fmin(to, 0.0f) <= -length) {
            folded = true;
            break;
        }
    }
    if (folded) {
        // A tight inside offset can cross its neighbors. Build the union from
        // positive-winding strips and outside joins to keep the interior filled.
        for (uint32_t i = 0; i < edges; ++i) {
            const uint32_t j = (i + 1u) % n;
            const Vec2 dir = unit(p[j] - p[i]), normal = right(dir);
            const bool first = !closed && i == 0, last = !closed && j == n - 1u;
            const Vec2 a = first && desc.cap == StrokeCap::Square ? p[i] - dir * half : p[i];
            const Vec2 b = last && desc.cap == StrokeCap::Square ? p[j] + dir * half : p[j];
            has_pen = false;
            line(a + normal * half);
            line(b + normal * half);
            if (last && desc.cap == StrokeCap::Round) arc(b, normal, -normal, pi);
            else line(b - normal * half);
            line(a - normal * half);
            if (first && desc.cap == StrokeCap::Round) arc(a, -normal, normal, pi);
            else line(contour_start);

            if (last) continue;
            Vec2 before = p[i], after = p[(j + 1u) % n];
            const float turn = stroke_cross(dir, unit(after - p[j]));
            if (turn < 0.0f) std::swap(before, after);
            const Vec2 u = right(unit(p[j] - before)), v = right(unit(after - p[j]));
            has_pen = false;
            line(p[j]);
            line(p[j] + u * half);
            join(before, p[j], after);
            line(p[j] + v * half);
            line(contour_start);
        }
        return;
    }

    // Traversing the right rail in each direction produces opposite winding for
    // closed paths. The shader takes absolute winding, so the outer contour of
    // each subpath contributes the same sign regardless of path direction.
    for (uint32_t rail = 0; rail < 2; ++rail) {
        const auto at = [&](uint32_t i) noexcept { return p[rail ? n - 1u - i : i]; };
        const Vec2 first_dir = unit(at(1) - at(0));
        const Vec2 last_dir = unit(at(n - 1) - at(n - 2));
        const float extension = desc.cap == StrokeCap::Square ? half : 0.0f;
        if (closed) has_pen = false;
        else if (rail == 0) line(at(0) + right(first_dir) * half - first_dir * extension);
        const uint32_t first = closed ? 0u : 1u;
        const uint32_t end = closed ? n : n - 1u;
        for (uint32_t i = first; i < end; ++i)
            join(at((i + n - 1) % n), at(i), at((i + 1) % n));
        if (closed) {
            line(contour_start);
        } else {
            const Vec2 normal = right(last_dir);
            const Vec2 cap = at(n - 1) + last_dir * extension;
            line(cap + normal * half);
            if (desc.cap == StrokeCap::Round) arc(cap, normal, -normal, pi);
            else line(cap - normal * half);
        }
    }
}

} // namespace

static uint16_t register_vector_icon(Context* ctx, Str svg_path_d, Vec4 view_box,
    uint16_t slot_hint, const SvgStrokeDesc* stroke) noexcept
{
    if (!ctx || !svg_path_d.chars || svg_path_d.length == 0u ||
        !std::isfinite(view_box.x) || !std::isfinite(view_box.y) ||
        !std::isfinite(view_box.z) || !std::isfinite(view_box.w) ||
        view_box.z <= 1e-3f || view_box.w <= 1e-3f ||
        (stroke && (stroke->stroke_width >= std::fmin(std::fmin(view_box.z, view_box.w) * 32768.0f, 1e16f) ||
                    (slot_hint != kInvalidIcon && slot_hint >= kMaxVectorIcons))))
    {
        return kInvalidIcon;
    }

    uint16_t slot = kInvalidIcon;
    if (slot_hint < kMaxVectorIcons) {
        slot = slot_hint;
    } else {
        for (uint16_t i = 0u; i < kMaxVectorIcons; ++i) {
            if (!ctx->atlas.icons[i].loaded) {
                slot = i;
                break;
            }
        }
    }
    if (slot == kInvalidIcon) {
        return kInvalidIcon;
    }

    const size_t mark = ctx->frame_arena.mark();
    uint32_t raw_count = 0;
    const SlugRawCurve* raw = nullptr;
    StrokeOutline outline{.arena = &ctx->frame_arena, .desc = stroke};
    if (stroke) {
        outline.dot_tolerance = std::fmin(view_box.z, view_box.w) * 0.001f;
        outline.tolerance = std::fmax(1e-5f, std::fmin(outline.dot_tolerance, stroke->stroke_width / 32.0f));
        if (decode_vector_path(ctx, svg_path_d, {}, view_box, &raw_count, stroke_subpath, &outline) && outline.complete) {
            raw = outline.curves;
            raw_count = outline.curve_count;
        }
    } else {
        raw = decode_vector_path(ctx, svg_path_d, {}, view_box, &raw_count);
    }

    const CompiledSlugShape compiled = compile_slug_contours(
        &ctx->atlas,
        raw,
        raw_count,
        0.0f,
        0.0f,
        view_box.z,
        view_box.w);
    ctx->frame_arena.rewind(mark);

    if (!compiled.has_outline) {
        return kInvalidIcon;
    }

    ctx->atlas.icons[slot] = VectorIconEntry{
        .band_offset = compiled.band_offset,
        .num_hbands = compiled.num_hbands,
        .num_vbands = compiled.num_vbands,
        .view_size = Vec2{ view_box.z, view_box.w },
        .loaded = true,
        .has_outline = true,
    };
    ctx->headless_ui.redraw_budget = 2u;
        for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i)
            if (ctx->claimed_windows[i]) ctx->claimed_windows[i]->ui.redraw_budget = 2u;
    return slot;
}

uint16_t register_svg_icon(Context* ctx, Str path, Vec4 view_box, uint16_t slot_hint) noexcept {
    return register_vector_icon(ctx, path, view_box, slot_hint, nullptr);
}

uint16_t register_svg_stroke_icon(Context* ctx, Str path, const SvgStrokeDesc& desc) noexcept {
    if (!std::isfinite(desc.stroke_width) || desc.stroke_width <= 0.0f ||
        !std::isfinite(desc.miter_limit) || desc.miter_limit < 1.0f ||
        desc.cap > StrokeCap::Square || desc.join > StrokeJoin::Bevel) return kInvalidIcon;
    return register_vector_icon(ctx, path, desc.view_box, desc.slot_hint, &desc);
}

bool is_icon_loaded(const Context* ctx, uint16_t icon_id) noexcept {
    return ctx && icon_id < kMaxVectorIcons && ctx->atlas.icons[icon_id].loaded;
}

} // namespace etch
