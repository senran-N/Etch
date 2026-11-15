// Gap-indexed document storage and viewport editor. Compiled in etch_unity.cpp.
namespace etch {
struct TextBufferEdit {
    uint32_t start = 0, removed = 0, inserted = 0, blob = 0;
    uint32_t before_cursor = 0, before_anchor = 0, after_cursor = 0, after_anchor = 0;
    double timestamp = 0; uint8_t group = 0;
};
namespace {
uint32_t tb_len(const TextBuffer* b) noexcept { return b->cap - (b->gap_end - b->gap_start); }
uint32_t tb_lines(const TextBuffer* b) noexcept { return b->line_cap - (b->line_gap_end - b->line_gap_start); }
uint32_t tb_start(const TextBuffer* b, uint32_t i) noexcept {
    return i < b->line_gap_start ? b->line_data[i] : tb_len(b) - b->line_data[i + (b->line_gap_end - b->line_gap_start)];
}
uint32_t tb_clamp(const TextBuffer* b, uint32_t p) noexcept {
    p = p < tb_len(b) ? p : tb_len(b);
    if(p < b->gap_start) return utf8_clamp_boundary(b->data,b->gap_start,p);
    return b->gap_start + utf8_clamp_boundary(b->data+b->gap_end,b->cap-b->gap_end,p-b->gap_start);
}
void tb_copy(const TextBuffer* b, uint32_t a, uint32_t z, char* out) noexcept {
    const uint32_t split = a < b->gap_start ? (z < b->gap_start ? z : b->gap_start) : a;
    if(split>a) std::memcpy(out,b->data+a,split-a);
    const uint32_t gap = b->gap_end - b->gap_start;
    if(z>split) std::memcpy(out+(split-a),b->data+(split+gap),z-split);
}
bool tb_reserve(TextBuffer* b, uint32_t bytes, uint32_t lines) noexcept {
    if(bytes > b->cap) {
        const uint32_t cap = bytes > b->cap*2 ? bytes : b->cap*2;
        char* p = static_cast<char*>(std::malloc(cap)); if(!p) return false;
        std::memcpy(p,b->data,b->gap_start);
        const uint32_t tail=b->cap-b->gap_end;
        std::memcpy(p+(cap-tail),b->data+b->gap_end,tail);
        std::free(b->data); b->data=p; b->gap_end=cap-tail; b->cap=cap;
    }
    if(lines > b->line_cap) {
        const uint32_t cap = lines > b->line_cap*2 ? lines : b->line_cap*2;
        auto* p=static_cast<uint32_t*>(std::malloc(size_t(cap)*sizeof(uint32_t))); if(!p) return false;
        std::memcpy(p,b->line_data,size_t(b->line_gap_start)*sizeof(uint32_t));
        const uint32_t tail=b->line_cap-b->line_gap_end;
        std::memcpy(p+(cap-tail),b->line_data+b->line_gap_end,size_t(tail)*sizeof(uint32_t));
        std::free(b->line_data); b->line_data=p; b->line_gap_end=cap-tail; b->line_cap=cap;
    }
    return true;
}
void tb_move(TextBuffer* b, uint32_t p) noexcept {
    const uint32_t len=tb_len(b);
    if(p < b->gap_start) {
        while(b->line_gap_start>1 && b->line_data[b->line_gap_start-1]>p) {
            const uint32_t v=b->line_data[--b->line_gap_start]; b->line_data[--b->line_gap_end]=len-v;
        }
        const uint32_t n=b->gap_start-p;
        std::memmove(b->data+(b->gap_end-n),b->data+p,n); b->gap_end-=n;
    } else {
        while(b->line_gap_end<b->line_cap && len-b->line_data[b->line_gap_end]<=p) {
            const uint32_t v=len-b->line_data[b->line_gap_end++]; b->line_data[b->line_gap_start++]=v;
        }
        const uint32_t n=p-b->gap_start;
        std::memmove(b->data+b->gap_start,b->data+b->gap_end,n); b->gap_end+=n;
    }
    b->gap_start=p;
}
void tb_drop_oldest(TextBuffer* b) noexcept {
    const uint32_t n=b->undo_ops[0].removed+b->undo_ops[0].inserted;
    std::memmove(b->undo_blob,b->undo_blob+n,b->undo_blob_used-n); b->undo_blob_used-=n;
    --b->undo_count; if(b->undo_cursor) --b->undo_cursor;
    std::memmove(b->undo_ops,b->undo_ops+1,size_t(b->undo_count)*sizeof(TextBufferEdit));
    for(uint32_t i=0;i<b->undo_count;++i) b->undo_ops[i].blob-=n;
}
}
uint32_t text_buffer_byte_length(const TextBuffer* b) noexcept { return b && b->data ? tb_len(b) : 0; }
uint32_t text_buffer_line_count(const TextBuffer* b) noexcept { return b && b->data ? tb_lines(b) : 0; }
void free_text_buffer(TextBuffer* b) noexcept {
    if(!b) return;
    std::free(b->data); std::free(b->line_data); std::free(b->undo_ops); std::free(b->undo_blob); *b={};
}
void text_buffer_clear_undo(TextBuffer* b) noexcept { if(b) b->undo_count=b->undo_cursor=b->undo_blob_used=0; }
bool init_text_buffer(TextBuffer* b, Str s, uint32_t cap) noexcept {
    if(!b || b->data || (!s.chars && s.length) || s.length>0x7FFFFFFFu) return false;
    TextBuffer v={}; v.cap=cap>4096?cap:4096; v.gap_end=v.cap;
    v.line_cap=64; v.line_gap_end=64; v.undo_cap=1024; v.undo_blob_cap=4u*1024u*1024u;
    v.data=static_cast<char*>(std::malloc(v.cap));
    v.line_data=static_cast<uint32_t*>(std::malloc(size_t(v.line_cap)*sizeof(uint32_t)));
    v.undo_ops=static_cast<TextBufferEdit*>(std::calloc(v.undo_cap,sizeof(TextBufferEdit)));
    v.undo_blob=static_cast<char*>(std::malloc(v.undo_blob_cap));
    if(!v.data || !v.line_data || !v.undo_ops || !v.undo_blob) { free_text_buffer(&v); return false; }
    v.line_data[0]=0; v.line_gap_start=1; *b=v;
    if(!text_buffer_replace(b,0,0,s,false)) {free_text_buffer(b);return false;} return true;
}
bool text_buffer_set(TextBuffer* b, Str s) noexcept {
    if(!b || !b->data) return false;
    if(!text_buffer_replace(b,0,tb_len(b),s,false)) return false;
    text_buffer_clear_undo(b); return true;
}
uint32_t text_buffer_line_start(const TextBuffer* b, uint32_t i) noexcept {
    return b && b->data ? tb_start(b,i<tb_lines(b)?i:tb_lines(b)-1) : 0;
}
uint32_t text_buffer_line_end(const TextBuffer* b, uint32_t i) noexcept {
    if(!b || !b->data) return 0;
    i=i<tb_lines(b)?i:tb_lines(b)-1;
    return i+1<tb_lines(b)?tb_start(b,i+1)-1:tb_len(b);
}
uint32_t text_buffer_copy_range(const TextBuffer* b,uint32_t a,uint32_t z,char* out,uint32_t cap) noexcept {
    if(!b || !b->data || !out || !cap) return 0;
    a=tb_clamp(b,a);z=tb_clamp(b,z);if(z<a)z=a;
    if(z-a>cap)z=tb_clamp(b,a+cap);
    tb_copy(b,a,z,out);return z-a;
}
Str text_buffer_line(const TextBuffer* b,uint32_t i,Arena* scratch) noexcept {
    if(!b || !b->data || i>=tb_lines(b))return {};
    const uint32_t a=tb_start(b,i),z=text_buffer_line_end(b,i);
    if(z<=b->gap_start)return {b->data+a,z-a};
    const uint32_t gap = b->gap_end - b->gap_start;
    if(a>=b->gap_start)return {b->data+(a+gap),z-a};
    if(!scratch) {auto* ctx=current_context(); if(!ctx)return {};scratch=&ctx->frame_arena;}
    char* p=scratch->alloc<char>(size_t(z-a)+1);if(!p)return {};
    tb_copy(b,a,z,p);p[z-a]=0;return {p,z-a};
}
TextLineCol text_buffer_line_col(const TextBuffer* b,uint32_t p) noexcept {
    if(!b || !b->data)return {};p=tb_clamp(b,p);
    uint32_t lo=0,hi=tb_lines(b);
    while(lo+1<hi){const uint32_t m=lo+(hi-lo)/2;if(tb_start(b,m)<=p)lo=m;else hi=m;}
    return {lo,p-tb_start(b,lo)};
}
uint32_t text_buffer_byte_offset(const TextBuffer* b,uint32_t i,uint32_t col) noexcept {
    if(!b || !b->data)return 0;i=i<tb_lines(b)?i:tb_lines(b)-1;
    const uint32_t a=tb_start(b,i),n=text_buffer_line_end(b,i)-a;return tb_clamp(b,a+(col<n?col:n));
}
namespace {
bool tb_replace_impl(TextBuffer* b,uint32_t a,uint32_t z,Str s,bool normalize_crlf,bool record,uint32_t bc,uint32_t ba,uint32_t ac,uint32_t aa,uint8_t group,double time) noexcept {
    bc=tb_clamp(b,bc);ba=tb_clamp(b,ba);
    a=tb_clamp(b,a);z=tb_clamp(b,z);if(z<a){const uint32_t t=a;a=z;z=t;}
    // Aliased slices must be detached before moving or growing the document gap.
    char* alias=nullptr;
    const uintptr_t addr=reinterpret_cast<uintptr_t>(s.chars),base=reinterpret_cast<uintptr_t>(b->data);
    if(s.length && addr>=base && addr-base<b->cap){alias=static_cast<char*>(std::malloc(s.length));if(!alias)return false;std::memcpy(alias,s.chars,s.length);s.chars=alias;}
    uint32_t n=0,nl=0;
    for(uint32_t i=0;i<s.length;++i){if(normalize_crlf && s.chars[i]=='\r' && i+1<s.length && s.chars[i+1]=='\n')continue;++n;if(s.chars[i]=='\n')++nl;}
    if(!tb_reserve(b,tb_len(b)-(z-a)+n+64,tb_lines(b)+nl+1)){std::free(alias);return false;}
    tb_move(b,a);
    if(group==1 && (!is_single_nonbreak_utf8_codepoint(s) || utf8_char_class(utf8_decode_at(s.chars,s.length,0))==0))group=0;
    TextBufferEdit* edit=nullptr;
    if(record){
        if(b->undo_cursor<b->undo_count){b->undo_blob_used=b->undo_ops[b->undo_cursor].blob;b->undo_count=b->undo_cursor;}
        const uint32_t need=z-a+n;
        if(need<=b->undo_blob_cap){
            while(b->undo_count && (b->undo_count==b->undo_cap || need>b->undo_blob_cap-b->undo_blob_used))tb_drop_oldest(b);
            edit=&b->undo_ops[b->undo_count++];*edit={a,z-a,n,b->undo_blob_used,bc,ba,ac,aa,time,group};
            tb_copy(b,a,z,b->undo_blob+b->undo_blob_used);b->undo_blob_used+=z-a;
        }else text_buffer_clear_undo(b);
    }
    const uint32_t oldlen=tb_len(b);
    while(b->line_gap_end<b->line_cap && oldlen-b->line_data[b->line_gap_end]<=z)++b->line_gap_end;
    b->gap_end+=z-a;
    for(uint32_t i=0;i<s.length;++i){const char c=s.chars[i];if(normalize_crlf && c=='\r' && i+1<s.length && s.chars[i+1]=='\n')continue;b->data[b->gap_start++]=c;if(c=='\n')b->line_data[b->line_gap_start++]=b->gap_start;}
    if(edit){
        edit->after_cursor=tb_clamp(b,ac);edit->after_anchor=tb_clamp(b,aa);
        std::memcpy(b->undo_blob+b->undo_blob_used,b->data+a,n);b->undo_blob_used+=n;
        if(b->undo_count>1 && group==1 && z==a && is_single_nonbreak_utf8_codepoint({b->data+a,n}) && utf8_char_class(utf8_decode_at(b->data+a,n,0))!=0){
            auto& prev=b->undo_ops[b->undo_count-2];
            if(prev.group==1 && !prev.removed && prev.start+prev.inserted==a && time>=prev.timestamp && time-prev.timestamp<0.75){prev.inserted+=n;prev.after_cursor=edit->after_cursor;prev.after_anchor=edit->after_anchor;prev.timestamp=time;--b->undo_count;}
        }
        b->undo_cursor=b->undo_count;
    }
    ++b->version;std::free(alias);return true;
}
}
bool text_buffer_replace(TextBuffer* b,uint32_t a,uint32_t z,Str s,bool record,uint32_t bc,uint32_t ba,uint32_t ac,uint32_t aa,uint8_t group,double time) noexcept {
    if(!b || !b->data || (!s.chars && s.length) || s.length>0x7FFFFFFFu)return false;
    return tb_replace_impl(b,a,z,s,true,record,bc,ba,ac,aa,group,time);
}
bool text_buffer_undo(TextBuffer* b,uint32_t* c,uint32_t* a) noexcept {
    if(!b || !b->data || !b->undo_cursor)return false;const auto e=b->undo_ops[b->undo_cursor-1];
    if(!tb_replace_impl(b,e.start,e.start+e.inserted,{b->undo_blob+e.blob,e.removed},false,false,0,0,0,0,0,0))return false;
    --b->undo_cursor;if(c)*c=e.before_cursor;if(a)*a=e.before_anchor;return true;
}
bool text_buffer_redo(TextBuffer* b,uint32_t* c,uint32_t* a) noexcept {
    if(!b || !b->data || b->undo_cursor==b->undo_count)return false;const auto e=b->undo_ops[b->undo_cursor];
    if(!tb_replace_impl(b,e.start,e.start+e.removed,{b->undo_blob+(e.blob+e.removed),e.inserted},false,false,0,0,0,0,0,0))return false;
    ++b->undo_cursor;if(c)*c=e.after_cursor;if(a)*a=e.after_anchor;return true;
}
uint32_t tokenize_cpp_hlsl(uint32_t,Str s,SyntaxToken* out,uint32_t cap,void*) noexcept {
    if(!s.chars || !out)return 0;uint32_t count=0,p=0;
    while(p<s.length && count<cap){const uint32_t a=p;Color color=Color::rgb_hex(0xCBD5E1);const char c=s.chars[p++];
        if(c=='/' && p<s.length && s.chars[p]=='/'){p=s.length;color=Color::rgb_hex(0x64748B);}
        else if(c=='/' && p<s.length && s.chars[p]=='*'){++p;while(p<s.length){if(s.chars[p++]=='*' && p<s.length && s.chars[p]=='/'){++p;break;}}color=Color::rgb_hex(0x64748B);}
        else if(c=='"' || c=='\''){while(p<s.length){const char v=s.chars[p++];if(v=='\\' && p<s.length)++p;else if(v==c)break;}color=Color::rgb_hex(0xA3BE8C);}
        else if(c>='0' && c<='9'){while(p<s.length && ((s.chars[p]>='0' && s.chars[p]<='9') || s.chars[p]=='.' || s.chars[p]=='f' || s.chars[p]=='x' || (s.chars[p]>='A' && s.chars[p]<='F')))++p;color=Color::rgb_hex(0xB48EAD);}
        else if((c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' || c=='#'){
            while(p<s.length && ((s.chars[p]>='a' && s.chars[p]<='z') || (s.chars[p]>='A' && s.chars[p]<='Z') || (s.chars[p]>='0' && s.chars[p]<='9') || s.chars[p]=='_'))++p;
            static constexpr const char* words[]={"float","float2","float3","float4","uint","int","bool","void","return","if","else","for","while","struct","cbuffer","static","const","constexpr","noexcept","Texture2D","SamplerState","StructuredBuffer","discard"};
            for(const char* w:words)if(std::strlen(w)==p-a && !std::memcmp(w,s.chars+a,p-a)){color=Color::rgb_hex(0x81A1C1);break;}
            if(c=='#')color=Color::rgb_hex(0xB48EAD);
        }
        out[count++]={a,p,color,kFontMono};
    }return count;
}
struct EditorLine { Str text = {}; uint32_t start = 0, index = 0; };
struct CodeEditorExtra {
    CodeEditorDesc desc = {}; EditorLine* lines = nullptr; uint32_t count = 0;
    uint32_t cursor = 0, anchor = 0, active_line = 0; bool focused = false;
    double scroll = 0, total_h = 0; float scroll_x = 0, stride = 20, gutter = 60;
    Str composition = {}; uint32_t comp_cursor = 0, comp_selection = 0;
};
namespace {
float editor_x(Context* ctx,const CodeEditorDesc& d,Str s,uint32_t p) noexcept {
    float x=0,unused=0,total=0;
    measure_line_segment_x(ctx,d.font_id,s.chars?s.chars:"",s.length,p,0,d.font_size_px,0,false,true,&x,&unused,&total,d.tab_width);return x;
}
uint32_t editor_hit(Context* ctx,const CodeEditorDesc& d,Str s,float x) noexcept {
    if (!s.chars || !s.length || x <= 0.0f) return 0u;
    float pen_x = 0.0f;
    const uint16_t slot_idx = (ctx && ctx->atlas.font_valid) ? resolve_font_slot_idx(&ctx->atlas, d.font_id) : kInvalidFont;
    if (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont) {
        const float cp_w = d.font_size_px * 0.55f;
        uint32_t p = 0u;
        while (p < s.length) {
            const uint32_t next = utf8_next_grapheme(s.chars, s.length, p);
            const char* cp_p = s.chars + p;
            const char* end = s.chars + next;
            float grp_adv = 0.0f;
            while (cp_p < end) {
                const uint32_t cp = next_utf8_codepoint(cp_p, end);
                grp_adv += cp == '\t' ? float(d.tab_width) * cp_w : cp_w;
            }
            if (x < pen_x + grp_adv * 0.5f) return p;
            pen_x += grp_adv;
            p = next;
        }
        return s.length;
    }

    const FontSlot& primary = ctx->atlas.slots[slot_idx];
    uint32_t pos = 0u;
    while (pos < s.length) {
        if (s.chars[pos] == '\t') {
            const float adv = float(d.tab_width) * primary.space_advance_em * d.font_size_px;
            if (x < pen_x + 0.5f * adv) return pos;
            pen_x += adv;
            ++pos;
            continue;
        }
        uint32_t run_end = pos;
        while (run_end < s.length && s.chars[run_end] != '\t') ++run_end;
        const uint32_t run_len = run_end - pos;
        bool is_rtl = false;
        const size_t run_mark = ctx->frame_arena.mark();
        const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, slot_idx, s.chars + pos, run_len, &is_rtl);
        const uint32_t gcount = static_cast<uint32_t>(glyphs.size());
        uint32_t gi = 0u;
        while (gi < gcount) {
            const uint32_t c0 = glyphs[gi].cluster;
            const uint32_t c1 = glyphs[gi].cluster_end;
            uint32_t gi_end = gi + 1u;
            while (gi_end < gcount && glyphs[gi_end].cluster == c0) ++gi_end;
            float cluster_adv = 0.0f;
            for (uint32_t k = gi; k < gi_end; ++k) cluster_adv += glyphs[k].x_advance_1px * d.font_size_px;
            is_rtl = glyphs[gi].is_rtl != 0;
            if (x < pen_x + cluster_adv && c1 > c0) {
                const uint32_t start = pos + c0, end = pos + c1;
                uint32_t n_grp = 0u;
                for (uint32_t p = start; p < end; p = utf8_next_grapheme(s.chars, end, p)) ++n_grp;
                // Map visual midpoints to logical grapheme boundaries without splitting marks.
                uint32_t target = 0u;
                const float rel_x = x - pen_x;
                while (target < n_grp && rel_x >= ((float(target) + 0.5f) / float(n_grp)) * cluster_adv) ++target;
                if (is_rtl) target = n_grp - target;
                uint32_t hit = start;
                for (uint32_t k = 0u; k < target; ++k) hit = utf8_next_grapheme(s.chars, end, hit);
                ctx->frame_arena.rewind(run_mark);
                return hit;
            }
            pen_x += cluster_adv;
            gi = gi_end;
        }
        ctx->frame_arena.rewind(run_mark);
        pos = run_end;
    }
    return s.length;
}
Str editor_slice(Context* ctx,const TextBuffer* b,uint32_t a,uint32_t z) noexcept {
    char* p=ctx->frame_arena.alloc<char>(size_t(z-a)+1);if(!p)return {};tb_copy(b,a,z,p);p[z-a]=0;return {p,z-a};
}
float editor_gutter(const CodeEditorDesc& d,uint32_t lines) noexcept {
    if(!d.show_line_numbers)return 0;if(d.gutter_width_px>0)return d.gutter_width_px;
    uint32_t digits=1;while(lines>=10){lines/=10;++digits;}return float(digits)*d.font_size_px*.65f+20;
}
}
void code_editor_set_cursor(uint32_t id,uint32_t a,uint32_t c,bool reveal) noexcept {
    auto* ctx=current_context();if(!ctx || !id)return;
    auto* ws=state_map_get_or_insert(&ctx->active_ui->states,id,ctx->active_ui->frame_index);if(!ws)return;
    const bool valid_backing=ws->edit_text_buffer && ws->sel_text_epoch==ctx->arena_epoch;
    ws->edit_initialized=true;ws->edit_cursor=valid_backing?tb_clamp(ws->edit_text_buffer,c):c;
    ws->edit_sel_anchor=valid_backing?tb_clamp(ws->edit_text_buffer,a):a;
    ws->editor_reveal=reveal;ws->edit_preferred_x=-1;request_redraw(ctx);
}
void code_editor_scroll_to_line(uint32_t id,uint32_t line,AlignY align) noexcept {
    auto* ctx=current_context();if(!ctx || !id)return;
    auto* ws=state_map_find(&ctx->active_ui->states,id);if(!ws)return;
    double y=double(line)*ws->vlist_stride;
    if(align==AlignY::Center)y-=(double(ws->viewport_dim.y)-ws->vlist_stride)*.5;
    if(align==AlignY::Bottom)y-=double(ws->viewport_dim.y)-ws->vlist_stride;
    ws->vscroll_pos_y=clamp_double(y,0,ws->vscroll_max_y);ws->vscroll_vel_y=0;request_redraw(ctx);
}
CodeEditorResult code_editor(const CodeEditorDesc& input) noexcept {
    auto* ctx=current_context();if(!ctx || !ctx->in_frame || !input.id || !input.buffer || !input.buffer->data)return {};
    CodeEditorDesc d=input;d.tab_width=d.tab_width?d.tab_width:4;d.tab_width=d.tab_width>32?32:d.tab_width;
    d.font_size_px=d.font_size_px>0?d.font_size_px:14;
    const float stride=d.line_height_px>0?d.line_height_px:d.font_size_px*1.5f;
    auto& ui=*ctx->active_ui;auto* ws=state_map_get_or_insert(&ui.states,d.id,ui.frame_index);if(!ws)return {};
    ws->is_user_element=true;ws->is_focusable=true;ws->focus_scope=ui.modal_scope && focus_scope_contains(ui,ui.modal_scope)?ui.modal_scope:ui.focus_scope;
    ws->is_virtual_list=true;ws->is_scroll_y=true;ws->is_text_input_writable=!d.read_only;ws->edit_text_buffer=d.buffer;ws->editor_show_scrollbar=d.show_scrollbar;
    ws->sel_text_epoch=ctx->arena_epoch;ws->sel_text_ptr=nullptr;ws->sel_text_len=0;
    ws->vlist_stride=stride;ws->edit_initialized=true;
    if(reserve_focus(&ui))ui.focus_order[ui.focus_count++]=d.id;
    if(ui.modal_needs_focus && focus_scope_contains(ui,ui.modal_scope)){ui.focus_id=d.id;ui.modal_needs_focus=false;clear_ime_composition(ctx);}
    TextBuffer* b=d.buffer;uint32_t c=tb_clamp(b,ws->edit_cursor),a=tb_clamp(b,ws->edit_sel_anchor);
    bool focus=ui.focus_id==d.id && (!ui.modal_scope || focus_scope_contains(ui,ui.modal_scope));bool changed=false;
    bool replaced_by_a11y=false;
    PendingA11yAction command={};
    while(!d.read_only && a11y_take(d.id,A11yAction::SetValue,&command)) {
        if(text_buffer_replace(b,0,tb_len(b),{command.text,command.length},true,c,a,command.length,command.length,0u,ui.elapsed_seconds)) {
            changed=true;c=a=tb_len(b);ws->editor_reveal=true;ws->edit_preferred_x=-1;replaced_by_a11y=true;
        }
    }
    if(replaced_by_a11y && focus)cancel_ime_composition(ctx);
    if(focus && !d.read_only){ui.ime_focused_id=d.id;ui.ime_focused_password=false;}
    const float gutter=editor_gutter(d,tb_lines(b));const Rect bounds=ws->last_box_logical;
    const float view_h=bounds.h>d.padding.top+d.padding.bottom?bounds.h-d.padding.top-d.padding.bottom:400;
    const float origin_x=bounds.x+d.padding.left+gutter,origin_y=bounds.y+d.padding.top;
    if(Clay_PointerOver(make_clay_id(d.id)))set_cursor(ui.pointer_pos.x>=origin_x && ui.pointer_pos.x<bounds.x+bounds.w-14?Cursor::IBeam:Cursor::Arrow);
    if(bounds.w>0 && (ws->pressed_this_frame || (ui.active_id==d.id && ws->pressed_down))){
        const bool dragging=!ws->pressed_this_frame;
        if(dragging){const float dy=ui.pointer_pos.y<origin_y?ui.pointer_pos.y-origin_y:(ui.pointer_pos.y>origin_y+view_h?ui.pointer_pos.y-origin_y-view_h:0);
            ws->vscroll_pos_y=clamp_double(ws->vscroll_pos_y+double(dy)*.2,0,ws->vscroll_max_y);
            const float dx=ui.pointer_pos.x<origin_x?ui.pointer_pos.x-origin_x:(ui.pointer_pos.x>bounds.x+bounds.w-16?ui.pointer_pos.x-bounds.x-bounds.w+16:0);
            ws->edit_scroll_x=std::fmax(0.f,ws->edit_scroll_x+dx*.2f);request_redraw(ctx);}
        const double line_y=(double(ui.pointer_pos.y-origin_y)+ws->vscroll_pos_y)/stride;
        const uint32_t line=static_cast<uint32_t>(clamp_double(std::floor(line_y),0,double(tb_lines(b)-1)));
        const Str s=text_buffer_line(b,line,&ctx->frame_arena);const uint32_t start=tb_start(b,line);
        const uint32_t hit=editor_hit(ctx,d,s,ui.pointer_pos.x-origin_x+ws->edit_scroll_x);
        if(ws->pressed_this_frame && ui.last_mouse_clicks>=3){a=start;c=text_buffer_line_end(b,line)+(line+1<tb_lines(b)?1u:0u);}
        else if(ws->pressed_this_frame && ui.last_mouse_clicks==2){utf8_word_bounds_at(s.chars,s.length,hit,&a,&c);a+=start;c+=start;}
        else {c=start+hit;if(ws->pressed_this_frame && !(ui.frame_key_mods&Mod_Shift))a=c;}
        ws->edit_preferred_x=-1;
    }
    const auto replace=[&](uint32_t lo,uint32_t hi,Str ins,uint32_t nc,uint32_t na,uint8_t group=0) noexcept {
        if(text_buffer_replace(b,lo,hi,ins,true,c,a,nc,na,group,ui.elapsed_seconds)){c=nc;a=na;changed=true;ws->editor_reveal=true;ws->edit_preferred_x=-1;}
    };
    if(focus)for(uint32_t i=0;i<ui.frame_text_op_count;++i){const auto& op=ui.frame_text_ops[i];uint32_t lo=c<a?c:a,hi=c>a?c:a;
        if(op.kind==TextEditOpKind::InsertText){if(!d.read_only){Str ins={ui.frame_text_utf8+op.text_offset,op.text_len};uint32_t n=ins.length;for(uint32_t j=0;j+1<ins.length;++j)if(ins.chars[j]=='\r' && ins.chars[j+1]=='\n')--n;replace(lo,hi,ins,lo+n,lo+n,lo==hi?1u:0u);}continue;}
        const bool shift=(op.mod&SDL_KMOD_SHIFT)!=0,primary=(op.mod&(SDL_KMOD_CTRL|SDL_KMOD_GUI))!=0,word=(op.mod&(SDL_KMOD_CTRL|SDL_KMOD_ALT))!=0;
        if(primary){
            if(op.key==SDLK_A){a=0;c=tb_len(b);continue;}
            if(op.key==SDLK_C || op.key==SDLK_X){if(hi>lo){const Str s=editor_slice(ctx,b,lo,hi);if(s.chars)set_clipboard_text(s.chars);if(op.key==SDLK_X && !d.read_only)replace(lo,hi,{},lo,lo);}continue;}
            if(op.key==SDLK_V){if(!d.read_only){const Str ins=get_clipboard_text(&ctx->frame_arena);uint32_t n=ins.length;for(uint32_t j=0;j+1<ins.length;++j)if(ins.chars[j]=='\r' && ins.chars[j+1]=='\n')--n;replace(lo,hi,ins,lo+n,lo+n);}continue;}
            if(op.key==SDLK_Z || op.key==SDLK_Y){if(!d.read_only){const bool pass=(op.key==SDLK_Y || shift)?text_buffer_redo(b,&c,&a):text_buffer_undo(b,&c,&a);changed|=pass;ws->editor_reveal=pass;ws->edit_preferred_x=-1;}continue;}
        }
        const TextLineCol lc=text_buffer_line_col(b,c);const uint32_t start=tb_start(b,lc.line);
        const Str line=text_buffer_line(b,lc.line,&ctx->frame_arena);
        uint32_t next=c;bool moved=false;
        if(op.key==SDLK_LEFT || op.key==SDLK_RIGHT || op.key==SDLK_BACKSPACE || op.key==SDLK_DELETE){
            const bool left=op.key==SDLK_LEFT || op.key==SDLK_BACKSPACE;
            if(left)next=lc.col_byte?start+(word?utf8_word_left(line.chars,line.length,lc.col_byte):utf8_prev_grapheme(line.chars,line.length,lc.col_byte)):(c?c-1:0);
            else next=lc.col_byte<line.length?start+(word?utf8_word_right(line.chars,line.length,lc.col_byte):utf8_next_grapheme(line.chars,line.length,lc.col_byte)):(c<tb_len(b)?c+1:c);
            if(word && left && !lc.col_byte && lc.line) {
                const Str previous=text_buffer_line(b,lc.line-1,&ctx->frame_arena);
                next=tb_start(b,lc.line-1)+utf8_word_left(previous.chars,previous.length,previous.length);
            }else if(word && !left && lc.col_byte==line.length && lc.line+1<tb_lines(b)) {
                const Str following=text_buffer_line(b,lc.line+1,&ctx->frame_arena);
                next=tb_start(b,lc.line+1)+utf8_word_right(following.chars,following.length,0);
            }
            if(op.key==SDLK_BACKSPACE || op.key==SDLK_DELETE){if(!d.read_only){if(lo==hi){lo=c<next?c:next;hi=c>next?c:next;}replace(lo,hi,{},lo,lo);}continue;}
            if(lo!=hi && !shift && !word)next=left?lo:hi;
            ws->edit_preferred_x=-1;moved=true;
        }else if(op.key==SDLK_HOME || op.key==SDLK_END){
            if(primary)next=op.key==SDLK_HOME?0:tb_len(b);
            else if(op.key==SDLK_END)next=start+line.length;
            else {uint32_t indent=0;while(indent<line.length && (line.chars[indent]==' ' || line.chars[indent]=='\t'))++indent;next=start+(lc.col_byte==indent?0:indent);}
            ws->edit_preferred_x=-1;moved=true;
        }else if(op.key==SDLK_UP || op.key==SDLK_DOWN || op.key==SDLK_PAGEUP || op.key==SDLK_PAGEDOWN){
            if(ws->edit_preferred_x<0)ws->edit_preferred_x=editor_x(ctx,d,line,lc.col_byte);
            const bool page=op.key==SDLK_PAGEUP || op.key==SDLK_PAGEDOWN;
            const int delta=(op.key==SDLK_UP || op.key==SDLK_PAGEUP?-1:1)*(page?static_cast<int>(std::fmax(1.f,std::floor(view_h/stride))):1);
            const uint32_t target=static_cast<uint32_t>(clamp_double(double(lc.line)+delta,0,double(tb_lines(b)-1)));
            next=tb_start(b,target)+editor_hit(ctx,d,text_buffer_line(b,target,&ctx->frame_arena),ws->edit_preferred_x);
            if(page)ws->vscroll_pos_y=clamp_double(ws->vscroll_pos_y+double(delta)*stride,0,ws->vscroll_max_y);moved=true;
        }else if(op.key==SDLK_ESCAPE){if(!ui.frame_escape_consumed_by_ime)a=c;
        }else if(!d.read_only && (op.key==SDLK_RETURN || op.key==SDLK_KP_ENTER)){
            uint32_t indent=0;while(indent<lc.col_byte && (line.chars[indent]==' ' || line.chars[indent]=='\t'))++indent;
            char* p=ctx->frame_arena.alloc<char>(size_t(indent)+1);if(p){p[0]='\n';std::memcpy(p+1,line.chars,indent);replace(lo,hi,{p,indent+1},lo+indent+1,lo+indent+1);}
        }else if(!d.read_only && op.key==SDLK_TAB){
            const auto first=text_buffer_line_col(b,lo),last=text_buffer_line_col(b,hi);
            const bool block=first.line!=last.line;
            if(block || shift){
                const uint32_t end_line=block && last.col_byte==0?last.line-1:last.line;
                const uint32_t begin=tb_start(b,first.line),end=text_buffer_line_end(b,end_line);
                const uint32_t count=end_line-first.line+1;
                char* p=ctx->frame_arena.alloc<char>(size_t(end-begin)+size_t(count)*d.tab_width+1);if(!p)continue;
                uint32_t used=0,newc=c,newa=a;
                for(uint32_t li=first.line;li<=end_line;++li){const Str s=text_buffer_line(b,li,&ctx->frame_arena);const uint32_t ls=tb_start(b,li);uint32_t removed=0;
                    if(shift){while(removed<s.length && removed<d.tab_width && s.chars[removed]==' ')++removed;if(!removed && s.length && s.chars[0]=='\t')removed=1;}
                    const uint32_t added=shift?0:d.tab_width;
                    const auto adjust=[&](uint32_t original,uint32_t current) noexcept {if(original<ls)return current;if(original<ls+removed)return current-(original-ls);return current-removed+added;};
                    newc=adjust(c,newc);newa=adjust(a,newa);
                    for(uint32_t j=0;j<added;++j)p[used++]=' ';
                    std::memcpy(p+used,s.chars+removed,s.length-removed);used+=s.length-removed;if(li<end_line)p[used++]='\n';
                }
                replace(begin,end,{p,used},newc,newa);
            }else {char spaces[32];std::memset(spaces,' ',d.tab_width);replace(lo,hi,{spaces,d.tab_width},lo+d.tab_width,lo+d.tab_width);}
        }
        if(moved){c=next;if(!shift)a=c;ws->editor_reveal=true;}
    }
    c=tb_clamp(b,c);a=tb_clamp(b,a);ws->edit_cursor=c;ws->edit_sel_anchor=a;
    const auto lc=text_buffer_line_col(b,c);
    ws->vscroll_total_h=double(tb_lines(b))*stride+d.padding.top+d.padding.bottom;
    ws->vscroll_max_y=std::fmax(0.0,ws->vscroll_total_h-(bounds.h>0?bounds.h:400));
    if(ws->editor_reveal){const double y=double(lc.line)*stride;
        if(y<ws->vscroll_pos_y)ws->vscroll_pos_y=y;
        if(y+stride>ws->vscroll_pos_y+view_h)ws->vscroll_pos_y=y+stride-view_h;
        const float x=editor_x(ctx,d,text_buffer_line(b,lc.line,&ctx->frame_arena),lc.col_byte);
        const float width=std::fmax(20.f,bounds.w-gutter-d.padding.left-d.padding.right-16);
        if(x<ws->edit_scroll_x)ws->edit_scroll_x=x;
        if(x>ws->edit_scroll_x+width)ws->edit_scroll_x=x-width;ws->editor_reveal=false;
    }
    ws->vscroll_pos_y=clamp_double(ws->vscroll_pos_y,0,ws->vscroll_max_y);
    const uint32_t first=static_cast<uint32_t>(ws->vscroll_pos_y/stride);
    const uint32_t end=static_cast<uint32_t>(std::fmin(double(tb_lines(b)),double(first)+std::ceil(view_h/stride)+2));
    auto* e=ctx->frame_arena.alloc<CodeEditorExtra>();auto* extra=ctx->frame_arena.alloc<BoxExtra>();
    if(!e || !extra)return {};
    *e={};e->desc=d;e->desc.buffer=nullptr;e->count=end-first;e->lines=ctx->frame_arena.alloc<EditorLine>(e->count);
    if(!e->lines)return {};
    for(uint32_t i=first;i<end;++i){const uint32_t st=tb_start(b,i);e->lines[i-first]={editor_slice(ctx,b,st,text_buffer_line_end(b,i)),st,i};}
    e->cursor=c;e->anchor=a;e->active_line=lc.line;e->focused=focus;e->scroll=ws->vscroll_pos_y;e->total_h=ws->vscroll_total_h;e->scroll_x=ws->edit_scroll_x;e->stride=stride;e->gutter=gutter;
    const bool composing=focus && !d.read_only && ui.ime_comp_len>0;
    if(composing){e->composition=ctx->frame_arena.push_str(ui.ime_comp_utf8,ui.ime_comp_len);e->comp_cursor=ui.ime_comp_cursor_byte;e->comp_selection=ui.ime_comp_sel_len_byte;}
    *extra={};extra->kind=CustomCmdKind::CodeEditor;extra->code_editor=e;extra->desc.id=d.id;extra->is_virtual_list=true;extra->desc.show_scrollbar=d.show_scrollbar;
    Clay__OpenElementWithId(make_clay_id(d.id));Clay_ElementDeclaration decl={};
    decl.layout.sizing.width=to_clay_sizing(d.width);decl.layout.sizing.height=to_clay_sizing(d.height);decl.custom.customData=extra;decl.userData=extra;
    Clay__ConfigureOpenElement(decl);Clay__CloseElement();
    if(changed)request_redraw(ctx);
    if(focus && !d.read_only){const float x=editor_x(ctx,d,text_buffer_line(b,lc.line,&ctx->frame_arena),lc.col_byte);
        ui.ime_cursor_logical={origin_x+x-ws->edit_scroll_x,origin_y+float(double(lc.line)*stride-ws->vscroll_pos_y),2,stride};ui.ime_cursor_offset_win=0;}
    const uint32_t summary_start=tb_start(b,lc.line),summary_end=text_buffer_line_end(b,lc.line);
    char summary[256];uint32_t summary_size=text_buffer_copy_range(b,summary_start,summary_end-summary_start>256?summary_start+256:summary_end,summary,sizeof(summary));
    summary_size=utf8_clamp_boundary(summary,summary_size,summary_size);
    auto* semantic=a11y_emit(ctx,d.id,A11yRole::CodeEditor,{}, {summary,summary_size},A11yState_Multiline|(d.read_only?A11yState_ReadOnly:0u),1u|(d.read_only?0u:128u));
    if(semantic){semantic->node.min_value=1;semantic->node.max_value=float(tb_lines(b));semantic->node.value_num=float(lc.line+1);}
    return {changed,focus,composing,c,a,lc.line,lc.col_byte,first,end,tb_lines(b),focus?ui.ime_cursor_logical:Rect{}};
}
void emit_code_editor_command(Context* ctx,const CodeEditorExtra* e,Clay_BoundingBox bb,const ClipEntry& outer,float dpi) noexcept {
    const auto& d=e->desc;auto* ws=state_map_find(&ctx->active_ui->states,d.id);
    const Rect box={bb.x,bb.y,bb.width,bb.height};
    ws->last_box_logical=box;ws->viewport_dim={box.w,box.h};ws->vscroll_total_h=e->total_h;ws->vscroll_max_y=std::fmax(0.0,e->total_h-box.h);
    const auto rect=[&](Rect r,Color color) noexcept {push_prim(ctx,Prim::make_rect({r.x*dpi,r.y*dpi,r.w*dpi,r.h*dpi},outer.rect,color));};
    push_prim(ctx,Prim::make_rect({box.x*dpi,box.y*dpi,box.w*dpi,box.h*dpi},outer.rect,d.background,scale_corners(d.radius,dpi),e->focused?d.focus_border_color:d.border_color,(e->focused?d.focus_border_width:d.border_width)*dpi));
    rect({box.x+1,box.y+1,e->gutter,box.h-2},d.gutter_background);
    const float ox=box.x+d.padding.left+e->gutter,oy=box.y+d.padding.top;
    const ClipEntry clip={rect_intersect(outer.rect,{ox*dpi,oy*dpi,std::fmax(0.f,box.w-e->gutter-d.padding.left-d.padding.right-14)*dpi,std::fmax(0.f,box.h-d.padding.top-d.padding.bottom)*dpi}),0};
    const ClipEntry gutter_clip={rect_intersect(outer.rect,{box.x*dpi,oy*dpi,(e->gutter+d.padding.left)*dpi,std::fmax(0.f,box.h-d.padding.top-d.padding.bottom)*dpi}),0};
    const uint16_t slot=resolve_font_slot_idx(&ctx->atlas,d.font_id);const float asc=slot!=kInvalidFont?ctx->atlas.slots[slot].ascent_em*d.font_size_px*dpi:d.font_size_px*dpi*.8f;
    const auto glyph=[&](Str s,Color color,uint16_t font,float x,float y,const ClipEntry& cp) noexcept {float px=0,py=0;emit_utf8_glyph_run(ctx,font,s.chars,s.length,d.font_size_px,0,asc,e->stride,color,x,y,0,0,cp,dpi,false,false,&px,&py,d.tab_width);};
    const uint32_t lo=e->cursor<e->anchor?e->cursor:e->anchor,hi=e->cursor>e->anchor?e->cursor:e->anchor;
    for(uint32_t i=0;i<e->count;++i){const auto& l=e->lines[i];const float y=oy+float(double(l.index)*e->stride-e->scroll);
        if(l.index==e->active_line)push_prim(ctx,Prim::make_rect({ox*dpi,y*dpi,(box.w-e->gutter-d.padding.left-d.padding.right)*dpi,e->stride*dpi},clip.rect,d.active_line_color));
        if(d.show_line_numbers){char n[16];const int len=std::snprintf(n,sizeof(n),"%u",l.index+1);const Str ns={n,static_cast<uint32_t>(len)};glyph(ns,l.index==e->active_line?d.gutter_active_text_color:d.gutter_text_color,d.font_id,ox-12-editor_x(ctx,d,ns,ns.length),y,gutter_clip);}
        const uint32_t length=l.text.length;
        if(hi>l.start && lo<=l.start+length){const uint32_t a=lo>l.start?lo-l.start:0,z=hi<l.start+length?hi-l.start:length;const float x=editor_x(ctx,d,l.text,a),end=editor_x(ctx,d,l.text,z);
            push_prim(ctx,Prim::make_rect({(ox+x-e->scroll_x)*dpi,y*dpi,(end-x+(hi>l.start+length?d.font_size_px*.6f:0))*dpi,e->stride*dpi},clip.rect,d.selection_color));}
        SyntaxToken tokens[256];const uint32_t count=d.tokenizer?d.tokenizer(l.index,l.text,tokens,256,d.tokenizer_user_data):0;
        const bool preedit=e->composition.length && l.index==e->active_line;
        const uint32_t caret=preedit?e->cursor-l.start:length;
        const float preedit_width=preedit?editor_x(ctx,d,e->composition,e->composition.length):0;
        const auto segment=[&](uint32_t a,uint32_t z,Color color,uint16_t font) noexcept {
            if(preedit && a<caret && z>caret){glyph({l.text.chars+a,caret-a},color,font,ox+editor_x(ctx,d,l.text,a)-e->scroll_x,y,clip);a=caret;}
            glyph({l.text.chars+a,z-a},color,font,ox+editor_x(ctx,d,l.text,a)-e->scroll_x+(preedit && a>=caret?preedit_width:0),y,clip);
        };
        uint32_t pos=0;
        for(uint32_t t=0;t<count && t<256;++t){const auto& token=tokens[t];uint32_t a=utf8_clamp_boundary(l.text.chars,length,token.byte_start),z=utf8_clamp_boundary(l.text.chars,length,token.byte_end);if(a<pos || z<a)continue;
            if(a>pos)segment(pos,a,d.text_color,d.font_id);
            segment(a,z,token.color,token.font_id);pos=z;}
        if(pos<length)segment(pos,length,d.text_color,d.font_id);
        if(e->focused && l.index==e->active_line){const uint32_t col=e->cursor-l.start;const float x=ox+editor_x(ctx,d,l.text,col)-e->scroll_x;
            float caret_x=x;
            const float width=preedit_width;
            const float ca=e->composition.length?editor_x(ctx,d,e->composition,e->comp_cursor):0;
            if(e->composition.length){const float cz=editor_x(ctx,d,e->composition,e->comp_cursor+e->comp_selection);
                push_prim(ctx,Prim::make_rect({(x+ca)*dpi,y*dpi,(cz-ca)*dpi,e->stride*dpi},clip.rect,d.selection_color));
                glyph(e->composition,d.composition_color,d.font_id,x,y,clip);push_prim(ctx,Prim::make_rect({x*dpi,(y+e->stride-2)*dpi,width*dpi,dpi},clip.rect,d.composition_color));caret_x+=ca;}
            if(ctx->active_ui->caret_blink_on)push_prim(ctx,Prim::make_rect({caret_x*dpi,y*dpi,1.5f*dpi,e->stride*dpi},clip.rect,d.caret_color));
            if(e->composition.length>0 && width>0.0f){
                ctx->active_ui->ime_cursor_logical={x,y,width>2.0f?width:2.0f,e->stride};
                ctx->active_ui->ime_cursor_offset_win=static_cast<int32_t>(std::floor(ca*logical_to_window_scale(ctx)+0.5f));
            }else{
                ctx->active_ui->ime_cursor_logical={caret_x,y,2.0f,e->stride};
                ctx->active_ui->ime_cursor_offset_win=0;
            }
        }
    }
    if(d.show_scrollbar){const ScrollbarGeom sb=compute_vscrollbar_geom(ws);if(sb.visible){rect(sb.track,Color::rgba8(255,255,255,18));rect(sb.thumb,Color::rgba8(148,163,184,160));}}
}
} // namespace etch
