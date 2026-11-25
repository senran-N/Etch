// Desktop logical-size windows. SDL properties follow the window lifetime.
namespace etch {
namespace {
constexpr const char* kDesktopScale = "etch.desktop.coordinate_scale";
constexpr const char* kDesktopMinW = "etch.desktop.minimum_width";
constexpr const char* kDesktopMinH = "etch.desktop.minimum_height";
float desktop_coordinate_scale(SDL_Window* window) noexcept {
    if (!window) return 1.0f;
    const float display_scale = SDL_GetWindowDisplayScale(window);
    const float safe_display = (std::isfinite(display_scale) && display_scale > 0.05f) ? display_scale : 1.0f;
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0u) {
        return safe_display;
    }
    const float density = SDL_GetWindowPixelDensity(window);
    if (!std::isfinite(density) || density <= 0.25f) {
        return safe_display;
    }
    const float ratio = safe_display / density;
    return (std::isfinite(ratio) && ratio >= 0.25f && ratio <= 8.0f) ? ratio : safe_display;
}
void sync_desktop_window(SDL_Window* window, Context* ctx = nullptr) noexcept {
    if (!window) return;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    // Never recompute coordinate scale or minimum size while minimized:
    // Win32 iconic windows report tiny/zero client rects that corrupt pixel density.
    if ((flags & SDL_WINDOW_MINIMIZED) != 0u) return;
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    const float old_scale = SDL_GetFloatProperty(props, kDesktopScale, 0);
    if (old_scale == 0) return;
    const float scale = desktop_coordinate_scale(window);
    if (std::fabs(scale - old_scale) < 0.0001f) return;
    SDL_SetFloatProperty(props, kDesktopScale, scale);
    if (Context* target = ctx ? ctx : current_context()) {
        target->atlas.hinted_gc_pending = true;
    }
    int w = 0, h = 0, min_w = 0, min_h = 0;
    SDL_GetWindowSize(window, &w, &h);
    SDL_GetWindowMinimumSize(window, &min_w, &min_h);
    const float logical_min_w=SDL_GetFloatProperty(props,kDesktopMinW,-1);
    const float logical_min_h=SDL_GetFloatProperty(props,kDesktopMinH,-1);
    SDL_SetWindowMinimumSize(window,static_cast<int>(logical_min_w>=0 ? std::ceil(logical_min_w*scale) : std::round(min_w*scale/old_scale)),
        static_cast<int>(logical_min_h>=0 ? std::ceil(logical_min_h*scale) : std::round(min_h*scale/old_scale)));
    if ((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED)) != 0) return;
#if defined(SDL_PLATFORM_WINDOWS)
    // SDL 3.4+ accepts the OS-suggested DPI-scaled rectangle for resizable windows.
    // Applying our ratio again would double-scale the client area.
    if ((flags & SDL_WINDOW_RESIZABLE) != 0 && SDL_GetVersion() >= SDL_VERSIONNUM(3, 4, 0)) return;
#endif
    SDL_SetWindowSize(window, static_cast<int>(std::round(w * scale / old_scale)),
        static_cast<int>(std::round(h * scale / old_scale)));
}
}
SDL_Window* create_desktop_window_on_display(const char* title, float logical_w, float logical_h, uint32_t display_id, uint64_t flags) noexcept {
    if (!title || !std::isfinite(logical_w) || !std::isfinite(logical_h) || logical_w < 1 || logical_h < 1 || logical_w > 32768 || logical_h > 32768) return nullptr;
    const SDL_DisplayID display = display_id ? display_id : SDL_GetPrimaryDisplay();
    if (!display) return nullptr;
    SDL_Rect usable = {};
    if (!SDL_GetDisplayUsableBounds(display, &usable)) return nullptr;
    // Probe window-coordinate density before applying logical dimensions (Retina uses points).
    SDL_Window* window = SDL_CreateWindow(title, 1, 1, flags | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!window) return nullptr;
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    const float scale = desktop_coordinate_scale(window);
    if ((flags & SDL_WINDOW_BORDERLESS) == 0) {
        int top=0, left=0, bottom=0, right=0;
        SDL_GetWindowBordersSize(window,&top,&left,&bottom,&right);
        usable.w -= left + right; usable.h -= top + bottom;
    }
    const float ratio = std::fmin(1.0f, std::fmin(usable.w / (logical_w * scale), usable.h / (logical_h * scale)));
    const int w = static_cast<int>(std::fmax(1.0f, std::round(logical_w * scale * ratio)));
    const int h = static_cast<int>(std::fmax(1.0f, std::round(logical_h * scale * ratio)));
    if (!SDL_SetWindowSize(window, w, h) || !SDL_SetFloatProperty(SDL_GetWindowProperties(window), kDesktopScale, scale)) {
        SDL_DestroyWindow(window); return nullptr;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    if ((flags & SDL_WINDOW_HIDDEN) == 0) SDL_ShowWindow(window);
    return window;
}
SDL_Window* create_desktop_window(const char* title, float w, float h, uint64_t flags) noexcept {
    return create_desktop_window_on_display(title,w,h,0,flags);
}
SDL_Window* create_desktop_window_at_pointer(const char* title, float w, float h, uint64_t flags) noexcept {
    float x=0,y=0; SDL_GetGlobalMouseState(&x,&y);
    const SDL_Point point={static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y))};
    return create_desktop_window_on_display(title,w,h,SDL_GetDisplayForPoint(&point),flags);
}
bool set_desktop_window_min_size(SDL_Window* window, float w, float h) noexcept {
    if(!window || !std::isfinite(w) || !std::isfinite(h) || w<0 || h<0 || w>32768 || h>32768) return false;
    const float scale=desktop_coordinate_scale(window);
    const SDL_PropertiesID props=SDL_GetWindowProperties(window);
    return SDL_SetFloatProperty(props,kDesktopScale,scale) && SDL_SetFloatProperty(props,kDesktopMinW,w) && SDL_SetFloatProperty(props,kDesktopMinH,h) &&
        SDL_SetWindowMinimumSize(window,static_cast<int>(std::ceil(w*scale)),static_cast<int>(std::ceil(h*scale)));
}

// ============================================================================
// Asynchronous Cross-Platform OS Folder & File Dialogs (Refcounted Thread-Safe Queue)
// ============================================================================

struct PendingDialogEvent {
    PendingDialogEvent* next = nullptr;
    uint32_t request_tag = 0;
    int32_t selected_filter = -1;
    bool cancelled = false;
    bool error = false;
    char** utf8_paths = nullptr;
    uint32_t path_count = 0;
};

struct DialogSharedQueue {
    SDL_AtomicInt refs = {};
    SDL_Mutex* mutex = nullptr;
    Context* live_ctx = nullptr;
    uint32_t wake_event_type = 0;
    PendingDialogEvent* head = nullptr;
    PendingDialogEvent* tail = nullptr;
};

namespace {

void free_pending_dialog_event(PendingDialogEvent* ev) noexcept {
    if (!ev) return;
    if (ev->utf8_paths) {
        for (uint32_t i = 0; i < ev->path_count; ++i) {
            SDL_free(ev->utf8_paths[i]);
        }
        SDL_free(ev->utf8_paths);
    }
    SDL_free(ev);
}

void retain_dialog_queue(DialogSharedQueue* q) noexcept {
    if (q) SDL_AtomicIncRef(&q->refs);
}

void release_dialog_queue(DialogSharedQueue* q) noexcept {
    if (!q) return;
    if (SDL_AtomicDecRef(&q->refs)) {
        PendingDialogEvent* cur = q->head;
        while (cur) {
            PendingDialogEvent* nxt = cur->next;
            free_pending_dialog_event(cur);
            cur = nxt;
        }
        if (q->mutex) SDL_DestroyMutex(q->mutex);
        SDL_free(q);
    }
}

DialogSharedQueue* ensure_dialog_queue(Context* ctx) noexcept {
    if (!ctx) return nullptr;
    if (ctx->dialog_queue) return ctx->dialog_queue;
    auto* q = static_cast<DialogSharedQueue*>(SDL_calloc(1u, sizeof(DialogSharedQueue)));
    if (!q) return nullptr;
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        SDL_free(q);
        return nullptr;
    }
    SDL_SetAtomicInt(&q->refs, 1);
    q->live_ctx = ctx;
    q->wake_event_type = ctx->wake_event_type;
    ctx->dialog_queue = q;
    return q;
}

struct DialogRequestCookie {
    DialogSharedQueue* queue = nullptr;
    uint32_t request_tag = 0;
    SDL_DialogFileFilter* filters = nullptr;
    uint32_t filter_count = 0;
};

char* dup_cstr_sdl(const char* s) noexcept {
    if (!s) s = "";
    const size_t n = std::strlen(s);
    char* copy = static_cast<char*>(SDL_malloc(n + 1u));
    if (copy) std::memcpy(copy, s, n + 1u);
    return copy;
}

void free_dialog_cookie(DialogRequestCookie* c) noexcept {
    if (!c) return;
    if (c->filters) {
        for (uint32_t i = 0; i < c->filter_count; ++i) {
            SDL_free(const_cast<char*>(c->filters[i].name));
            SDL_free(const_cast<char*>(c->filters[i].pattern));
        }
        SDL_free(c->filters);
    }
    release_dialog_queue(c->queue);
    SDL_free(c);
}

DialogRequestCookie* make_dialog_cookie(Context* ctx, uint32_t request_tag, std::span<const FileDialogFilter> filters) noexcept {
    DialogSharedQueue* q = ensure_dialog_queue(ctx);
    if (!q) return nullptr;
    auto* c = static_cast<DialogRequestCookie*>(SDL_calloc(1u, sizeof(DialogRequestCookie)));
    if (!c) return nullptr;
    c->queue = q;
    retain_dialog_queue(q);
    c->request_tag = request_tag;
    if (!filters.empty()) {
        const uint32_t n = static_cast<uint32_t>(filters.size());
        c->filters = static_cast<SDL_DialogFileFilter*>(SDL_calloc(n, sizeof(SDL_DialogFileFilter)));
        if (!c->filters) {
            free_dialog_cookie(c);
            return nullptr;
        }
        c->filter_count = n;
        for (uint32_t i = 0; i < n; ++i) {
            char* nm = dup_cstr_sdl(filters[i].name ? filters[i].name : "Files");
            char* pt = dup_cstr_sdl(filters[i].pattern ? filters[i].pattern : "*");
            c->filters[i].name = nm;
            c->filters[i].pattern = pt;
            if (!nm || !pt) {
                free_dialog_cookie(c);
                return nullptr;
            }
        }
    }
    return c;
}

void enqueue_dialog_event(DialogSharedQueue* q, PendingDialogEvent* ev) noexcept {
    if (!q || !ev) return;
    SDL_LockMutex(q->mutex);
    if (!q->live_ctx) {
        SDL_UnlockMutex(q->mutex);
        free_pending_dialog_event(ev);
        return;
    }
    const uint32_t wake_ev = q->wake_event_type;
    if (q->tail) q->tail->next = ev;
    else q->head = ev;
    q->tail = ev;
    SDL_UnlockMutex(q->mutex);
    if (wake_ev != 0u) {
        SDL_Event push_ev = {};
        push_ev.type = wake_ev;
        SDL_PushEvent(&push_ev);
    }
}

void SDLCALL sdl_dialog_callback(void* userdata, const char* const* filelist, int filter) {
    auto* cookie = static_cast<DialogRequestCookie*>(userdata);
    if (!cookie) return;
    auto* ev = static_cast<PendingDialogEvent*>(SDL_calloc(1u, sizeof(PendingDialogEvent)));
    if (ev) {
        ev->request_tag = cookie->request_tag;
        ev->selected_filter = static_cast<int32_t>(filter);
        if (filelist == nullptr) {
            ev->error = true;
        } else if (filelist[0] == nullptr) {
            ev->cancelled = true;
        } else {
            uint32_t count = 0;
            while (filelist[count] != nullptr && count < 4096u) ++count;
            ev->utf8_paths = static_cast<char**>(SDL_calloc(count, sizeof(char*)));
            if (ev->utf8_paths) {
                for (uint32_t i = 0; i < count; ++i) {
                    char* copy = dup_cstr_sdl(filelist[i]);
                    if (copy) ev->utf8_paths[ev->path_count++] = copy;
                }
            } else {
                ev->error = true;
            }
        }
        enqueue_dialog_event(cookie->queue, ev);
    }
    free_dialog_cookie(cookie);
}

} // namespace

void shutdown_dialog_queue(Context* ctx) noexcept {
    if (!ctx || !ctx->dialog_queue) return;
    DialogSharedQueue* q = ctx->dialog_queue;
    ctx->dialog_queue = nullptr;
    SDL_LockMutex(q->mutex);
    q->live_ctx = nullptr;
    SDL_UnlockMutex(q->mutex);
    release_dialog_queue(q);
}

bool open_folder_dialog(Context* ctx, SDL_Window* parent_window, const char* default_location, uint32_t request_tag, bool allow_many) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || c->headless) return false;
    SDL_Window* win = parent_window ? parent_window : (c->active_window ? c->active_window : c->window);
    DialogRequestCookie* cookie = make_dialog_cookie(c, request_tag, {});
    if (!cookie) return false;
    SDL_ShowOpenFolderDialog(sdl_dialog_callback, cookie, win, default_location, allow_many);
    return true;
}

bool open_file_dialog(Context* ctx, SDL_Window* parent_window, std::span<const FileDialogFilter> filters, const char* default_location, uint32_t request_tag, bool allow_many) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || c->headless) return false;
    SDL_Window* win = parent_window ? parent_window : (c->active_window ? c->active_window : c->window);
    DialogRequestCookie* cookie = make_dialog_cookie(c, request_tag, filters);
    if (!cookie) return false;
    SDL_ShowOpenFileDialog(sdl_dialog_callback, cookie, win, cookie->filters, static_cast<int>(cookie->filter_count), default_location, allow_many);
    return true;
}

bool open_save_file_dialog(Context* ctx, SDL_Window* parent_window, std::span<const FileDialogFilter> filters, const char* default_location, uint32_t request_tag) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || c->headless) return false;
    SDL_Window* win = parent_window ? parent_window : (c->active_window ? c->active_window : c->window);
    DialogRequestCookie* cookie = make_dialog_cookie(c, request_tag, filters);
    if (!cookie) return false;
    SDL_ShowSaveFileDialog(sdl_dialog_callback, cookie, win, cookie->filters, static_cast<int>(cookie->filter_count), default_location);
    return true;
}

FileDialogResult poll_file_dialog(Context* ctx, Arena* arena) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || !c->dialog_queue) return {};
    Arena* dst_arena = arena ? arena : &c->frame_arena;

    PendingDialogEvent* ev = nullptr;
    SDL_LockMutex(c->dialog_queue->mutex);
    if (c->dialog_queue->head) {
        ev = c->dialog_queue->head;
        c->dialog_queue->head = ev->next;
        if (!c->dialog_queue->head) c->dialog_queue->tail = nullptr;
    }
    SDL_UnlockMutex(c->dialog_queue->mutex);
    if (!ev) return {};

    FileDialogResult out{};
    out.ready = true;
    out.cancelled = ev->cancelled;
    out.error = ev->error;
    out.request_tag = ev->request_tag;
    out.selected_filter = ev->selected_filter;
    if (ev->path_count > 0 && dst_arena) {
        Str* slice = dst_arena->alloc<Str>(ev->path_count);
        if (slice) {
            uint32_t copied = 0;
            for (uint32_t i = 0; i < ev->path_count; ++i) {
                const uint32_t len = static_cast<uint32_t>(std::strlen(ev->utf8_paths[i]));
                Str s = dst_arena->push_str(ev->utf8_paths[i], len);
                if (s.chars || len == 0u) slice[copied++] = s;
            }
            out.paths = std::span<const Str>(slice, copied);
        } else {
            out.error = true;
        }
    }
    free_pending_dialog_event(ev);
    return out;
}

void inject_file_dialog_result(Context* ctx, uint32_t request_tag, std::span<const Str> paths, bool cancelled, int32_t selected_filter) noexcept {
    if (!ctx) return;
    DialogSharedQueue* q = ensure_dialog_queue(ctx);
    if (!q) return;
    auto* ev = static_cast<PendingDialogEvent*>(SDL_calloc(1u, sizeof(PendingDialogEvent)));
    if (!ev) return;
    ev->request_tag = request_tag;
    ev->selected_filter = selected_filter;
    ev->cancelled = cancelled;
    if (!cancelled && !paths.empty()) {
        const uint32_t n = static_cast<uint32_t>(paths.size());
        ev->utf8_paths = static_cast<char**>(SDL_calloc(n, sizeof(char*)));
        if (ev->utf8_paths) {
            for (uint32_t i = 0; i < n; ++i) {
                char* copy = static_cast<char*>(SDL_malloc(paths[i].length + 1u));
                if (copy) {
                    if (paths[i].chars && paths[i].length > 0u) std::memcpy(copy, paths[i].chars, paths[i].length);
                    copy[paths[i].length] = '\0';
                    ev->utf8_paths[ev->path_count++] = copy;
                }
            }
        }
    }
    enqueue_dialog_event(q, ev);
}

// ============================================================================
// Multi-MIME Clipboard (Deterministic Local Store + Live SDL3 OS Clipboard)
// ============================================================================

namespace {

constexpr uint32_t kMaxClipboardMimeEntries = 16u;

struct OwnedMimeEntry {
    char* mime = nullptr;
    uint32_t mime_len = 0u;
    uint8_t* bytes = nullptr;
    uint32_t size = 0u;
};

struct OwnedMimeBundle {
    OwnedMimeEntry entries[kMaxClipboardMimeEntries] = {};
    const char* mime_ptrs[kMaxClipboardMimeEntries] = {};
    uint32_t count = 0u;
    bool* sync_cleaned = nullptr;
};

OwnedMimeBundle g_clipboard_mime_store = {};
bool g_clipboard_mime_atexit_registered = false;

void free_mime_bundle_contents(OwnedMimeBundle* b) noexcept {
    for (uint32_t i = 0u; i < b->count; ++i) {
        SDL_free(b->entries[i].mime);
        SDL_free(b->entries[i].bytes);
        b->entries[i] = {};
        b->mime_ptrs[i] = nullptr;
    }
    b->count = 0u;
}

void cleanup_global_clipboard_mime_store() noexcept {
    free_mime_bundle_contents(&g_clipboard_mime_store);
}

bool clone_mime_bundle(std::span<const MimeDragEntry> src, OwnedMimeBundle* dst) noexcept {
    const uint32_t n = (src.size() > kMaxClipboardMimeEntries)
        ? kMaxClipboardMimeEntries
        : static_cast<uint32_t>(src.size());
    for (uint32_t i = 0u; i < n; ++i) {
        const MimeDragEntry& e = src[i];
        if (!e.mime_type.chars || e.mime_type.length == 0u || (e.size > 0u && !e.bytes)) {
            free_mime_bundle_contents(dst);
            return false;
        }
        char* mime_copy = static_cast<char*>(SDL_malloc(static_cast<size_t>(e.mime_type.length) + 1u));
        if (!mime_copy) {
            free_mime_bundle_contents(dst);
            return false;
        }
        std::memcpy(mime_copy, e.mime_type.chars, e.mime_type.length);
        mime_copy[e.mime_type.length] = '\0';

        uint8_t* bytes_copy = nullptr;
        if (e.size > 0u) {
            bytes_copy = static_cast<uint8_t*>(SDL_malloc(e.size));
            if (!bytes_copy) {
                SDL_free(mime_copy);
                free_mime_bundle_contents(dst);
                return false;
            }
            std::memcpy(bytes_copy, e.bytes, e.size);
        }
        dst->entries[dst->count] = OwnedMimeEntry{
            .mime = mime_copy,
            .mime_len = e.mime_type.length,
            .bytes = bytes_copy,
            .size = e.size,
        };
        dst->mime_ptrs[dst->count] = mime_copy;
        ++dst->count;
    }
    return true;
}

const OwnedMimeEntry* find_local_mime_entry(Str mime_type) noexcept {
    for (uint32_t i = 0u; i < g_clipboard_mime_store.count; ++i) {
        const OwnedMimeEntry& e = g_clipboard_mime_store.entries[i];
        if (e.mime_len == mime_type.length &&
            std::memcmp(e.mime, mime_type.chars, mime_type.length) == 0)
        {
            return &e;
        }
    }
    return nullptr;
}

const void* SDLCALL sdl_clipboard_mime_data_cb(void* userdata, const char* mime_type, size_t* size) {
    if (size) *size = 0u;
    const auto* bundle = static_cast<const OwnedMimeBundle*>(userdata);
    if (!bundle || !mime_type) return nullptr;
    static const uint8_t kEmptyMimePayload = 0u;
    for (uint32_t i = 0u; i < bundle->count; ++i) {
        if (bundle->entries[i].mime && std::strcmp(bundle->entries[i].mime, mime_type) == 0) {
            if (size) *size = static_cast<size_t>(bundle->entries[i].size);
            return bundle->entries[i].bytes ? static_cast<const void*>(bundle->entries[i].bytes)
                                            : static_cast<const void*>(&kEmptyMimePayload);
        }
    }
    return nullptr;
}

void SDLCALL sdl_clipboard_mime_cleanup_cb(void* userdata) {
    auto* bundle = static_cast<OwnedMimeBundle*>(userdata);
    if (!bundle) return;
    if (bundle->sync_cleaned) {
        *bundle->sync_cleaned = true;
    }
    free_mime_bundle_contents(bundle);
    SDL_free(bundle);
}

bool copy_mime_to_cstr(Str mime_type, char* out_buf, size_t out_cap) noexcept {
    if (!mime_type.chars || mime_type.length == 0u || static_cast<size_t>(mime_type.length) >= out_cap) {
        return false;
    }
    std::memcpy(out_buf, mime_type.chars, mime_type.length);
    out_buf[mime_type.length] = '\0';
    return true;
}

ClipboardMimeData copy_bytes_to_arena(const void* raw, size_t sz, Arena* dst_arena) noexcept {
    if (sz == 0u) {
        return ClipboardMimeData{};
    }
    if (!raw || !dst_arena || sz > static_cast<size_t>(UINT32_MAX) || sz > dst_arena->remaining()) {
        return ClipboardMimeData{};
    }
    uint8_t* copy = dst_arena->alloc_uninit<uint8_t>(sz);
    if (!copy) {
        return ClipboardMimeData{};
    }
    std::memcpy(copy, raw, sz);
    return ClipboardMimeData{ .bytes = copy, .size_bytes = static_cast<uint32_t>(sz) };
}

} // namespace

bool set_clipboard_mime(std::span<const MimeDragEntry> entries) noexcept {
    Context* ctx = current_context();
    if (entries.empty()) {
        free_mime_bundle_contents(&g_clipboard_mime_store);
        if ((!ctx || !ctx->headless) && SDL_WasInit(SDL_INIT_VIDEO) != 0u) {
            return SDL_ClearClipboardData();
        }
        return true;
    }

    OwnedMimeBundle next_local = {};
    if (!clone_mime_bundle(entries, &next_local)) {
        return false;
    }
    if (!g_clipboard_mime_atexit_registered) {
        g_clipboard_mime_atexit_registered = true;
        std::atexit(cleanup_global_clipboard_mime_store);
    }
    free_mime_bundle_contents(&g_clipboard_mime_store);
    g_clipboard_mime_store = next_local;

    if ((!ctx || !ctx->headless) && SDL_WasInit(SDL_INIT_VIDEO) != 0u) {
        auto* sdl_bundle = static_cast<OwnedMimeBundle*>(SDL_calloc(1u, sizeof(OwnedMimeBundle)));
        if (!sdl_bundle) {
            return false;
        }
        if (!clone_mime_bundle(entries, sdl_bundle)) {
            SDL_free(sdl_bundle);
            return false;
        }
        bool cleaned_in_call = false;
        sdl_bundle->sync_cleaned = &cleaned_in_call;
        const bool sdl_ok = SDL_SetClipboardData(
            sdl_clipboard_mime_data_cb,
            sdl_clipboard_mime_cleanup_cb,
            sdl_bundle,
            sdl_bundle->mime_ptrs,
            static_cast<size_t>(sdl_bundle->count));
        if (sdl_ok) {
            if (!cleaned_in_call) {
                sdl_bundle->sync_cleaned = nullptr;
            }
        } else if (!cleaned_in_call) {
            free_mime_bundle_contents(sdl_bundle);
            SDL_free(sdl_bundle);
        }
        return sdl_ok;
    }
    return true;
}

bool has_clipboard_mime(Str mime_type) noexcept {
    if (!mime_type.chars || mime_type.length == 0u) {
        return false;
    }
    const Context* ctx = current_context();
    if ((!ctx || !ctx->headless) && SDL_WasInit(SDL_INIT_VIDEO) != 0u) {
        char mime_cstr[256] = {};
        if (!copy_mime_to_cstr(mime_type, mime_cstr, sizeof(mime_cstr))) {
            return false;
        }
        return SDL_HasClipboardData(mime_cstr);
    }
    return find_local_mime_entry(mime_type) != nullptr;
}

ClipboardMimeData get_clipboard_mime(Str mime_type, Arena* arena) noexcept {
    if (!mime_type.chars || mime_type.length == 0u) {
        return ClipboardMimeData{};
    }
    Context* ctx = current_context();
    Arena* dst_arena = arena ? arena : (ctx ? &ctx->frame_arena : nullptr);
    if (!dst_arena) {
        return ClipboardMimeData{};
    }
    if ((!ctx || !ctx->headless) && SDL_WasInit(SDL_INIT_VIDEO) != 0u) {
        char mime_cstr[256] = {};
        if (!copy_mime_to_cstr(mime_type, mime_cstr, sizeof(mime_cstr))) {
            return ClipboardMimeData{};
        }
        size_t sz = 0u;
        void* raw = SDL_GetClipboardData(mime_cstr, &sz);
        if (!raw) {
            return ClipboardMimeData{};
        }
        const ClipboardMimeData out = copy_bytes_to_arena(raw, sz, dst_arena);
        SDL_free(raw);
        return out;
    }
    const OwnedMimeEntry* e = find_local_mime_entry(mime_type);
    if (!e) {
        return ClipboardMimeData{};
    }
    return copy_bytes_to_arena(e->bytes, static_cast<size_t>(e->size), dst_arena);
}

// ============================================================================
// Desktop OS Integration: External URLs, Window Attention/Progress,
// Notifications & System Tray
// ============================================================================

namespace {
bool has_safe_url_scheme(Str url) noexcept {
    if (!url.chars || url.length < 6u || url.length > 4096u) return false;
    for (uint32_t i = 0u; i < url.length; ++i) {
        const unsigned char c = static_cast<unsigned char>(url.chars[i]);
        if (c < 0x20u || c == 0x7Fu) return false;
    }
    auto starts_ci = [&](const char* prefix) noexcept -> bool {
        const size_t n = std::strlen(prefix);
        if (url.length < n) return false;
        for (size_t i = 0u; i < n; ++i) {
            char a = url.chars[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
            if (a != prefix[i]) return false;
        }
        return true;
    };
    return starts_ci("https://") || starts_ci("http://") || starts_ci("mailto:") || starts_ci("file://");
}
} // namespace

bool open_url(Str url) noexcept {
    if (!has_safe_url_scheme(url)) return false;
    const Context* ctx = current_context();
    if ((ctx && ctx->headless) || SDL_WasInit(SDL_INIT_VIDEO) == 0u) {
        return true;
    }
    char buf[4097];
    std::memcpy(buf, url.chars, url.length);
    buf[url.length] = '\0';
    return SDL_OpenURL(buf);
}

bool flash_window(SDL_Window* window, WindowFlashKind kind) noexcept {
    const Context* ctx = current_context();
    SDL_Window* target = window ? window : (ctx ? ctx->window : nullptr);
    if (!target) return false;
    if ((ctx && ctx->headless) || SDL_WasInit(SDL_INIT_VIDEO) == 0u) return true;
    SDL_FlashOperation op = SDL_FLASH_BRIEFLY;
    if (kind == WindowFlashKind::Cancel) op = SDL_FLASH_CANCEL;
    else if (kind == WindowFlashKind::UntilFocused) op = SDL_FLASH_UNTIL_FOCUSED;
    return SDL_FlashWindow(target, op);
}

bool set_window_progress(SDL_Window* window, WindowProgressState state, float value_0_to_1) noexcept {
    const Context* ctx = current_context();
    SDL_Window* target = window ? window : (ctx ? ctx->window : nullptr);
    if (!target) return false;
    if ((ctx && ctx->headless) || SDL_WasInit(SDL_INIT_VIDEO) == 0u) return true;
    SDL_ProgressState ps = SDL_PROGRESS_STATE_NONE;
    switch (state) {
        case WindowProgressState::Indeterminate: ps = SDL_PROGRESS_STATE_INDETERMINATE; break;
        case WindowProgressState::Normal:        ps = SDL_PROGRESS_STATE_NORMAL; break;
        case WindowProgressState::Paused:        ps = SDL_PROGRESS_STATE_PAUSED; break;
        case WindowProgressState::Error:         ps = SDL_PROGRESS_STATE_ERROR; break;
        case WindowProgressState::None:
        default:                                 ps = SDL_PROGRESS_STATE_NONE; break;
    }
    bool ok = SDL_SetWindowProgressState(target, ps);
    if (state == WindowProgressState::Normal || state == WindowProgressState::Paused || state == WindowProgressState::Error) {
        const float clamped = std::isfinite(value_0_to_1) ? (value_0_to_1 < 0.0f ? 0.0f : (value_0_to_1 > 1.0f ? 1.0f : value_0_to_1)) : 0.0f;
        ok = SDL_SetWindowProgressValue(target, clamped) && ok;
    }
    return ok;
}

bool system_beep() noexcept {
    const Context* ctx = current_context();
    if ((ctx && ctx->headless) || SDL_WasInit(SDL_INIT_VIDEO) == 0u) return true;
#if defined(_WIN32)
    return MessageBeep(MB_OK) != 0;
#elif defined(__APPLE__) && defined(__MACH__)
    using NsBeepFn = void (*)();
    if (auto* fn = reinterpret_cast<NsBeepFn>(dlsym(RTLD_DEFAULT, "NSBeep"))) {
        fn();
        return true;
    }
    return true;
#else
    if (isatty(STDERR_FILENO)) {
        const char bel = '\a';
        const ssize_t ignored = write(STDERR_FILENO, &bel, 1);
        (void)ignored;
    }
    return true;
#endif
}

#if defined(_WIN32)
namespace {
void utf8_to_wide_trunc(Str src, wchar_t* dst, size_t dst_cap) noexcept {
    if (!dst || dst_cap == 0u) return;
    dst[0] = L'\0';
    if (!src.chars || src.length == 0u) return;
    const int n = MultiByteToWideChar(CP_UTF8, 0, src.chars, static_cast<int>(src.length),
                                      dst, static_cast<int>(dst_cap - 1u));
    if (n > 0) dst[n] = L'\0';
    else dst[dst_cap - 1u] = L'\0';
}
bool SDLCALL win32_native_menu_msg_hook(void* userdata, MSG* msg) {
    auto* ctx = static_cast<Context*>(userdata);
    if (!ctx || !msg) return true;
    if (msg->message == WM_COMMAND && HIWORD(msg->wParam) == 0 && msg->lParam == 0) {
        const uint32_t cmd_id = static_cast<uint32_t>(LOWORD(msg->wParam));
        if (cmd_id != 0u && (!ctx->native_menu_hwnd || msg->hwnd == static_cast<HWND>(ctx->native_menu_hwnd))) {
            if (ctx->native_menu_count < 32u) {
                ctx->native_menu_ring[(ctx->native_menu_head + ctx->native_menu_count) % 32u] = cmd_id;
                ++ctx->native_menu_count;
            } else {
                ctx->native_menu_ring[ctx->native_menu_head] = cmd_id;
                ctx->native_menu_head = (ctx->native_menu_head + 1u) % 32u;
            }
            wake_up(ctx);
        }
    }
    return true;
}
HMENU build_win32_menu_recursive(std::span<const MenuBarItemDesc> items, bool top_level, uint32_t depth) noexcept {
    if (depth > 4u) return nullptr;
    HMENU menu = top_level ? CreateMenu() : CreatePopupMenu();
    if (!menu) return nullptr;
    for (const MenuBarItemDesc& it : items) {
        if (it.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        wchar_t label_w[256] = {};
        utf8_to_wide_trunc(it.label, label_w, 200u);
        if (!top_level && it.shortcut.chars && it.shortcut.length > 0u) {
            const size_t cur = wcslen(label_w);
            if (cur + 2u < 255u) {
                label_w[cur] = L'\t';
                utf8_to_wide_trunc(it.shortcut, label_w + cur + 1u, 255u - cur - 1u);
            }
        }
        UINT flags = MF_STRING;
        if (it.disabled) flags |= MF_GRAYED;
        if (it.checked) flags |= MF_CHECKED;
        if (!it.children.empty()) {
            HMENU sub = build_win32_menu_recursive(it.children, false, depth + 1u);
            if (sub) {
                AppendMenuW(menu, flags | MF_POPUP, reinterpret_cast<UINT_PTR>(sub), label_w);
            }
        } else {
            AppendMenuW(menu, flags, static_cast<UINT_PTR>(it.id & 0xFFFFu), label_w);
        }
    }
    return menu;
}
} // namespace
#endif

bool notify_desktop(Context* ctx, const DesktopNotificationDesc& desc) noexcept {
    Context* target = ctx ? ctx : current_context();
    if (!target) return false;
    if ((!desc.title.chars || desc.title.length == 0u) && (!desc.body.chars || desc.body.length == 0u)) {
        return false;
    }
    ++target->desktop_notification_count;
    if (!target->headless && SDL_WasInit(SDL_INIT_VIDEO) != 0u) {
#if defined(_WIN32)
        HWND hwnd = target->window
            ? static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(target->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
            : nullptr;
        if (hwnd) {
            NOTIFYICONDATAW nid = {};
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 0x45544348u; // 'ETCH'
            nid.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;
            nid.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_INFORMATION));
            nid.dwInfoFlags = NIIF_INFO | (desc.silent ? NIIF_NOSOUND : 0u);
            utf8_to_wide_trunc(desc.title.length ? desc.title : str_lit("Notification"), nid.szInfoTitle, 64u);
            utf8_to_wide_trunc(desc.body.length ? desc.body : desc.title, nid.szInfo, 256u);
            utf8_to_wide_trunc(desc.title.length ? desc.title : str_lit("Etch"), nid.szTip, 128u);
            if (!target->win32_notify_icon_added || target->win32_notify_hwnd != hwnd) {
                if (Shell_NotifyIconW(NIM_ADD, &nid)) {
                    target->win32_notify_icon_added = true;
                    target->win32_notify_hwnd = hwnd;
                }
            } else {
                (void)Shell_NotifyIconW(NIM_MODIFY, &nid);
            }
        }
#elif !defined(__APPLE__)
        // Linux/BSD: non-blocking execvp("notify-send", ...) without shell interpolation.
        char title_z[256] = {};
        char body_z[512] = {};
        const uint32_t tn = (desc.title.chars && desc.title.length > 0u) ? (desc.title.length < 255u ? desc.title.length : 255u) : 0u;
        const uint32_t bn = (desc.body.chars && desc.body.length > 0u) ? (desc.body.length < 511u ? desc.body.length : 511u) : 0u;
        if (tn > 0u) std::memcpy(title_z, desc.title.chars, tn);
        if (bn > 0u) std::memcpy(body_z, desc.body.chars, bn);
        const pid_t pid = fork();
        if (pid == 0) {
            char* const argv[] = {
                const_cast<char*>("notify-send"),
                tn > 0u ? title_z : const_cast<char*>("Notification"),
                bn > 0u ? body_z : const_cast<char*>(""),
                nullptr
            };
            execvp("notify-send", argv);
            _exit(127);
        }
#endif
        if (target->window) {
            (void)SDL_FlashWindow(target->window, SDL_FLASH_BRIEFLY);
        }
        if (!desc.silent) {
            (void)system_beep();
        }
    }
    return true;
}

uint32_t desktop_notification_count(const Context* ctx) noexcept {
    const Context* target = ctx ? ctx : current_context();
    return target ? target->desktop_notification_count : 0u;
}

void shutdown_native_os_integration(Context* ctx) noexcept {
    if (!ctx) return;
#if defined(_WIN32)
    if (ctx->win32_notify_icon_added && ctx->win32_notify_hwnd) {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = static_cast<HWND>(ctx->win32_notify_hwnd);
        nid.uID = 0x45544348u;
        (void)Shell_NotifyIconW(NIM_DELETE, &nid);
        ctx->win32_notify_icon_added = false;
        ctx->win32_notify_hwnd = nullptr;
    }
    if (ctx->native_menu_handle) {
        SDL_SetWindowsMessageHook(nullptr, nullptr);
        if (ctx->native_menu_hwnd && IsWindow(static_cast<HWND>(ctx->native_menu_hwnd))) {
            SetMenu(static_cast<HWND>(ctx->native_menu_hwnd), nullptr);
        }
        DestroyMenu(static_cast<HMENU>(ctx->native_menu_handle));
        ctx->native_menu_handle = nullptr;
        ctx->native_menu_hwnd = nullptr;
    }
#endif
    ctx->native_menu_item_count = 0u;
    ctx->native_menu_head = 0u;
    ctx->native_menu_count = 0u;
}

struct Tray {
    Context* ctx = nullptr;
    SDL_Tray* sdl_tray = nullptr;
    SDL_TrayMenu* sdl_menu = nullptr;
    SDL_Mutex* mutex = nullptr;
    struct EntryCookie { Tray* owner; uint32_t item_id; };
    EntryCookie* cookies = nullptr;
    uint32_t cookie_count = 0u;
    uint32_t clicked_ring[32] = {};
    uint32_t ring_head = 0u;
    uint32_t ring_count = 0u;
    uint32_t icon_w = 0u;
    uint32_t icon_h = 0u;
};

namespace {
void SDLCALL tray_entry_cb(void* userdata, SDL_TrayEntry* entry) {
    (void)entry;
    auto* cookie = static_cast<Tray::EntryCookie*>(userdata);
    if (!cookie || !cookie->owner) return;
    Tray* t = cookie->owner;
    if (t->mutex) SDL_LockMutex(t->mutex);
    if (t->ring_count < 32u) {
        t->clicked_ring[(t->ring_head + t->ring_count) % 32u] = cookie->item_id;
        ++t->ring_count;
    } else {
        t->clicked_ring[t->ring_head] = cookie->item_id;
        t->ring_head = (t->ring_head + 1u) % 32u;
    }
    if (t->mutex) SDL_UnlockMutex(t->mutex);
    if (t->ctx) wake_up(t->ctx);
}
uint32_t count_tray_tree_items(std::span<const TrayItemDesc> items, uint32_t depth) noexcept {
    if (depth > 4u) return 0u;
    uint32_t n = static_cast<uint32_t>(items.size());
    for (const TrayItemDesc& it : items) {
        if (!it.submenu.empty()) n += count_tray_tree_items(it.submenu, depth + 1u);
    }
    return n;
}
void populate_tray_menu_recursive(
    Tray* tray, SDL_TrayMenu* menu, std::span<const TrayItemDesc> items,
    uint32_t* cookie_cursor, uint32_t depth) noexcept
{
    if (!tray || depth > 4u) return;
    for (const TrayItemDesc& it : items) {
        if (*cookie_cursor >= tray->cookie_count) break;
        const uint32_t ci = (*cookie_cursor)++;
        tray->cookies[ci] = { tray, it.id };
        if (!menu) {
            if (!it.submenu.empty()) {
                populate_tray_menu_recursive(tray, nullptr, it.submenu, cookie_cursor, depth + 1u);
            }
            continue;
        }
        if (it.separator) {
            SDL_InsertTrayEntryAt(menu, -1, nullptr, SDL_TRAYENTRY_BUTTON);
            continue;
        }
        char lbl[256] = {};
        const uint32_t n = (it.label.chars && it.label.length > 0u) ? (it.label.length < 255u ? it.label.length : 255u) : 0u;
        if (n > 0u) std::memcpy(lbl, it.label.chars, n);
        lbl[n] = '\0';
        if (!it.submenu.empty()) {
            SDL_TrayEntryFlags flags = SDL_TRAYENTRY_SUBMENU;
            if (it.disabled) flags |= SDL_TRAYENTRY_DISABLED;
            SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(menu, -1, lbl, flags);
            SDL_TrayMenu* sub = entry ? SDL_CreateTraySubmenu(entry) : nullptr;
            populate_tray_menu_recursive(tray, sub, it.submenu, cookie_cursor, depth + 1u);
        } else {
            SDL_TrayEntryFlags flags = it.checkbox ? SDL_TRAYENTRY_CHECKBOX : SDL_TRAYENTRY_BUTTON;
            if (it.disabled) flags |= SDL_TRAYENTRY_DISABLED;
            if (it.checkbox && it.checked) flags |= SDL_TRAYENTRY_CHECKED;
            SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(menu, -1, lbl, flags);
            if (entry && !it.disabled && it.id != 0u) {
                SDL_SetTrayEntryCallback(entry, tray_entry_cb, &tray->cookies[ci]);
            }
        }
    }
}
uint32_t count_menu_bar_items(std::span<const MenuBarItemDesc> items, uint32_t depth) noexcept {
    if (depth > 4u) return 0u;
    uint32_t total = static_cast<uint32_t>(items.size());
    for (const MenuBarItemDesc& it : items) {
        if (!it.children.empty()) total += count_menu_bar_items(it.children, depth + 1u);
    }
    return total;
}
} // namespace

Tray* create_tray(Context* ctx, Str tooltip) noexcept {
    Context* target = ctx ? ctx : current_context();
    auto* t = static_cast<Tray*>(SDL_calloc(1u, sizeof(Tray)));
    if (!t) return nullptr;
    t->ctx = target;
    t->mutex = SDL_CreateMutex();
    if (!t->mutex) {
        SDL_free(t);
        return nullptr;
    }
    if ((!target || !target->headless) && SDL_WasInit(SDL_INIT_VIDEO) != 0u) {
        char tip[256] = {};
        const uint32_t n = (tooltip.chars && tooltip.length > 0u) ? (tooltip.length < 255u ? tooltip.length : 255u) : 0u;
        if (n > 0u) std::memcpy(tip, tooltip.chars, n);
        tip[n] = '\0';
        t->sdl_tray = SDL_CreateTray(nullptr, n > 0u ? tip : nullptr);
        if (t->sdl_tray) {
            t->sdl_menu = SDL_CreateTrayMenu(t->sdl_tray);
        }
    }
    return t;
}

void destroy_tray(Tray* tray) noexcept {
    if (!tray) return;
    if (tray->sdl_tray) {
        SDL_DestroyTray(tray->sdl_tray);
        tray->sdl_tray = nullptr;
        tray->sdl_menu = nullptr;
    }
    SDL_free(tray->cookies);
    tray->cookies = nullptr;
    tray->cookie_count = 0u;
    if (tray->mutex) {
        SDL_DestroyMutex(tray->mutex);
        tray->mutex = nullptr;
    }
    SDL_free(tray);
}

bool tray_set_tooltip(Tray* tray, Str tooltip) noexcept {
    if (!tray) return false;
    if (tray->sdl_tray) {
        char tip[256] = {};
        const uint32_t n = (tooltip.chars && tooltip.length > 0u) ? (tooltip.length < 255u ? tooltip.length : 255u) : 0u;
        if (n > 0u) std::memcpy(tip, tooltip.chars, n);
        tip[n] = '\0';
        SDL_SetTrayTooltip(tray->sdl_tray, n > 0u ? tip : nullptr);
    }
    return true;
}

bool tray_set_icon_rgba(Tray* tray, const uint8_t* rgba_pixels, uint32_t width, uint32_t height) noexcept {
    if (!tray || !rgba_pixels || width == 0u || height == 0u || width > 512u || height > 512u) return false;
    tray->icon_w = width;
    tray->icon_h = height;
    if (tray->sdl_tray) {
        SDL_Surface* surf = SDL_CreateSurfaceFrom(
            static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32,
            const_cast<uint8_t*>(rgba_pixels), static_cast<int>(width * 4u));
        if (!surf) return false;
        SDL_SetTrayIcon(tray->sdl_tray, surf);
        SDL_DestroySurface(surf);
    }
    return true;
}

bool tray_set_menu(Tray* tray, std::span<const TrayItemDesc> items) noexcept {
    if (!tray || items.size() > 64u) return false;
    const uint32_t total = count_tray_tree_items(items, 0u);
    if (total > 256u) return false;
    if (tray->sdl_menu) {
        int existing_count = 0;
        const SDL_TrayEntry** existing = SDL_GetTrayEntries(tray->sdl_menu, &existing_count);
        if (existing && existing_count > 0) {
            for (int i = existing_count - 1; i >= 0; --i) {
                SDL_RemoveTrayEntry(const_cast<SDL_TrayEntry*>(existing[i]));
            }
        }
    }
    SDL_free(tray->cookies);
    tray->cookies = nullptr;
    tray->cookie_count = 0u;
    if (total == 0u) return true;
    tray->cookies = static_cast<Tray::EntryCookie*>(SDL_calloc(total, sizeof(Tray::EntryCookie)));
    if (!tray->cookies) return false;
    tray->cookie_count = total;
    uint32_t cursor = 0u;
    populate_tray_menu_recursive(tray, tray->sdl_menu, items, &cursor, 0u);
    return true;
}

uint32_t tray_poll_clicked_item(Tray* tray) noexcept {
    if (!tray) return 0u;
    uint32_t out = 0u;
    if (tray->mutex) SDL_LockMutex(tray->mutex);
    if (tray->ring_count > 0u) {
        out = tray->clicked_ring[tray->ring_head];
        tray->ring_head = (tray->ring_head + 1u) % 32u;
        --tray->ring_count;
    }
    if (tray->mutex) SDL_UnlockMutex(tray->mutex);
    return out;
}

bool set_native_menu_bar(Context* ctx, SDL_Window* window, std::span<const MenuBarItemDesc> menus) noexcept {
    Context* target = ctx ? ctx : current_context();
    if (!target || menus.size() > 32u) return false;
    const uint32_t total = count_menu_bar_items(menus, 0u);
    if (total > 512u) return false;
    target->native_menu_item_count = total;
    target->native_menu_head = 0u;
    target->native_menu_count = 0u;
#if defined(_WIN32)
    if (target->native_menu_handle) {
        SDL_SetWindowsMessageHook(nullptr, nullptr);
        if (target->native_menu_hwnd && IsWindow(static_cast<HWND>(target->native_menu_hwnd))) {
            SetMenu(static_cast<HWND>(target->native_menu_hwnd), nullptr);
            DrawMenuBar(static_cast<HWND>(target->native_menu_hwnd));
        }
        DestroyMenu(static_cast<HMENU>(target->native_menu_handle));
        target->native_menu_handle = nullptr;
        target->native_menu_hwnd = nullptr;
    }
    SDL_Window* target_win = window ? window : target->window;
    if (!target->headless && target_win && !menus.empty() && SDL_WasInit(SDL_INIT_VIDEO) != 0u) {
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(target_win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (hwnd) {
            HMENU hmenu = build_win32_menu_recursive(menus, true, 0u);
            if (!hmenu) return false;
            if (!SetMenu(hwnd, hmenu)) {
                DestroyMenu(hmenu);
                return false;
            }
            DrawMenuBar(hwnd);
            target->native_menu_handle = hmenu;
            target->native_menu_hwnd = hwnd;
            SDL_SetWindowsMessageHook(win32_native_menu_msg_hook, target);
        }
    }
#else
    (void)window;
#endif
    return true;
}

uint32_t poll_native_menu_item(Context* ctx) noexcept {
    Context* target = ctx ? ctx : current_context();
    if (!target || target->native_menu_count == 0u) return 0u;
    const uint32_t out = target->native_menu_ring[target->native_menu_head];
    target->native_menu_head = (target->native_menu_head + 1u) % 32u;
    --target->native_menu_count;
    return out;
}

} // namespace etch
