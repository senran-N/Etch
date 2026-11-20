#include "etch/etch.h"
#include "etch/etch_c.h"
#include "etch_testing.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include "checks_desktop.h"
#include "checks_perceptual_color.h"
#include "checks_motion.h"
#include "checks_gpu_materials.h"
#include "checks_shader_surface.h"
#include "checks_workbench_materials.h"
#include "checks_desktop_precision.h"
#include "checks_visual_quality.h"
#include "checks_workbench.h"
#include "checks_closure.h"
#include "checks_editor.h"
#include "checks_a11y.h"
#include "checks_media_emoji.h"
#include "checks_theme_wrap.h"
#include "checks_os_window_a11y.h"
#include "checks_v021.h"
#include "checks_v022.h"
#include "etch_uber_vs_msl.h"
#include "etch_uber_ps_msl.h"
#include "etch_kawase_vs_msl.h"
#include "etch_kawase_ps_msl.h"
using namespace etch;
#if defined(ETCH_CMAKE_VERSION_MAJOR)
static_assert(ETCH_VERSION_MAJOR == ETCH_CMAKE_VERSION_MAJOR && ETCH_VERSION_MINOR == ETCH_CMAKE_VERSION_MINOR && ETCH_VERSION_PATCH == ETCH_CMAKE_VERSION_PATCH);
#define ETCH_EXPECTED_VERSION_STRING ETCH_VERSION_STR(ETCH_CMAKE_VERSION_MAJOR) "." ETCH_VERSION_STR(ETCH_CMAKE_VERSION_MINOR) "." ETCH_VERSION_STR(ETCH_CMAKE_VERSION_PATCH)
#else
#define ETCH_EXPECTED_VERSION_STRING ETCH_VERSION_STRING
#endif
static bool arena_suite() noexcept {
    static_assert(sizeof(EtchPrim) == sizeof(Prim) && sizeof(EtchGlobalsUniform) == sizeof(GlobalsUniform));
    Arena a=arena_create(128);if(!a.base)return false;
    auto* p=a.alloc<uint32_t>(8);const bool ok=p && (reinterpret_cast<uintptr_t>(p)%alignof(uint32_t)==0) && a.remaining()==96 && std::strcmp(ETCH_VERSION_STRING, ETCH_EXPECTED_VERSION_STRING) == 0 && etch_id("x") == id("x");
    a.reset();const bool reset=a.offset==0 && a.alloc<uint32_t>(8)==p;arena_destroy(&a);
    EtchArena ca = etch_arena_create(64);
    const bool ca_ok = ca.base && etch_arena_alloc_bytes(&ca, 16, 8) != nullptr;
    etch_arena_destroy(&ca);
    Context* c=create_context({.max_prims_per_frame=64,.headless=true});if(!c)return false;
    begin_frame(c,{.viewport_w=800,.viewport_h=500});for(uint32_t i=0;i<256;++i)box({.width=Sizing::fixed(1),.height=Sizing::fixed(1),.background=Color::white()});end_frame(c);
    const bool elastic=frame_stats(c).prim_capacity>64;destroy_context(c);set_current_context(nullptr);return ok && reset && ca_ok && elastic;
}
static bool unicode_suite() noexcept {
    Context* c=create_context({.headless=true});if(!c)return false;set_current_context(c);
    char data[64]="A\xCC\x81 \xD7\x90\xD7\x91 \xF0\x9F\x91\xA9";const uint32_t tid=id("unicode");
    begin_frame(c,{.viewport_w=400,.viewport_h=200});text_input({.id=tid,.buffer=data,.capacity=64});end_frame(c);
    const auto* n=find_a11y_node(tid,c);bool ok=n && n->value_text.length==std::strlen(data) && n->bounds.w>0;
    request_focus(tid);set_text_selection(tid,3,3);
    begin_frame(c,{.viewport_w=400,.viewport_h=200,.key_pressed_code=SDLK_LEFT});const auto moved=text_input({.id=tid,.buffer=data,.capacity=64});end_frame(c);
    ok &= moved.cursor_byte==0;
    begin_frame(c,{.viewport_w=400,.viewport_h=200,.key_pressed_code=SDLK_DELETE});text_input({.id=tid,.buffer=data,.capacity=64});end_frame(c);
    ok &= data[0]==' ' && static_cast<unsigned char>(data[1])==0xD7;
    TextBuffer b={};ok &= init_text_buffer(&b,str_cstr(data));ok &= text_buffer_line_count(&b)==1;free_text_buffer(&b);
    destroy_context(c);set_current_context(nullptr);return ok;
}
static bool v020_regression() noexcept {
    Context* c = create_context({.headless = true});
    if (!c) return false;
    bool ok = true;
    const auto inject_text_input = [&](const char* utf8) noexcept {
        SDL_Event ev = {};
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = utf8;
        process_event(c, ev);
    };
    const uint32_t ti_id = id("v020_ti");
    const uint32_t next_id = id("v020_next");
    const uint32_t scroll_id = id("v020_scroll");
    const uint32_t term_id = id("v020_term");
    char buf[64] = "line1\nline2";
    bool suppress = true;

    const auto step_ui = [&](uint32_t key = 0u, TextInputResult* out_res = nullptr, bool* saw_key = nullptr) noexcept {
        begin_frame(c, {.viewport_w = 400, .viewport_h = 300, .key_pressed_code = key});
        if (saw_key && key != 0u) *saw_key = key_pressed(key);
        const TextInputResult r = text_input({
            .id = ti_id, .buffer = buf, .capacity = sizeof(buf),
            .width = Sizing::fixed(200), .height = Sizing::fixed(80),
            .multiline = true, .suppress_popup_keys = suppress
        });
        if (out_res) *out_res = r;
        box({.id = next_id, .width = Sizing::fixed(100), .height = Sizing::fixed(30), .focusable = true});
        begin_box({.id = scroll_id, .width = Sizing::fixed(200), .height = Sizing::fixed(100), .scroll_y = true});
        box({.width = Sizing::fixed(180), .height = Sizing::fixed(350)});
        end_box();
        box({.id = term_id, .width = Sizing::fixed(200), .height = Sizing::fixed(40), .focusable = true});
        end_frame(c);
    };

    ok &= scroll_delta().x == 0.0f && scroll_delta().y == 0.0f && typed_text().length == 0u;
    step_ui();
    ok &= std::fabs(scroll_max(scroll_id).y - 250.0f) < 0.5f && scroll_max(id("missing")).y == 0.0f;

    request_focus(ti_id);
    set_text_selection(ti_id, 3u, 3u);
    step_ui();

    bool saw = false;
    TextInputResult res = {};
    step_ui(SDLK_TAB, &res, &saw);
    ok &= focused(ti_id) && !focused(next_id) && saw;

    saw = false;
    step_ui(SDLK_DOWN, &res, &saw);
    ok &= saw && res.cursor_byte == 3u;

    set_text_selection(ti_id, 9u, 9u);
    step_ui();
    saw = false;
    step_ui(SDLK_UP, &res, &saw);
    ok &= saw && res.cursor_byte == 9u;

    set_text_selection(ti_id, 3u, 3u);
    step_ui();

    saw = false;
    step_ui(SDLK_RETURN, &res, &saw);
    ok &= saw && !res.changed && !res.submitted && std::strcmp(buf, "line1\nline2") == 0 && res.cursor_byte == 3u;

    inject_text_input("X");
    step_ui(0u, &res);
    ok &= res.changed && std::strcmp(buf, "linXe1\nline2") == 0 && res.cursor_byte == 4u;

    step_ui(SDLK_BACKSPACE, &res);
    ok &= res.changed && std::strcmp(buf, "line1\nline2") == 0 && res.cursor_byte == 3u;

    step_ui(SDLK_LEFT, &res);
    ok &= res.cursor_byte == 2u;

    suppress = false;
    step_ui();
    step_ui(SDLK_TAB);
    ok &= focused(next_id);

    SDL_Event wheel = {};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.x = 1.0f;
    wheel.wheel.y = -2.5f;
    process_event(c, wheel);
    inject_text_input("ab");
    begin_frame(c, {.viewport_w = 400, .viewport_h = 300});
    ok &= std::fabs(scroll_delta().x - 1.0f) < 1e-4f && std::fabs(scroll_delta().y + 2.5f) < 1e-4f;
    ok &= typed_text(term_id).length == 0u && typed_text().length == 2u && std::memcmp(typed_text().chars, "ab", 2) == 0;
    request_text_input(term_id, {10.0f, 20.0f, 2.0f, 16.0f});
    end_frame(c);
    ok &= scroll_delta().x == 0.0f && scroll_delta().y == 0.0f && typed_text().length == 0u;

    request_focus(term_id);
    inject_text_input("ls");
    inject_text_input("!");
    begin_frame(c, {.viewport_w = 400, .viewport_h = 300});
    request_text_input(term_id, {12.0f, 24.0f, 2.0f, 16.0f});
    const Str tt = typed_text(term_id);
    const Rect cr = ime_cursor_rect(c);
    ok &= tt.length == 3u && std::memcmp(tt.chars, "ls!", 3) == 0 && tt.chars[3] == '\0';
    ok &= std::fabs(cr.x - 12.0f) < 1e-4f && std::fabs(cr.y - 24.0f) < 1e-4f && cr.h == 16.0f;
    end_frame(c);

    destroy_context(c);
    set_current_context(nullptr);
    return ok;
}

static bool layout_suite() noexcept {
    if(!verify_snap_sheet() || !verify_caret_deadline() || !v020_regression())return false;
    Context* c=create_context({.headless=true});if(!c)return false;
    begin_frame(c,{.viewport_w=400,.viewport_h=200});begin_box({.id=id("grid"),.width=Sizing::fixed(400),.height=Sizing::fixed(200),.gap=10,.grid_columns=2});
    box({.id=id("left"),.width=Sizing::grow(),.height=Sizing::fixed(40)});box({.id=id("right"),.width=Sizing::grow(),.height=Sizing::fixed(40)});end_box();end_frame(c);
    const auto* l=find_a11y_node(id("left"),c);const auto* r=find_a11y_node(id("right"),c);
    const bool ok=l && r && l->bounds.w>0 && r->bounds.x>=l->bounds.x+l->bounds.w && l->parent_id==id("grid");destroy_context(c);set_current_context(nullptr);return ok;
}
static bool table_viewport_regression() noexcept {
    Context* c=create_context({.headless=true});if(!c)return false;
    const uint32_t table=id("bounded_table");
    const uint32_t dock=id("bounded_table_dock"), pane=id("bounded_table_pane");
    const uint32_t root=dock_reset_root(dock);
    dock_add_pane(dock,root,{.id=pane,.title=str_lit("Table")});
    const TableColumnDesc columns[]={{.title=str_lit("Name"),.sizing=TableColumnSizing::Stretch,.width=1}};
    bool ok=true;
    for(uint32_t f=0;f<90 && ok;++f) {
        const uint32_t height=f<30 ? 480u : f<60 ? 240u : 720u;
        begin_frame(c,{.viewport_w=800,.viewport_h=height});
        begin_box({.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::grow(),.clip_children=true});
        box({.height=Sizing::fixed(40)});
        begin_dock_space({.id=dock});
        if(next_dock_pane()!=pane) {ok=false;end_dock_space();end_box();end_frame(c);break;}
        begin_box({.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::grow(),.shrink_height=true});
        const auto result=begin_table({.id=table,.columns=columns,.item_count=1000000,.row_height_px=24,.horizontal_scroll=f<45});
        uint32_t rows=0;
        while(table_next_row()) {++rows;while(table_next_column())text(str_lit("cell"));}
        end_table();end_box();
        if(next_dock_pane()!=0) ok=false;
        end_dock_space();end_box();end_frame(c);
        const auto bounds=element_bounds(table);
        // Slice calculation uses the previous frame's measured viewport on resize.
        ok &= rows>0 && rows<720/24+10 && bounds.h>20 && bounds.y+bounds.h<=float(height)+.5f;
        if(!ok) std::fprintf(stderr,"table viewport regression: frame=%u rows=%u y=%.1f h=%.1f viewport=%u\n",f,rows,bounds.y,bounds.h,height);
        if(f==15 || f==45 || f==75) {
            table_scroll_to_row(table,900000+f,24,AlignY::Top);
        }
        if(f==20 || f==50 || f==80) ok &= result.slice.first_index>899990;
        if(f==29 || f==59 || f==89) ok &= !needs_redraw(c);
    }
    destroy_context(c);set_current_context(nullptr);return ok;
}
static float stream_height(uint32_t, void* value) noexcept { return value ? *static_cast<float*>(value) : 300; }
static bool list_append_regression() noexcept {
    Context* c=create_context({.headless=true});if(!c)return false;
    const auto metrics=font_metrics(kFontSans,24);
    const auto c_metrics=etch_font_metrics(ETCH_FONT_SANS,24);
    const auto scaled=font_metrics(kFontSans,48);
    const uint32_t list=id("append_heights");bool ok=metrics.ascent>0 && metrics.descent>=0 && metrics.line_height>0 && metrics.ascent==c_metrics.ascent && scaled.ascent==metrics.ascent*2;double before=0;

    for(uint32_t f=0;f<8;++f) {
        begin_frame(c,{.viewport_w=400,.viewport_h=200});
        if(f==4)virtual_list_invalidate_heights(list);
        const auto slice=begin_virtual_list({.id=list,.item_count=f<4 ? 255u : 260u,.height=Sizing::fixed(200),.measure_height_fn=stream_height,.variable_height=true});
        end_virtual_list();end_frame(c);
        if(f==2)virtual_list_scroll_to_row(list,240,300,AlignY::Top);
        if(f==3)before=virtual_list_scroll_offset(list);
        if(f>=4)ok &= slice.first_index>=235 && std::fabs(virtual_list_scroll_offset(list)-before)<1;
    }
    float height=1;
    begin_frame(c,{.viewport_w=400,.viewport_h=200});
    begin_virtual_list({.id=id("invalidate_scroll"),.item_count=256,.height=Sizing::fixed(200),.measure_height_fn=stream_height,.measure_user_data=&height,.variable_height=true});end_virtual_list();end_frame(c);
    height=300;virtual_list_invalidate_heights(id("invalidate_scroll"));virtual_list_scroll_to_row(id("invalidate_scroll"),255,28,AlignY::Top);
    ok &= std::fabs(virtual_list_scroll_offset(id("invalidate_scroll"))-76500)<1;
    begin_frame(c,{.viewport_w=400,.viewport_h=200});
    begin_virtual_list({.id=id("explicit_heights"),.item_count=2,.height=Sizing::fixed(200),.measure_height_fn=stream_height,.variable_height=true});end_virtual_list();end_frame(c);
    virtual_list_invalidate_heights(id("explicit_heights"));virtual_list_set_item_height(id("explicit_heights"),0,420);
    set_virtual_list_scroll_offset(id("explicit_heights"),10000);
    ok &= std::fabs(virtual_list_scroll_offset(id("explicit_heights"))-520)<1;
    begin_frame(c,{.viewport_w=400,.viewport_h=200});begin_virtual_list({.id=id("explicit_heights"),.item_count=2,.height=Sizing::fixed(200),.measure_height_fn=stream_height,.variable_height=true});end_virtual_list();end_frame(c);
    virtual_list_scroll_to_row(id("explicit_heights"),1,28,AlignY::Top);
    ok &= std::fabs(virtual_list_scroll_offset(id("explicit_heights"))-420)<1;

    uint32_t var_scale = 1u;
    const auto var_measure = [](uint32_t row, void* ud) noexcept -> float {
        const uint32_t s = *static_cast<const uint32_t*>(ud);
        return static_cast<float>((20u + (row % 5u) * 10u) * s);
    };
    const uint32_t var_id = id("invalidate_scroll_400");
    begin_frame(c, {.viewport_w = 400, .viewport_h = 200});
    begin_virtual_list({.id = var_id, .item_count = 600, .item_height_px = 20, .height = Sizing::fixed(200),
        .measure_height_fn = var_measure, .measure_user_data = &var_scale, .variable_height = true});
    end_virtual_list();
    end_frame(c);
    var_scale = 2u;
    double expected_y400 = 0.0;
    for (uint32_t r = 0; r < 400u; ++r) expected_y400 += static_cast<double>(var_measure(r, &var_scale));
    virtual_list_invalidate_heights(var_id);
    virtual_list_scroll_to_row(var_id, 400u, 20.0f, AlignY::Top);
    ok &= std::fabs(virtual_list_scroll_offset(var_id) - expected_y400) < 0.5;
    begin_frame(c, {.viewport_w = 400, .viewport_h = 200});
    const auto slice400 = begin_virtual_list({.id = var_id, .item_count = 600, .item_height_px = 20, .height = Sizing::fixed(200),
        .measure_height_fn = var_measure, .measure_user_data = &var_scale, .variable_height = true});
    end_virtual_list();
    end_frame(c);
    ok &= std::fabs(slice400.scroll_y - expected_y400) < 0.5 && slice400.first_index <= 400u && slice400.end_index > 400u;

    const uint32_t grow_id = id("invalidate_grow_400");
    begin_frame(c, {.viewport_w = 400, .viewport_h = 200});
    begin_virtual_list({.id = grow_id, .item_count = 100, .item_height_px = 20, .height = Sizing::fixed(200),
        .measure_height_fn = var_measure, .measure_user_data = &var_scale, .variable_height = true});
    end_virtual_list();
    end_frame(c);
    virtual_list_invalidate_heights(grow_id);
    virtual_list_scroll_to_row(grow_id, 400u, 20.0f, AlignY::Top);
    begin_frame(c, {.viewport_w = 400, .viewport_h = 200});
    const auto grow_slice = begin_virtual_list({.id = grow_id, .item_count = 600, .item_height_px = 20, .height = Sizing::fixed(200),
        .measure_height_fn = var_measure, .measure_user_data = &var_scale, .variable_height = true});
    end_virtual_list();
    end_frame(c);
    ok &= std::fabs(grow_slice.scroll_y - expected_y400) < 0.5 && grow_slice.first_index <= 400u && grow_slice.end_index > 400u;

    destroy_context(c);set_current_context(nullptr);return ok;
}
static bool list_suite() noexcept {
    Context* c=create_context({.headless=true});if(!c)return false;VirtualListSlice slice={};
    for(uint32_t i=0;i<3;++i){begin_frame(c,{.viewport_w=400,.viewport_h=200});slice=begin_virtual_list({.id=id("million"),.item_count=1000000,.item_height_px=20,.height=Sizing::fixed(200),.variable_height=true});end_virtual_list();end_frame(c);if(i==0)virtual_list_scroll_to_row(id("million"),999900,20,AlignY::Top);}
    const bool ok=slice.first_index>999880 && slice.end_index-slice.first_index<20 && frame_stats(c).arena_used_bytes<1024*1024;destroy_context(c);set_current_context(nullptr);return ok && table_viewport_regression() && list_append_regression();
}
static bool source_ok(const unsigned char* p,size_t size,const char* entry) noexcept {
    if(size<2 || p[size-1]!=0 || std::strlen(reinterpret_cast<const char*>(p))!=size-1)return false;
    const char* source=reinterpret_cast<const char*>(p);
    return std::strstr(source,entry) && !std::strstr(source,"SampleLevel") && !std::strstr(source,"StructuredBuffer<") && !std::strstr(source,"register(");
}
static bool v019_regression() noexcept {
    Context* ctx = create_context({.headless = true});
    if (!ctx) return false;
    bool ok = true;
    const auto check = [&](bool condition, int line) noexcept {
        if (!condition) std::fprintf(stderr, "Etch %s regression at line %d\n", ETCH_VERSION_STRING, line);
        ok &= condition;
    };
    const Color red = Color::rgb_hex(0xE92134), green = Color::rgb_hex(0x16C25E);
    const Color blue = Color::rgb_hex(0x231DE6), hover = Color::rgb_hex(0x15DFE0);
    const Str poly = str_lit("M4 18 L4 5 H19");
    const uint16_t open = register_svg_stroke_icon(ctx, poly, {.cap = StrokeCap::Butt, .join = StrokeJoin::Miter});
    const uint16_t ring = register_svg_stroke_icon(ctx, str_lit("M5 5 H19 V19 H5 z"));
    const uint16_t lucide = register_svg_stroke_icon(ctx,
        str_lit("M20 20l-5-5 M17 10a7 7 0 1 1-14 0A7 7 0 1 1 17 10Z M6 11c1-4 2-3 4-2s3 0 4-1"));
    const uint16_t tight = register_svg_stroke_icon(ctx, str_lit("M11 11h2v2h-2Z"),
        {.stroke_width = 4, .join = StrokeJoin::Bevel});
    check(register_svg_stroke_icon(ctx, str_lit("m3 12 q3-8 6 0 t6 0 Q18 4 21 12"),
          {.cap = StrokeCap::Square, .join = StrokeJoin::Miter, .miter_limit = 1}) != kInvalidIcon, __LINE__);
    const uint16_t dot = register_svg_stroke_icon(ctx, str_lit("M12 16h.01"));
    const uint16_t square = register_svg_stroke_icon(ctx, str_lit("M12 16L12 16"), {.cap = StrokeCap::Square});
    check(open != kInvalidIcon && ring != kInvalidIcon && lucide != kInvalidIcon &&
          dot != kInvalidIcon && square != kInvalidIcon && tight != kInvalidIcon, __LINE__);
    check(register_svg_stroke_icon(nullptr, poly) == kInvalidIcon &&
          register_svg_stroke_icon(ctx, {}) == kInvalidIcon &&
          register_svg_stroke_icon(ctx, poly, {.stroke_width = 0}) == kInvalidIcon &&
          register_svg_stroke_icon(ctx, poly, {.stroke_width = NAN}) == kInvalidIcon &&
          register_svg_stroke_icon(ctx, str_lit("M12 16L12 16"), {.cap = StrokeCap::Butt}) == kInvalidIcon &&
          register_svg_stroke_icon(ctx, str_lit("M2 2 L6 6 L")) == kInvalidIcon, __LINE__);

    set_current_context(ctx);
    const bool italic = is_font_loaded(ctx, kFontItalic);
    check(italic == etch_is_font_loaded(reinterpret_cast<EtchContext*>(ctx), ETCH_FONT_ITALIC) &&
          font_metrics(kFontItalic, 16).line_height > 0, __LINE__);
    constexpr uint32_t span_id = id("inline chip"), rich_id = id("rich select");
    char input[] = "@file $skill /cmd \xE7\x95\x8C";
    const auto scene = [&](Vec2 pointer, bool highlights_enabled = true, bool down = false) noexcept {
        begin_frame(ctx, {.viewport_w = 360, .viewport_h = 230, .pointer_pos = pointer, .pointer_down = down});
        begin_box({.axis = Axis::Col, .width = Sizing::grow(), .height = Sizing::grow(), .gap = 4,
                   .background = Color::white()});
        begin_box({.axis = Axis::Row, .height = Sizing::fixed(48), .gap = 4});
        for (uint16_t value : {open, ring, lucide, dot, square, tight})
            icon({.icon_id = value, .size_px = 48, .color = red});
        end_box();
        begin_box({.axis = Axis::Row, .height = Sizing::fixed(36), .gap = 4});
        const Padding widths[] = {{3,0,0,0}, {0,3,0,0}, {0,0,3,0}, {0,0,0,3}};
        for (Padding side : widths) {
            begin_box({.width = Sizing::fixed(48), .height = Sizing::fixed(36),
                       .border_color = blue, .radius = Corners::all(5), .clip_children = true,
                       .border_sides = side});
            box({.width = Sizing::grow(), .height = Sizing::grow(), .background = Color::white()});
            end_box();
        }
        box({.width = Sizing::fixed(64), .height = Sizing::fixed(36), .border_color = blue,
             .border_width = 3, .border_dash_px = 5, .border_gap_px = 4});
        box({.width = Sizing::fixed(64), .height = Sizing::fixed(36), .border_color = green,
             .border_width = 3, .radius = Corners::all(9), .border_dash_px = 8, .border_gap_px = 5});
        end_box();

        begin_box({.width = Sizing::fixed(94), .height = Sizing::fixed(56)});
        text_spans({{.background = blue, .padding_x = 2, .id = span_id, .icon_id = dot,
                     .icon_color = green, .icon_size_px = 20, .padding_y = 3, .hover_background = hover},
                    {.text = str_lit("queue.ts "), .font_id = kFontItalic, .icon_id = open, .icon_size_px = 16},
                    {.text = str_lit("next \xC3\xA9 token")}},
                   {.id = rich_id, .selectable = true});
        end_box();

        TextInputHighlight ranges[] = {
            {.byte_offset = 0, .byte_length = 5, .text_color = red, .background = green},
            {.byte_offset = 6, .byte_length = 6, .background = blue},
            {.byte_offset = 13, .byte_length = 4, .border_color = red},
            {.byte_offset = 19, .byte_length = UINT32_MAX, .background = green},
            {.byte_offset = 500, .byte_length = UINT32_MAX, .background = hover},
        };
        begin_box({.axis = Axis::Row, .height = Sizing::fixed(64), .gap = 4});
        const TextInputDesc field = {.buffer = input, .capacity = sizeof(input), .width = Sizing::fixed(238),
            .height = Sizing::fixed(32), .padding = Padding::xy(4, 4), .font_size_px = 15, .font_id = kFontMono,
            .text_color = Color::rgb_hex(0x111111), .background = Color::white(), .border_color = {},
            .border_width = 0, .radius = {}, .read_only = true,
            .highlights = highlights_enabled ? std::span<const TextInputHighlight>(ranges) : std::span<const TextInputHighlight>{}};
        auto single = text_input(field);
        TextInputDesc multi = field;
        multi.width = Sizing::fixed(66);
        multi.height = Sizing::fixed(64);
        multi.multiline = multi.word_wrap = true;
        auto wrapped = text_input(multi);
        for (auto& range : ranges) range = {}; // Rendering must use the frame-owned copy.
        end_box();
        end_box();
        end_frame(ctx);
        check(single.byte_length == sizeof(input) - 1 && wrapped.cursor_byte == single.cursor_byte, __LINE__);
    };
    scene({-1,-1}, false);
    Vec4 glyph_bounds[256]{};
    uint32_t glyph_count = 0;
    for (const Prim& p : frame_prims(ctx)) {
        if ((p.style.x & 15) == uint32_t(PrimKind::SlugGlyph) && glyph_count < 256)
            glyph_bounds[glyph_count++] = p.bounds;
    }
    scene({8,101});
    scene({8,101});
    check(hovered(span_id), __LINE__);
    set_text_selection(rich_id, 0, 22);
    const Str copy = selected_text(rich_id);
    check(copy.length == 22 && std::memcmp(copy.chars, "queue.ts next \xC3\xA9 token", 22) == 0, __LINE__);
    uint32_t glyph_index = 0, highlighted = 0, wrapped_pills = 0;
    bool hover_pill = false, corner_arc = false;
    float first_rich_y = -1.0f;
    bool wrapped_rich = false;
    for (const Prim& p : frame_prims(ctx)) {
        const auto kind = static_cast<PrimKind>(p.style.x & 15);
        if (kind == PrimKind::SlugGlyph) {
            check(glyph_index < glyph_count && std::memcmp(&p.bounds, &glyph_bounds[glyph_index], sizeof(Vec4)) == 0, __LINE__);
            ++glyph_index;
            if (p.bounds.y >= 92 && p.bounds.y < 148) {
                if (first_rich_y < 0) first_rich_y = p.bounds.y;
                wrapped_rich |= p.bounds.y > first_rich_y + 21.0f;
            }
            if (p.bounds.y > 150 && p.style.y == red.pack_rgba8()) ++highlighted;
        }
        if (kind == PrimKind::Rect && p.bounds.x >= 242 && p.bounds.y > 150 &&
            (p.style.y == green.pack_rgba8() || p.style.y == blue.pack_rgba8())) ++wrapped_pills;
        if (kind == PrimKind::Rect && p.style.y == hover.pack_rgba8()) {
            check(p.bounds.y < 140 && p.bounds.z >= 24 && p.bounds.z < 25 && p.bounds.w > 22, __LINE__);
            hover_pill = true;
        }
        corner_arc |= kind == PrimKind::RingArc;
    }
    check(glyph_index == glyph_count && highlighted >= 8 && wrapped_pills >= 3 && hover_pill &&
          corner_arc && wrapped_rich, __LINE__);

    {
        const char* file = "etch-v019-pixels.png";
        const bool rendered = render_headless_png(ctx, 360, 230, file, Color::white());
        SDL_Surface* pixels = rendered ? SDL_LoadPNG(file) : nullptr;
        check(pixels && frame_stats(ctx).draw_call_count == 1u, __LINE__);
        if (pixels) {
            const auto pixel = [&](int x, int y, Color expected) noexcept {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                const bool match = SDL_ReadSurfacePixel(pixels, x, y, &r, &g, &b, &a) &&
                    std::abs(int(r) - expected.r) < 12 && std::abs(int(g) - expected.g) < 12 &&
                    std::abs(int(b) - expected.b) < 12;
                if (!match) std::fprintf(stderr, "Etch %s pixel (%d,%d) = %u,%u,%u\n", ETCH_VERSION_STRING, x, y, r, g, b);
                return match;
            };
            check(pixel(8, 20, red) && pixel(8, 3, Color::white()), __LINE__);
            check(pixel(62, 24, red) && pixel(76, 24, Color::white()), __LINE__);
            check(pixel(134, 30, red) && pixel(180, 32, red) && pixel(232, 32, red) &&
                  pixel(284, 24, red), __LINE__);
            check(pixel(1, 70, blue) && pixel(98, 70, blue) && pixel(128, 53, blue) &&
                  pixel(180, 86, blue) && pixel(0, 52, Color::white()), __LINE__);
            check(pixel(210, 53, blue) && pixel(214, 53, Color::white()) && pixel(218, 53, blue), __LINE__);
            check(pixel(6, 103, hover) && pixel(22, 174, green), __LINE__);
            SDL_DestroySurface(pixels);
        }
        std::remove(file);
    }
    clear_text_selection(rich_id);
    scene({29, 106});
    scene({29, 106}, true, true);
    scene({90, 139}, true, true);
    scene({90, 139});
    const Str dragged = selected_text(rich_id);
    check(dragged.length == 22 && std::memcmp(dragged.chars, "queue.ts next \xC3\xA9 token", 22) == 0, __LINE__);
    const uint32_t field_id = id("scrolled highlight");
    char long_input[] = "before before before @file";
    const TextInputHighlight trailing = {.byte_offset = 21, .byte_length = 5, .background = green};
    const auto scrolled_field = [&]() noexcept {
        begin_frame(ctx, {.viewport_w = 150, .viewport_h = 60});
        const auto result = text_input({.id = field_id, .buffer = long_input, .capacity = sizeof(long_input),
            .width = Sizing::fixed(80), .height = Sizing::fixed(30), .font_size_px = 15,
            .font_id = kFontMono, .highlights = {&trailing, 1u}});
        end_frame(ctx);
        return result;
    };
    (void)scrolled_field();
    request_focus(field_id);
    const auto scrolled = scrolled_field();
    bool trailing_visible = false;
    for (const Prim& p : frame_prims(ctx))
        trailing_visible |= p.style.y == green.pack_rgba8() && p.bounds.x < 80 && p.bounds.x + p.bounds.z > 30;
    check(scrolled.focused && scrolled.cursor_byte == sizeof(long_input) - 1u && trailing_visible, __LINE__);

    destroy_context(ctx);
    set_current_context(nullptr);
    return ok;
}

static bool gpu_suite() noexcept {
    const bool media_ok=v019_regression() && verify_v021_gpu() && verify_v022_gpu() && verify_workbench_materials_gpu() && verify_shader_surface_gpu() && verify_texture_regions_gpu() && verify_mesh_gpu() && verify_punch_gpu() && verify_beam_fade_gpu() && verify_media_emoji_checks() && verify_sprite_recycling_gpu() && verify_border_coverage() && verify_visual_quality_gpu() && verify_closure_gpu();
    const bool v070_ok=verify_theme_wrap_checks();
    static_assert(sizeof(Prim)==64);
    bool ok=source_ok(g_etch_uber_vs_msl,sizeof(g_etch_uber_vs_msl),"vertex vs_main_out vs_main(") && source_ok(g_etch_uber_ps_msl,sizeof(g_etch_uber_ps_msl),"fragment ps_main_out ps_main(") && source_ok(g_etch_kawase_vs_msl,sizeof(g_etch_kawase_vs_msl),"vertex vs_main_out vs_main(") && source_ok(g_etch_kawase_ps_msl,sizeof(g_etch_kawase_ps_msl),"fragment ps_main_out ps_main(");
    const char* vs=reinterpret_cast<const char*>(g_etch_uber_vs_msl);const char* ps=reinterpret_cast<const char*>(g_etch_uber_ps_msl);const char* ks=reinterpret_cast<const char*>(g_etch_kawase_ps_msl);
    ok &= std::strstr(vs,"GlobalsVS [[buffer(0)]]") && std::strstr(vs,"g_prims [[buffer(1)]]") && std::strstr(ks,"KawaseFS [[buffer(0)]]") && std::strstr(ks,"g_src_tex [[texture(0)]]") && std::strstr(ks,"g_src_texSmplr [[sampler(0)]]");
    for(uint32_t i=0;i<9;++i){char slot[80];std::snprintf(slot,sizeof(slot),"g_tex%u [[texture(%u)]]",i,i);ok &= std::strstr(ps,slot)!=nullptr;std::snprintf(slot,sizeof(slot),"g_tex%uSmplr [[sampler(%u)]]",i,i);ok &= std::strstr(ps,slot)!=nullptr;}
    const char* buffers[]={"g_slug_curves [[buffer(0)]]","g_slug_bands [[buffer(1)]]","g_dyn_slug_curves [[buffer(2)]]","g_dyn_slug_bands [[buffer(3)]]","g_clip_shapes [[buffer(4)]]"};for(const char* b:buffers)ok &= std::strstr(ps,b)!=nullptr;
    Context* bad=create_context({.headless=true,.gpu_driver="nonexistent_driver_xyz"});
    if(!bad)return false;
    ok &= gpu_device(bad)==nullptr;
    destroy_context(bad);set_current_context(nullptr);
    Context* vk=create_context({.headless=true,.gpu_driver="vulkan"});
    if(!vk)return false;
    if(SDL_GPUDevice* vdev=gpu_device(vk);vdev!=nullptr)ok &= std::strcmp(SDL_GetGPUDeviceDriver(vdev),"vulkan")==0;
    destroy_context(vk);set_current_context(nullptr);
    Context* c=create_context({.headless=true});if(!c)return false;
    ok &= gpu_device(c)!=nullptr;
    for(uint32_t i=0;i<4;++i){begin_frame(c,{.viewport_w=320,.viewport_h=200});button({.id=id("gpu"),.label=str_lit("GPU")});end_frame(c);}
    ok &= frame_stats(c).idle_sleeping && render_headless_png(c,320,200,"etch_ctest_gpu.png") && frame_stats(c).draw_call_count<=3;
    ok &= media_ok && v070_ok;
    std::remove("etch_ctest_gpu.png");destroy_context(c);set_current_context(nullptr);return ok;
}
int main(int argc,char** argv) {
    if(argc==2 && std::strcmp(argv[1],"--suite=v019")==0) return v019_regression() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=v021")==0) return verify_v021_gpu() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=v022")==0) return verify_v022_gpu() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=mesh_grad")==0) return verify_mesh_gpu() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=punch_hole")==0) return verify_punch_gpu() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=workbench_materials")==0) return verify_workbench_materials_gpu() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=snap_sheet")==0) return verify_snap_sheet() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=shader_surface")==0) return verify_shader_surface_gpu() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=beam_fade")==0) return verify_beam_fade_gpu() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=texture_regions")==0) return verify_texture_regions_gpu() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=motion_layers_presence")==0) return verify_motion_checks() ? 0 : 1;
    if(argc==2 && std::strcmp(argv[1],"--suite=closure_gpu")==0) return verify_closure_gpu() ? 0 : 1;
    const char* names[]={"arena_and_elasticity","unicode_bidi_grapheme","layout_clay_flex_grid","fenwick_virtual_list_1m","widgets_and_dockspace","text_buffer_and_code_editor","a11y_semantic_tree","headless_gpu_invariants"};
    const char* selected=argc==2 && std::strncmp(argv[1],"--suite=",8)==0?argv[1]+8:nullptr;
    if(argc>1 && !selected) { return 2; } bool found=false,ok=true;
    for(uint32_t i=0;i<8;++i)if(!selected || std::strcmp(selected,names[i])==0){found=true;bool pass=false;
        switch(i){case 0:pass=arena_suite();break;case 1:pass=unicode_suite();break;case 2:pass=perceptual_color_checks::verify() && layout_suite() && verify_desktop_precision() && verify_visual_quality_cpu() && verify_workbench_checks() && verify_closure_controls();break;case 3:pass=list_suite();break;case 4:pass=verify_desktop_checks(nullptr);break;case 5:pass=verify_editor_checks(nullptr);break;case 6:pass=verify_a11y_checks(nullptr) && verify_os_window_a11y_checks();break;case 7:pass=gpu_suite();break;}
        std::printf("%s: %s\n",names[i],pass?"PASS":"FAIL");ok &= pass;
    }return !found?2:ok?0:1;
}
