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

} // namespace etch
