// Gap-indexed document storage and viewport editor. Compiled in etch_unity.cpp.
namespace etch {
struct TextBufferEdit {
    uint32_t start = 0, removed = 0, inserted = 0, blob = 0;
    uint32_t before_cursor = 0, before_anchor = 0, after_cursor = 0, after_anchor = 0;
    double timestamp = 0; uint8_t group = 0;
    uint32_t batch_count = 0;
};
struct EditorExtEntry {
    uint32_t editor_id = 0u;
    char* ghost_text = nullptr;
    uint32_t ghost_len = 0u;
    uint32_t ghost_cap = 0u;
    bool completion_accepted = false;
    bool completion_dismissed = false;
    char* search_query = nullptr;
    uint32_t search_len = 0u;
    uint32_t search_cap = 0u;
    bool search_case_sensitive = false;
    struct FoldRange { uint32_t start_line = 0u, end_line = 0u; };
    FoldRange folds[64] = {};
    uint32_t fold_count = 0u;
    uint32_t extra_cursors[32] = {};
    uint32_t extra_cursor_count = 0u;
};
bool text_buffer_set_diagnostics(TextBuffer*, uint64_t, std::span<const TextDiagnostic>) noexcept;
namespace {
uint32_t tb_len(const TextBuffer* b) noexcept { return b->cap - (b->gap_end - b->gap_start); }
uint32_t tb_lines(const TextBuffer* b) noexcept { return b->line_cap - (b->line_gap_end - b->line_gap_start); }
char tb_byte(const TextBuffer* b, uint32_t pos) noexcept {
    return b->data[pos < b->gap_start ? pos : pos + (b->gap_end - b->gap_start)];
}
char ed_ascii_lower(char ch) noexcept {
    return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + ('a' - 'A')) : ch;
}
bool ed_is_ident_byte(char ch) noexcept {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}
EditorExtEntry* find_editor_ext(WindowUiState* ui, uint32_t editor_id) noexcept {
    for (uint32_t i = 0u; i < ui->editor_ext_count; ++i) {
        if (ui->editor_exts[i].editor_id == editor_id) return &ui->editor_exts[i];
    }
    return nullptr;
}
const EditorExtEntry* find_editor_ext_const(const WindowUiState* ui, uint32_t editor_id) noexcept {
    for (uint32_t i = 0u; i < ui->editor_ext_count; ++i) {
        if (ui->editor_exts[i].editor_id == editor_id) return &ui->editor_exts[i];
    }
    return nullptr;
}
EditorExtEntry* ensure_editor_ext(WindowUiState* ui, uint32_t editor_id) noexcept {
    if (EditorExtEntry* existing = find_editor_ext(ui, editor_id)) return existing;
    if (ui->editor_ext_count == ui->editor_ext_cap) {
        const uint32_t next_cap = ui->editor_ext_cap ? (ui->editor_ext_cap * 2u) : 8u;
        void* mem = std::realloc(ui->editor_exts, size_t(next_cap) * sizeof(EditorExtEntry));
        if (!mem) return nullptr;
        ui->editor_exts = static_cast<EditorExtEntry*>(mem);
        ui->editor_ext_cap = next_cap;
    }
    EditorExtEntry* slot = &ui->editor_exts[ui->editor_ext_count++];
    *slot = {};
    slot->editor_id = editor_id;
    return slot;
}
bool ed_assign_str(char** buf, uint32_t* len, uint32_t* cap, Str s) noexcept {
    if (!s.chars || s.length == 0u) {
        *len = 0u;
        if (*buf) (*buf)[0] = '\0';
        return true;
    }
    const uint32_t need = s.length + 1u;
    if (need > *cap) {
        uint32_t next_cap = *cap ? *cap : 64u;
        while (next_cap < need) next_cap *= 2u;
        char* mem = static_cast<char*>(std::realloc(*buf, next_cap));
        if (!mem) return false;
        *buf = mem;
        *cap = next_cap;
    }
    std::memcpy(*buf, s.chars, s.length);
    (*buf)[s.length] = '\0';
    *len = s.length;
    return true;
}
bool ed_is_line_folded(const EditorExtEntry* ext, uint32_t line) noexcept {
    if (!ext) return false;
    for (uint32_t i = 0u; i < ext->fold_count; ++i) {
        const auto& f = ext->folds[i];
        if (line > f.start_line && line <= f.end_line) return true;
    }
    return false;
}
bool ed_has_fold_start(const EditorExtEntry* ext, uint32_t line) noexcept {
    if (!ext) return false;
    for (uint32_t i = 0u; i < ext->fold_count; ++i) {
        const auto& f = ext->folds[i];
        if (f.start_line == line && f.end_line > line) return true;
    }
    return false;
}
bool ed_find_substr(const char* hay, uint32_t hay_len, const char* needle, uint32_t needle_len,
                    uint32_t start, bool case_sensitive, uint32_t* out_pos) noexcept {
    if (!hay || !needle || needle_len == 0u || start > hay_len || needle_len > hay_len - start) return false;
    const uint32_t last = hay_len - needle_len;
    const char n0 = case_sensitive ? needle[0] : ed_ascii_lower(needle[0]);
    for (uint32_t p = start; p <= last; ++p) {
        const char h0 = case_sensitive ? hay[p] : ed_ascii_lower(hay[p]);
        if (h0 != n0) continue;
        bool matched = true;
        for (uint32_t j = 1u; j < needle_len; ++j) {
            const char hc = case_sensitive ? hay[p + j] : ed_ascii_lower(hay[p + j]);
            const char nc = case_sensitive ? needle[j] : ed_ascii_lower(needle[j]);
            if (hc != nc) { matched = false; break; }
        }
        if (matched) {
            *out_pos = p;
            return true;
        }
    }
    return false;
}
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
constexpr uint32_t kEditorUndoEntries = 1024u;
constexpr uint32_t kEditorUndoBytes = 4u * 1024u * 1024u;
struct BatchPart { uint32_t start, removed, inserted, offset; };

bool tb_prepare_undo(TextBuffer* b, uint32_t bytes) noexcept {
    const uint32_t count = b->undo_cursor;
    const uint32_t used = count < b->undo_count ? b->undo_ops[count].blob : b->undo_blob_used;
    if (count == b->undo_cap && count < kEditorUndoEntries) {
        const uint32_t cap = count ? count * 2u : 16u;
        auto* ops = static_cast<TextBufferEdit*>(std::realloc(b->undo_ops, cap * sizeof(TextBufferEdit)));
        if (!ops) return false;
        b->undo_ops = ops;
        b->undo_cap = cap;
    }
    const uint32_t needed = std::min(kEditorUndoBytes, used + bytes);
    if (!b->undo_blob || needed > b->undo_blob_cap) {
        uint32_t cap = b->undo_blob_cap ? b->undo_blob_cap : 256u;
        while (cap < needed) cap *= 2u;
        char* blob = static_cast<char*>(std::realloc(b->undo_blob, cap));
        if (!blob) return false;
        b->undo_blob = blob;
        b->undo_blob_cap = cap;
    }
    uint32_t drop = 0, skipped = 0;
    while (count - drop == kEditorUndoEntries || used - skipped > kEditorUndoBytes - bytes) {
        const auto& op = b->undo_ops[drop++];
        skipped = op.blob + op.removed + op.inserted + op.batch_count * static_cast<uint32_t>(sizeof(BatchPart));
    }
    b->undo_blob_used = used - skipped;
    b->undo_count = b->undo_cursor = count - drop;
    if (drop) {
        std::memmove(b->undo_blob, b->undo_blob + skipped, b->undo_blob_used);
        std::memmove(b->undo_ops, b->undo_ops + drop, b->undo_count * sizeof(TextBufferEdit));
        for (uint32_t i = 0; i < b->undo_count; ++i) b->undo_ops[i].blob -= skipped;
    }
    return true;
}
}
uint32_t text_buffer_byte_length(const TextBuffer* b) noexcept { return b && b->data ? tb_len(b) : 0; }
uint32_t text_buffer_line_count(const TextBuffer* b) noexcept { return b && b->data ? tb_lines(b) : 0; }
void free_text_buffer(TextBuffer* b) noexcept {
    if(!b) return;
    std::free(b->data); std::free(b->line_data); std::free(b->undo_ops); std::free(b->undo_blob);
    std::free(b->diagnostics); *b={};
}
void text_buffer_clear_undo(TextBuffer* b) noexcept { if(b) b->undo_count=b->undo_cursor=b->undo_blob_used=0; }
bool init_text_buffer(TextBuffer* b, Str s, uint32_t cap) noexcept {
    if(!b || b->data || (!s.chars && s.length) || s.length>0x7FFFFFFFu) return false;
    TextBuffer v={}; v.cap=cap>4096?cap:4096; v.gap_end=v.cap;
    v.line_cap=64; v.line_gap_end=64;
    v.data=static_cast<char*>(std::malloc(v.cap));
    v.line_data=static_cast<uint32_t*>(std::malloc(size_t(v.line_cap)*sizeof(uint32_t)));
    if(!v.data || !v.line_data) { free_text_buffer(&v); return false; }
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
    if(!b || !b->data) { return {}; } p=tb_clamp(b,p);
    uint32_t lo=0,hi=tb_lines(b);
    while(lo+1<hi){const uint32_t m=lo+(hi-lo)/2;if(tb_start(b,m)<=p)lo=m;else hi=m;}
    return {lo,p-tb_start(b,lo)};
}
uint32_t text_buffer_byte_offset(const TextBuffer* b,uint32_t i,uint32_t col) noexcept {
    if(!b || !b->data) { return 0; } i=i<tb_lines(b)?i:tb_lines(b)-1;
    const uint32_t a=tb_start(b,i),n=text_buffer_line_end(b,i)-a;return tb_clamp(b,a+(col<n?col:n));
}
namespace {
// Each side of the gap ends on a scalar boundary. Walk the two physical
// spans without materializing the line in an arena.
uint32_t tb_walk_utf16(const TextBuffer* b, uint32_t start, uint32_t end, uint32_t limit, uint32_t& units) noexcept {
    uint32_t byte = start;
    units = 0u;
    while (byte < end && units < limit) {
        const bool prefix = byte < b->gap_start;
        const uint32_t stop = prefix ? std::min(end, b->gap_start) : end;
        const char* p = b->data + (prefix ? byte : b->gap_end + (byte - b->gap_start));
        const char* last = p + (stop - byte);
        while (p != last) {
            const char* prev = p;
            const uint32_t width = next_utf8_codepoint(p, last) > 0xffffu ? 2u : 1u;
            if (width > limit - units) return byte;
            units += width;
            byte += static_cast<uint32_t>(p - prev);
            if (units == limit) return byte;
        }
    }
    return byte;
}
}

TextUtf16Position text_buffer_utf16_position(const TextBuffer* b, uint32_t byte) noexcept {
    if (!b || !b->data) return {};
    const auto lc = text_buffer_line_col(b, byte);
    const uint32_t start = tb_start(b, lc.line);
    uint32_t units;
    tb_walk_utf16(b, start, start + lc.col_byte, UINT32_MAX, units);
    return {lc.line, units};
}

uint32_t text_buffer_byte_from_utf16(const TextBuffer* b, TextUtf16Position position) noexcept {
    if (!b || !b->data) return 0u;
    if (position.line >= tb_lines(b)) return tb_len(b);
    uint32_t units;
    return tb_walk_utf16(b, tb_start(b, position.line), text_buffer_line_end(b, position.line), position.character, units);
}

uint64_t text_buffer_revision(const TextBuffer* b) noexcept { return b && b->data ? b->version : 0u; }

bool text_buffer_set_diagnostics(TextBuffer* b, uint64_t revision,
                                 std::span<const TextDiagnostic> diagnostics) noexcept {
    if (!b || !b->data || b->version != revision || diagnostics.size() > 65536u) return false;
    constexpr size_t kMaxDiagnosticMessageBytes = 4u * 1024u * 1024u;
    size_t msg_bytes = 0u;
    for (const auto& d : diagnostics) {
        if (d.byte_start > d.byte_end || d.byte_end > tb_len(b) ||
            tb_clamp(b, d.byte_start) != d.byte_start || tb_clamp(b, d.byte_end) != d.byte_end ||
            static_cast<uint8_t>(d.severity) > static_cast<uint8_t>(TextDiagnosticSeverity::Hint) ||
            (!d.message.chars && d.message.length) ||
            d.message.length > kMaxDiagnosticMessageBytes - msg_bytes) return false;
        msg_bytes += d.message.length;
    }
    TextDiagnostic* copy = nullptr;
    if (!diagnostics.empty()) {
        const size_t array_bytes = diagnostics.size_bytes();
        char* raw = static_cast<char*>(std::malloc(array_bytes + msg_bytes));
        if (!raw) return false;
        copy = reinterpret_cast<TextDiagnostic*>(raw);
        char* msg_dst = raw + array_bytes;
        for (size_t i = 0; i < diagnostics.size(); ++i) {
            copy[i] = diagnostics[i];
            if (diagnostics[i].message.length > 0u) {
                std::memcpy(msg_dst, diagnostics[i].message.chars, diagnostics[i].message.length);
                copy[i].message = {msg_dst, diagnostics[i].message.length};
                msg_dst += diagnostics[i].message.length;
            } else {
                copy[i].message = {};
            }
        }
    }
    std::free(b->diagnostics);
    b->diagnostics = copy;
    b->diagnostic_count = static_cast<uint32_t>(diagnostics.size());
    b->diagnostic_capacity = b->diagnostic_count;
    b->diagnostics_revision = revision;
    return true;
}

void text_buffer_clear_diagnostics(TextBuffer* b) noexcept {
    if (!b) return;
    std::free(b->diagnostics);
    b->diagnostics = nullptr;
    b->diagnostic_count = b->diagnostic_capacity = 0u;
    b->diagnostics_revision = 0u;
}

bool text_buffer_find(
    const TextBuffer* buf,
    Str needle,
    uint32_t start_byte,
    bool case_sensitive,
    bool whole_word,
    TextSearchMatch* out_match) noexcept
{
    if (!buf || !buf->data || !needle.chars || needle.length == 0u) return false;
    const uint32_t total = tb_len(buf);
    if (start_byte > total || needle.length > total - start_byte) return false;
    const uint32_t last = total - needle.length;
    const char n0 = case_sensitive ? needle.chars[0] : ed_ascii_lower(needle.chars[0]);
    for (uint32_t pos = start_byte; pos <= last; ++pos) {
        const char h0 = case_sensitive ? tb_byte(buf, pos) : ed_ascii_lower(tb_byte(buf, pos));
        if (h0 != n0) continue;
        bool matched = true;
        for (uint32_t j = 1u; j < needle.length; ++j) {
            const char hc = case_sensitive ? tb_byte(buf, pos + j) : ed_ascii_lower(tb_byte(buf, pos + j));
            const char nc = case_sensitive ? needle.chars[j] : ed_ascii_lower(needle.chars[j]);
            if (hc != nc) {
                matched = false;
                break;
            }
        }
        if (!matched) continue;
        if (whole_word) {
            if (pos > 0u && ed_is_ident_byte(tb_byte(buf, pos - 1u))) continue;
            if (pos + needle.length < total && ed_is_ident_byte(tb_byte(buf, pos + needle.length))) continue;
        }
        if (out_match) {
            *out_match = { pos, pos + needle.length };
        }
        return true;
    }
    return false;
}

uint32_t text_buffer_find_all(
    const TextBuffer* buf,
    Str needle,
    bool case_sensitive,
    bool whole_word,
    TextSearchMatch* out_matches,
    uint32_t max_matches) noexcept
{
    if (!buf || !buf->data || !needle.chars || needle.length == 0u) return 0u;
    uint32_t found = 0u;
    uint32_t pos = 0u;
    TextSearchMatch m = {};
    while (text_buffer_find(buf, needle, pos, case_sensitive, whole_word, &m)) {
        if (out_matches) {
            if (found >= max_matches) break;
            out_matches[found] = m;
        }
        ++found;
        pos = m.byte_end;
    }
    return found;
}

namespace {
// The caller has reserved room in both gaps. Positions use the current document.
void tb_splice(TextBuffer* b, uint32_t start, uint32_t end, Str text, bool normalize) noexcept {
    tb_move(b, start);
    const uint32_t old_length = tb_len(b);
    while (b->line_gap_end < b->line_cap && old_length - b->line_data[b->line_gap_end] <= end)
        ++b->line_gap_end;
    b->gap_end += end - start;
    for (uint32_t i = 0; i < text.length; ++i) {
        const char c = text.chars[i];
        if (normalize && c == '\r' && i + 1u < text.length && text.chars[i + 1u] == '\n') continue;
        b->data[b->gap_start++] = c;
        if (c == '\n') b->line_data[b->line_gap_start++] = b->gap_start;
    }
}
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
    if(group==1 && (!is_single_nonbreak_utf8_codepoint(s) || utf8_char_class(utf8_decode_at(s.chars,s.length,0))==0))group=0;
    TextBufferEdit* edit=nullptr;
    if(record){
        if(n <= kEditorUndoBytes && z-a <= kEditorUndoBytes-n){
            if (!tb_prepare_undo(b, z-a+n)) { std::free(alias); return false; }
            edit=&b->undo_ops[b->undo_count++];*edit={a,z-a,n,b->undo_blob_used,bc,ba,ac,aa,time,group};
            tb_copy(b,a,z,b->undo_blob+b->undo_blob_used);b->undo_blob_used+=z-a;
        }else text_buffer_clear_undo(b);
    }
    tb_splice(b, a, z, s, normalize_crlf);
    if(edit){
        edit->after_cursor=tb_clamp(b,ac);edit->after_anchor=tb_clamp(b,aa);
        std::memcpy(b->undo_blob+b->undo_blob_used,b->data+a,n);b->undo_blob_used+=n;
        if(b->undo_count>1){
            auto& prev=b->undo_ops[b->undo_count-2];
            if(group==1 && z==a && is_single_nonbreak_utf8_codepoint({b->data+a,n}) && utf8_char_class(utf8_decode_at(b->data+a,n,0))!=0){
                if(prev.group==1 && !prev.removed && prev.start+prev.inserted==a && time>=prev.timestamp && time-prev.timestamp<0.75){
                    prev.inserted+=n;prev.after_cursor=edit->after_cursor;prev.after_anchor=edit->after_anchor;prev.timestamp=time;--b->undo_count;
                }
            } else if(group==3 && n==0 && edit->removed>0 && prev.group==3 && !prev.inserted &&
                      prev.start==a && edit->blob==prev.blob+prev.removed &&
                      time>=prev.timestamp && time-prev.timestamp<0.75 &&
                      std::memchr(b->undo_blob+edit->blob,'\n',edit->removed)==nullptr){
                prev.removed+=edit->removed;prev.after_cursor=edit->after_cursor;prev.after_anchor=edit->after_anchor;prev.timestamp=time;--b->undo_count;
            } else if(group==2 && n==0 && edit->removed>0 && edit->removed<=64u && prev.group==2 && !prev.inserted &&
                      a+edit->removed==prev.start && edit->blob==prev.blob+prev.removed &&
                      time>=prev.timestamp && time-prev.timestamp<0.75 &&
                      std::memchr(b->undo_blob+edit->blob,'\n',edit->removed)==nullptr){
                char tmp[64];
                std::memcpy(tmp,b->undo_blob+edit->blob,edit->removed);
                std::memmove(b->undo_blob+prev.blob+edit->removed,b->undo_blob+prev.blob,prev.removed);
                std::memcpy(b->undo_blob+prev.blob,tmp,edit->removed);
                prev.start=a;prev.removed+=edit->removed;prev.after_cursor=edit->after_cursor;prev.after_anchor=edit->after_anchor;prev.timestamp=time;--b->undo_count;
            }
        }
        b->undo_cursor=b->undo_count;
    }
    ++b->version;std::free(alias);return true;
}
}
bool text_buffer_replace(TextBuffer* b,uint32_t a,uint32_t z,Str s,bool record,uint32_t bc,uint32_t ba,uint32_t ac,uint32_t aa,uint8_t group,double time) noexcept {
    if(!b || !b->data || (!s.chars && s.length) || s.length>0x7FFFFFFFu)return false;
    const bool changes_text = !record && (s.length || tb_clamp(b,a) != tb_clamp(b,z));
    if (!tb_replace_impl(b,a,z,s,true,record,bc,ba,ac,aa,group,time)) return false;
    if (changes_text) text_buffer_clear_undo(b);
    return true;
}
namespace {
void tb_replay_batch(TextBuffer* b, const TextBufferEdit& op, bool forward) noexcept {
    const char* data = b->undo_blob + op.blob;
    for (uint32_t n = 0; n < op.batch_count; ++n) {
        const uint32_t i = forward ? op.batch_count - n - 1u : n;
        BatchPart p;
        std::memcpy(&p, data + i * sizeof(p), sizeof(p));
        const char* bytes = data + p.offset;
        if (forward) tb_splice(b, p.start, p.start + p.removed, {bytes + p.removed, p.inserted}, false);
        else tb_splice(b, p.start, p.start + p.inserted, {bytes, p.removed}, false);
    }
    ++b->version;
}

bool tb_measure_patch_text(Str text, uint32_t& added, uint32_t& lines) noexcept {
    if (!text.length) return true;
    if (!text.chars || text.length > 2u * kEditorUndoBytes) return false;
    const char* p = text.chars;
    const char* end = p + text.length;
    while (p < end) {
        const char* start = p;
        const uint32_t scalar = next_utf8_codepoint(p, end);
        if (scalar == 0xfffdu && (p - start != 3 || std::memcmp(start, "\xef\xbf\xbd", 3) != 0)) return false;
        if (scalar == '\r' && p < end && *p == '\n') continue;
        added += static_cast<uint32_t>(p - start);
        if (scalar == '\n') ++lines;
    }
    return true;
}
}
bool apply_text_edits(TextBuffer* b, uint64_t revision, const void* edits, size_t count, TextEditReadFn read) noexcept {
    if (!b || !b->data || b->version != revision || (!edits && count) ||
        count > kEditorUndoBytes / sizeof(BatchPart)) return false;
    const uint32_t table_bytes = static_cast<uint32_t>(count * sizeof(BatchPart));
    uint32_t removed = 0, added = 0, lines = 0, previous_end = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto e = read(edits, i);
        if (e.byte_start < previous_end || e.byte_start > e.byte_end || e.byte_end > tb_len(b) ||
            tb_clamp(b, e.byte_start) != e.byte_start || tb_clamp(b, e.byte_end) != e.byte_end) return false;
        previous_end = e.byte_end;
        const uint32_t old_bytes = e.byte_end - e.byte_start;
        if (old_bytes > kEditorUndoBytes - table_bytes - removed - added) return false;
        removed += old_bytes;
        if (!tb_measure_patch_text(e.text, added, lines) || added > kEditorUndoBytes - table_bytes - removed) return false;
    }
    if (!added && !removed) return true;
    if (tb_len(b) > UINT32_MAX - added - 64u || tb_lines(b) > UINT32_MAX - lines - 1u) return false;
    const uint32_t size = table_bytes + removed + added;
    // Detach source aliases before either gap grows. Only changed bytes enter history.
    char* patch = static_cast<char*>(std::malloc(size));
    if (!patch) return false;
    uint32_t offset = table_bytes;
    for (size_t i = 0; i < count; ++i) {
        const auto e = read(edits, i);
        BatchPart p = {e.byte_start, e.byte_end - e.byte_start, 0u, offset};
        tb_copy(b, e.byte_start, e.byte_end, patch + offset);
        offset += p.removed;
        for (uint32_t j = 0; j < e.text.length; ++j) {
            const char c = e.text.chars[j];
            if (c == '\r' && j + 1u < e.text.length && e.text.chars[j + 1u] == '\n') continue;
            patch[offset++] = c;
        }
        p.inserted = offset - p.offset - p.removed;
        std::memcpy(patch + i * sizeof(p), &p, sizeof(p));
    }
    const uint32_t first = read(edits, 0).byte_start;
    const uint32_t after = previous_end - removed + added;
    if (!tb_reserve(b, tb_len(b) + added + 64u, tb_lines(b) + lines + 1u) || !tb_prepare_undo(b, size)) {
        std::free(patch);
        return false;
    }
    auto& op = b->undo_ops[b->undo_count++];
    op = {first, removed, added, b->undo_blob_used, first, first, after, after, 0.0, 0u, static_cast<uint32_t>(count)};
    std::memcpy(b->undo_blob + op.blob, patch, size);
    std::free(patch);
    b->undo_blob_used += size;
    b->undo_cursor = b->undo_count;
    tb_replay_batch(b, op, true);
    return true;
}
bool text_buffer_apply_edits(TextBuffer* b, uint64_t revision, std::span<const TextEdit> edits) noexcept {
    return apply_text_edits(b, revision, edits.data(), edits.size(), [](const void* data, size_t i) noexcept {
        return static_cast<const TextEdit*>(data)[i];
    });
}
bool text_buffer_undo(TextBuffer* b,uint32_t* c,uint32_t* a) noexcept {
    if(!b || !b->data || !b->undo_cursor) { return false; } const auto e=b->undo_ops[b->undo_cursor-1];
    if (e.batch_count) tb_replay_batch(b, e, false);
    else if(!tb_replace_impl(b,e.start,e.start+e.inserted,{b->undo_blob+e.blob,e.removed},false,false,0,0,0,0,0,0))return false;
    --b->undo_cursor;if(c)*c=e.before_cursor;if(a)*a=e.before_anchor;return true;
}
bool text_buffer_redo(TextBuffer* b,uint32_t* c,uint32_t* a) noexcept {
    if(!b || !b->data || b->undo_cursor==b->undo_count) { return false; } const auto e=b->undo_ops[b->undo_cursor];
    if (e.batch_count) tb_replay_batch(b, e, true);
    else if(!tb_replace_impl(b,e.start,e.start+e.removed,{b->undo_blob+(e.blob+e.removed),e.inserted},false,false,0,0,0,0,0,0))return false;
    ++b->undo_cursor;if(c)*c=e.after_cursor;if(a)*a=e.after_anchor;return true;
}
namespace {
using TokenEmitFn=void (*)(void*,uint32_t,uint32_t,uint32_t,Color) noexcept;
uint32_t tokenize_cpp_hlsl_emit(Str s,void* out,uint32_t cap,const Theme& thm,TokenEmitFn emit) noexcept {
    if(!s.chars || !out) { return 0; } uint32_t count=0,p=0;
    while(p<s.length && count<cap){const uint32_t a=p;Color color=thm.text_code;const char c=s.chars[p++];
        if(c=='/' && p<s.length && s.chars[p]=='/'){p=s.length;color=thm.syntax_comment;}
        else if(c=='/' && p<s.length && s.chars[p]=='*'){++p;while(p<s.length){if(s.chars[p++]=='*' && p<s.length && s.chars[p]=='/'){++p;break;}}color=thm.syntax_comment;}
        else if(c=='"' || c=='\''){while(p<s.length){const char v=s.chars[p++];if(v=='\\' && p<s.length)++p;else if(v==c)break;}color=thm.syntax_string;}
        else if(c>='0' && c<='9'){while(p<s.length && ((s.chars[p]>='0' && s.chars[p]<='9') || s.chars[p]=='.' || s.chars[p]=='f' || s.chars[p]=='x' || (s.chars[p]>='A' && s.chars[p]<='F')))++p;color=thm.syntax_number;}
        else if((c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' || c=='#'){
            while(p<s.length && ((s.chars[p]>='a' && s.chars[p]<='z') || (s.chars[p]>='A' && s.chars[p]<='Z') || (s.chars[p]>='0' && s.chars[p]<='9') || s.chars[p]=='_'))++p;
            static constexpr const char* words[]={"float","float2","float3","float4","uint","int","bool","void","return","if","else","for","while","struct","cbuffer","static","const","constexpr","noexcept","Texture2D","SamplerState","StructuredBuffer","discard"};
            for(const char* w:words)if(std::strlen(w)==p-a && !std::memcmp(w,s.chars+a,p-a)){color=thm.syntax_keyword;break;}
            if(c=='#')color=thm.syntax_number;
        }
        if(color.pack_rgba8()!=thm.text_code.pack_rgba8()){emit(out,count,a,p,color);++count;}
    }return count;
 }
uint32_t tokenize_cpp_hlsl_with_theme(Str s,SyntaxToken* out,uint32_t cap,const Theme& thm) noexcept {
    return tokenize_cpp_hlsl_emit(s,out,cap,thm,[](void* ptr,uint32_t i,uint32_t a,uint32_t z,Color color) noexcept {
        static_cast<SyntaxToken*>(ptr)[i]={a,z,color,kFontMono};
    });
}
}
uint32_t tokenize_cpp_hlsl(uint32_t,Str s,SyntaxToken* out,uint32_t cap,void*) noexcept {
    return tokenize_cpp_hlsl_with_theme(s,out,cap,active_theme(current_context()));
}
struct EditorLine {
    Str text = {};
    uint32_t start = 0u;
    uint32_t index = 0u;
    float y_rel = 0.0f;
    float height = 0.0f;
    uint8_t diag_severity = 4u;
    bool has_fold_marker = false;
};
struct CodeEditorExtra {
    CodeEditorDesc desc = {};
    Theme scoped_theme = {};
    EditorLine* lines = nullptr;
    uint32_t count = 0u;
    uint32_t cursor = 0u, anchor = 0u, active_line = 0u;
    bool focused = false, wrap_affinity = false;
    double scroll = 0.0, total_h = 0.0;
    float scroll_x = 0.0f, stride = 20.0f, gutter = 60.0f, wrap_w = 0.0f;
    Str composition = {};
    uint32_t comp_cursor = 0u, comp_selection = 0u;
    Str ghost_text = {};
    Str search_query = {};
    bool search_case_sensitive = false;
    uint32_t* extra_cursors = nullptr;
    uint32_t extra_cursor_count = 0u;
};
namespace {
struct EditorWrapMeasureCtx {
    Context* ctx = nullptr;
    TextBuffer* buf = nullptr;
    uint16_t font_id = kInvalidFont;
    float font_size_px = 14.0f;
    float stride = 20.0f;
    uint32_t tab_width = 4u;
    float wrap_w = 0.0f;
};
float editor_x(Context* ctx, const CodeEditorDesc& d, Str s, uint32_t p) noexcept {
    float x = 0.0f, unused = 0.0f, total = 0.0f;
    measure_line_segment_x(ctx, d.font_id, s.chars ? s.chars : "", s.length, p, 0u, d.font_size_px, 0.0f, false, true, &x, &unused, &total, d.tab_width);
    return x;
}
uint32_t editor_hit(Context* ctx, const CodeEditorDesc& d, Str s, float x) noexcept {
    return hit_line_segment_x(ctx, d.font_id, s.chars ? s.chars : "", s.length, d.font_size_px, 0.0f, false, d.tab_width, x, nullptr);
}
float editor_wrap_width(const CodeEditorDesc& d, float box_w, float gutter) noexcept {
    if (!d.word_wrap || box_w <= 0.0f) return 0.0f;
    const float avail = box_w - gutter - d.padding.left - d.padding.right - 14.0f - 3.0f;
    return (avail > 6.0f) ? avail : 0.0f;
}
Str splice_editor_line(Arena* arena, Str line_text, uint32_t caret_col, Str comp) noexcept {
    if (!comp.chars || comp.length == 0u) return line_text;
    const uint32_t col = caret_col <= line_text.length ? caret_col : line_text.length;
    char* sp = arena->alloc<char>(size_t(line_text.length) + size_t(comp.length) + 1u);
    if (!sp) return line_text;
    if (col > 0u && line_text.chars) std::memcpy(sp, line_text.chars, col);
    std::memcpy(sp + col, comp.chars, comp.length);
    if (line_text.length > col && line_text.chars) std::memcpy(sp + col + comp.length, line_text.chars + col, line_text.length - col);
    sp[line_text.length + comp.length] = 0;
    return { sp, line_text.length + comp.length };
}
double measure_preedit_delta_h(
    Context* ctx,
    const CodeEditorDesc& d,
    TextBuffer* b,
    VarVirtualListCache* vc,
    uint32_t line,
    uint32_t col_byte,
    Str comp,
    float stride,
    float wrap_w,
    float* out_comp_h) noexcept
{
    const float base_h = editor_vlist_height(vc, line);
    if (!comp.chars || comp.length == 0u || wrap_w <= 0.0f) {
        if (out_comp_h) *out_comp_h = base_h;
        return 0.0;
    }
    const size_t mk = ctx->frame_arena.mark();
    const Str line_s = text_buffer_line(b, line, &ctx->frame_arena);
    const Str spl = splice_editor_line(&ctx->frame_arena, line_s, col_byte, comp);
    const uint32_t subs = count_wrapped_sublines(
        ctx, d.font_id, spl.chars, spl.length, d.font_size_px, 0.0f, false, d.tab_width, wrap_w);
    ctx->frame_arena.rewind(mk);
    const float comp_h = float(subs > 0u ? subs : 1u) * stride;
    if (out_comp_h) *out_comp_h = comp_h;
    return double(comp_h) - double(base_h);
}
float editor_measure_row_height(uint32_t row, void* ud) noexcept {
    const auto* m = static_cast<const EditorWrapMeasureCtx*>(ud);
    if (!m || !m->ctx || !m->buf || m->wrap_w <= 0.0f) {
        return m ? m->stride : 20.0f;
    }
    const size_t mark = m->ctx->frame_arena.mark();
    const Str s = text_buffer_line(m->buf, row, &m->ctx->frame_arena);
    const uint32_t sublines = count_wrapped_sublines(
        m->ctx, m->font_id, s.chars, s.length, m->font_size_px, 0.0f, false, m->tab_width, m->wrap_w);
    m->ctx->frame_arena.rewind(mark);
    return static_cast<float>(sublines > 0u ? sublines : 1u) * m->stride;
}
uint32_t editor_wrapped_hit(
    Context* ctx,
    const CodeEditorDesc& d,
    Str s,
    uint32_t target_sub,
    float local_x,
    float wrap_w,
    bool* out_affinity) noexcept
{
    if (out_affinity) *out_affinity = false;
    if (!s.chars || s.length == 0u) return 0u;
    WrappedSubline picked = {};
    bool matched = false;
    for_each_wrapped_subline(ctx, d.font_id, s.chars, s.length, d.font_size_px, 0.0f, false, d.tab_width, wrap_w,
        [&](const WrappedSubline& sub) noexcept {
            picked = sub;
            if (sub.sub_idx == target_sub) {
                matched = true;
                return false;
            }
            return true;
        });
    (void)matched;
    bool past_end = false;
    const uint32_t rel = hit_line_segment_x(
        ctx, d.font_id, s.chars + picked.start, picked.end - picked.start,
        d.font_size_px, 0.0f, false, d.tab_width, local_x, &past_end);
    if (out_affinity) {
        *out_affinity = (picked.start + rel == picked.end) && (picked.end < s.length) && past_end;
    }
    return picked.start + rel;
}
void step_wrapped_visual_line(
    Context* ctx,
    TextBuffer* b,
    const CodeEditorDesc& d,
    uint32_t cur_line,
    uint32_t cur_col,
    bool cur_aff,
    int32_t visual_delta,
    float wrap_w,
    float* io_pref_x,
    uint32_t* out_byte,
    bool* out_aff) noexcept
{
    const uint32_t n_lines = tb_lines(b);
    const size_t mk0 = ctx->frame_arena.mark();
    const Str cur_s = text_buffer_line(b, cur_line, &ctx->frame_arena);
    float cur_x = 0.0f;
    const WrappedSubline cur_sub = locate_wrapped_offset(
        ctx, d.font_id, cur_s.chars, cur_s.length, cur_col, cur_aff,
        d.font_size_px, 0.0f, false, d.tab_width, wrap_w, &cur_x);
    if (*io_pref_x < 0.0f) {
        *io_pref_x = cur_x;
    }
    const uint32_t line_subs = count_wrapped_sublines(
        ctx, d.font_id, cur_s.chars, cur_s.length, d.font_size_px, 0.0f, false, d.tab_width, wrap_w);
    ctx->frame_arena.rewind(mk0);
    const uint32_t cur_subs = (line_subs > 0u) ? line_subs : 1u;

    uint32_t t_line = cur_line;
    uint32_t t_sub = cur_sub.sub_idx;
    if (visual_delta < 0) {
        uint32_t rem = static_cast<uint32_t>(-visual_delta);
        if (rem <= t_sub) {
            t_sub -= rem;
        } else {
            rem -= t_sub;
            t_sub = 0u;
            while (rem > 0u) {
                if (t_line == 0u) {
                    if (visual_delta == -1 && cur_line == 0u && cur_sub.sub_idx == 0u) {
                        *out_byte = 0u;
                        *out_aff = false;
                        return;
                    }
                    t_sub = 0u;
                    break;
                }
                --t_line;
                const size_t mk = ctx->frame_arena.mark();
                const Str ls = text_buffer_line(b, t_line, &ctx->frame_arena);
                uint32_t cnt = count_wrapped_sublines(
                    ctx, d.font_id, ls.chars, ls.length, d.font_size_px, 0.0f, false, d.tab_width, wrap_w);
                ctx->frame_arena.rewind(mk);
                if (cnt == 0u) cnt = 1u;
                if (rem <= cnt) {
                    t_sub = cnt - rem;
                    rem = 0u;
                } else {
                    rem -= cnt;
                }
            }
        }
    } else if (visual_delta > 0) {
        uint32_t rem = static_cast<uint32_t>(visual_delta);
        if (t_sub + rem < cur_subs) {
            t_sub += rem;
        } else {
            rem -= (cur_subs - 1u - t_sub);
            t_sub = cur_subs - 1u;
            while (rem > 0u) {
                if (t_line + 1u >= n_lines) {
                    if (visual_delta == 1 && cur_line + 1u == n_lines && cur_sub.sub_idx + 1u == cur_subs) {
                        *out_byte = tb_len(b);
                        *out_aff = false;
                        return;
                    }
                    const size_t mk = ctx->frame_arena.mark();
                    const Str ls = text_buffer_line(b, t_line, &ctx->frame_arena);
                    const uint32_t cnt = count_wrapped_sublines(
                        ctx, d.font_id, ls.chars, ls.length, d.font_size_px, 0.0f, false, d.tab_width, wrap_w);
                    ctx->frame_arena.rewind(mk);
                    t_sub = (cnt > 0u) ? (cnt - 1u) : 0u;
                    break;
                }
                ++t_line;
                const size_t mk = ctx->frame_arena.mark();
                const Str ls = text_buffer_line(b, t_line, &ctx->frame_arena);
                uint32_t cnt = count_wrapped_sublines(
                    ctx, d.font_id, ls.chars, ls.length, d.font_size_px, 0.0f, false, d.tab_width, wrap_w);
                ctx->frame_arena.rewind(mk);
                if (cnt == 0u) cnt = 1u;
                if (rem <= cnt) {
                    t_sub = rem - 1u;
                    rem = 0u;
                } else {
                    rem -= cnt;
                }
            }
        }
    }

    const size_t mk_hit = ctx->frame_arena.mark();
    const Str ts = text_buffer_line(b, t_line, &ctx->frame_arena);
    const uint32_t hit = editor_wrapped_hit(ctx, d, ts, t_sub, *io_pref_x, wrap_w, out_aff);
    ctx->frame_arena.rewind(mk_hit);
    *out_byte = tb_start(b, t_line) + hit;
}
Str editor_slice(Context* ctx, const TextBuffer* b, uint32_t a, uint32_t z) noexcept {
    char* p = ctx->frame_arena.alloc<char>(size_t(z - a) + 1u);
    if (!p) return {};
    tb_copy(b, a, z, p);
    p[z - a] = 0;
    return { p, z - a };
}
float editor_gutter(const CodeEditorDesc& d, uint32_t lines, float eff_font_px) noexcept {
    const float tag_w = (d.line_decorator && d.gutter_tag_width_px > 0.0f) ? d.gutter_tag_width_px : 0.0f;
    if (!d.show_line_numbers && tag_w <= 0.0f) return 0.0f;
    if (d.gutter_width_px > 0.0f) return d.gutter_width_px + tag_w;
    float num_w = 0.0f;
    if (d.show_line_numbers) {
        uint32_t digits = 1u;
        while (lines >= 10u) { lines /= 10u; ++digits; }
        num_w = float(digits) * eff_font_px * 0.65f + 20.0f;
    }
    return num_w + tag_w;
}
}
void code_editor_set_cursor(uint32_t id, uint32_t a, uint32_t c, bool reveal) noexcept {
    auto* ctx = current_context();
    if (!ctx || !id) return;
    auto* ws = state_map_get_or_insert(&ctx->active_ui->states, id, ctx->active_ui->frame_index);
    WidgetCold* cold = ensure_widget_cold(ws);
    if (!cold) return;
    const bool valid_backing = cold->edit_text_buffer && cold->sel_text_epoch == ctx->arena_epoch;
    cold->edit_initialized = true;
    cold->edit_cursor = valid_backing ? tb_clamp(cold->edit_text_buffer, c) : c;
    cold->edit_sel_anchor = valid_backing ? tb_clamp(cold->edit_text_buffer, a) : a;
    cold->edit_wrap_affinity = false;
    cold->editor_reveal = reveal;
    cold->edit_preferred_x = -1.0f;
    request_redraw(ctx);
}
void code_editor_scroll_to_line(uint32_t id, uint32_t line, AlignY align) noexcept {
    auto* ctx = current_context();
    if (!ctx || !id) return;
    auto* ws = state_map_get_or_insert(&ctx->active_ui->states, id, ctx->active_ui->frame_index);
    WidgetCold* cold = ensure_widget_cold(ws);
    if (!cold) return;
    ws->is_user_element = true;
    double y = double(line) * cold->vlist_stride;
    float lh = cold->vlist_stride;
    if (auto* vc = editor_vlist_find(ctx->active_ui, id)) {
        if (vc->item_count > 0u) {
            const uint32_t clamped = (line < vc->item_count) ? line : (vc->item_count - 1u);
            y = editor_vlist_current_row_top(vc, clamped);
            lh = editor_vlist_current_height(vc, clamped);
            cold->vscroll_total_h = vc->total_stride_sum;
            cold->vscroll_max_y = std::fmax(0.0, cold->vscroll_total_h - (ws->viewport_dim.y > 0.0f ? double(ws->viewport_dim.y) : 400.0));
        }
    }
    if (align == AlignY::Center) y -= (double(ws->viewport_dim.y) - double(lh)) * 0.5;
    if (align == AlignY::Bottom) y -= double(ws->viewport_dim.y) - double(lh);
    cold->vscroll_pos_y = clamp_double(y, 0.0, cold->vscroll_max_y);
    cold->vscroll_vel_y = 0.0;
    cold->editor_scroll_pending = true;
    cold->editor_scroll_line = line;
    cold->editor_scroll_align = static_cast<uint8_t>(align);
    request_redraw(ctx);
}

void free_editor_ext_entries(WindowUiState* ui) noexcept {
    if (!ui) return;
    for (uint32_t i = 0u; i < ui->editor_ext_count; ++i) {
        std::free(ui->editor_exts[i].ghost_text);
        std::free(ui->editor_exts[i].search_query);
    }
    std::free(ui->editor_exts);
    ui->editor_exts = nullptr;
    ui->editor_ext_count = 0u;
    ui->editor_ext_cap = 0u;
}

void code_editor_set_inline_completion(uint32_t editor_id, Str ghost_text) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || editor_id == 0u) return;
    if (!ghost_text.chars || ghost_text.length == 0u) {
        if (EditorExtEntry* ext = find_editor_ext(ctx->active_ui, editor_id)) {
            ext->ghost_len = 0u;
            if (ext->ghost_text) ext->ghost_text[0] = '\0';
            request_redraw(ctx);
        }
        return;
    }
    if (EditorExtEntry* ext = ensure_editor_ext(ctx->active_ui, editor_id)) {
        if (ed_assign_str(&ext->ghost_text, &ext->ghost_len, &ext->ghost_cap, ghost_text)) {
            request_redraw(ctx);
        }
    }
}

bool code_editor_completion_accepted(uint32_t editor_id) noexcept {
    const Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || editor_id == 0u) return false;
    const EditorExtEntry* ext = find_editor_ext_const(ctx->active_ui, editor_id);
    return ext && ext->completion_accepted;
}

bool code_editor_completion_dismissed(uint32_t editor_id) noexcept {
    const Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || editor_id == 0u) return false;
    const EditorExtEntry* ext = find_editor_ext_const(ctx->active_ui, editor_id);
    return ext && ext->completion_dismissed;
}

void code_editor_set_search_highlights(uint32_t editor_id, Str query, bool case_sensitive) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || editor_id == 0u) return;
    if (!query.chars || query.length == 0u) {
        if (EditorExtEntry* ext = find_editor_ext(ctx->active_ui, editor_id)) {
            ext->search_len = 0u;
            if (ext->search_query) ext->search_query[0] = '\0';
            ext->search_case_sensitive = case_sensitive;
            request_redraw(ctx);
        }
        return;
    }
    if (EditorExtEntry* ext = ensure_editor_ext(ctx->active_ui, editor_id)) {
        if (ed_assign_str(&ext->search_query, &ext->search_len, &ext->search_cap, query)) {
            ext->search_case_sensitive = case_sensitive;
            request_redraw(ctx);
        }
    }
}

bool code_editor_set_fold(uint32_t editor_id, uint32_t start_line, uint32_t end_line, bool collapsed) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || editor_id == 0u || end_line <= start_line) return false;
    if (collapsed) {
        EditorExtEntry* ext = ensure_editor_ext(ctx->active_ui, editor_id);
        if (!ext) return false;
        for (uint32_t i = 0u; i < ext->fold_count; ++i) {
            if (ext->folds[i].start_line == start_line) {
                ext->folds[i].end_line = end_line;
                request_redraw(ctx);
                return true;
            }
        }
        if (ext->fold_count >= 64u) return false;
        ext->folds[ext->fold_count++] = { start_line, end_line };
        request_redraw(ctx);
        return true;
    }
    if (EditorExtEntry* ext = find_editor_ext(ctx->active_ui, editor_id)) {
        for (uint32_t i = 0u; i < ext->fold_count; ++i) {
            if (ext->folds[i].start_line == start_line && ext->folds[i].end_line == end_line) {
                const uint32_t tail = ext->fold_count - i - 1u;
                if (tail > 0u) {
                    std::memmove(ext->folds + i, ext->folds + i + 1u, size_t(tail) * sizeof(EditorExtEntry::FoldRange));
                }
                --ext->fold_count;
                request_redraw(ctx);
                break;
            }
        }
    }
    return true;
}

bool code_editor_is_line_folded(uint32_t editor_id, uint32_t line) noexcept {
    const Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || editor_id == 0u) return false;
    return ed_is_line_folded(find_editor_ext_const(ctx->active_ui, editor_id), line);
}

bool code_editor_add_cursor(uint32_t editor_id, uint32_t byte_offset) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || editor_id == 0u) return false;
    uint32_t off = byte_offset;
    uint32_t primary_off = UINT32_MAX;
    if (WidgetState* ws = state_map_find(&ctx->active_ui->states, editor_id); ws && ws->cold) {
        const WidgetCold* cold = ws->cold;
        if (cold->edit_text_buffer && cold->sel_text_epoch == ctx->arena_epoch) {
            off = tb_clamp(cold->edit_text_buffer, off);
            primary_off = tb_clamp(cold->edit_text_buffer, cold->edit_cursor);
        } else if (cold->edit_initialized) {
            primary_off = cold->edit_cursor;
        }
    }
    if (primary_off != UINT32_MAX && off == primary_off) return false;
    EditorExtEntry* ext = ensure_editor_ext(ctx->active_ui, editor_id);
    if (!ext) return false;
    for (uint32_t i = 0u; i < ext->extra_cursor_count; ++i) {
        if (ext->extra_cursors[i] == off) return false;
    }
    if (ext->extra_cursor_count >= 32u) return false;
    ext->extra_cursors[ext->extra_cursor_count++] = off;
    request_redraw(ctx);
    return true;
}

void code_editor_clear_extra_cursors(uint32_t editor_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || editor_id == 0u) return;
    if (EditorExtEntry* ext = find_editor_ext(ctx->active_ui, editor_id)) {
        if (ext->extra_cursor_count > 0u) {
            ext->extra_cursor_count = 0u;
            request_redraw(ctx);
        }
    }
}

uint32_t code_editor_cursor_count(uint32_t editor_id) noexcept {
    const Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || editor_id == 0u) return 0u;
    const EditorExtEntry* ext = find_editor_ext_const(ctx->active_ui, editor_id);
    return 1u + (ext ? ext->extra_cursor_count : 0u);
}
CodeEditorResult code_editor(const CodeEditorDesc& input) noexcept {
    auto* ctx = current_context();
    if (!ctx || !ctx->in_frame || !input.id || !input.buffer || !input.buffer->data) return {};
    const Theme& thm = active_theme(ctx);
    CodeEditorDesc d = input;
    d.scroll_fade_px=std::isfinite(d.scroll_fade_px) && d.scroll_fade_px>0 ? d.scroll_fade_px : 0;
    d.tab_width = d.tab_width ? d.tab_width : 4u;
    d.tab_width = d.tab_width > 32u ? 32u : d.tab_width;
    d.font_size_px = d.font_size_px > 0.0f ? d.font_size_px : 14.0f;
    d.background = resolve_theme_color(d.background, thm.bg_input);
    d.gutter_background = resolve_theme_color(d.gutter_background, thm.bg_popup);
    d.active_line_color = resolve_theme_color(d.active_line_color, thm.active_line);
    d.selection_color = resolve_theme_color(d.selection_color, thm.selection);
    d.gutter_active_text_color = resolve_theme_color(d.gutter_active_text_color, thm.text_secondary);
    d.gutter_text_color = resolve_theme_color(d.gutter_text_color, thm.text_disabled);
    d.text_color = resolve_theme_color(d.text_color, thm.text_code);
    d.caret_color = resolve_theme_color(d.caret_color, thm.accent);
    d.composition_color = resolve_theme_color(d.composition_color, thm.warning);
    d.border_color = resolve_theme_color(d.border_color, thm.border_control);
    d.focus_border_color = resolve_theme_color(d.focus_border_color, thm.accent);
    d.radius = resolve_theme_corners(d.radius, Corners::all(thm.radius_control_px));

    const float eff_font_px = scaled_font_span_px(ctx, d.font_id, d.font_size_px);
    const float stride = d.line_height_px > 0.0f ? d.line_height_px : eff_font_px * 1.5f;
    auto& ui = *ctx->active_ui;
    auto* ws = state_map_get_or_insert(&ui.states, d.id, ui.frame_index);
    WidgetCold* cold = ensure_widget_cold(ws);
    if (!cold) return {};
    ws->is_user_element = true;
    ws->is_focusable = true;
    ws->focus_scope = ui.modal_scope && focus_scope_contains(ui, ui.modal_scope) ? ui.modal_scope : ui.focus_scope;
    ws->is_virtual_list = true;
    ws->is_scroll_y = true;
    ws->is_text_input_writable = !d.read_only;
    cold->edit_text_buffer = d.buffer;
    cold->editor_show_scrollbar = d.show_scrollbar;
    cold->sel_text_epoch = ctx->arena_epoch;
    cold->sel_text_ptr = nullptr;
    cold->sel_text_len = 0u;
    cold->vlist_stride = stride;
    cold->edit_initialized = true;
    if (reserve_focus(&ui)) ui.focus_order[ui.focus_count++] = d.id;
    if (ui.modal_needs_focus && focus_scope_contains(ui, ui.modal_scope)) {
        ui.focus_id = d.id;
        ui.modal_needs_focus = false;
        clear_ime_composition(ctx);
    }

    TextBuffer* b = d.buffer;
    uint32_t c = tb_clamp(b, cold->edit_cursor);
    uint32_t a = tb_clamp(b, cold->edit_sel_anchor);
    if (c != cold->edit_cursor) cold->edit_wrap_affinity = false;
    const uint32_t menu_key=text_input_menu({.id=d.id,.read_only=d.read_only},tb_len(b),c!=a);
    ws=state_map_find(&ui.states,d.id);
    cold = ensure_widget_cold(ws);
    if (!cold) return {};
    const bool focus = ui.focus_id == d.id && (!ui.modal_scope || focus_scope_contains(ui, ui.modal_scope));
    bool changed = false;
    EditorExtEntry* ext = find_editor_ext(&ui, d.id);
    if (ext) {
        ext->completion_accepted = false;
        ext->completion_dismissed = false;
    }
    const auto sanitize_extra_cursors = [&](uint32_t primary_c) noexcept {
        if (!ext || ext->extra_cursor_count == 0u) return;
        uint32_t keep = 0u;
        for (uint32_t ek = 0u; ek < ext->extra_cursor_count; ++ek) {
            const uint32_t clamped = tb_clamp(b, ext->extra_cursors[ek]);
            if (clamped == primary_c) continue;
            bool dup = false;
            for (uint32_t ej = 0u; ej < keep; ++ej) {
                if (ext->extra_cursors[ej] == clamped) { dup = true; break; }
            }
            if (!dup) ext->extra_cursors[keep++] = clamped;
        }
        ext->extra_cursor_count = keep;
    };
    sanitize_extra_cursors(c);

    const float gutter = editor_gutter(d, tb_lines(b), eff_font_px);
    const Rect bounds = ws->last_box_logical;
    const float known_box_h = (bounds.h > 0.0f)
        ? bounds.h
        : ((d.height.kind == SizingKind::Fixed && d.height.value > 0.0f) ? d.height.value : 400.0f);
    const float view_h = known_box_h > d.padding.top + d.padding.bottom ? known_box_h - d.padding.top - d.padding.bottom : 400.0f;
    const float origin_x = bounds.x + d.padding.left + gutter;
    const float origin_y = bounds.y + d.padding.top;
    const float known_box_w = (bounds.w > 0.0f)
        ? bounds.w
        : ((d.width.kind == SizingKind::Fixed && d.width.value > 0.0f) ? d.width.value : 0.0f);
    const float wrap_w = d.word_wrap ? editor_wrap_width(d, known_box_w, gutter) : 0.0f;
    if (wrap_w > 0.0f) {
        cold->edit_scroll_x = 0.0f;
    } else {
        cold->edit_wrap_affinity = false;
    }

    const uint32_t pre_lines = tb_lines(b);
    EditorWrapMeasureCtx mctx = { ctx, b, d.font_id, d.font_size_px, stride, d.tab_width, wrap_w };
    VarVirtualListCache* vc = nullptr;
    if (wrap_w > 0.0f) {
        const bool external_dirty = (cold->edit_wrap_version != 0u && b->version != cold->edit_wrap_version);
        const bool style_dirty = (std::fabs(cold->edit_wrap_width - wrap_w) > 0.5f) ||
            (cold->edit_wrap_tab_width != d.tab_width) ||
            (std::fabs(cold->edit_wrap_font_size - d.font_size_px) > 0.01f) ||
            (cold->edit_wrap_font_id != d.font_id) || (cold->edit_wrap_font_revision != ctx->atlas.font_revision);
        vc = editor_vlist_prepare(&ui, d.id, pre_lines, stride, editor_measure_row_height, &mctx);
        if (vc && (external_dirty || style_dirty)) {
            editor_vlist_reset(vc);
        }
    } else {
        if (auto* stale_vc = editor_vlist_find(&ui, d.id)) {
            editor_vlist_free(stale_vc);
        }
        cold->edit_wrap_version = 0u;
        cold->edit_wrap_lines = 0u;
        cold->edit_wrap_width = 0.0f;
    }

    int32_t hovered_line = -1;
    int32_t clicked_gutter_line = -1;
    if (pointer_over_id(ctx->active_ui, d.id)) {
        const bool in_gutter = gutter > 0.0f && ui.pointer_pos.x >= bounds.x && ui.pointer_pos.x < origin_x;
        set_cursor(ui.pointer_pos.x >= origin_x && ui.pointer_pos.x < bounds.x + bounds.w - 14.0f
            ? Cursor::IBeam
            : ((in_gutter && d.line_decorator) ? Cursor::Hand : Cursor::Arrow));
        if (bounds.w > 0.0f && pre_lines > 0u && ui.pointer_pos.y >= origin_y && ui.pointer_pos.y < origin_y + view_h) {
            uint32_t hit_line = 0u;
            const double y_doc = double(ui.pointer_pos.y - origin_y) + cold->vscroll_pos_y;
            if (wrap_w > 0.0f && vc) {
                hit_line = (y_doc <= 0.0) ? 0u
                    : ((y_doc >= vc->total_stride_sum) ? (pre_lines - 1u) : editor_vlist_find_first_at_y(vc, y_doc));
            } else {
                hit_line = static_cast<uint32_t>(clamp_double(std::floor(y_doc / double(stride)), 0.0, double(pre_lines - 1u)));
            }
            hovered_line = static_cast<int32_t>(hit_line);
            if (in_gutter && ws->pressed_this_frame) {
                clicked_gutter_line = hovered_line;
            }
        }
    }
    if (bounds.w > 0.0f && (ws->pressed_this_frame || (ui.active_id == d.id && ws->pressed_down))) {
        const bool dragging = !ws->pressed_this_frame;
        if (dragging) {
            const float dy = ui.pointer_pos.y < origin_y ? ui.pointer_pos.y - origin_y
                : (ui.pointer_pos.y > origin_y + view_h ? ui.pointer_pos.y - origin_y - view_h : 0.0f);
            cold->vscroll_pos_y = clamp_double(cold->vscroll_pos_y + double(dy) * 0.2, 0.0, cold->vscroll_max_y);
            if (wrap_w <= 0.0f) {
                const float dx = ui.pointer_pos.x < origin_x ? ui.pointer_pos.x - origin_x
                    : (ui.pointer_pos.x > bounds.x + bounds.w - 16.0f ? ui.pointer_pos.x - bounds.x - bounds.w + 16.0f : 0.0f);
                cold->edit_scroll_x = std::fmax(0.0f, cold->edit_scroll_x + dx * 0.2f);
            }
            request_redraw(ctx);
        }
        uint32_t hit = 0u;
        bool hit_aff = false;
        uint32_t line = 0u;
        uint32_t start = 0u;
        Str s = {};
        if (wrap_w > 0.0f && vc) {
            const bool has_pre = focus && !d.read_only && ui.ime_comp_len > 0u && pre_lines > 0u;
            const TextLineCol pre_lc = has_pre ? text_buffer_line_col(b, c) : TextLineCol{};
            float pre_comp_h = stride;
            const double pre_delta_h = has_pre
                ? measure_preedit_delta_h(ctx, d, b, vc, pre_lc.line, pre_lc.col_byte, { ui.ime_comp_utf8, ui.ime_comp_len }, stride, wrap_w, &pre_comp_h)
                : 0.0;
            const double pre_top = (pre_delta_h != 0.0) ? editor_vlist_row_top(vc, pre_lc.line) : 0.0;
            const double y_doc = double(ui.pointer_pos.y - origin_y) + cold->vscroll_pos_y;
            uint32_t sub_idx = 0u;
            if (y_doc <= 0.0) {
                line = 0u;
                sub_idx = 0u;
            } else if (y_doc >= vc->total_stride_sum + pre_delta_h) {
                line = (pre_lines > 0u) ? (pre_lines - 1u) : 0u;
                sub_idx = UINT32_MAX;
            } else if (pre_delta_h != 0.0 && y_doc >= pre_top) {
                if (y_doc < pre_top + double(pre_comp_h)) {
                    line = pre_lc.line;
                    sub_idx = static_cast<uint32_t>(std::fmax(0.0, std::floor((y_doc - pre_top) / double(stride))));
                } else {
                    line = editor_vlist_find_first_at_y(vc, y_doc - pre_delta_h);
                    if (line <= pre_lc.line && pre_lc.line + 1u < pre_lines) {
                        line = pre_lc.line + 1u;
                    }
                    const double line_top = editor_vlist_row_top(vc, line) + pre_delta_h;
                    sub_idx = static_cast<uint32_t>(std::fmax(0.0, std::floor((y_doc - line_top) / double(stride))));
                }
            } else {
                line = editor_vlist_find_first_at_y(vc, y_doc);
                const double line_top = editor_vlist_row_top(vc, line);
                const double rel_y = y_doc - line_top;
                sub_idx = static_cast<uint32_t>(std::fmax(0.0, std::floor(rel_y / double(stride))));
            }
            s = text_buffer_line(b, line, &ctx->frame_arena);
            start = tb_start(b, line);
            hit = editor_wrapped_hit(ctx, d, s, sub_idx, ui.pointer_pos.x - origin_x, wrap_w, &hit_aff);
        } else {
            const double line_y = (double(ui.pointer_pos.y - origin_y) + cold->vscroll_pos_y) / double(stride);
            line = static_cast<uint32_t>(clamp_double(std::floor(line_y), 0.0, double(pre_lines - 1u)));
            s = text_buffer_line(b, line, &ctx->frame_arena);
            start = tb_start(b, line);
            hit = editor_hit(ctx, d, s, ui.pointer_pos.x - origin_x + cold->edit_scroll_x);
        }
        if (ws->pressed_this_frame && ui.last_mouse_clicks >= 3u) {
            a = start;
            c = text_buffer_line_end(b, line) + (line + 1u < pre_lines ? 1u : 0u);
            cold->edit_wrap_affinity = false;
        } else if (ws->pressed_this_frame && ui.last_mouse_clicks == 2u) {
            utf8_word_bounds_at(s.chars, s.length, hit, &a, &c);
            a += start;
            c += start;
            cold->edit_wrap_affinity = false;
        } else {
            c = start + hit;
            if (ws->pressed_this_frame && !(ui.frame_key_mods & Mod_Shift)) {
                a = c;
            }
            cold->edit_wrap_affinity = (wrap_w > 0.0f) && hit_aff;
        }
        cold->edit_preferred_x = -1.0f;
        if (ws->pressed_this_frame && focus && ui.ime_comp_len > 0u) {
            cancel_ime_composition(ctx);
        }
    }

    bool force_wrap_reset = false;
    uint32_t splice_line = UINT32_MAX;
    uint32_t splice_old_span = 0u;
    uint32_t splice_new_span = 0u;
    bool replaced_by_a11y = false;
    PendingA11yAction command = {};
    while (!d.read_only && a11y_take(d.id, A11yAction::SetValue, &command)) {
        if (text_buffer_replace(b, 0u, tb_len(b), { command.text, command.length }, true, c, a, command.length, command.length, 0u, ui.elapsed_seconds)) {
            changed = true;
            c = a = tb_len(b);
            cold->editor_reveal = true;
            cold->edit_preferred_x = -1.0f;
            cold->edit_wrap_affinity = false;
            force_wrap_reset = true;
            replaced_by_a11y = true;
        }
    }
    if (replaced_by_a11y && focus) cancel_ime_composition(ctx);
    if (focus && !d.read_only) {
        ui.ime_focused_id = d.id;
        ui.ime_focused_password = false;
    }

    const auto replace = [&](uint32_t lo, uint32_t hi, Str ins, uint32_t nc, uint32_t na, uint8_t group = 0u) noexcept {
        uint32_t l_lo = 0u;
        uint32_t l_hi = 0u;
        uint32_t pre_rep_lines = 0u;
        if (wrap_w > 0.0f && !force_wrap_reset) {
            l_lo = text_buffer_line_col(b, lo).line;
            l_hi = text_buffer_line_col(b, hi).line;
            pre_rep_lines = tb_lines(b);
        }
        if (text_buffer_replace(b, lo, hi, ins, true, c, a, nc, na, group, ui.elapsed_seconds)) {
            c = nc;
            a = na;
            changed = true;
            cold->editor_reveal = true;
            cold->edit_preferred_x = -1.0f;
            cold->edit_wrap_affinity = false;
            if (wrap_w > 0.0f && !force_wrap_reset) {
                const uint32_t post_rep_lines = tb_lines(b);
                const uint32_t old_span = (l_hi - l_lo) + 1u;
                const uint32_t new_span = (post_rep_lines + old_span >= pre_rep_lines)
                    ? (post_rep_lines + old_span - pre_rep_lines)
                    : 1u;
                if (old_span <= 16u && new_span <= 16u && splice_line == UINT32_MAX) {
                    splice_line = l_lo;
                    splice_old_span = old_span;
                    splice_new_span = new_span;
                } else if (splice_line == l_lo && old_span == 1u && new_span == 1u &&
                           splice_old_span == 1u && splice_new_span == 1u) {
                    // Repeated single-line edit in the same frame keeps single-row update.
                } else {
                    force_wrap_reset = true;
                }
            }
        }
    };

    if (focus) {
        const PendingTextOp menu_op={.kind=TextEditOpKind::KeyPress,.mod=SDL_KMOD_CTRL,.key=menu_key};
        for (uint32_t i = 0u; i < ui.frame_text_op_count+(menu_key?1u:0u); ++i) {
            const auto& op = i<ui.frame_text_op_count ? ui.frame_text_ops[i] : menu_op;
            uint32_t lo = c < a ? c : a;
            uint32_t hi = c > a ? c : a;
            if (op.kind == TextEditOpKind::InsertText) {
                if (!d.read_only) {
                    const Str ins = { ui.frame_text_utf8 + op.text_offset, op.text_len };
                    uint32_t n = ins.length;
                    for (uint32_t j = 0u; j + 1u < ins.length; ++j) {
                        if (ins.chars[j] == '\r' && ins.chars[j + 1u] == '\n') --n;
                    }
                    if (ext && ext->extra_cursor_count > 0u) {
                        struct MultiCursorEdit { uint32_t start, end; bool is_primary; };
                        MultiCursorEdit items[33] = {};
                        uint32_t item_count = 0u;
                        items[item_count++] = { lo, hi, true };
                        for (uint32_t ek = 0u; ek < ext->extra_cursor_count && item_count < 33u; ++ek) {
                            const uint32_t ep = tb_clamp(b, ext->extra_cursors[ek]);
                            if (lo < hi && ep >= lo && ep <= hi) continue;
                            bool dup = false;
                            for (uint32_t ej = 0u; ej < item_count; ++ej) {
                                if (items[ej].start == ep) { dup = true; break; }
                            }
                            if (!dup) items[item_count++] = { ep, ep, false };
                        }
                        for (uint32_t ej = 1u; ej < item_count; ++ej) {
                            const MultiCursorEdit key = items[ej];
                            uint32_t ek = ej;
                            while (ek > 0u && items[ek - 1u].start > key.start) {
                                items[ek] = items[ek - 1u];
                                --ek;
                            }
                            items[ek] = key;
                        }
                        TextEdit edits[33] = {};
                        for (uint32_t ej = 0u; ej < item_count; ++ej) {
                            edits[ej] = { items[ej].start, items[ej].end, ins };
                        }
                        if (text_buffer_apply_edits(b, text_buffer_revision(b), std::span<const TextEdit>(edits, item_count))) {
                            uint32_t added_so_far = 0u;
                            uint32_t removed_so_far = 0u;
                            uint32_t out_extra = 0u;
                            for (uint32_t ej = 0u; ej < item_count; ++ej) {
                                const uint32_t rem = items[ej].end - items[ej].start;
                                const uint32_t updated = items[ej].start + added_so_far - removed_so_far + n;
                                if (items[ej].is_primary) {
                                    c = a = updated;
                                } else {
                                    ext->extra_cursors[out_extra++] = updated;
                                }
                                added_so_far += n;
                                removed_so_far += rem;
                            }
                            ext->extra_cursor_count = out_extra;
                            changed = true;
                            cold->editor_reveal = true;
                            cold->edit_preferred_x = -1.0f;
                            cold->edit_wrap_affinity = false;
                            force_wrap_reset = true;
                        }
                    } else {
                        replace(lo, hi, ins, lo + n, lo + n, lo == hi ? 1u : 0u);
                    }
                }
                continue;
            }
            const bool shift = (op.mod & SDL_KMOD_SHIFT) != 0u;
            const bool primary = (op.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0u;
            const bool word = (op.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0u;
            if (primary) {
                if (op.key == SDLK_A) {
                    a = 0u;
                    c = tb_len(b);
                    cold->edit_wrap_affinity = false;
                    cold->edit_preferred_x = -1.0f;
                    continue;
                }
                if (op.key == SDLK_C || op.key == SDLK_X) {
                    if (hi > lo) {
                        const Str s = editor_slice(ctx, b, lo, hi);
                        if (s.chars) set_clipboard_text(s.chars);
                        if (op.key == SDLK_X && !d.read_only) replace(lo, hi, {}, lo, lo);
                    }
                    continue;
                }
                if (op.key == SDLK_V) {
                    if (!d.read_only) {
                        const Str ins = get_clipboard_text(&ctx->frame_arena);
                        uint32_t n = ins.length;
                        for (uint32_t j = 0u; j + 1u < ins.length; ++j) {
                            if (ins.chars[j] == '\r' && ins.chars[j + 1u] == '\n') --n;
                        }
                        replace(lo, hi, ins, lo + n, lo + n);
                    }
                    continue;
                }
                if (op.key == SDLK_Z || op.key == SDLK_Y) {
                    if (!d.read_only) {
                        const bool pass = (op.key == SDLK_Y || shift) ? text_buffer_redo(b, &c, &a) : text_buffer_undo(b, &c, &a);
                        changed |= pass;
                        if (pass) {
                            cold->editor_reveal = true;
                            cold->edit_preferred_x = -1.0f;
                            cold->edit_wrap_affinity = false;
                            force_wrap_reset = true;
                        }
                    }
                    continue;
                }
            }
            const TextLineCol lc = text_buffer_line_col(b, c);
            const uint32_t start = tb_start(b, lc.line);
            const Str line = text_buffer_line(b, lc.line, &ctx->frame_arena);
            uint32_t next = c;
            bool next_aff = false;
            bool moved = false;
            if (op.key == SDLK_LEFT || op.key == SDLK_RIGHT || op.key == SDLK_BACKSPACE || op.key == SDLK_DELETE) {
                const bool left = op.key == SDLK_LEFT || op.key == SDLK_BACKSPACE;
                if (left) {
                    next = lc.col_byte
                        ? start + (word ? utf8_word_left(line.chars, line.length, lc.col_byte) : utf8_prev_grapheme(line.chars, line.length, lc.col_byte))
                        : (c ? c - 1u : 0u);
                } else {
                    next = lc.col_byte < line.length
                        ? start + (word ? utf8_word_right(line.chars, line.length, lc.col_byte) : utf8_next_grapheme(line.chars, line.length, lc.col_byte))
                        : (c < tb_len(b) ? c + 1u : c);
                }
                if (word && left && !lc.col_byte && lc.line) {
                    const Str previous = text_buffer_line(b, lc.line - 1u, &ctx->frame_arena);
                    next = tb_start(b, lc.line - 1u) + utf8_word_left(previous.chars, previous.length, previous.length);
                } else if (word && !left && lc.col_byte == line.length && lc.line + 1u < tb_lines(b)) {
                    const Str following = text_buffer_line(b, lc.line + 1u, &ctx->frame_arena);
                    next = tb_start(b, lc.line + 1u) + utf8_word_right(following.chars, following.length, 0u);
                }
                if (op.key == SDLK_BACKSPACE || op.key == SDLK_DELETE) {
                    if (!d.read_only) {
                        const bool collapsed = (lo == hi);
                        if (collapsed) {
                            lo = c < next ? c : next;
                            hi = c > next ? c : next;
                        }
                        const uint8_t del_group = (!collapsed || word) ? 0u : (op.key == SDLK_BACKSPACE ? 2u : 3u);
                        replace(lo, hi, {}, lo, lo, del_group);
                    }
                    continue;
                }
                if (lo != hi && !shift && !word) next = left ? lo : hi;
                cold->edit_preferred_x = -1.0f;
                next_aff = false;
                moved = true;
            } else if (op.key == SDLK_HOME || op.key == SDLK_END) {
                if (primary) {
                    next = (op.key == SDLK_HOME) ? 0u : tb_len(b);
                    next_aff = false;
                } else if (wrap_w > 0.0f) {
                    const WrappedSubline sub = locate_wrapped_offset(
                        ctx, d.font_id, line.chars, line.length, lc.col_byte, cold->edit_wrap_affinity,
                        d.font_size_px, 0.0f, false, d.tab_width, wrap_w, nullptr);
                    if (op.key == SDLK_END) {
                        next = start + sub.end;
                        next_aff = (sub.end < line.length);
                    } else if (sub.sub_idx > 0u) {
                        next = start + sub.start;
                        next_aff = false;
                    } else {
                        uint32_t indent = 0u;
                        while (indent < line.length && (line.chars[indent] == ' ' || line.chars[indent] == '\t')) ++indent;
                        next = start + (lc.col_byte == indent ? 0u : indent);
                        next_aff = false;
                    }
                } else if (op.key == SDLK_END) {
                    next = start + line.length;
                } else {
                    uint32_t indent = 0u;
                    while (indent < line.length && (line.chars[indent] == ' ' || line.chars[indent] == '\t')) ++indent;
                    next = start + (lc.col_byte == indent ? 0u : indent);
                }
                cold->edit_preferred_x = -1.0f;
                moved = true;
            } else if (op.key == SDLK_UP || op.key == SDLK_DOWN || op.key == SDLK_PAGEUP || op.key == SDLK_PAGEDOWN) {
                const bool page = op.key == SDLK_PAGEUP || op.key == SDLK_PAGEDOWN;
                const int32_t delta = (op.key == SDLK_UP || op.key == SDLK_PAGEUP ? -1 : 1) *
                    (page ? static_cast<int32_t>(std::fmax(1.0f, std::floor(view_h / stride))) : 1);
                if (wrap_w > 0.0f) {
                    step_wrapped_visual_line(
                        ctx, b, d, lc.line, lc.col_byte, cold->edit_wrap_affinity,
                        delta, wrap_w, &cold->edit_preferred_x, &next, &next_aff);
                } else {
                    if (cold->edit_preferred_x < 0.0f) cold->edit_preferred_x = editor_x(ctx, d, line, lc.col_byte);
                    const uint32_t target = static_cast<uint32_t>(clamp_double(double(lc.line) + delta, 0.0, double(tb_lines(b) - 1u)));
                    next = tb_start(b, target) + editor_hit(ctx, d, text_buffer_line(b, target, &ctx->frame_arena), cold->edit_preferred_x);
                    next_aff = false;
                }
                if (page) cold->vscroll_pos_y = clamp_double(cold->vscroll_pos_y + double(delta) * stride, 0.0, cold->vscroll_max_y);
                moved = true;
            } else if (op.key == SDLK_ESCAPE) {
                if (!ui.frame_escape_consumed_by_ime) {
                    if (!d.read_only && ext && ext->ghost_len > 0u) {
                        ext->ghost_len = 0u;
                        if (ext->ghost_text) ext->ghost_text[0] = '\0';
                        ext->completion_dismissed = true;
                        request_redraw(ctx);
                        continue;
                    }
                    a = c;
                    cold->edit_wrap_affinity = false;
                }
            } else if (!d.read_only && (op.key == SDLK_RETURN || op.key == SDLK_KP_ENTER)) {
                uint32_t indent = 0u;
                while (indent < lc.col_byte && (line.chars[indent] == ' ' || line.chars[indent] == '\t')) ++indent;
                char* p = ctx->frame_arena.alloc<char>(size_t(indent) + 1u);
                if (p) {
                    p[0] = '\n';
                    std::memcpy(p + 1, line.chars, indent);
                    replace(lo, hi, { p, indent + 1u }, lo + indent + 1u, lo + indent + 1u);
                }
            } else if (!d.read_only && op.key == SDLK_TAB) {
                if (!shift && !primary && ext && ext->ghost_len > 0u) {
                    const Str ghost = ctx->frame_arena.push_str(ext->ghost_text, ext->ghost_len);
                    if (ghost.chars) {
                        uint32_t gn = ghost.length;
                        for (uint32_t gj = 0u; gj + 1u < ghost.length; ++gj) {
                            if (ghost.chars[gj] == '\r' && ghost.chars[gj + 1u] == '\n') --gn;
                        }
                        replace(c, c, ghost, c + gn, c + gn);
                        ext->ghost_len = 0u;
                        if (ext->ghost_text) ext->ghost_text[0] = '\0';
                        ext->completion_accepted = true;
                    }
                    continue;
                }
                const auto first = text_buffer_line_col(b, lo);
                const auto last = text_buffer_line_col(b, hi);
                const bool block = first.line != last.line;
                if (block || shift) {
                    const uint32_t end_line = block && last.col_byte == 0u ? last.line - 1u : last.line;
                    const uint32_t begin = tb_start(b, first.line);
                    const uint32_t end = text_buffer_line_end(b, end_line);
                    const uint32_t count = end_line - first.line + 1u;
                    char* p = ctx->frame_arena.alloc<char>(size_t(end - begin) + size_t(count) * d.tab_width + 1u);
                    if (!p) continue;
                    uint32_t used = 0u, newc = c, newa = a;
                    for (uint32_t li = first.line; li <= end_line; ++li) {
                        const Str s = text_buffer_line(b, li, &ctx->frame_arena);
                        const uint32_t ls = tb_start(b, li);
                        uint32_t removed = 0u;
                        if (shift) {
                            while (removed < s.length && removed < d.tab_width && s.chars[removed] == ' ') ++removed;
                            if (!removed && s.length && s.chars[0] == '\t') removed = 1u;
                        }
                        const uint32_t added = shift ? 0u : d.tab_width;
                        const auto adjust = [&](uint32_t original, uint32_t current) noexcept {
                            if (original < ls) return current;
                            if (original < ls + removed) return current - (original - ls);
                            return current - removed + added;
                        };
                        newc = adjust(c, newc);
                        newa = adjust(a, newa);
                        for (uint32_t j = 0u; j < added; ++j) p[used++] = ' ';
                        std::memcpy(p + used, s.chars + removed, s.length - removed);
                        used += s.length - removed;
                        if (li < end_line) p[used++] = '\n';
                    }
                    replace(begin, end, { p, used }, newc, newa);
                } else {
                    char spaces[32];
                    std::memset(spaces, ' ', d.tab_width);
                    replace(lo, hi, { spaces, d.tab_width }, lo + d.tab_width, lo + d.tab_width);
                }
            }
            if (moved) {
                c = next;
                if (!shift) a = c;
                cold->edit_wrap_affinity = (wrap_w > 0.0f) && next_aff;
                cold->editor_reveal = true;
            }
        }
    }

    const uint32_t clamped_c = tb_clamp(b, c);
    if (clamped_c != c) cold->edit_wrap_affinity = false;
    c = clamped_c;
    a = tb_clamp(b, a);
    cold->edit_cursor = c;
    cold->edit_sel_anchor = a;
    sanitize_extra_cursors(c);
    const auto lc = text_buffer_line_col(b, c);

    const bool composing = focus && !d.read_only && ui.ime_comp_len > 0u;
    const uint32_t post_lines = tb_lines(b);

    struct HiddenSpan { uint32_t first_line, last_line; };
    HiddenSpan hspans[64] = {};
    uint32_t hspan_count = 0u;
    uint32_t total_hidden_lines = 0u;
    if (ext && ext->fold_count > 0u && post_lines > 1u) {
        for (uint32_t fi = 0u; fi < ext->fold_count; ++fi) {
            const uint32_t h0 = ext->folds[fi].start_line + 1u;
            const uint32_t h1 = (ext->folds[fi].end_line < post_lines) ? ext->folds[fi].end_line : (post_lines - 1u);
            if (h0 <= h1) hspans[hspan_count++] = { h0, h1 };
        }
        for (uint32_t fi = 1u; fi < hspan_count; ++fi) {
            const HiddenSpan key = hspans[fi];
            uint32_t fj = fi;
            while (fj > 0u && hspans[fj - 1u].first_line > key.first_line) {
                hspans[fj] = hspans[fj - 1u];
                --fj;
            }
            hspans[fj] = key;
        }
        uint32_t merged = 0u;
        for (uint32_t fi = 0u; fi < hspan_count; ++fi) {
            if (merged > 0u && hspans[fi].first_line <= hspans[merged - 1u].last_line + 1u) {
                if (hspans[fi].last_line > hspans[merged - 1u].last_line) {
                    hspans[merged - 1u].last_line = hspans[fi].last_line;
                }
            } else {
                hspans[merged++] = hspans[fi];
            }
        }
        hspan_count = merged;
        for (uint32_t fi = 0u; fi < hspan_count; ++fi) {
            total_hidden_lines += (hspans[fi].last_line - hspans[fi].first_line + 1u);
        }
    }
    const uint32_t visible_line_count = (post_lines > total_hidden_lines) ? (post_lines - total_hidden_lines) : (post_lines > 0u ? 1u : 0u);
    const auto doc_to_vis = [&](uint32_t doc_l) noexcept -> uint32_t {
        uint32_t skipped = 0u;
        for (uint32_t fi = 0u; fi < hspan_count; ++fi) {
            if (doc_l < hspans[fi].first_line) break;
            if (doc_l <= hspans[fi].last_line) {
                skipped += (doc_l - hspans[fi].first_line);
                break;
            }
            skipped += (hspans[fi].last_line - hspans[fi].first_line + 1u);
        }
        return doc_l - skipped;
    };
    const auto vis_to_doc = [&](uint32_t vis_l) noexcept -> uint32_t {
        uint32_t doc_l = vis_l;
        for (uint32_t fi = 0u; fi < hspan_count; ++fi) {
            if (doc_l < hspans[fi].first_line) break;
            doc_l += (hspans[fi].last_line - hspans[fi].first_line + 1u);
        }
        return doc_l < post_lines ? doc_l : post_lines;
    };

    if (wrap_w > 0.0f) {
        if (!vc || force_wrap_reset) {
            vc = editor_vlist_prepare(&ui, d.id, post_lines, stride, editor_measure_row_height, &mctx);
            if (vc && force_wrap_reset && post_lines == pre_lines) {
                editor_vlist_reset(vc);
            }
        } else if (post_lines != pre_lines) {
            if (splice_line == UINT32_MAX ||
                !editor_vlist_splice(vc, splice_line, splice_old_span, splice_new_span))
            {
                vc = editor_vlist_prepare(&ui, d.id, post_lines, stride, editor_measure_row_height, &mctx);
            }
        } else if (splice_line != UINT32_MAX) {
            for (uint32_t row = splice_line; row < splice_line + splice_new_span && row < post_lines; ++row) {
                vlist_set_row_height_direct(vc, row, editor_measure_row_height(row, &mctx));
            }
        }
        cold->edit_wrap_version = b->version;
        cold->edit_wrap_lines = post_lines;
        cold->edit_wrap_width = wrap_w;
        cold->edit_wrap_tab_width = d.tab_width;
        cold->edit_wrap_font_size = d.font_size_px;
        cold->edit_wrap_font_id = d.font_id;
        cold->edit_wrap_font_revision = ctx->atlas.font_revision;
        cold->vscroll_total_h = (vc ? vc->total_stride_sum : double(visible_line_count) * stride) + d.padding.top + d.padding.bottom;
    } else {
        cold->vscroll_total_h = double(visible_line_count) * stride + d.padding.top + d.padding.bottom;
    }
    cold->vscroll_max_y = std::fmax(0.0, cold->vscroll_total_h - double(known_box_h));

    float active_caret_x = 0.0f;
    double active_caret_y = double(hspan_count > 0u ? doc_to_vis(lc.line) : lc.line) * double(stride);
    float comp_active_h = stride;
    double preedit_delta_h = 0.0;
    {
        const size_t mk_cur = ctx->frame_arena.mark();
        const Str cur_line_str = text_buffer_line(b, lc.line, &ctx->frame_arena);
        const Str comp_str = composing ? Str{ ui.ime_comp_utf8, ui.ime_comp_len } : Str{};
        const Str eval_str = composing ? splice_editor_line(&ctx->frame_arena, cur_line_str, lc.col_byte, comp_str) : cur_line_str;
        const uint32_t eval_col = composing ? (lc.col_byte + ui.ime_comp_cursor_byte) : lc.col_byte;
        const bool eval_aff = composing ? false : cold->edit_wrap_affinity;
        if (wrap_w > 0.0f && vc) {
            const float base_h = editor_vlist_height(vc, lc.line);
            comp_active_h = base_h;
            if (composing) {
                const uint32_t comp_subs = count_wrapped_sublines(
                    ctx, d.font_id, eval_str.chars, eval_str.length, d.font_size_px, 0.0f, false, d.tab_width, wrap_w);
                comp_active_h = float(comp_subs > 0u ? comp_subs : 1u) * stride;
                preedit_delta_h = double(comp_active_h) - double(base_h);
            }
            const WrappedSubline csub = locate_wrapped_offset(
                ctx, d.font_id, eval_str.chars, eval_str.length, eval_col, eval_aff,
                d.font_size_px, 0.0f, false, d.tab_width, wrap_w, &active_caret_x);
            active_caret_y = editor_vlist_row_top(vc, lc.line) + double(csub.sub_idx) * double(stride);
        } else {
            active_caret_x = editor_x(ctx, d, eval_str, eval_col);
        }
        ctx->frame_arena.rewind(mk_cur);
    }
    if (wrap_w > 0.0f && vc) {
        cold->vscroll_total_h = vc->total_stride_sum + preedit_delta_h + d.padding.top + d.padding.bottom;
        cold->vscroll_max_y = std::fmax(0.0, cold->vscroll_total_h - double(known_box_h));
    }

    if (cold->editor_scroll_pending) {
        const uint32_t clamped = post_lines > 0u ? (cold->editor_scroll_line < post_lines ? cold->editor_scroll_line : post_lines - 1u) : 0u;
        double y = (wrap_w > 0.0f && vc) ? editor_vlist_row_top(vc, clamped) : double(hspan_count > 0u ? doc_to_vis(clamped) : clamped) * double(stride);
        float lh = (wrap_w > 0.0f && vc) ? editor_vlist_height(vc, clamped) : stride;
        if (wrap_w > 0.0f && vc) {
            if (preedit_delta_h != 0.0) {
                if (clamped > lc.line) y += preedit_delta_h;
                else if (clamped == lc.line) lh = comp_active_h;
            }
            cold->vscroll_total_h = vc->total_stride_sum + preedit_delta_h + d.padding.top + d.padding.bottom;
            cold->vscroll_max_y = std::fmax(0.0, cold->vscroll_total_h - double(known_box_h));
        }
        const AlignY align = static_cast<AlignY>(cold->editor_scroll_align);
        if (align == AlignY::Center) y -= (double(view_h) - double(lh)) * 0.5;
        if (align == AlignY::Bottom) y -= double(view_h) - double(lh);
        cold->vscroll_pos_y = clamp_double(y, 0.0, cold->vscroll_max_y);
        cold->vscroll_vel_y = 0.0;
        cold->editor_scroll_pending = false;
    }

    if (cold->editor_reveal || (composing && ui.frame_ime_comp_edited)) {
        if (active_caret_y < cold->vscroll_pos_y) cold->vscroll_pos_y = active_caret_y;
        if (active_caret_y + double(stride) > cold->vscroll_pos_y + double(view_h)) {
            cold->vscroll_pos_y = active_caret_y + double(stride) - double(view_h);
        }
        if (wrap_w > 0.0f) {
            cold->edit_scroll_x = 0.0f;
        } else {
            const float width = std::fmax(20.0f, bounds.w - gutter - d.padding.left - d.padding.right - 16.0f);
            if (active_caret_x < cold->edit_scroll_x) cold->edit_scroll_x = active_caret_x;
            if (active_caret_x > cold->edit_scroll_x + width) cold->edit_scroll_x = active_caret_x - width;
        }
        cold->editor_reveal = false;
    }
    cold->vscroll_pos_y = clamp_double(cold->vscroll_pos_y, 0.0, cold->vscroll_max_y);

    uint32_t first = 0u;
    uint32_t end = 0u;
    uint32_t vis_count = 0u;
    double cur_top = 0.0;
    if (wrap_w > 0.0f && vc && post_lines > 0u) {
        const double active_top = (preedit_delta_h != 0.0) ? editor_vlist_row_top(vc, lc.line) : 0.0;
        if (preedit_delta_h != 0.0 && cold->vscroll_pos_y > active_top) {
            if (cold->vscroll_pos_y < active_top + double(comp_active_h)) {
                first = lc.line;
            } else {
                first = editor_vlist_find_first_at_y(vc, cold->vscroll_pos_y - preedit_delta_h);
                if (first <= lc.line && lc.line + 1u < post_lines) {
                    first = lc.line + 1u;
                }
            }
        } else {
            first = editor_vlist_find_first_at_y(vc, cold->vscroll_pos_y);
        }
        cur_top = editor_vlist_row_top(vc, first) + ((preedit_delta_h != 0.0 && first > lc.line) ? preedit_delta_h : 0.0);
        double y_cur = cur_top;
        const double y_limit = cold->vscroll_pos_y + double(view_h) + double(stride) * 2.0;
        end = first;
        while (end < post_lines && y_cur < y_limit) {
            if (!ed_is_line_folded(ext, end)) {
                const float rh = (preedit_delta_h != 0.0 && end == lc.line) ? comp_active_h : editor_vlist_height(vc, end);
                y_cur += double(rh);
                ++vis_count;
            }
            ++end;
        }
        if (vis_count == 0u && first < post_lines) {
            uint32_t scan = first;
            while (scan < post_lines && ed_is_line_folded(ext, scan)) ++scan;
            if (scan < post_lines) {
                first = scan;
                end = scan + 1u;
                vis_count = 1u;
            }
        }
        cold->vscroll_total_h = vc->total_stride_sum + preedit_delta_h + d.padding.top + d.padding.bottom;
        cold->vscroll_max_y = std::fmax(0.0, cold->vscroll_total_h - double(known_box_h));
        cold->vscroll_pos_y = clamp_double(cold->vscroll_pos_y, 0.0, cold->vscroll_max_y);
    } else if (hspan_count > 0u) {
        uint32_t first_vis = static_cast<uint32_t>(cold->vscroll_pos_y / double(stride));
        if (first_vis > visible_line_count) first_vis = visible_line_count;
        const uint32_t max_vis = static_cast<uint32_t>(std::ceil(double(view_h) / double(stride))) + 2u;
        const uint32_t avail_vis = visible_line_count > first_vis ? (visible_line_count - first_vis) : 0u;
        vis_count = max_vis < avail_vis ? max_vis : avail_vis;
        first = vis_to_doc(first_vis);
        cur_top = double(first_vis) * double(stride);
    } else {
        first = static_cast<uint32_t>(cold->vscroll_pos_y / double(stride));
        if (first > post_lines) first = post_lines;
        end = static_cast<uint32_t>(std::fmin(double(post_lines), double(first) + std::ceil(double(view_h) / double(stride)) + 2.0));
        vis_count = (end > first) ? (end - first) : 0u;
        cur_top = double(first) * double(stride);
    }

    auto* e = ctx->frame_arena.alloc<CodeEditorExtra>();
    auto* extra = ctx->frame_arena.alloc<BoxExtra>();
    if (!e || !extra) {
        if (vc) vc->measure_user_data = nullptr;
        return {};
    }
    *e = {};
    e->desc = d;
    e->desc.buffer = nullptr;
    e->scoped_theme = thm;
    e->count = vis_count;
    e->lines = e->count > 0u ? ctx->frame_arena.alloc<EditorLine>(e->count) : nullptr;
    if (e->count > 0u && !e->lines) {
        if (vc) vc->measure_user_data = nullptr;
        return {};
    }
    const bool has_live_diags = (b->diagnostics_revision == b->version && b->diagnostic_count > 0u && b->diagnostics != nullptr);
    const auto compute_line_diag = [&](uint32_t st, uint32_t le) noexcept -> uint8_t {
        uint8_t best_sev = 4u;
        if (has_live_diags) {
            for (uint32_t di = 0u; di < b->diagnostic_count; ++di) {
                const auto& diagnostic = b->diagnostics[di];
                const bool matches = (diagnostic.byte_start == diagnostic.byte_end)
                    ? (diagnostic.byte_start >= st && diagnostic.byte_start <= le)
                    : (diagnostic.byte_end > st && diagnostic.byte_start <= le);
                if (!matches) continue;
                const uint8_t sev = static_cast<uint8_t>(diagnostic.severity);
                if (sev < best_sev) {
                    best_sev = sev;
                    if (best_sev == 0u) break;
                }
            }
        }
        return best_sev;
    };
    if (hspan_count == 0u) {
        for (uint32_t i = first; i < end; ++i) {
            const uint32_t st = tb_start(b, i);
            const uint32_t le = text_buffer_line_end(b, i);
            const float lh = (wrap_w > 0.0f && vc) ? ((preedit_delta_h != 0.0 && i == lc.line) ? comp_active_h : editor_vlist_height(vc, i)) : stride;
            e->lines[i - first] = {
                editor_slice(ctx, b, st, le),
                st,
                i,
                float(cur_top - cold->vscroll_pos_y),
                lh,
                compute_line_diag(st, le),
                false,
            };
            cur_top += double(lh);
        }
    } else {
        uint32_t written = 0u;
        uint32_t hidx = 0u;
        uint32_t doc_line = first;
        const uint32_t limit = (wrap_w > 0.0f && vc) ? end : post_lines;
        while (written < vis_count && doc_line < limit) {
            while (hidx < hspan_count && hspans[hidx].last_line < doc_line) ++hidx;
            if (hidx < hspan_count && doc_line >= hspans[hidx].first_line && doc_line <= hspans[hidx].last_line) {
                doc_line = hspans[hidx].last_line + 1u;
                continue;
            }
            const uint32_t st = tb_start(b, doc_line);
            const uint32_t le = text_buffer_line_end(b, doc_line);
            const float lh = (wrap_w > 0.0f && vc) ? ((preedit_delta_h != 0.0 && doc_line == lc.line) ? comp_active_h : editor_vlist_height(vc, doc_line)) : stride;
            e->lines[written++] = {
                editor_slice(ctx, b, st, le),
                st,
                doc_line,
                float(cur_top - cold->vscroll_pos_y),
                lh,
                compute_line_diag(st, le),
                ed_has_fold_start(ext, doc_line),
            };
            cur_top += double(lh);
            ++doc_line;
        }
        e->count = written;
        end = written > 0u ? (e->lines[written - 1u].index + 1u) : first;
    }
    if (vc) vc->measure_user_data = nullptr;
    e->cursor = c;
    e->anchor = a;
    e->active_line = lc.line;
    e->focused = focus;
    e->wrap_affinity = (wrap_w > 0.0f) && cold->edit_wrap_affinity;
    e->scroll = cold->vscroll_pos_y;
    e->total_h = cold->vscroll_total_h;
    e->scroll_x = cold->edit_scroll_x;
    e->stride = stride;
    e->gutter = gutter;
    e->wrap_w = wrap_w;

    if (ext) {
        if (!d.read_only && ext->ghost_len > 0u) {
            e->ghost_text = ctx->frame_arena.push_str(ext->ghost_text, ext->ghost_len);
        }
        if (ext->search_len > 0u) {
            e->search_query = ctx->frame_arena.push_str(ext->search_query, ext->search_len);
            e->search_case_sensitive = ext->search_case_sensitive;
        }
        if (ext->extra_cursor_count > 0u) {
            e->extra_cursors = ctx->frame_arena.alloc_uninit<uint32_t>(ext->extra_cursor_count);
            if (e->extra_cursors) {
                std::memcpy(e->extra_cursors, ext->extra_cursors, size_t(ext->extra_cursor_count) * sizeof(uint32_t));
                e->extra_cursor_count = ext->extra_cursor_count;
            }
        }
    }

    if (composing) {
        e->composition = ctx->frame_arena.push_str(ui.ime_comp_utf8, ui.ime_comp_len);
        e->comp_cursor = ui.ime_comp_cursor_byte;
        e->comp_selection = ui.ime_comp_sel_len_byte;
    }
    *extra = {};
    extra->layer = ctx->active_ui->current_layer;
    extra->kind = CustomCmdKind::CodeEditor;
    extra->code_editor = e;
    extra->desc.id = d.id;
    extra->is_virtual_list = true;
    extra->desc.show_scrollbar = d.show_scrollbar;

    layout_open(ctx->active_ui->layout, d.id);
    LayoutNodeConfig decl = {};
    decl.width = d.width;
    decl.height = d.height;
    decl.emit_custom = true;
    decl.custom_data = extra;
    decl.user_data = extra;
    layout_configure_open(ctx->active_ui->layout, decl);
    layout_close(ctx->active_ui->layout);
    if (changed) request_redraw(ctx);
    if (focus && !d.read_only) {
        ui.ime_cursor_logical = {
            origin_x + active_caret_x - cold->edit_scroll_x,
            origin_y + float(active_caret_y - cold->vscroll_pos_y),
            2.0f,
            stride,
        };
        ui.ime_cursor_offset_win = 0;
    }
    const uint32_t summary_start = tb_start(b, lc.line), summary_end = text_buffer_line_end(b, lc.line);
    char summary[256];
    uint32_t summary_size = text_buffer_copy_range(b, summary_start, summary_end - summary_start > 256u ? summary_start + 256u : summary_end, summary, sizeof(summary));
    summary_size = utf8_clamp_boundary(summary, summary_size, summary_size);
    auto* semantic = a11y_emit(ctx, d.id, A11yRole::CodeEditor, {}, { summary, summary_size }, A11yState_Multiline | (d.read_only ? A11yState_ReadOnly : 0u), 1u | (d.read_only ? 0u : 128u));
    if (semantic) {
        semantic->node.min_value = 1.0f;
        semantic->node.max_value = float(post_lines);
        semantic->node.value_num = float(lc.line + 1u);
    }
    return { changed, focus, composing, c, a, lc.line, lc.col_byte, first, end, post_lines, focus ? ui.ime_cursor_logical : Rect{}, hovered_line, clicked_gutter_line };
}

namespace {
void emit_editor_colored_run(
    Context* ctx,
    const CodeEditorDesc& d,
    const char* s,
    uint32_t len,
    uint32_t token_base_off,
    const SyntaxToken* tokens,
    uint32_t token_count,
    float ox,
    float sy,
    float scroll_x,
    float asc,
    float stride,
    const ClipEntry& clip,
    float dpi,
    float* io_pen_x) noexcept
{
    if (!ctx || !ctx->atlas.font_valid || !s || len == 0u || !io_pen_x) return;
    const uint16_t primary_slot = resolve_font_slot_idx(&ctx->atlas, d.font_id);
    if (primary_slot == kInvalidFont) return;
    const uint32_t slice_end = token_base_off + len;
    bool same_font = true;
    bool tokens_sorted = true;
    uint32_t prev_end = 0u;
    for (uint32_t t = 0u; t < token_count; ++t) {
        const auto& tok = tokens[t];
        if (tok.byte_start < prev_end || tok.byte_end < tok.byte_start) {
            tokens_sorted = false;
        }
        prev_end = tok.byte_end;
        if (tok.byte_end <= token_base_off || tok.byte_start >= slice_end) continue;
        if (tok.font_id != d.font_id && resolve_font_slot_idx(&ctx->atlas, tok.font_id) != primary_slot) {
            same_font = false;
        }
    }
    if (same_font) {
        const FontSlot& primary = ctx->atlas.slots[primary_slot];
        const float base_px = d.font_size_px;
        const float phys_px = base_px * dpi;
        const float base_y_px = sy * dpi + asc;
        const auto lookup_color = [&](uint32_t abs_byte) noexcept -> Color {
            if (token_count == 0u) return d.text_color;
            if (tokens_sorted) {
                uint32_t lo = 0u, hi = token_count;
                while (lo < hi) {
                    const uint32_t mid = lo + ((hi - lo) >> 1u);
                    if (tokens[mid].byte_end <= abs_byte) lo = mid + 1u;
                    else hi = mid;
                }
                if (lo < token_count && abs_byte >= tokens[lo].byte_start && abs_byte < tokens[lo].byte_end) {
                    return resolve_theme_color(tokens[lo].color, d.text_color);
                }
                return d.text_color;
            }
            for (uint32_t t = 0u; t < token_count; ++t) {
                if (abs_byte >= tokens[t].byte_start && abs_byte < tokens[t].byte_end) {
                    return resolve_theme_color(tokens[t].color, d.text_color);
                }
            }
            return d.text_color;
        };
        uint32_t pos = 0u;
        while (pos < len) {
            if (s[pos] == '\t') {
                *io_pen_x += static_cast<float>(d.tab_width) * primary.space_advance_em * base_px;
                ++pos;
                continue;
            }
            uint32_t run_end = pos;
            while (run_end < len && s[run_end] != '\t') {
                ++run_end;
            }
            const size_t mk = ctx->frame_arena.mark();
            const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, primary_slot, s + pos, run_end - pos);
            for (const ShapedGlyph& g : glyphs) {
                const float pen_x_px = (ox + *io_pen_x - scroll_x) * dpi;
                emit_slug_glyph_quad(
                    ctx, g.font_slot, g.glyph_id, phys_px,
                    pen_x_px, base_y_px, g.x_offset_em, g.y_offset_em,
                    clip, lookup_color(token_base_off + pos + g.cluster));
                *io_pen_x += g.x_advance_1px * base_px;
            }
            ctx->frame_arena.rewind(mk);
            pos = run_end;
        }
        return;
    }
    uint32_t pos = token_base_off;
    float py = 0.0f;
    for (uint32_t t = 0u; t < token_count; ++t) {
        const auto& tok = tokens[t];
        if (tok.byte_end <= pos || tok.byte_end <= tok.byte_start) continue;
        if (tok.byte_start >= slice_end) break;
        const uint32_t ca_raw = (tok.byte_start > pos) ? tok.byte_start : pos;
        const uint32_t cz_raw = (tok.byte_end < slice_end) ? tok.byte_end : slice_end;
        const uint32_t ca = token_base_off + utf8_clamp_boundary(s, len, ca_raw - token_base_off);
        const uint32_t cz = token_base_off + utf8_clamp_boundary(s, len, cz_raw - token_base_off);
        if (ca > pos) {
            emit_utf8_glyph_run(ctx, d.font_id, s + (pos - token_base_off), ca - pos,
                d.font_size_px, 0.0f, asc, stride, d.text_color,
                ox, sy, scroll_x, 0.0f, clip, dpi, false, false, io_pen_x, &py, d.tab_width);
        }
        if (cz > ca) {
            emit_utf8_glyph_run(ctx, tok.font_id, s + (ca - token_base_off), cz - ca,
                d.font_size_px, 0.0f, asc, stride, resolve_theme_color(tok.color, d.text_color),
                ox, sy, scroll_x, 0.0f, clip, dpi, false, false, io_pen_x, &py, d.tab_width);
        }
        pos = (cz > pos) ? cz : pos;
    }
    if (pos < slice_end) {
        emit_utf8_glyph_run(ctx, d.font_id, s + (pos - token_base_off), slice_end - pos,
            d.font_size_px, 0.0f, asc, stride, d.text_color,
            ox, sy, scroll_x, 0.0f, clip, dpi, false, false, io_pen_x, &py, d.tab_width);
    }
}

void emit_editor_token_slice(
    Context* ctx,
    const CodeEditorDesc& d,
    Str sub_str,
    Str orig_str,
    uint32_t slice_orig_start,
    uint32_t slice_orig_end,
    uint32_t sub_local_off,
    const SyntaxToken* tokens,
    uint32_t token_count,
    float ox,
    float sy,
    float asc,
    float stride,
    const ClipEntry& clip,
    float dpi) noexcept
{
    (void)orig_str;
    if (slice_orig_end <= slice_orig_start || !sub_str.chars) return;
    float pen_x = (sub_local_off > 0u) ? editor_x(ctx, d, sub_str, sub_local_off) : 0.0f;
    emit_editor_colored_run(
        ctx, d, sub_str.chars + sub_local_off, slice_orig_end - slice_orig_start,
        slice_orig_start, tokens, token_count,
        ox, sy, 0.0f, asc, stride, clip, dpi, &pen_x);
}
} // namespace

void emit_code_editor_command(Context* ctx, const CodeEditorExtra* e, Rect bb, const ClipEntry& outer, float dpi) noexcept {
    const auto& d = e->desc;
    auto* ws = state_map_find(&ctx->active_ui->states, d.id);
    WidgetCold* cold = ensure_widget_cold(ws);
    const Rect box = bb;
    ws->last_box_logical = box;
    ws->viewport_dim = { box.w, box.h };
    const double max_y = std::fmax(0.0, e->total_h - double(box.h));
    if (cold) {
        cold->vscroll_total_h = e->total_h;
        cold->vscroll_max_y = max_y;
    }

    const float actual_wrap_w = editor_wrap_width(d, box.w, e->gutter);
    if (d.word_wrap && actual_wrap_w > 0.0f && std::fabs(actual_wrap_w - e->wrap_w) > 0.5f) {
        request_redraw(ctx);
    }

    const auto rect = [&](Rect r, Color color) noexcept {
        push_prim(ctx, Prim::make_rect({ r.x * dpi, r.y * dpi, r.w * dpi, r.h * dpi }, outer.rect, color));
    };
    push_prim(ctx, Prim::make_rect({ box.x * dpi, box.y * dpi, box.w * dpi, box.h * dpi }, outer.rect, d.background, scale_corners(d.radius, dpi), e->focused ? d.focus_border_color : d.border_color, (e->focused ? d.focus_border_width : d.border_width) * dpi));
    rect({ box.x + 1.0f, box.y + 1.0f, e->gutter, box.h - 2.0f }, d.gutter_background);
    const uint32_t outer_clip_index=ctx->render_clip_index;
    const uint32_t outer_clip_shape=ctx->render_clip_shape;
    if(d.scroll_fade_px>0) {
        const Vec4 fade={float(std::fmin(d.scroll_fade_px,e->scroll))*dpi,
            float(std::fmin(d.scroll_fade_px,std::fmax(0.0,max_y-e->scroll)))*dpi,0,0};
        const ClipEntry content_clip=make_box_clip(ctx,outer,
            {box.x*dpi,(box.y+d.padding.top)*dpi,box.w*dpi,std::fmax(0.f,box.h-d.padding.top-d.padding.bottom)*dpi},{},false,fade);
        ctx->render_clip_index=content_clip.indexed ? content_clip.shape : outer_clip_index;
        ctx->render_clip_shape=content_clip.shape ? content_clip.shape : outer_clip_shape;
    }
    const float ox = box.x + d.padding.left + e->gutter;
    const float oy = box.y + d.padding.top;
    const ClipEntry clip = { rect_intersect(outer.rect, layer_rect(ctx,{ ox * dpi, oy * dpi, std::fmax(0.0f, box.w - e->gutter - d.padding.left - d.padding.right - 14.0f) * dpi, std::fmax(0.0f, box.h - d.padding.top - d.padding.bottom) * dpi })), 0.0f };
    const ClipEntry gutter_clip = { rect_intersect(outer.rect, layer_rect(ctx,{ box.x * dpi, oy * dpi, (e->gutter + d.padding.left) * dpi, std::fmax(0.0f, box.h - d.padding.top - d.padding.bottom) * dpi })), 0.0f };
    const uint16_t slot = resolve_font_slot_idx(&ctx->atlas, d.font_id);
    const float asc = slot != kInvalidFont ? ctx->atlas.slots[slot].ascent_em * d.font_size_px * dpi : d.font_size_px * dpi * 0.8f;
    const auto glyph = [&](Str s, Color color, uint16_t font, float x, float y, const ClipEntry& cp) noexcept {
        float px = 0.0f, py = 0.0f;
        emit_utf8_glyph_run(ctx, font, s.chars, s.length, d.font_size_px, 0.0f, asc, e->stride, color, x, y, 0.0f, 0.0f, cp, dpi, false, false, &px, &py, d.tab_width);
    };
    const uint32_t lo = e->cursor < e->anchor ? e->cursor : e->anchor;
    const uint32_t hi = e->cursor > e->anchor ? e->cursor : e->anchor;
    const float vis_top = oy - e->stride;
    const float vis_bot = oy + std::fmax(0.0f, box.h - d.padding.top - d.padding.bottom) + e->stride;
    Context* prev_cur = current_context();
    set_current_context(ctx);
    const Theme prev_t0 = ctx->theme_stack[0];
    const uint32_t prev_td = ctx->theme_depth;
    const uint32_t prev_to = ctx->theme_overflow;
    ctx->theme_stack[0] = e->scoped_theme;
    ctx->theme_depth = 1u;
    ctx->theme_overflow = 0u;

    const float tag_w = (d.line_decorator && d.gutter_tag_width_px > 0.0f) ? d.gutter_tag_width_px : 0.0f;
    for (uint32_t i = 0u; i < e->count; ++i) {
        const auto& l = e->lines[i];
        const float y = oy + l.y_rel;
        CodeLineDecoration dec = {};
        bool has_dec = d.line_decorator && d.line_decorator(l.index, &dec, d.line_decorator_user_data);
        if (!has_dec && l.diag_severity < 4u) {
            static constexpr Color colors[] = {
                Color::rgba8(234, 67, 53, 72), Color::rgba8(245, 158, 11, 72),
                Color::rgba8(59, 130, 246, 56), Color::rgba8(139, 92, 246, 56)
            };
            static constexpr Color bars[] = {
                Color::rgba8(234, 67, 53), Color::rgba8(245, 158, 11),
                Color::rgba8(59, 130, 246), Color::rgba8(139, 92, 246)
            };
            dec.row_bg = colors[l.diag_severity];
            dec.gutter_bar_color = bars[l.diag_severity];
            has_dec = true;
        }
        if (has_dec && dec.row_bg.a > 0u) {
            push_prim(ctx, Prim::make_rect({ ox * dpi, y * dpi, (box.w - e->gutter - d.padding.left - d.padding.right) * dpi, l.height * dpi }, clip.rect, dec.row_bg));
        }
        if (l.index == e->active_line) {
            push_prim(ctx, Prim::make_rect({ ox * dpi, y * dpi, (box.w - e->gutter - d.padding.left - d.padding.right) * dpi, l.height * dpi }, clip.rect, d.active_line_color));
        }
        if (has_dec && dec.gutter_bar_color.a > 0u && e->gutter > 0.0f) {
            push_prim(ctx, Prim::make_rect({ (box.x + 1.0f) * dpi, y * dpi, 2.0f * dpi, l.height * dpi }, gutter_clip.rect, dec.gutter_bar_color));
        }
        if (d.show_line_numbers) {
            char n[16];
            const int len = std::snprintf(n, sizeof(n), "%u", l.index + 1u);
            const Str ns = { n, static_cast<uint32_t>(len) };
            glyph(ns, l.index == e->active_line ? d.gutter_active_text_color : d.gutter_text_color, d.font_id, ox - tag_w - 12.0f - editor_x(ctx, d, ns, ns.length), y, gutter_clip);
        }
        if (has_dec && dec.gutter_tag.chars && dec.gutter_tag.length > 0u && tag_w > 0.0f) {
            const Color tcol = resolve_theme_color(dec.gutter_tag_color, d.gutter_text_color);
            glyph(dec.gutter_tag, tcol, d.font_id, ox - 8.0f - editor_x(ctx, d, dec.gutter_tag, dec.gutter_tag.length), y, gutter_clip);
        }

        const uint32_t length = l.text.length;
        SyntaxToken local_tokens[256];
        const uint32_t token_capacity=l.text.length>256 ? l.text.length : 256;
        SyntaxToken* tokens=token_capacity>256 ? ctx->frame_arena.alloc<SyntaxToken>(token_capacity) : local_tokens;
        const uint32_t returned=tokens ? ((d.tokenizer == tokenize_cpp_hlsl)
            ? tokenize_cpp_hlsl_with_theme(l.text,tokens,token_capacity,e->scoped_theme)
            : (d.tokenizer ? d.tokenizer(l.index,l.text,tokens,token_capacity,d.tokenizer_user_data) : 0u)) : 0;
        const uint32_t count=returned<token_capacity ? returned : token_capacity;
        const bool preedit = e->composition.length > 0u && l.index == e->active_line;
        const uint32_t caret = preedit && e->cursor >= l.start && e->cursor <= l.start + length ? e->cursor - l.start : length;

        if (e->wrap_w <= 0.0f) {
            if (!preedit && e->search_query.length > 0u && length >= e->search_query.length) {
                constexpr Color kSearchHl = Color::rgba8(245, 196, 66, 84);
                uint32_t mpos = 0u;
                while (ed_find_substr(l.text.chars, length, e->search_query.chars, e->search_query.length,
                                      mpos, e->search_case_sensitive, &mpos)) {
                    emit_selection_bands(
                        ctx, d.font_id, l.text.chars, length, mpos, mpos + e->search_query.length,
                        d.font_size_px, 0.0f, false, true, d.tab_width,
                        ox, y * dpi, e->stride * dpi, e->scroll_x, dpi,
                        clip.rect, kSearchHl, 0.0f, 0.0f);
                    mpos += e->search_query.length;
                }
            }
            if (!preedit && hi > l.start && lo <= l.start + length) {
                const uint32_t a = lo > l.start ? lo - l.start : 0u;
                const uint32_t z = hi < l.start + length ? hi - l.start : length;
                const bool spans_nl = hi > l.start + length;
                emit_selection_bands(
                    ctx, d.font_id, l.text.chars, length, a, z,
                    d.font_size_px, 0.0f, false, true, d.tab_width,
                    ox, y * dpi, e->stride * dpi, e->scroll_x, dpi,
                    clip.rect, d.selection_color, 0.0f, spans_nl ? d.font_size_px * 0.6f : 0.0f);
            }
            const float preedit_width = preedit ? editor_x(ctx, d, e->composition, e->composition.length) : 0.0f;
            float line_pen_x = 0.0f;
            if (!preedit) {
                emit_editor_colored_run(
                    ctx, d, l.text.chars, length, 0u, tokens, count,
                    ox, y, e->scroll_x, asc, e->stride, clip, dpi, &line_pen_x);
            } else {
                if (caret > 0u) {
                    emit_editor_colored_run(
                        ctx, d, l.text.chars, caret, 0u, tokens, count,
                        ox, y, e->scroll_x, asc, e->stride, clip, dpi, &line_pen_x);
                }
                line_pen_x += preedit_width;
                if (caret < length) {
                    emit_editor_colored_run(
                        ctx, d, l.text.chars + caret, length - caret, caret, tokens, count,
                        ox, y, e->scroll_x, asc, e->stride, clip, dpi, &line_pen_x);
                }
            }
            if (l.has_fold_marker) {
                const Str dots = str_lit("...");
                const float mx = ox + line_pen_x - e->scroll_x + 6.0f;
                const float dw = editor_x(ctx, d, dots, dots.length);
                push_prim(ctx, Prim::make_rect(
                    { (mx - 3.0f) * dpi, (y + 2.0f) * dpi, (dw + 6.0f) * dpi, std::fmax(2.0f, e->stride - 4.0f) * dpi },
                    clip.rect, Color::rgba8(140, 150, 170, 36), Corners::all(3.0f * dpi)));
                glyph(dots, Color::rgba8(140, 150, 170, 195), d.font_id, mx, y, clip);
            }
            if (e->focused && l.index == e->active_line) {
                const uint32_t col = e->cursor - l.start;
                const float x = ox + editor_x(ctx, d, l.text, col) - e->scroll_x;
                float caret_x = x;
                const float ca = e->composition.length ? editor_x(ctx, d, e->composition, e->comp_cursor) : 0.0f;
                if (e->composition.length) {
                    const float cz = editor_x(ctx, d, e->composition, e->comp_cursor + e->comp_selection);
                    push_prim(ctx, Prim::make_rect({ (x + ca) * dpi, y * dpi, (cz - ca) * dpi, e->stride * dpi }, clip.rect, d.selection_color));
                    glyph(e->composition, d.composition_color, d.font_id, x, y, clip);
                    push_prim(ctx, Prim::make_rect({ x * dpi, (y + e->stride - 2.0f) * dpi, preedit_width * dpi, dpi }, clip.rect, d.composition_color));
                    caret_x += ca;
                }
                if (ctx->active_ui->caret_blink_on) {
                    push_prim(ctx, Prim::make_rect({ caret_x * dpi, y * dpi, 1.5f * dpi, e->stride * dpi }, clip.rect, d.caret_color));
                }
                if (!preedit && e->ghost_text.length > 0u) {
                    uint32_t gl = e->ghost_text.length;
                    for (uint32_t gk = 0u; gk < gl; ++gk) {
                        if (e->ghost_text.chars[gk] == '\n' || e->ghost_text.chars[gk] == '\r') { gl = gk; break; }
                    }
                    if (gl > 0u) {
                        glyph({ e->ghost_text.chars, gl }, Color::rgba8(140, 150, 170, 170), d.font_id, caret_x + 1.5f, y, clip);
                    }
                }
                if (e->composition.length > 0u && preedit_width > 0.0f) {
                    ctx->active_ui->ime_cursor_logical = { x, y, preedit_width > 2.0f ? preedit_width : 2.0f, e->stride };
                    ctx->active_ui->ime_cursor_offset_win = static_cast<int32_t>(std::floor(ca * logical_to_window_scale(ctx) + 0.5f));
                } else {
                    ctx->active_ui->ime_cursor_logical = { caret_x, y, 2.0f, e->stride };
                    ctx->active_ui->ime_cursor_offset_win = 0;
                }
            }
            if (e->focused && ctx->active_ui->caret_blink_on && e->extra_cursor_count > 0u) {
                for (uint32_t ek = 0u; ek < e->extra_cursor_count; ++ek) {
                    const uint32_t ec = e->extra_cursors[ek];
                    if (ec >= l.start && ec <= l.start + length) {
                        const float ex = ox + editor_x(ctx, d, l.text, ec - l.start) - e->scroll_x;
                        push_prim(ctx, Prim::make_rect({ ex * dpi, y * dpi, 1.5f * dpi, e->stride * dpi }, clip.rect, d.caret_color));
                    }
                }
            }
        } else {
            const size_t sp_mark = ctx->frame_arena.mark();
            const uint32_t comp_len = preedit ? e->composition.length : 0u;
            const uint32_t comp_end = caret + comp_len;
            const Str draw_str = (comp_len > 0u)
                ? splice_editor_line(&ctx->frame_arena, l.text, caret, e->composition)
                : l.text;

            const bool has_sel = (hi > l.start && lo <= l.start + length);
            const uint32_t raw_sel_a = has_sel ? (lo > l.start ? lo - l.start : 0u) : 0u;
            const uint32_t raw_sel_z = has_sel ? (hi < l.start + length ? hi - l.start : length) : 0u;
            const uint32_t spl_sel_a = (comp_len > 0u && raw_sel_a >= caret) ? (raw_sel_a + comp_len) : raw_sel_a;
            const uint32_t spl_sel_z = (comp_len > 0u && raw_sel_z > caret) ? (raw_sel_z + comp_len) : raw_sel_z;
            const bool spans_nl = has_sel && (hi > l.start + length);

            // Remap tokens if preedit is spliced inline before suffix text.
            SyntaxToken local_prefix[256],local_suffix[256];
            SyntaxToken* orig_prefix_tokens=count>256 ? ctx->frame_arena.alloc<SyntaxToken>(count) : local_prefix;
            SyntaxToken* orig_suffix_tokens=count>256 ? ctx->frame_arena.alloc<SyntaxToken>(count) : local_suffix;
            if(!orig_prefix_tokens || !orig_suffix_tokens) { ctx->frame_arena.rewind(sp_mark); continue; }
            for (uint32_t t = 0u; t < count; ++t) {
                orig_prefix_tokens[t] = tokens[t];
                orig_suffix_tokens[t] = tokens[t];
                orig_suffix_tokens[t].byte_start = tokens[t].byte_start >= caret ? tokens[t].byte_start - caret : 0u;
                orig_suffix_tokens[t].byte_end = tokens[t].byte_end >= caret ? tokens[t].byte_end - caret : 0u;
            }

            const bool has_caret = e->focused && l.index == e->active_line;
            WrappedSubline csub = {};
            float caret_x = ox;
            float caret_y = y;
            if (has_caret) {
                const uint32_t col = e->cursor - l.start;
                const uint32_t caretspl = (comp_len > 0u) ? (caret + e->comp_cursor) : col;
                const bool caret_aff = (comp_len > 0u) ? false : e->wrap_affinity;
                float caret_local_x = 0.0f;
                csub = locate_wrapped_offset(
                    ctx, d.font_id, draw_str.chars, draw_str.length, caretspl, caret_aff,
                    d.font_size_px, 0.0f, false, d.tab_width, e->wrap_w, &caret_local_x);
                caret_x = ox + caret_local_x;
                caret_y = y + float(csub.sub_idx) * e->stride;
            }

            bool comp_rect_set = false;
            float comp_first_x = ox, comp_first_y = y, comp_first_w = 0.0f;

            for_each_wrapped_subline(ctx, d.font_id, draw_str.chars, draw_str.length, d.font_size_px, 0.0f, false, d.tab_width, e->wrap_w,
                [&](const WrappedSubline& sub) noexcept {
                    const float sy = y + float(sub.sub_idx) * e->stride;
                    if (sy < vis_top && !(comp_len > 0u && has_caret && sub.sub_idx == csub.sub_idx)) return true;
                    if (sy > vis_bot && !(comp_len > 0u && has_caret && sub.sub_idx <= csub.sub_idx)) return false;
                    const Str sub_str = { draw_str.chars ? draw_str.chars + sub.start : "", sub.end - sub.start };
                    const bool is_last = (sub.end >= draw_str.length);

                    if (!preedit && e->search_query.length > 0u && length >= e->search_query.length) {
                        constexpr Color kSearchHl = Color::rgba8(245, 196, 66, 84);
                        uint32_t mpos = 0u;
                        while (ed_find_substr(l.text.chars, length, e->search_query.chars, e->search_query.length,
                                              mpos, e->search_case_sensitive, &mpos)) {
                            const uint32_t m_end = mpos + e->search_query.length;
                            if (m_end > sub.start && mpos < sub.end) {
                                const uint32_t hs = mpos > sub.start ? mpos : sub.start;
                                const uint32_t he = m_end < sub.end ? m_end : sub.end;
                                emit_selection_bands(
                                    ctx, d.font_id, sub_str.chars, sub_str.length,
                                    hs - sub.start, he - sub.start,
                                    d.font_size_px, 0.0f, false, true, d.tab_width,
                                    ox, sy * dpi, e->stride * dpi, 0.0f, dpi,
                                    clip.rect, kSearchHl, 0.0f, 0.0f);
                            }
                            mpos = m_end;
                        }
                    }

                    if (has_sel && !preedit) {
                        const uint32_t hs = spl_sel_a > sub.start ? spl_sel_a : sub.start;
                        const uint32_t he = spl_sel_z < sub.end ? spl_sel_z : sub.end;
                        const bool sub_nl = is_last && spans_nl;
                        if (he > hs || sub_nl) {
                            emit_selection_bands(
                                ctx, d.font_id, sub_str.chars, sub_str.length,
                                hs - sub.start, (he > sub.start) ? (he - sub.start) : 0u,
                                d.font_size_px, 0.0f, false, true, d.tab_width,
                                ox, sy * dpi, e->stride * dpi, 0.0f, dpi,
                                clip.rect, d.selection_color, 0.0f, sub_nl ? d.font_size_px * 0.6f : 0.0f);
                        }
                    }

                    // 1. Pre-composition prefix of this subline (or entire subline when not composing)
                    const uint32_t p1_end = (comp_len > 0u && sub.end > caret) ? caret : sub.end;
                    if (p1_end > sub.start) {
                        emit_editor_token_slice(
                            ctx, d, sub_str, l.text,
                            sub.start, p1_end, 0u,
                            orig_prefix_tokens, count, ox, sy, asc, e->stride, clip, dpi);
                    }

                    // 2. Inline IME composition inside this subline
                    if (comp_len > 0u && sub.end > caret && sub.start < comp_end) {
                        const uint32_t cs = sub.start > caret ? sub.start : caret;
                        const uint32_t ce = sub.end < comp_end ? sub.end : comp_end;
                        const float cx0 = ox + editor_x(ctx, d, sub_str, cs - sub.start);
                        const float cx1 = ox + editor_x(ctx, d, sub_str, ce - sub.start);
                        const uint32_t cl_a = caret + e->comp_cursor;
                        const uint32_t cl_z = caret + e->comp_cursor + e->comp_selection;
                        if (e->comp_selection > 0u && cl_z > cs && cl_a < ce) {
                            const uint32_t ha = cl_a > cs ? cl_a : cs;
                            const uint32_t hz = cl_z < ce ? cl_z : ce;
                            const float hx0 = ox + editor_x(ctx, d, sub_str, ha - sub.start);
                            const float hx1 = ox + editor_x(ctx, d, sub_str, hz - sub.start);
                            if (hx1 > hx0) {
                                push_prim(ctx, Prim::make_rect({ hx0 * dpi, sy * dpi, (hx1 - hx0) * dpi, e->stride * dpi }, clip.rect, d.selection_color));
                            }
                        }
                        glyph({ draw_str.chars + cs, ce - cs }, d.composition_color, d.font_id, cx0, sy, clip);
                        if (cx1 > cx0) {
                            push_prim(ctx, Prim::make_rect({ cx0 * dpi, (sy + e->stride - 2.0f) * dpi, (cx1 - cx0) * dpi, dpi }, clip.rect, d.composition_color));
                            if (!comp_rect_set || (has_caret && sub.sub_idx == csub.sub_idx)) {
                                comp_rect_set = true;
                                comp_first_x = cx0;
                                comp_first_y = sy;
                                comp_first_w = cx1 - cx0;
                            }
                        }
                    }

                    // 3. Post-composition suffix of this subline
                    if (comp_len > 0u && sub.end > comp_end) {
                        const uint32_t ss = sub.start > comp_end ? sub.start : comp_end;
                        if (sub.end > ss) {
                            const Str orig_sfx = (l.text.chars && length > caret) ? Str{ l.text.chars + caret, length - caret } : Str{};
                            emit_editor_token_slice(
                                ctx, d, sub_str, orig_sfx,
                                ss - comp_end, sub.end - comp_end, ss - sub.start,
                                orig_suffix_tokens, count,
                                ox, sy, asc, e->stride, clip, dpi);
                        }
                    }
                    if (is_last && l.has_fold_marker) {
                        const Str dots = str_lit("...");
                        const float mx = ox + editor_x(ctx, d, sub_str, sub_str.length) + 6.0f;
                        const float dw = editor_x(ctx, d, dots, dots.length);
                        push_prim(ctx, Prim::make_rect(
                            { (mx - 3.0f) * dpi, (sy + 2.0f) * dpi, (dw + 6.0f) * dpi, std::fmax(2.0f, e->stride - 4.0f) * dpi },
                            clip.rect, Color::rgba8(140, 150, 170, 36), Corners::all(3.0f * dpi)));
                        glyph(dots, Color::rgba8(140, 150, 170, 195), d.font_id, mx, sy, clip);
                    }
                    return true;
                });

            if (has_caret) {
                if (ctx->active_ui->caret_blink_on) {
                    push_prim(ctx, Prim::make_rect({ caret_x * dpi, caret_y * dpi, 1.5f * dpi, e->stride * dpi }, clip.rect, d.caret_color));
                }
                if (comp_len == 0u && e->ghost_text.length > 0u) {
                    uint32_t gl = e->ghost_text.length;
                    for (uint32_t gk = 0u; gk < gl; ++gk) {
                        if (e->ghost_text.chars[gk] == '\n' || e->ghost_text.chars[gk] == '\r') { gl = gk; break; }
                    }
                    if (gl > 0u) {
                        glyph({ e->ghost_text.chars, gl }, Color::rgba8(140, 150, 170, 170), d.font_id, caret_x + 1.5f, caret_y, clip);
                    }
                }
                if (comp_len > 0u && comp_rect_set && comp_first_w > 0.0f && std::fabs(comp_first_y - caret_y) < 0.5f) {
                    ctx->active_ui->ime_cursor_logical = { comp_first_x, comp_first_y, comp_first_w > 2.0f ? comp_first_w : 2.0f, e->stride };
                    ctx->active_ui->ime_cursor_offset_win = static_cast<int32_t>(std::floor(std::fmax(0.0f, caret_x - comp_first_x) * logical_to_window_scale(ctx) + 0.5f));
                } else {
                    ctx->active_ui->ime_cursor_logical = { caret_x, caret_y, 2.0f, e->stride };
                    ctx->active_ui->ime_cursor_offset_win = 0;
                }
            }
            if (e->focused && ctx->active_ui->caret_blink_on && e->extra_cursor_count > 0u) {
                for (uint32_t ek = 0u; ek < e->extra_cursor_count; ++ek) {
                    const uint32_t ec = e->extra_cursors[ek];
                    if (ec >= l.start && ec <= l.start + length) {
                        const uint32_t ec_col = ec - l.start;
                        const uint32_t ec_spl = (comp_len > 0u && ec_col >= caret) ? (ec_col + comp_len) : ec_col;
                        float ex_local = 0.0f;
                        const WrappedSubline esub = locate_wrapped_offset(
                            ctx, d.font_id, draw_str.chars, draw_str.length, ec_spl, false,
                            d.font_size_px, 0.0f, false, d.tab_width, e->wrap_w, &ex_local);
                        push_prim(ctx, Prim::make_rect(
                            { (ox + ex_local) * dpi, (y + float(esub.sub_idx) * e->stride) * dpi, 1.5f * dpi, e->stride * dpi },
                            clip.rect, d.caret_color));
                    }
                }
            }
            ctx->frame_arena.rewind(sp_mark);
        }
    }
    ctx->theme_stack[0] = prev_t0;
    ctx->theme_depth = prev_td;
    ctx->theme_overflow = prev_to;
    set_current_context(prev_cur);
    ctx->render_clip_index=outer_clip_index;
    ctx->render_clip_shape=outer_clip_shape;
    if (d.show_scrollbar) {
        const ScrollbarGeom sb = compute_vscrollbar_geom(ws);
        emit_scrollbar_overlay(
            ctx, d.id, sb, false, outer,
            e->scoped_theme.scrollbar_track, e->scoped_theme.scrollbar_thumb,
            e->scoped_theme.scrollbar_thumb_hover, e->scoped_theme.scrollbar_thumb_drag, dpi);
    }
}
} // namespace etch
