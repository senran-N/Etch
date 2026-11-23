// Platform window chrome, smooth live-resize event watch, and optional OS accessibility bridge.
// Compiled via src/etch_unity.cpp.

#ifndef ETCH_ENABLE_OS_A11Y
#    define ETCH_ENABLE_OS_A11Y 1
#endif

#if defined(__APPLE__) && defined(__MACH__)
#    include <TargetConditionals.h>
#    if TARGET_OS_OSX
#        include <CoreGraphics/CGGeometry.h>
#        include <dlfcn.h>
#        include <objc/message.h>
#        include <objc/runtime.h>
#    endif
#elif defined(__linux__) && ETCH_ENABLE_OS_A11Y
#    include <dlfcn.h>
#endif

#if !defined(_WIN32) && defined(ETCH_USE_ACCESSKIT) && ETCH_ENABLE_OS_A11Y
#    include <accesskit.h>
#endif

#if defined(_WIN32) && ETCH_ENABLE_OS_A11Y
#    ifndef CINTERFACE
#        define CINTERFACE
#        define ETCH_DEFINED_CINTERFACE 1
#    endif
#    include <windows.h>
#    include <oleauto.h>
#    include <UIAutomation.h>
#    ifdef ETCH_DEFINED_CINTERFACE
#        undef CINTERFACE
#        undef ETCH_DEFINED_CINTERFACE
#    endif
#    ifdef small
#        undef small
#    endif
#    ifdef hyper
#        undef hyper
#    endif
#    ifdef near
#        undef near
#    endif
#    ifdef far
#        undef far
#    endif
#    ifdef min
#        undef min
#    endif
#    ifdef max
#        undef max
#    endif
#    ifdef CreateWindow
#        undef CreateWindow
#    endif
#    ifdef DrawText
#        undef DrawText
#    endif
#    ifdef LoadImage
#        undef LoadImage
#    endif
#    ifdef GetObject
#        undef GetObject
#    endif
#endif

namespace etch {

namespace {

ClaimedWindowSlot* find_claimed_slot(Context* ctx, SDL_Window* win) noexcept {
    if (!ctx || !win) {
        return nullptr;
    }
    for (uint32_t i = 0u; i < ctx->claimed_window_cap; ++i) {
        if (ctx->claimed_windows[i] && ctx->claimed_windows[i]->window == win) {
            return ctx->claimed_windows[i];
        }
    }
    return nullptr;
}

const ClaimedWindowSlot* find_claimed_slot(const Context* ctx, SDL_Window* win) noexcept {
    return find_claimed_slot(const_cast<Context*>(ctx), win);
}

bool sync_window_metrics_from_os(WindowUiState* ui, SDL_Window* window) noexcept {
    if (!ui || !window) {
        return false;
    }
    bool changed = false;
    int pw = 0;
    int ph = 0;
    if (SDL_GetWindowSizeInPixels(window, &pw, &ph) && pw > 0 && ph > 0) {
        const uint32_t next_w = static_cast<uint32_t>(pw);
        const uint32_t next_h = static_cast<uint32_t>(ph);
        if (ui->viewport_w != next_w || ui->viewport_h != next_h) {
            ui->viewport_w = next_w;
            ui->viewport_h = next_h;
            changed = true;
        }
    }
    float dpi = SDL_GetWindowDisplayScale(window);
    if (dpi <= 0.0f) {
        dpi = 1.0f;
    }
    if (ui->min_dpi_scale > 0.0f && dpi < ui->min_dpi_scale) {
        dpi = ui->min_dpi_scale;
    }
    if (std::fabs(ui->dpi_scale - dpi) > 1e-4f) {
        ui->dpi_scale = dpi;
        ui->applied_dpi_scale = dpi;
        changed = true;
    }
    if (changed) {
        ui->applied_ime_rect = {-1, -1, 0, 0};
        ui->applied_ime_cursor = -1;
        if (ui->redraw_budget < 2u) {
            ui->redraw_budget = 2u;
        }
    }
    return changed;
}

bool SDLCALL etch_sdl_event_watch(void* userdata, SDL_Event* event) noexcept {
    Context* ctx = static_cast<Context*>(userdata);
    if (!ctx || !event || !SDL_IsMainThread()) {
        return true;
    }
    if (event->type == SDL_EVENT_WINDOW_DESTROYED && !ctx->in_frame && !ctx->in_refresh_callback) {
        if (SDL_Window* win = SDL_GetWindowFromID(event->window.windowID)) {
            if (find_claimed_slot(ctx, win)) {
                release_window(ctx, win);
            }
        }
        return true;
    }
    if (!ctx->refresh_fn || ctx->in_frame || ctx->in_refresh_callback) {
        return true;
    }
    SDL_WindowID win_id = 0u;
    bool live_trigger = false;
    switch (event->type) {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            win_id = event->window.windowID;
            live_trigger = true;
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            win_id = event->window.windowID;
            live_trigger = (event->window.data1 == 1);
            break;
        default:
            break;
    }
    if (!live_trigger || win_id == 0u) {
        return true;
    }
    SDL_Window* win = SDL_GetWindowFromID(win_id);
    ClaimedWindowSlot* slot = find_claimed_slot(ctx, win);
    if (!slot || (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED) != 0u) {
        return true;
    }
    if (event->type == SDL_EVENT_WINDOW_EXPOSED) {
        int pw = 0;
        int ph = 0;
        bool size_changed = false;
        if (SDL_GetWindowSizeInPixels(win, &pw, &ph) && pw > 0 && ph > 0) {
            size_changed = (static_cast<uint32_t>(pw) != slot->ui.viewport_w) ||
                           (static_cast<uint32_t>(ph) != slot->ui.viewport_h);
        }
        float scale = SDL_GetWindowDisplayScale(win);
        if (scale <= 0.0f) {
            scale = 1.0f;
        }
        if (slot->ui.min_dpi_scale > 0.0f && scale < slot->ui.min_dpi_scale) {
            scale = slot->ui.min_dpi_scale;
        }
        const bool scale_changed = std::fabs(scale - slot->ui.dpi_scale) > 1e-4f;
        if (!size_changed && !scale_changed) {
            // Pure Win32 move (in_win32_sizemove && !in_win32_sizing) or unchanged modal-loop timer EXPOSED:
            // DWM composites the existing swapchain surface at native refresh rate without sync redraw stalls.
            if (slot->in_win32_sizemove && !slot->in_win32_sizing) {
                return true;
            }
            return true;
        }
    }
    sync_window_metrics_from_os(&slot->ui, win);
    if (slot->ui.redraw_budget < 2u) {
        slot->ui.redraw_budget = 2u;
    }
    const uint64_t now_ns = SDL_GetTicksNS();
    float dt = 0.0f;
    if (ctx->last_refresh_ns != 0u && now_ns > ctx->last_refresh_ns) {
        dt = static_cast<float>(now_ns - ctx->last_refresh_ns) * 1e-9f;
        if (dt > (1.0f / 30.0f)) {
            dt = 1.0f / 30.0f;
        }
    }
    const WindowRefreshFn fn = ctx->refresh_fn;
    void* const ud = ctx->refresh_user_data;
    ctx->last_refresh_ns = now_ns;
    ctx->in_refresh_callback = true;
    fn(ctx, win, dt, ud);
    ctx->in_refresh_callback = false;
    return true;
}

void sync_window_event_watch(Context* ctx) noexcept {
    if (!ctx) {
        return;
    }
    const bool want = !ctx->headless && (ctx->refresh_fn != nullptr || ctx->claimed_window_count > 0u);
    if (want && !ctx->event_watch_attached) {
        if (SDL_WasInit(SDL_INIT_EVENTS) || SDL_WasInit(SDL_INIT_VIDEO)) {
            if (SDL_AddEventWatch(etch_sdl_event_watch, ctx)) {
                ctx->event_watch_attached = true;
            }
        }
    } else if (!want && ctx->event_watch_attached) {
        SDL_RemoveEventWatch(etch_sdl_event_watch, ctx);
        ctx->event_watch_attached = false;
    }
}

void detach_window_event_watch(Context* ctx) noexcept {
    if (ctx && ctx->event_watch_attached) {
        SDL_RemoveEventWatch(etch_sdl_event_watch, ctx);
        ctx->event_watch_attached = false;
    }
}

void apply_platform_window_chrome(Context* ctx, ClaimedWindowSlot* slot) noexcept {
    if (!ctx || !slot || !slot->window || ctx->headless) {
        return;
    }
#if defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX
    void* nswin = SDL_GetPointerProperty(SDL_GetWindowProperties(slot->window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!nswin) {
        return;
    }
    const ::id ns_obj = reinterpret_cast<::id>(nswin);
    constexpr unsigned long kFullSizeContentView = 1UL << 15;
    using GetMaskFn = unsigned long (*)(::id, SEL);
    using SetMaskFn = void (*)(::id, SEL, unsigned long);
    using SetBoolFn = void (*)(::id, SEL, signed char);
    using SetLongFn = void (*)(::id, SEL, long);
    using StdBtnFn = ::id (*)(::id, SEL, unsigned long);

    const SEL sel_mask = sel_registerName("styleMask");
    const SEL sel_set_mask = sel_registerName("setStyleMask:");
    const SEL sel_set_trans = sel_registerName("setTitlebarAppearsTransparent:");
    const SEL sel_set_vis = sel_registerName("setTitleVisibility:");
    const SEL sel_std_btn = sel_registerName("standardWindowButton:");
    const SEL sel_set_hidden = sel_registerName("setHidden:");

    const unsigned long mask = reinterpret_cast<GetMaskFn>(objc_msgSend)(ns_obj, sel_mask);
    if (slot->ui.chrome_desc.enabled && slot->ui.chrome_desc.macos_traffic_lights) {
        reinterpret_cast<SetMaskFn>(objc_msgSend)(ns_obj, sel_set_mask, mask | kFullSizeContentView);
        reinterpret_cast<SetBoolFn>(objc_msgSend)(ns_obj, sel_set_trans, static_cast<signed char>(1));
        reinterpret_cast<SetLongFn>(objc_msgSend)(ns_obj, sel_set_vis, 1L);
        for (unsigned long b = 0UL; b <= 2UL; ++b) {
            if (const ::id btn = reinterpret_cast<StdBtnFn>(objc_msgSend)(ns_obj, sel_std_btn, b)) {
                reinterpret_cast<SetBoolFn>(objc_msgSend)(btn, sel_set_hidden, static_cast<signed char>(0));
            }
        }
        slot->macos_chrome_applied = true;
    } else if (slot->macos_chrome_applied) {
        reinterpret_cast<SetMaskFn>(objc_msgSend)(ns_obj, sel_set_mask, mask & ~kFullSizeContentView);
        reinterpret_cast<SetBoolFn>(objc_msgSend)(ns_obj, sel_set_trans, static_cast<signed char>(0));
        reinterpret_cast<SetLongFn>(objc_msgSend)(ns_obj, sel_set_vis, 0L);
        slot->macos_chrome_applied = false;
    }
#elif defined(_WIN32)
    apply_win32_chrome(ctx,slot);
#else
    (void)slot;
#endif
}

#if defined(_WIN32) && ETCH_ENABLE_OS_A11Y

constexpr GUID kIidIUnknown = {0x00000000u, 0x0000u, 0x0000u, {0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u}};
constexpr GUID kIidUiaSimple = {0xd6dd68d1u, 0x86fdu, 0x4332u, {0x86u, 0x66u, 0x9au, 0xbeu, 0xdeu, 0xa2u, 0xd2u, 0x4cu}};
constexpr GUID kIidUiaFragment = {0xf7063da8u, 0x8359u, 0x439cu, {0x92u, 0x97u, 0xbbu, 0xc5u, 0x29u, 0x9au, 0x7du, 0x87u}};
constexpr GUID kIidUiaFragmentRoot = {0x620ce2a5u, 0xab8fu, 0x40a9u, {0x86u, 0xcbu, 0xdeu, 0x3cu, 0x75u, 0x59u, 0x9bu, 0x58u}};
constexpr GUID kIidUiaInvoke = {0x54fcb24bu, 0xe18eu, 0x47a2u, {0xb4u, 0xd3u, 0xecu, 0xcbu, 0xe7u, 0x75u, 0x99u, 0xa2u}};
constexpr GUID kIidUiaValue = {0xc7935180u, 0x6fb3u, 0x4201u, {0xb1u, 0x74u, 0x7du, 0xf7u, 0x3au, 0xdbu, 0xf6u, 0x4au}};
constexpr GUID kIidUiaRangeValue = {0x36dc7aefu, 0x33e6u, 0x4691u, {0xafu, 0xe1u, 0x2bu, 0xe7u, 0x27u, 0x4bu, 0x3du, 0x33u}};
constexpr GUID kIidUiaToggle = {0x56d00bd0u, 0xc4f4u, 0x433cu, {0xa8u, 0x36u, 0x1au, 0x52u, 0xa5u, 0x7eu, 0x08u, 0x92u}};
constexpr GUID kIidUiaExpandCollapse = {0xd847d3a5u, 0xcab0u, 0x4a98u, {0x8cu, 0x32u, 0xecu, 0xb4u, 0x5cu, 0x59u, 0xadu, 0x24u}};
constexpr GUID kIidUiaSelectionItem = {0x2acad808u, 0xb2d4u, 0x452du, {0xa4u, 0x07u, 0x91u, 0xffu, 0x1au, 0xd1u, 0x67u, 0xb2u}};
constexpr GUID kIidUiaText = {0x3589c92cu, 0x63f3u, 0x4367u, {0x99u, 0xbbu, 0xadu, 0xa6u, 0x53u, 0xb7u, 0x7cu, 0xf2u}};
constexpr GUID kIidUiaTextRange = {0x5347ad7bu, 0xc355u, 0x46f8u, {0xafu, 0xf5u, 0x90u, 0x90u, 0x33u, 0x58u, 0x2fu, 0x63u}};
constexpr GUID kIidUiaScroll = {0xb38b8077u, 0x1fc3u, 0x42a5u, {0x8cu, 0xaeu, 0xd4u, 0x0cu, 0x22u, 0x15u, 0x05u, 0x5au}};
constexpr GUID kIidUiaGrid = {0xb17d6187u, 0x0907u, 0x464bu, {0xa1u, 0x68u, 0x0eu, 0xf1u, 0x7au, 0x15u, 0x72u, 0xb1u}};
constexpr GUID kIidUiaGridItem = {0xd02541f1u, 0xfb81u, 0x4d64u, {0xaeu, 0x32u, 0xf5u, 0x20u, 0xf8u, 0xa6u, 0xdbu, 0xd1u}};
constexpr GUID kIidUiaTable = {0x9c860395u, 0x97b3u, 0x490au, {0xb5u, 0x2au, 0x85u, 0x8cu, 0xc2u, 0x2au, 0xf1u, 0x66u}};
constexpr GUID kIidUiaTableItem = {0xb9734fa6u, 0x771fu, 0x4d78u, {0x9cu, 0x90u, 0x25u, 0x17u, 0x99u, 0x93u, 0x49u, 0xcdu}};

constexpr UINT_PTR kUiaSubclassId = 0x45544131u;
constexpr HRESULT kUiaElemNotAvail = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
constexpr HRESULT kUiaInvalidOp = static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
constexpr uint32_t kUiaMaxTrackedNodes = 512u;
constexpr uint32_t kUiaMaxPropEventsPerCommit = 16u;

struct UiaDispatchCall;

struct WinUiaApi {
    HMODULE dll = nullptr;
    LRESULT(WINAPI* return_raw)(HWND, WPARAM, LPARAM, IRawElementProviderSimple*) = nullptr;
    HRESULT(WINAPI* host_from_hwnd)(HWND, IRawElementProviderSimple**) = nullptr;
    HRESULT(WINAPI* raise_event)(IRawElementProviderSimple*, EVENTID) = nullptr;
    HRESULT(WINAPI* raise_prop_changed)(IRawElementProviderSimple*, PROPERTYID, VARIANT, VARIANT) = nullptr;
    HRESULT(WINAPI* raise_structure_changed)(IRawElementProviderSimple*, enum StructureChangeType, int*, int) = nullptr;
    HRESULT(WINAPI* get_not_supported)(IUnknown**) = nullptr;
    BOOL(WINAPI* clients_listening)() = nullptr;
    UINT dispatch_msg = 0u;
    SRWLOCK dispatch_lock = SRWLOCK_INIT;
    CONDITION_VARIABLE dispatch_cv = CONDITION_VARIABLE_INIT;
    UiaDispatchCall* pending_calls = nullptr;
    uintptr_t next_dispatch_seq = 0u;
    bool loaded = false;
    bool ok = false;
};

WinUiaApi g_win_uia = {};

template <typename Fn>
Fn load_proc(HMODULE mod, const char* name) noexcept {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(mod, name)));
}

bool ensure_win_uia_api() noexcept {
    if (g_win_uia.loaded) {
        return g_win_uia.ok;
    }
    g_win_uia.loaded = true;
    g_win_uia.dll = LoadLibraryW(L"UIAutomationCore.dll");
    if (!g_win_uia.dll) {
        return false;
    }
    g_win_uia.return_raw = load_proc<decltype(g_win_uia.return_raw)>(g_win_uia.dll, "UiaReturnRawElementProvider");
    g_win_uia.host_from_hwnd = load_proc<decltype(g_win_uia.host_from_hwnd)>(g_win_uia.dll, "UiaHostProviderFromHwnd");
    g_win_uia.raise_event = load_proc<decltype(g_win_uia.raise_event)>(g_win_uia.dll, "UiaRaiseAutomationEvent");
    g_win_uia.raise_prop_changed = load_proc<decltype(g_win_uia.raise_prop_changed)>(g_win_uia.dll, "UiaRaiseAutomationPropertyChangedEvent");
    g_win_uia.raise_structure_changed = load_proc<decltype(g_win_uia.raise_structure_changed)>(g_win_uia.dll, "UiaRaiseStructureChangedEvent");
    g_win_uia.get_not_supported = load_proc<decltype(g_win_uia.get_not_supported)>(g_win_uia.dll, "UiaGetReservedNotSupportedValue");
    g_win_uia.clients_listening = load_proc<decltype(g_win_uia.clients_listening)>(g_win_uia.dll, "UiaClientsAreListening");
    g_win_uia.dispatch_msg = RegisterWindowMessageW(L"EtchOsA11yDispatch");
    g_win_uia.ok = (g_win_uia.return_raw != nullptr && g_win_uia.host_from_hwnd != nullptr && g_win_uia.dispatch_msg != 0u);
    return g_win_uia.ok;
}

struct WinUiaNode;
struct WinUiaSlot;

struct WinUiaFacet {
    const void* lpVtbl;
    WinUiaNode* owner;
};
static_assert(offsetof(WinUiaFacet, lpVtbl) == 0);

struct WinUiaNode {
    WinUiaFacet simple;
    WinUiaFacet fragment;
    WinUiaFacet fragment_root;
    WinUiaFacet invoke;
    WinUiaFacet value;
    WinUiaFacet range_value;
    WinUiaFacet toggle;
    WinUiaFacet expand_collapse;
    WinUiaFacet selection_item;
    WinUiaFacet text;
    WinUiaFacet scroll;
    WinUiaFacet grid;
    WinUiaFacet grid_item;
    WinUiaFacet table;
    WinUiaFacet table_item;
    WinUiaSlot* slot;
    HWND hwnd;
    DWORD owner_tid;
    uint32_t element_id;
    LONG ref_count;
    bool is_root;
    volatile LONG detached;
};

struct WinUiaTextRange {
    const ITextRangeProviderVtbl* lpVtbl;
    WinUiaNode* owner;
    uint32_t start_u16;
    uint32_t end_u16;
    LONG ref_count;
};
static_assert(offsetof(WinUiaTextRange, lpVtbl) == 0);

struct WinUiaTrackedNode {
    uint32_t id;
    uint32_t state_flags;
    uint64_t label_hash;
    uint64_t value_hash;
    uint32_t label_len;
    uint32_t value_len;
    A11yRole role;
};

struct WinUiaSlot {
    Context* ctx;
    ClaimedWindowSlot* claimed;
    SDL_Window* window;
    HWND hwnd;
    DWORD owner_tid;
    bool subclass_attached;
    WinUiaNode* root;
    WinUiaNode** nodes;
    uint32_t node_count;
    uint32_t node_cap;
    bool has_prev_snapshot;
    uint32_t prev_node_count;
    uint64_t prev_struct_hash;
    WinUiaTrackedNode* prev_tracked;
    uint32_t prev_tracked_cap;
    uint32_t prev_tracked_count;
};

bool uia_is_detached(const WinUiaNode* n) noexcept {
    return !n || InterlockedCompareExchange(const_cast<volatile LONG*>(&n->detached), 0L, 0L) != 0L;
}

struct UiaDispatchCall {
    WinUiaNode* node;
    HRESULT (*fn)(void* ctx) noexcept;
    void* ctx;
    HRESULT hr;
    uintptr_t seq;
    uint8_t state; // 0 = pending, 1 = executing on UI thread, 2 = completed, 3 = rejected
    UiaDispatchCall* next;
};

template <typename F>
HRESULT uia_run_on_owner(WinUiaNode* n, F&& fn) noexcept {
    if (uia_is_detached(n)) {
        return kUiaElemNotAvail;
    }
    if (GetCurrentThreadId() == n->owner_tid) {
        if (!n->slot) {
            return kUiaElemNotAvail;
        }
        return fn();
    }
    if (!n->hwnd || !IsWindow(n->hwnd) || g_win_uia.dispatch_msg == 0u) {
        return kUiaElemNotAvail;
    }
    using FnObj = std::remove_reference_t<F>;
    UiaDispatchCall call = {
        .node = n,
        .fn = [](void* p) noexcept -> HRESULT { return (*static_cast<FnObj*>(p))(); },
        .ctx = &fn,
        .hr = kUiaElemNotAvail,
        .seq = 0u,
        .state = 0u,
        .next = nullptr,
    };
    AcquireSRWLockExclusive(&g_win_uia.dispatch_lock);
    uintptr_t seq = ++g_win_uia.next_dispatch_seq;
    if (seq == 0u) {
        seq = ++g_win_uia.next_dispatch_seq;
    }
    call.seq = seq;
    call.next = g_win_uia.pending_calls;
    g_win_uia.pending_calls = &call;
    ReleaseSRWLockExclusive(&g_win_uia.dispatch_lock);

    DWORD_PTR msg_res = 0u;
    (void)SendMessageTimeoutW(
        n->hwnd,
        g_win_uia.dispatch_msg,
        static_cast<WPARAM>(seq),
        reinterpret_cast<LPARAM>(&call),
        SMTO_ABORTIFHUNG | SMTO_NORMAL,
        2000u,
        &msg_res);

    AcquireSRWLockExclusive(&g_win_uia.dispatch_lock);
    while (call.state == 1u) {
        SleepConditionVariableSRW(&g_win_uia.dispatch_cv, &g_win_uia.dispatch_lock, INFINITE, 0u);
    }
    for (UiaDispatchCall** pp = &g_win_uia.pending_calls; *pp; pp = &(*pp)->next) {
        if (*pp == &call) {
            *pp = call.next;
            break;
        }
    }
    const HRESULT out = (call.state == 2u) ? call.hr : kUiaElemNotAvail;
    ReleaseSRWLockExclusive(&g_win_uia.dispatch_lock);
    return out;
}

bool uia_guid_eq(REFIID a, const GUID& b) noexcept {
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

WinUiaNode* uia_owner(void* facet_ptr) noexcept {
    return facet_ptr ? reinterpret_cast<WinUiaFacet*>(facet_ptr)->owner : nullptr;
}

ULONG uia_addref(WinUiaNode* n) noexcept {
    return n ? static_cast<ULONG>(InterlockedIncrement(&n->ref_count)) : 0u;
}

ULONG uia_release(WinUiaNode* n) noexcept {
    if (!n) {
        return 0u;
    }
    const LONG next = InterlockedDecrement(&n->ref_count);
    if (next == 0L) {
        std::free(n);
    }
    return static_cast<ULONG>(next);
}

const A11yNode* uia_live_node(WinUiaNode* n, uint32_t* out_idx = nullptr) noexcept {
    if (uia_is_detached(n) || !n->slot || !n->slot->ctx || !n->slot->window) {
        return nullptr;
    }
    const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
    if (n->is_root) {
        if (span.empty()) {
            return nullptr;
        }
        if (out_idx) {
            *out_idx = 0u;
        }
        return &span[0];
    }
    for (uint32_t i = 0u; i < span.size(); ++i) {
        if (span[i].id == n->element_id) {
            if (out_idx) {
                *out_idx = i;
            }
            return &span[i];
        }
    }
    return nullptr;
}

WindowUiState* uia_window_ui(WinUiaSlot* slot) noexcept {
    if (!slot) return nullptr;
    return slot->claimed ? &slot->claimed->ui : window_ui(slot->ctx, slot->window);
}

uint32_t uia_find_index_by_id(std::span<const A11yNode> span, uint32_t elem_id) noexcept {
    for (uint32_t i = 0u; i < span.size(); ++i) {
        if (span[i].id == elem_id) return i;
    }
    return UINT32_MAX;
}

bool uia_is_table_row(
    std::span<const A11yNode> span,
    uint32_t idx,
    uint32_t* out_table_idx = nullptr,
    uint32_t* out_row_ord = nullptr) noexcept
{
    if (idx == 0u || idx >= span.size() || span[idx].parent_id == 0u) return false;
    const uint32_t parent_idx = uia_find_index_by_id(span, span[idx].parent_id);
    if (parent_idx >= span.size() || span[parent_idx].role != A11yRole::Table) return false;
    if (out_table_idx) *out_table_idx = parent_idx;
    if (out_row_ord) {
        uint32_t ord = 0u;
        uint32_t c = span[parent_idx].first_child_idx;
        uint32_t guard = 0u;
        const uint32_t count = static_cast<uint32_t>(span.size());
        while (c < count && guard++ < count) {
            if (c == idx) {
                *out_row_ord = ord;
                break;
            }
            ++ord;
            c = span[c].next_sibling_idx;
        }
    }
    return true;
}

bool uia_cell_coords(
    std::span<const A11yNode> span,
    uint32_t idx,
    uint32_t* out_table_idx = nullptr,
    uint32_t* out_row = nullptr,
    uint32_t* out_col = nullptr) noexcept
{
    if (idx == 0u || idx >= span.size() || span[idx].parent_id == 0u) return false;
    const uint32_t row_idx = uia_find_index_by_id(span, span[idx].parent_id);
    if (row_idx >= span.size()) return false;
    uint32_t tbl_idx = UINT32_MAX;
    uint32_t row_ord = 0u;
    if (!uia_is_table_row(span, row_idx, &tbl_idx, &row_ord)) return false;
    uint32_t col_ord = 0u;
    uint32_t c = span[row_idx].first_child_idx;
    uint32_t guard = 0u;
    const uint32_t count = static_cast<uint32_t>(span.size());
    bool found = false;
    while (c < count && guard++ < count) {
        if (c == idx) {
            found = true;
            break;
        }
        ++col_ord;
        c = span[c].next_sibling_idx;
    }
    if (!found) return false;
    if (out_table_idx) *out_table_idx = tbl_idx;
    if (out_row) *out_row = row_ord;
    if (out_col) *out_col = col_ord;
    return true;
}

void uia_table_dims(
    WinUiaSlot* slot,
    std::span<const A11yNode> span,
    uint32_t tbl_idx,
    uint32_t* out_rows,
    uint32_t* out_cols) noexcept
{
    uint32_t rows = 0u;
    uint32_t max_cols = 0u;
    const uint32_t count = static_cast<uint32_t>(span.size());
    if (tbl_idx < count) {
        uint32_t r = span[tbl_idx].first_child_idx;
        uint32_t r_guard = 0u;
        while (r < count && r_guard++ < count) {
            ++rows;
            uint32_t cols = 0u;
            uint32_t c = span[r].first_child_idx;
            uint32_t c_guard = 0u;
            while (c < count && c_guard++ < count) {
                ++cols;
                c = span[c].next_sibling_idx;
            }
            if (cols > max_cols) max_cols = cols;
            r = span[r].next_sibling_idx;
        }
        if (max_cols == 0u && slot) {
            if (const WindowUiState* ui = uia_window_ui(slot)) {
                for (uint32_t i = 0u; i < ui->table_count; ++i) {
                    if (ui->tables[i].id == span[tbl_idx].id) {
                        max_cols = ui->tables[i].count;
                        break;
                    }
                }
            }
        }
    }
    if (out_rows) *out_rows = rows;
    if (out_cols) *out_cols = max_cols;
}

uint32_t uia_table_cell_at(std::span<const A11yNode> span, uint32_t tbl_idx, uint32_t target_row, uint32_t target_col) noexcept {
    const uint32_t count = static_cast<uint32_t>(span.size());
    if (tbl_idx >= count) return UINT32_MAX;
    uint32_t r = span[tbl_idx].first_child_idx;
    uint32_t cur_r = 0u;
    uint32_t guard = 0u;
    while (r < count && guard++ < count) {
        if (cur_r == target_row) {
            uint32_t c = span[r].first_child_idx;
            uint32_t cur_c = 0u;
            uint32_t c_guard = 0u;
            while (c < count && c_guard++ < count) {
                if (cur_c == target_col) return c;
                ++cur_c;
                c = span[c].next_sibling_idx;
            }
            return UINT32_MAX;
        }
        ++cur_r;
        r = span[r].next_sibling_idx;
    }
    return UINT32_MAX;
}

uint32_t uia_table_column_header_id(const WindowUiState* ui, uint32_t table_id, uint32_t col) noexcept {
    if (!ui) return 0u;
    for (uint32_t i = 0u; i < ui->table_count; ++i) {
        if (ui->tables[i].id == table_id) {
            return (col < ui->tables[i].count) ? id_index(ui->tables[i].column_ids[col], 0x7601u) : 0u;
        }
    }
    return 0u;
}

bool uia_is_control_element(std::span<const A11yNode> span, uint32_t idx) noexcept {
    if (idx >= span.size()) return false;
    const A11yNode& an = span[idx];
    if (an.role != A11yRole::Group && an.role != A11yRole::None) {
        return true;
    }
    if (an.label.length > 0u || an.value_text.length > 0u) {
        return true;
    }
    constexpr uint32_t kSemanticStates =
        A11yState_Focused | A11yState_Selected | A11yState_Checked | A11yState_Expanded;
    if ((an.state_flags & kSemanticStates) != 0u) {
        return true;
    }
    if ((an.actions_mask & ~a11y_action_bit(A11yAction::Press)) != 0u) {
        return true;
    }
    if (uia_is_table_row(span, idx) || uia_cell_coords(span, idx)) {
        return true;
    }
    return false;
}

bool uia_is_live_role(A11yRole role) noexcept {
    return role == A11yRole::Tooltip || role == A11yRole::ProgressBar || role == A11yRole::Dialog;
}

bool uia_supports_invoke(const A11yNode* n) noexcept {
    if (!n || (n->actions_mask & a11y_action_bit(A11yAction::Press)) == 0u) {
        return false;
    }
    return n->role != A11yRole::Checkbox && n->role != A11yRole::Switch &&
           n->role != A11yRole::Tab && n->role != A11yRole::ListItem &&
           n->role != A11yRole::TreeItem;
}

bool uia_supports_toggle(const A11yNode* n) noexcept {
    return n && (n->role == A11yRole::Checkbox || n->role == A11yRole::Switch) &&
           (n->actions_mask & a11y_action_bit(A11yAction::Press)) != 0u;
}

bool uia_supports_range_value(const A11yNode* n) noexcept {
    return n && (n->role == A11yRole::Slider || n->role == A11yRole::SpinButton ||
                 n->role == A11yRole::ProgressBar);
}

bool uia_supports_value(const A11yNode* n) noexcept {
    return n && (n->role == A11yRole::TextInput || n->role == A11yRole::CodeEditor ||
                 n->role == A11yRole::ComboBox);
}

bool uia_supports_expand_collapse(const A11yNode* n) noexcept {
    return n && (n->actions_mask & (a11y_action_bit(A11yAction::Expand) | a11y_action_bit(A11yAction::Collapse))) != 0u;
}

bool uia_supports_selection_item(const A11yNode* n) noexcept {
    return n && (n->role == A11yRole::Tab || n->role == A11yRole::ListItem || n->role == A11yRole::TreeItem);
}

bool uia_supports_text(WinUiaNode* n, const A11yNode* an) noexcept {
    if (!an) return false;
    if (an->role == A11yRole::TextInput || an->role == A11yRole::CodeEditor) return true;
    if (an->role == A11yRole::StaticText && n && n->slot) {
        if (WindowUiState* ui = uia_window_ui(n->slot)) {
            const WidgetState* ws = state_map_find(&ui->states, an->id);
            if (ws && ws->is_selectable_text) return true;
        }
    }
    return false;
}

bool uia_supports_scroll(const A11yNode* an) noexcept {
    return an && (an->role == A11yRole::ScrollArea || an->role == A11yRole::List || an->role == A11yRole::Table);
}

bool uia_supports_grid(const A11yNode* an) noexcept {
    return an && an->role == A11yRole::Table;
}

bool uia_supports_grid_item(std::span<const A11yNode> span, uint32_t idx) noexcept {
    return uia_cell_coords(span, idx);
}

struct UiaScrollGeom {
    WidgetState* v_ws = nullptr;
    WidgetState* h_ws = nullptr;
    bool is_vlist = false;
    bool h_scrollable = false;
    bool v_scrollable = false;
    double h_pos = 0.0;
    double h_max = 0.0;
    double h_view = 0.0;
    double h_content = 0.0;
    double v_pos = 0.0;
    double v_max = 0.0;
    double v_view = 0.0;
    double v_content = 0.0;
};

bool uia_read_scroll_geom(WinUiaNode* n, const A11yNode* an, UiaScrollGeom* out) noexcept {
    *out = {};
    if (!n || !n->slot || !an) return false;
    WindowUiState* ui = uia_window_ui(n->slot);
    if (!ui) return false;
    WidgetState* ws = state_map_find(&ui->states, an->id);
    if (!ws) return false;
    if (ws->is_virtual_list) {
        const WidgetCold* c = ws->cold;
        out->v_ws = ws;
        out->is_vlist = true;
        out->v_pos = c ? c->vscroll_pos_y : 0.0;
        out->v_max = c ? c->vscroll_max_y : 0.0;
        out->v_view = (ws->viewport_dim.y > 0.5f) ? static_cast<double>(ws->viewport_dim.y) : static_cast<double>(an->bounds.h);
        const double total_h = c ? c->vscroll_total_h : 0.0;
        out->v_content = (total_h > out->v_view) ? total_h : (out->v_view + out->v_max);
        out->v_scrollable = (out->v_max > 0.5);
        if (an->role == A11yRole::Table) {
            if (WidgetState* outer_ws = state_map_find(&ui->states, id_index(an->id, 0x7400u))) {
                if (outer_ws->is_scroll_x) {
                    out->h_ws = outer_ws;
                    out->h_pos = static_cast<double>(outer_ws->scroll_pos.x);
                    out->h_max = static_cast<double>(outer_ws->scroll_max.x);
                    out->h_view = (outer_ws->viewport_dim.x > 0.5f) ? static_cast<double>(outer_ws->viewport_dim.x) : static_cast<double>(an->bounds.w);
                    out->h_content = (outer_ws->content_dim.x > out->h_view) ? static_cast<double>(outer_ws->content_dim.x) : (out->h_view + out->h_max);
                    out->h_scrollable = (out->h_max > 0.5);
                }
            }
        }
        return true;
    }
    out->h_ws = ws->is_scroll_x ? ws : nullptr;
    out->v_ws = ws->is_scroll_y ? ws : nullptr;
    if (ws->is_scroll_x) {
        out->h_pos = static_cast<double>(ws->scroll_pos.x);
        out->h_max = static_cast<double>(ws->scroll_max.x);
        out->h_view = (ws->viewport_dim.x > 0.5f) ? static_cast<double>(ws->viewport_dim.x) : static_cast<double>(an->bounds.w);
        out->h_content = (ws->content_dim.x > out->h_view) ? static_cast<double>(ws->content_dim.x) : (out->h_view + out->h_max);
        out->h_scrollable = (out->h_max > 0.5);
    }
    if (ws->is_scroll_y) {
        out->v_pos = static_cast<double>(ws->scroll_pos.y);
        out->v_max = static_cast<double>(ws->scroll_max.y);
        out->v_view = (ws->viewport_dim.y > 0.5f) ? static_cast<double>(ws->viewport_dim.y) : static_cast<double>(an->bounds.h);
        out->v_content = (ws->content_dim.y > out->v_view) ? static_cast<double>(ws->content_dim.y) : (out->v_view + out->v_max);
        out->v_scrollable = (out->v_max > 0.5);
    }
    return true;
}

double uia_scroll_percent(bool scrollable, double pos, double max_val) noexcept {
    if (!scrollable || max_val <= 0.5) return UIA_ScrollPatternNoScroll;
    const double pct = (pos / max_val) * 100.0;
    return (pct < 0.0) ? 0.0 : ((pct > 100.0) ? 100.0 : pct);
}

double uia_scroll_view_size(bool scrollable, double view, double content) noexcept {
    if (!scrollable || content <= 0.5) return 100.0;
    const double pct = (view / content) * 100.0;
    return (pct < 0.0) ? 0.0 : ((pct > 100.0) ? 100.0 : pct);
}

SAFEARRAY* uia_make_unk_safearray(IUnknown* const* items, uint32_t count) noexcept {
    SAFEARRAY* arr = SafeArrayCreateVector(VT_UNKNOWN, 0, count);
    if (!arr) return nullptr;
    for (uint32_t i = 0u; i < count; ++i) {
        LONG idx = static_cast<LONG>(i);
        if (FAILED(SafeArrayPutElement(arr, &idx, items[i]))) {
            SafeArrayDestroy(arr);
            return nullptr;
        }
    }
    return arr;
}

WinUiaNode* uia_get_or_create_node(WinUiaSlot* slot, uint32_t elem_id, bool is_root) noexcept;

HRESULT uia_qi_node(WinUiaNode* n, REFIID riid, void** ppv) noexcept {
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;
    if (uia_is_detached(n)) {
        return kUiaElemNotAvail;
    }
    if (uia_guid_eq(riid, kIidIUnknown) || uia_guid_eq(riid, kIidUiaSimple)) {
        *ppv = &n->simple;
        uia_addref(n);
        return S_OK;
    }
    if (uia_guid_eq(riid, kIidUiaFragment)) {
        *ppv = &n->fragment;
        uia_addref(n);
        return S_OK;
    }
    if (uia_guid_eq(riid, kIidUiaFragmentRoot)) {
        if (!n->is_root) {
            return E_NOINTERFACE;
        }
        *ppv = &n->fragment_root;
        uia_addref(n);
        return S_OK;
    }
    const bool wants_invoke = uia_guid_eq(riid, kIidUiaInvoke);
    const bool wants_value = !wants_invoke && uia_guid_eq(riid, kIidUiaValue);
    const bool wants_range = !wants_invoke && !wants_value && uia_guid_eq(riid, kIidUiaRangeValue);
    const bool wants_toggle = !wants_invoke && !wants_value && !wants_range && uia_guid_eq(riid, kIidUiaToggle);
    const bool wants_expand = !wants_invoke && !wants_value && !wants_range && !wants_toggle && uia_guid_eq(riid, kIidUiaExpandCollapse);
    const bool wants_select = !wants_invoke && !wants_value && !wants_range && !wants_toggle && !wants_expand && uia_guid_eq(riid, kIidUiaSelectionItem);
    const bool wants_text = !wants_invoke && !wants_value && !wants_range && !wants_toggle && !wants_expand && !wants_select && uia_guid_eq(riid, kIidUiaText);
    const bool wants_scroll = !wants_invoke && !wants_value && !wants_range && !wants_toggle && !wants_expand && !wants_select && !wants_text && uia_guid_eq(riid, kIidUiaScroll);
    const bool wants_grid = !wants_invoke && !wants_value && !wants_range && !wants_toggle && !wants_expand && !wants_select && !wants_text && !wants_scroll && uia_guid_eq(riid, kIidUiaGrid);
    const bool wants_grid_item = !wants_invoke && !wants_value && !wants_range && !wants_toggle && !wants_expand && !wants_select && !wants_text && !wants_scroll && !wants_grid && uia_guid_eq(riid, kIidUiaGridItem);
    const bool wants_table = !wants_invoke && !wants_value && !wants_range && !wants_toggle && !wants_expand && !wants_select && !wants_text && !wants_scroll && !wants_grid && !wants_grid_item && uia_guid_eq(riid, kIidUiaTable);
    const bool wants_table_item = !wants_invoke && !wants_value && !wants_range && !wants_toggle && !wants_expand && !wants_select && !wants_text && !wants_scroll && !wants_grid && !wants_grid_item && !wants_table && uia_guid_eq(riid, kIidUiaTableItem);
    if (!wants_invoke && !wants_value && !wants_range && !wants_toggle && !wants_expand && !wants_select &&
        !wants_text && !wants_scroll && !wants_grid && !wants_grid_item && !wants_table && !wants_table_item)
    {
        return E_NOINTERFACE;
    }
    void* iface = nullptr;
    const HRESULT hr = uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        const A11yNode* an = uia_live_node(n, &idx);
        if (!an) {
            return E_NOINTERFACE;
        }
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        if (wants_invoke && uia_supports_invoke(an)) {
            iface = &n->invoke;
        } else if (wants_value && uia_supports_value(an)) {
            iface = &n->value;
        } else if (wants_range && uia_supports_range_value(an)) {
            iface = &n->range_value;
        } else if (wants_toggle && uia_supports_toggle(an)) {
            iface = &n->toggle;
        } else if (wants_expand && uia_supports_expand_collapse(an)) {
            iface = &n->expand_collapse;
        } else if (wants_select && uia_supports_selection_item(an)) {
            iface = &n->selection_item;
        } else if (wants_text && uia_supports_text(n, an)) {
            iface = &n->text;
        } else if (wants_scroll && uia_supports_scroll(an)) {
            iface = &n->scroll;
        } else if (wants_grid && uia_supports_grid(an)) {
            iface = &n->grid;
        } else if (wants_grid_item && uia_supports_grid_item(span, idx)) {
            iface = &n->grid_item;
        } else if (wants_table && uia_supports_grid(an)) {
            iface = &n->table;
        } else if (wants_table_item && uia_supports_grid_item(span, idx)) {
            iface = &n->table_item;
        }
        if (!iface) {
            return E_NOINTERFACE;
        }
        uia_addref(n);
        return S_OK;
    });
    if (SUCCEEDED(hr)) {
        *ppv = iface;
    }
    return hr;
}

template <typename T>
HRESULT STDMETHODCALLTYPE uia_com_qi(T* This, REFIID riid, void** ppv) noexcept {
    return uia_qi_node(uia_owner(This), riid, ppv);
}

template <typename T>
ULONG STDMETHODCALLTYPE uia_com_addref(T* This) noexcept {
    return uia_addref(uia_owner(This));
}

template <typename T>
ULONG STDMETHODCALLTYPE uia_com_release(T* This) noexcept {
    return uia_release(uia_owner(This));
}

BSTR uia_alloc_bstr_utf8(Str s) noexcept {
    if (!s.chars || s.length == 0u) {
        return SysAllocStringLen(L"", 0u);
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0u, s.chars, static_cast<int>(s.length), nullptr, 0);
    if (needed <= 0) {
        return SysAllocStringLen(L"", 0u);
    }
    wchar_t stack_buf[512];
    wchar_t* wbuf = (needed <= static_cast<int>(ETCH_COUNTOF(stack_buf)))
        ? stack_buf
        : static_cast<wchar_t*>(std::malloc(static_cast<size_t>(needed) * sizeof(wchar_t)));
    if (!wbuf) {
        return nullptr;
    }
    MultiByteToWideChar(CP_UTF8, 0u, s.chars, static_cast<int>(s.length), wbuf, needed);
    BSTR out = SysAllocStringLen(wbuf, static_cast<UINT>(needed));
    if (wbuf != stack_buf) {
        std::free(wbuf);
    }
    return out;
}

long uia_control_type(A11yRole role) noexcept {
    switch (role) {
        case A11yRole::Window:      return UIA_WindowControlTypeId;
        case A11yRole::Dialog:      return UIA_WindowControlTypeId;
        case A11yRole::Group:
        case A11yRole::None:        return UIA_GroupControlTypeId;
        case A11yRole::Region:
        case A11yRole::ScrollArea:
        case A11yRole::Canvas:      return UIA_PaneControlTypeId;
        case A11yRole::Toolbar:     return UIA_ToolBarControlTypeId;
        case A11yRole::MenuBar:     return UIA_MenuBarControlTypeId;
        case A11yRole::Menu:        return UIA_MenuControlTypeId;
        case A11yRole::MenuItem:    return UIA_MenuItemControlTypeId;
        case A11yRole::TabList:     return UIA_TabControlTypeId;
        case A11yRole::Tab:         return UIA_TabItemControlTypeId;
        case A11yRole::List:        return UIA_ListControlTypeId;
        case A11yRole::ListItem:    return UIA_ListItemControlTypeId;
        case A11yRole::Table:       return UIA_TableControlTypeId;
        case A11yRole::Tree:        return UIA_TreeControlTypeId;
        case A11yRole::TreeItem:    return UIA_TreeItemControlTypeId;
        case A11yRole::StaticText:  return UIA_TextControlTypeId;
        case A11yRole::Button:      return UIA_ButtonControlTypeId;
        case A11yRole::Checkbox:
        case A11yRole::Switch:      return UIA_CheckBoxControlTypeId;
        case A11yRole::Slider:      return UIA_SliderControlTypeId;
        case A11yRole::SpinButton:  return UIA_SpinnerControlTypeId;
        case A11yRole::ProgressBar: return UIA_ProgressBarControlTypeId;
        case A11yRole::ComboBox:    return UIA_ComboBoxControlTypeId;
        case A11yRole::TextInput:   return UIA_EditControlTypeId;
        case A11yRole::CodeEditor:  return UIA_DocumentControlTypeId;
        case A11yRole::Tooltip:     return UIA_ToolTipControlTypeId;
        default:                    return UIA_CustomControlTypeId;
    }
}

void uia_compute_client_scale(WinUiaSlot* slot, double* out_sx, double* out_sy) noexcept {
    RECT cr = {};
    GetClientRect(slot->hwnd, &cr);
    const WindowUiState* ui = uia_window_ui(slot);
    const float dpi = (ui && ui->dpi_scale > 0.0f) ? ui->dpi_scale : 1.0f;
    const double cw = static_cast<double>(cr.right - cr.left);
    const double ch = static_cast<double>(cr.bottom - cr.top);
    const double lw = (ui && ui->viewport_w > 0u) ? (static_cast<double>(ui->viewport_w) / dpi) : cw;
    const double lh = (ui && ui->viewport_h > 0u) ? (static_cast<double>(ui->viewport_h) / dpi) : ch;
    *out_sx = (cw > 0.0 && lw > 0.0) ? (cw / lw) : static_cast<double>(dpi);
    *out_sy = (ch > 0.0 && lh > 0.0) ? (ch / lh) : static_cast<double>(dpi);
}

bool uia_value_is_readonly(const A11yNode* an) noexcept {
    return (an->state_flags & A11yState_ReadOnly) != 0u ||
           (an->actions_mask & a11y_action_bit(A11yAction::SetValue)) == 0u;
}

double uia_range_step(const A11yNode* an, double scale, double fallback) noexcept {
    const double span = static_cast<double>(an->max_value - an->min_value);
    return (span > 0.0) ? (span * scale) : fallback;
}

HRESULT STDMETHODCALLTYPE uia_get_provider_options(IRawElementProviderSimple* This, enum ProviderOptions* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    if (uia_is_detached(n)) return kUiaElemNotAvail;
    *pRetVal = static_cast<enum ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_ProviderOwnsSetFocus);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_get_pattern_provider(IRawElementProviderSimple* This, PATTERNID patternId, IUnknown** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        const A11yNode* an = uia_live_node(n, &idx);
        if (!an) return (uia_is_detached(n) || !n->is_root) ? kUiaElemNotAvail : S_OK;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        void* iface = nullptr;
        if (patternId == UIA_InvokePatternId && uia_supports_invoke(an)) {
            iface = &n->invoke;
        } else if (patternId == UIA_TogglePatternId && uia_supports_toggle(an)) {
            iface = &n->toggle;
        } else if (patternId == UIA_RangeValuePatternId && uia_supports_range_value(an)) {
            iface = &n->range_value;
        } else if (patternId == UIA_ValuePatternId && uia_supports_value(an)) {
            iface = &n->value;
        } else if (patternId == UIA_ExpandCollapsePatternId && uia_supports_expand_collapse(an)) {
            iface = &n->expand_collapse;
        } else if (patternId == UIA_SelectionItemPatternId && uia_supports_selection_item(an)) {
            iface = &n->selection_item;
        } else if (patternId == UIA_TextPatternId && uia_supports_text(n, an)) {
            iface = &n->text;
        } else if (patternId == UIA_ScrollPatternId && uia_supports_scroll(an)) {
            iface = &n->scroll;
        } else if (patternId == UIA_GridPatternId && uia_supports_grid(an)) {
            iface = &n->grid;
        } else if (patternId == UIA_GridItemPatternId && uia_supports_grid_item(span, idx)) {
            iface = &n->grid_item;
        } else if (patternId == UIA_TablePatternId && uia_supports_grid(an)) {
            iface = &n->table;
        } else if (patternId == UIA_TableItemPatternId && uia_supports_grid_item(span, idx)) {
            iface = &n->table_item;
        }
        if (iface) {
            *pRetVal = reinterpret_cast<IUnknown*>(iface);
            uia_addref(n);
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_get_property_value(IRawElementProviderSimple* This, PROPERTYID propertyId, VARIANT* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    VariantInit(pRetVal);
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        const A11yNode* an = uia_live_node(n, &idx);
        if (!an) return (uia_is_detached(n) || !n->is_root) ? kUiaElemNotAvail : S_OK;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);

        switch (propertyId) {
            case UIA_ControlTypePropertyId:
                pRetVal->vt = VT_I4;
                if (an->role == A11yRole::Group && (uia_is_table_row(span, idx) || uia_cell_coords(span, idx))) {
                    pRetVal->lVal = UIA_DataItemControlTypeId;
                } else {
                    pRetVal->lVal = uia_control_type(an->role);
                }
                break;
            case UIA_NamePropertyId:
                if (an->label.length > 0u) {
                    pRetVal->vt = VT_BSTR;
                    pRetVal->bstrVal = uia_alloc_bstr_utf8(an->label);
                }
                break;
            case UIA_AutomationIdPropertyId: {
                char buf[24] = {};
                std::snprintf(buf, sizeof(buf), "etch_%08X", an->id);
                pRetVal->vt = VT_BSTR;
                pRetVal->bstrVal = uia_alloc_bstr_utf8(str_cstr(buf));
                break;
            }
            case UIA_FrameworkIdPropertyId:
                pRetVal->vt = VT_BSTR;
                pRetVal->bstrVal = SysAllocString(L"Etch");
                break;
            case UIA_IsEnabledPropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = (an->state_flags & A11yState_Disabled) ? VARIANT_FALSE : VARIANT_TRUE;
                break;
            case UIA_HasKeyboardFocusPropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = (an->state_flags & A11yState_Focused) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsKeyboardFocusablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = (!(an->state_flags & A11yState_Disabled) &&
                    ((an->actions_mask & a11y_action_bit(A11yAction::Focus)) != 0u || n->is_root))
                    ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsPasswordPropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = (an->state_flags & A11yState_Password) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsControlElementPropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_is_control_element(span, idx) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsContentElementPropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_is_control_element(span, idx) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_LiveSettingPropertyId:
                pRetVal->vt = VT_I4;
                if (an->role == A11yRole::Dialog && (an->state_flags & A11yState_Modal) != 0u) {
                    pRetVal->lVal = Assertive;
                } else if (uia_is_live_role(an->role)) {
                    pRetVal->lVal = Polite;
                } else {
                    pRetVal->lVal = Off;
                }
                break;
            case UIA_IsInvokePatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_invoke(an) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsTogglePatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_toggle(an) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsRangeValuePatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_range_value(an) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsValuePatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_value(an) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsExpandCollapsePatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_expand_collapse(an) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsSelectionItemPatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_selection_item(an) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsTextPatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_text(n, an) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsScrollPatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_scroll(an) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsGridPatternAvailablePropertyId:
            case UIA_IsTablePatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_grid(an) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_IsGridItemPatternAvailablePropertyId:
            case UIA_IsTableItemPatternAvailablePropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = uia_supports_grid_item(span, idx) ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case UIA_ToggleToggleStatePropertyId:
                if (uia_supports_toggle(an)) {
                    pRetVal->vt = VT_I4;
                    pRetVal->lVal = (an->state_flags & A11yState_Checked) ? ToggleState_On : ToggleState_Off;
                }
                break;
            case UIA_RangeValueValuePropertyId:
                if (uia_supports_range_value(an)) {
                    pRetVal->vt = VT_R8;
                    pRetVal->dblVal = static_cast<double>(an->value_num);
                }
                break;
            case UIA_RangeValueMinimumPropertyId:
                if (uia_supports_range_value(an)) {
                    pRetVal->vt = VT_R8;
                    pRetVal->dblVal = static_cast<double>(an->min_value);
                }
                break;
            case UIA_RangeValueMaximumPropertyId:
                if (uia_supports_range_value(an)) {
                    pRetVal->vt = VT_R8;
                    pRetVal->dblVal = static_cast<double>(an->max_value);
                }
                break;
            case UIA_RangeValueLargeChangePropertyId:
                if (uia_supports_range_value(an)) {
                    pRetVal->vt = VT_R8;
                    pRetVal->dblVal = uia_range_step(an, 0.1, 10.0);
                }
                break;
            case UIA_RangeValueSmallChangePropertyId:
                if (uia_supports_range_value(an)) {
                    pRetVal->vt = VT_R8;
                    pRetVal->dblVal = uia_range_step(an, 0.01, 1.0);
                }
                break;
            case UIA_RangeValueIsReadOnlyPropertyId:
                if (uia_supports_range_value(an)) {
                    pRetVal->vt = VT_BOOL;
                    pRetVal->boolVal = uia_value_is_readonly(an) ? VARIANT_TRUE : VARIANT_FALSE;
                }
                break;
            case UIA_ValueValuePropertyId:
                if (uia_supports_value(an)) {
                    pRetVal->vt = VT_BSTR;
                    pRetVal->bstrVal = uia_alloc_bstr_utf8(an->value_text);
                }
                break;
            case UIA_ValueIsReadOnlyPropertyId:
                if (uia_supports_value(an)) {
                    pRetVal->vt = VT_BOOL;
                    pRetVal->boolVal = uia_value_is_readonly(an) ? VARIANT_TRUE : VARIANT_FALSE;
                }
                break;
            case UIA_ExpandCollapseExpandCollapseStatePropertyId:
                if (uia_supports_expand_collapse(an)) {
                    pRetVal->vt = VT_I4;
                    pRetVal->lVal = (an->state_flags & A11yState_Expanded) ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
                }
                break;
            case UIA_SelectionItemIsSelectedPropertyId:
                if (uia_supports_selection_item(an)) {
                    pRetVal->vt = VT_BOOL;
                    pRetVal->boolVal = (an->state_flags & A11yState_Selected) ? VARIANT_TRUE : VARIANT_FALSE;
                }
                break;
            case UIA_ScrollHorizontalScrollPercentPropertyId:
            case UIA_ScrollVerticalScrollPercentPropertyId:
            case UIA_ScrollHorizontalViewSizePropertyId:
            case UIA_ScrollVerticalViewSizePropertyId:
            case UIA_ScrollHorizontallyScrollablePropertyId:
            case UIA_ScrollVerticallyScrollablePropertyId:
                if (uia_supports_scroll(an)) {
                    UiaScrollGeom g = {};
                    uia_read_scroll_geom(n, an, &g);
                    if (propertyId == UIA_ScrollHorizontalScrollPercentPropertyId) {
                        pRetVal->vt = VT_R8;
                        pRetVal->dblVal = uia_scroll_percent(g.h_scrollable, g.h_pos, g.h_max);
                    } else if (propertyId == UIA_ScrollVerticalScrollPercentPropertyId) {
                        pRetVal->vt = VT_R8;
                        pRetVal->dblVal = uia_scroll_percent(g.v_scrollable, g.v_pos, g.v_max);
                    } else if (propertyId == UIA_ScrollHorizontalViewSizePropertyId) {
                        pRetVal->vt = VT_R8;
                        pRetVal->dblVal = uia_scroll_view_size(g.h_scrollable, g.h_view, g.h_content);
                    } else if (propertyId == UIA_ScrollVerticalViewSizePropertyId) {
                        pRetVal->vt = VT_R8;
                        pRetVal->dblVal = uia_scroll_view_size(g.v_scrollable, g.v_view, g.v_content);
                    } else if (propertyId == UIA_ScrollHorizontallyScrollablePropertyId) {
                        pRetVal->vt = VT_BOOL;
                        pRetVal->boolVal = g.h_scrollable ? VARIANT_TRUE : VARIANT_FALSE;
                    } else {
                        pRetVal->vt = VT_BOOL;
                        pRetVal->boolVal = g.v_scrollable ? VARIANT_TRUE : VARIANT_FALSE;
                    }
                }
                break;
            case UIA_GridRowCountPropertyId:
            case UIA_GridColumnCountPropertyId:
                if (uia_supports_grid(an)) {
                    uint32_t rows = 0u, cols = 0u;
                    uia_table_dims(n->slot, span, idx, &rows, &cols);
                    pRetVal->vt = VT_I4;
                    pRetVal->lVal = static_cast<long>(propertyId == UIA_GridRowCountPropertyId ? rows : cols);
                }
                break;
            case UIA_TableRowOrColumnMajorPropertyId:
                if (uia_supports_grid(an)) {
                    pRetVal->vt = VT_I4;
                    pRetVal->lVal = RowOrColumnMajor_RowMajor;
                }
                break;
            case UIA_GridItemRowPropertyId:
            case UIA_GridItemColumnPropertyId:
            case UIA_GridItemRowSpanPropertyId:
            case UIA_GridItemColumnSpanPropertyId:
            case UIA_GridItemContainingGridPropertyId: {
                uint32_t tbl_idx = UINT32_MAX, row_ord = 0u, col_ord = 0u;
                if (uia_cell_coords(span, idx, &tbl_idx, &row_ord, &col_ord)) {
                    if (propertyId == UIA_GridItemRowPropertyId) {
                        pRetVal->vt = VT_I4;
                        pRetVal->lVal = static_cast<long>(row_ord);
                    } else if (propertyId == UIA_GridItemColumnPropertyId) {
                        pRetVal->vt = VT_I4;
                        pRetVal->lVal = static_cast<long>(col_ord);
                    } else if (propertyId == UIA_GridItemRowSpanPropertyId || propertyId == UIA_GridItemColumnSpanPropertyId) {
                        pRetVal->vt = VT_I4;
                        pRetVal->lVal = 1;
                    } else if (tbl_idx < span.size()) {
                        if (WinUiaNode* tbl = uia_get_or_create_node(n->slot, span[tbl_idx].id, false)) {
                            pRetVal->vt = VT_UNKNOWN;
                            pRetVal->punkVal = reinterpret_cast<IUnknown*>(&tbl->simple);
                            uia_addref(tbl);
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_get_host_provider(IRawElementProviderSimple* This, IRawElementProviderSimple** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    if (uia_is_detached(n)) return kUiaElemNotAvail;
    if (!n->is_root) return S_OK;
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        if (!n->slot) return kUiaElemNotAvail;
        return ensure_win_uia_api() ? g_win_uia.host_from_hwnd(n->slot->hwnd, pRetVal) : E_FAIL;
    });
}

HRESULT STDMETHODCALLTYPE uia_navigate(IRawElementProviderFragment* This, enum NavigateDirection direction, IRawElementProviderFragment** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        const A11yNode* an = uia_live_node(n, &idx);
        if (!an) return (uia_is_detached(n) || !n->is_root) ? kUiaElemNotAvail : S_OK;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        const uint32_t count = static_cast<uint32_t>(span.size());

        uint32_t target_idx = 0xFFFFFFFFu;
        switch (direction) {
            case NavigateDirection_Parent:
                if (!n->is_root && an->parent_id != 0u) {
                    for (uint32_t i = 0u; i < count; ++i) {
                        if (span[i].id == an->parent_id) { target_idx = i; break; }
                    }
                }
                break;
            case NavigateDirection_FirstChild:
                if (an->first_child_idx < count) {
                    target_idx = an->first_child_idx;
                }
                break;
            case NavigateDirection_LastChild: {
                uint32_t c = an->first_child_idx;
                uint32_t steps = 0u;
                while (c < count && steps++ < count) {
                    target_idx = c;
                    c = span[c].next_sibling_idx;
                }
                break;
            }
            case NavigateDirection_NextSibling:
                if (!n->is_root && an->next_sibling_idx < count) {
                    target_idx = an->next_sibling_idx;
                }
                break;
            case NavigateDirection_PreviousSibling:
                if (!n->is_root) {
                    for (uint32_t i = 0u; i < count; ++i) {
                        if (span[i].next_sibling_idx == idx) { target_idx = i; break; }
                    }
                }
                break;
            default:
                break;
        }
        if (target_idx < count) {
            WinUiaNode* dst = uia_get_or_create_node(n->slot, span[target_idx].id, target_idx == 0u);
            if (dst) {
                *pRetVal = reinterpret_cast<IRawElementProviderFragment*>(&dst->fragment);
                uia_addref(dst);
            }
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_get_runtime_id(IRawElementProviderFragment* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    if (uia_is_detached(n)) return kUiaElemNotAvail;
    if (n->is_root) return S_OK;
    SAFEARRAY* arr = SafeArrayCreateVector(VT_I4, 0, 2u);
    if (!arr) return E_OUTOFMEMORY;
    int rids[2] = { UiaAppendRuntimeId, static_cast<int>(n->element_id) };
    LONG i0 = 0, i1 = 1;
    SafeArrayPutElement(arr, &i0, &rids[0]);
    SafeArrayPutElement(arr, &i1, &rids[1]);
    *pRetVal = arr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_get_bounding_rect(IRawElementProviderFragment* This, struct UiaRect* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = {};
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return (uia_is_detached(n) || !n->is_root) ? kUiaElemNotAvail : S_OK;
        double sx = 1.0, sy = 1.0;
        uia_compute_client_scale(n->slot, &sx, &sy);
        POINT tl = {0, 0};
        ClientToScreen(n->slot->hwnd, &tl);
        pRetVal->left = static_cast<double>(tl.x) + static_cast<double>(an->bounds.x) * sx;
        pRetVal->top = static_cast<double>(tl.y) + static_cast<double>(an->bounds.y) * sy;
        pRetVal->width = static_cast<double>(an->bounds.w) * sx;
        pRetVal->height = static_cast<double>(an->bounds.h) * sy;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_get_embedded_roots(IRawElementProviderFragment* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_is_detached(n) ? kUiaElemNotAvail : S_OK;
}

HRESULT STDMETHODCALLTYPE uia_set_focus(IRawElementProviderFragment* This) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        if (n->is_root) {
            if (IsWindowVisible(n->hwnd) && GetFocus() != n->hwnd) {
                SetFocus(n->hwnd);
            }
            return S_OK;
        }
        if (!perform_a11y_action(an->id, A11yAction::Focus, 0.0f, {}, n->slot->ctx, n->slot->window)) {
            return E_FAIL;
        }
        if (IsWindowVisible(n->hwnd) && GetFocus() != n->hwnd) {
            SetFocus(n->hwnd);
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_get_fragment_root(IRawElementProviderFragment* This, IRawElementProviderFragmentRoot** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        if (uia_is_detached(n) || !n->slot || !n->slot->root) return kUiaElemNotAvail;
        *pRetVal = reinterpret_cast<IRawElementProviderFragmentRoot*>(&n->slot->root->fragment_root);
        uia_addref(n->slot->root);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_elem_from_point(IRawElementProviderFragmentRoot* This, double x, double y, IRawElementProviderFragment** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* root = uia_owner(This);
    return uia_run_on_owner(root, [&]() noexcept -> HRESULT {
        if (uia_is_detached(root) || !root->slot) return kUiaElemNotAvail;
        double sx = 1.0, sy = 1.0;
        uia_compute_client_scale(root->slot, &sx, &sy);
        POINT tl = {0, 0};
        ClientToScreen(root->slot->hwnd, &tl);
        const float lx = static_cast<float>((x - static_cast<double>(tl.x)) / sx);
        const float ly = static_cast<float>((y - static_cast<double>(tl.y)) / sy);
        const Vec2 pt = {lx, ly};
        const auto span = frame_a11y_nodes(root->slot->ctx, root->slot->window);
        if (span.empty()) return S_OK;

        uint32_t hit_idx = 0u;
        bool matched = false;
        bool occluded_by_hitbox = false;
        if (const WindowUiState* ui = uia_window_ui(root->slot); ui && ui->hitboxes.count > 0u) {
            PodBuf<uint32_t> hit_ids = {};
            uint32_t topmost_id = 0u;
            uint32_t hovered_span_id = 0u;
            hitbox_run_hit_test(ui->hitboxes.data, ui->hitboxes.count, pt, true, &hit_ids, &topmost_id, &hovered_span_id);
            if (hovered_span_id != 0u) {
                const uint32_t si = uia_find_index_by_id(span, hovered_span_id);
                if (si < span.size()) {
                    hit_idx = si;
                    matched = true;
                }
            }
            if (!matched && hit_ids.count > 0u) {
                for (uint32_t k = hit_ids.count; k > 0u; --k) {
                    const uint32_t si = uia_find_index_by_id(span, hit_ids.data[k - 1u]);
                    if (si < span.size() && uia_is_control_element(span, si)) {
                        hit_idx = si;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    for (uint32_t k = hit_ids.count; k > 0u; --k) {
                        const uint32_t si = uia_find_index_by_id(span, hit_ids.data[k - 1u]);
                        if (si < span.size()) {
                            hit_idx = si;
                            matched = true;
                            break;
                        }
                    }
                }
            }
            hit_ids.destroy();
            if (!matched) {
                for (uint32_t i = ui->hitboxes.count; i > 0u; --i) {
                    const HitboxEntry& h = ui->hitboxes.data[i - 1u];
                    if (h.behavior == HitboxBehavior::Passthrough) continue;
                    if (!hitbox_rounded_contains(h.clip_rect, h.clip_radius, pt)) continue;
                    if (!h.visual_rect.contains(pt)) continue;
                    if (h.behavior == HitboxBehavior::BlockMouse || h.is_floating_root || h.id != 0u || h.is_opaque_surface) {
                        occluded_by_hitbox = true;
                        if (h.layout_elem_id != 0u) {
                            const uint32_t si = uia_find_index_by_id(span, h.layout_elem_id);
                            if (si < span.size()) {
                                hit_idx = si;
                                matched = true;
                            }
                        }
                        break;
                    }
                }
            }
        }
        if (!matched && !occluded_by_hitbox) {
            for (uint32_t i = 0u; i < span.size(); ++i) {
                if (span[i].bounds.w > 0.0f && span[i].bounds.h > 0.0f && span[i].bounds.contains(pt)) {
                    hit_idx = i;
                }
            }
        }
        WinUiaNode* dst = uia_get_or_create_node(root->slot, span[hit_idx].id, hit_idx == 0u);
        if (dst) {
            *pRetVal = reinterpret_cast<IRawElementProviderFragment*>(&dst->fragment);
            uia_addref(dst);
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_get_focus(IRawElementProviderFragmentRoot* This, IRawElementProviderFragment** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* root = uia_owner(This);
    return uia_run_on_owner(root, [&]() noexcept -> HRESULT {
        if (uia_is_detached(root) || !root->slot) return kUiaElemNotAvail;
        const WindowUiState* ui = root->slot->claimed ? &root->slot->claimed->ui : window_ui(root->slot->ctx, root->slot->window);
        if (!ui || ui->focus_id == 0u) return S_OK;
        const auto span = frame_a11y_nodes(root->slot->ctx, root->slot->window);
        for (uint32_t i = 0u; i < span.size(); ++i) {
            if (span[i].id == ui->focus_id) {
                WinUiaNode* dst = uia_get_or_create_node(root->slot, span[i].id, i == 0u);
                if (dst) {
                    *pRetVal = reinterpret_cast<IRawElementProviderFragment*>(&dst->fragment);
                    uia_addref(dst);
                }
                break;
            }
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_invoke(IInvokeProvider* This) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        return perform_a11y_action(an->id, A11yAction::Press, 0.0f, {}, n->slot->ctx, n->slot->window) ? S_OK : E_FAIL;
    });
}

HRESULT STDMETHODCALLTYPE uia_value_set(IValueProvider* This, LPCWSTR val) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        if ((an->state_flags & (A11yState_Disabled | A11yState_ReadOnly)) != 0u ||
            (an->actions_mask & a11y_action_bit(A11yAction::SetValue)) == 0u)
        {
            return E_ACCESSDENIED;
        }
        char utf8[1024] = {};
        int len = 0;
        if (val && val[0] != L'\0') {
            len = WideCharToMultiByte(CP_UTF8, 0u, val, -1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
            if (len <= 0) {
                return (GetLastError() == ERROR_INSUFFICIENT_BUFFER) ? E_INVALIDARG : E_FAIL;
            }
            --len;
        }
        return perform_a11y_action(an->id, A11yAction::SetValue, 0.0f, Str{utf8, static_cast<uint32_t>(len)}, n->slot->ctx, n->slot->window)
            ? S_OK : E_FAIL;
    });
}

HRESULT STDMETHODCALLTYPE uia_value_get(IValueProvider* This, BSTR* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = uia_alloc_bstr_utf8(an->value_text);
        return (*pRetVal) ? S_OK : E_OUTOFMEMORY;
    });
}

HRESULT STDMETHODCALLTYPE uia_value_readonly(IValueProvider* This, BOOL* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = uia_value_is_readonly(an) ? TRUE : FALSE;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_range_set(IRangeValueProvider* This, double val) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        if (!std::isfinite(val)) return E_INVALIDARG;
        if ((an->state_flags & (A11yState_Disabled | A11yState_ReadOnly)) != 0u ||
            (an->actions_mask & a11y_action_bit(A11yAction::SetValue)) == 0u)
        {
            return E_ACCESSDENIED;
        }
        return perform_a11y_action(an->id, A11yAction::SetValue, static_cast<float>(val), {}, n->slot->ctx, n->slot->window)
            ? S_OK : E_FAIL;
    });
}

HRESULT STDMETHODCALLTYPE uia_range_get(IRangeValueProvider* This, double* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = static_cast<double>(an->value_num);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_range_readonly(IRangeValueProvider* This, BOOL* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = uia_value_is_readonly(an) ? TRUE : FALSE;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_range_max(IRangeValueProvider* This, double* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = static_cast<double>(an->max_value);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_range_min(IRangeValueProvider* This, double* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = static_cast<double>(an->min_value);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_range_large(IRangeValueProvider* This, double* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = uia_range_step(an, 0.1, 10.0);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_range_small(IRangeValueProvider* This, double* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = uia_range_step(an, 0.01, 1.0);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_toggle(IToggleProvider* This) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        return perform_a11y_action(an->id, A11yAction::Press, 0.0f, {}, n->slot->ctx, n->slot->window) ? S_OK : E_FAIL;
    });
}

HRESULT STDMETHODCALLTYPE uia_toggle_state(IToggleProvider* This, enum ToggleState* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = (an->state_flags & A11yState_Checked) ? ToggleState_On : ToggleState_Off;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_expand(IExpandCollapseProvider* This) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        return perform_a11y_action(an->id, A11yAction::Expand, 0.0f, {}, n->slot->ctx, n->slot->window) ? S_OK : E_FAIL;
    });
}

HRESULT STDMETHODCALLTYPE uia_collapse(IExpandCollapseProvider* This) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        return perform_a11y_action(an->id, A11yAction::Collapse, 0.0f, {}, n->slot->ctx, n->slot->window) ? S_OK : E_FAIL;
    });
}

HRESULT STDMETHODCALLTYPE uia_expand_state(IExpandCollapseProvider* This, enum ExpandCollapseState* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = (an->state_flags & A11yState_Expanded) ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_select_item(ISelectionItemProvider* This) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        return perform_a11y_action(an->id, A11yAction::Press, 0.0f, {}, n->slot->ctx, n->slot->window) ? S_OK : E_FAIL;
    });
}

HRESULT STDMETHODCALLTYPE uia_select_add(ISelectionItemProvider* This) noexcept {
    return uia_select_item(This);
}

HRESULT STDMETHODCALLTYPE uia_select_remove(ISelectionItemProvider* This) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_is_detached(n) ? kUiaElemNotAvail : E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE uia_select_is_selected(ISelectionItemProvider* This, BOOL* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        *pRetVal = (an->state_flags & A11yState_Selected) ? TRUE : FALSE;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_select_container(ISelectionItemProvider* This, IRawElementProviderSimple** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_is_detached(n) ? kUiaElemNotAvail : S_OK;
}

// ============================================================================
// ITextProvider + ITextRangeProvider
// ============================================================================

struct UiaTextSnapshot {
    const char* utf8 = nullptr;
    uint32_t utf8_len = 0u;
    char* owned_heap = nullptr;
    char stack_buf[512] = {};
    uint32_t sel_start_u16 = 0u;
    uint32_t sel_end_u16 = 0u;
    uint32_t total_u16 = 0u;
    bool is_password = false;
    bool is_code_editor = false;
    bool is_read_only = false;
};

void uia_text_snapshot_free(UiaTextSnapshot* s) noexcept {
    if (s && s->owned_heap) {
        std::free(s->owned_heap);
        s->owned_heap = nullptr;
    }
}

uint32_t uia_utf8_byte_to_utf16(const char* s, uint32_t len, uint32_t byte_off) noexcept {
    if (!s || len == 0u || byte_off == 0u) return 0u;
    if (byte_off > len) byte_off = len;
    const char* p = s;
    const char* limit = s + byte_off;
    const char* end = s + len;
    uint32_t u16 = 0u;
    while (p < limit) {
        const uint32_t cp = next_utf8_codepoint(p, end);
        u16 += (cp > 0xFFFFu) ? 2u : 1u;
    }
    return u16;
}

uint32_t uia_utf16_to_utf8_byte(const char* s, uint32_t len, uint32_t u16_off) noexcept {
    if (!s || len == 0u || u16_off == 0u) return 0u;
    const char* p = s;
    const char* end = s + len;
    uint32_t u16 = 0u;
    while (p < end && u16 < u16_off) {
        const char* prev = p;
        const uint32_t cp = next_utf8_codepoint(p, end);
        const uint32_t units = (cp > 0xFFFFu) ? 2u : 1u;
        if (u16 + units > u16_off) {
            return static_cast<uint32_t>(prev - s);
        }
        u16 += units;
    }
    return static_cast<uint32_t>(p - s);
}

uint32_t uia_utf8_codepoint_index(const char* s, uint32_t len, uint32_t byte_off) noexcept {
    if (!s || len == 0u || byte_off == 0u) return 0u;
    if (byte_off > len) byte_off = len;
    const char* p = s;
    const char* limit = s + byte_off;
    const char* end = s + len;
    uint32_t idx = 0u;
    while (p < limit) {
        (void)next_utf8_codepoint(p, end);
        ++idx;
    }
    return idx;
}

uint32_t uia_utf8_byte_at_codepoint(const char* s, uint32_t len, uint32_t cp_idx) noexcept {
    if (!s || len == 0u || cp_idx == 0u) return 0u;
    const char* p = s;
    const char* end = s + len;
    uint32_t cur = 0u;
    while (p < end && cur < cp_idx) {
        (void)next_utf8_codepoint(p, end);
        ++cur;
    }
    return static_cast<uint32_t>(p - s);
}

void uia_line_bounds_at(const char* s, uint32_t len, uint32_t byte_off, uint32_t* out_a, uint32_t* out_b) noexcept {
    if (!s || len == 0u) {
        *out_a = 0u;
        *out_b = 0u;
        return;
    }
    byte_off = utf8_clamp_boundary(s, len, byte_off);
    uint32_t a = byte_off;
    while (a > 0u && s[a - 1u] != '\n') --a;
    uint32_t b = byte_off;
    while (b < len && s[b] != '\n') ++b;
    *out_a = a;
    *out_b = b;
}

bool uia_capture_text(WinUiaNode* n, const A11yNode* an, UiaTextSnapshot* out) noexcept {
    *out = {};
    if (!n || !n->slot || !an) return false;
    WindowUiState* ui = uia_window_ui(n->slot);
    const WidgetState* ws = ui ? state_map_find(&ui->states, an->id) : nullptr;
    out->is_password = (an->state_flags & A11yState_Password) != 0u;
    out->is_code_editor = (an->role == A11yRole::CodeEditor);
    out->is_read_only = uia_value_is_readonly(an) || an->role == A11yRole::StaticText;

    const WidgetCold* c = ws ? ws->cold : nullptr;
    if (an->role == A11yRole::CodeEditor && c && c->edit_text_buffer && c->sel_text_epoch == n->slot->ctx->arena_epoch) {
        const uint32_t total = text_buffer_byte_length(c->edit_text_buffer);
        if (total > 0u) {
            char* dst = nullptr;
            if (total < sizeof(out->stack_buf)) {
                dst = out->stack_buf;
            } else {
                out->owned_heap = static_cast<char*>(std::malloc(static_cast<size_t>(total) + 1u));
                dst = out->owned_heap;
            }
            if (dst) {
                const uint32_t copied = text_buffer_copy_range(c->edit_text_buffer, 0u, total, dst, total);
                dst[copied] = '\0';
                out->utf8 = dst;
                out->utf8_len = copied;
            }
        }
    } else if (an->role == A11yRole::StaticText) {
        out->utf8 = an->label.chars;
        out->utf8_len = an->label.length;
    } else {
        out->utf8 = an->value_text.chars;
        out->utf8_len = an->value_text.length;
    }

    out->total_u16 = uia_utf8_byte_to_utf16(out->utf8, out->utf8_len, out->utf8_len);

    if (c) {
        uint32_t a_u16 = 0u;
        uint32_t c_u16 = 0u;
        if (out->is_password) {
            if (c->sel_text_ptr && (c->sel_text_static || c->sel_text_epoch == n->slot->ctx->arena_epoch)) {
                a_u16 = uia_utf8_codepoint_index(c->sel_text_ptr, c->sel_text_len, c->edit_sel_anchor);
                c_u16 = uia_utf8_codepoint_index(c->sel_text_ptr, c->sel_text_len, c->edit_cursor);
            } else {
                a_u16 = out->total_u16;
                c_u16 = out->total_u16;
            }
        } else {
            a_u16 = uia_utf8_byte_to_utf16(out->utf8, out->utf8_len, c->edit_sel_anchor);
            c_u16 = uia_utf8_byte_to_utf16(out->utf8, out->utf8_len, c->edit_cursor);
        }
        if (a_u16 > out->total_u16) a_u16 = out->total_u16;
        if (c_u16 > out->total_u16) c_u16 = out->total_u16;
        out->sel_start_u16 = (a_u16 < c_u16) ? a_u16 : c_u16;
        out->sel_end_u16 = (a_u16 < c_u16) ? c_u16 : a_u16;
    }
    return true;
}

extern const ITextRangeProviderVtbl kUiaTextRangeVtbl;

WinUiaTextRange* uia_create_text_range(WinUiaNode* owner, uint32_t start_u16, uint32_t end_u16) noexcept {
    if (!owner) return nullptr;
    auto* r = static_cast<WinUiaTextRange*>(std::calloc(1u, sizeof(WinUiaTextRange)));
    if (!r) return nullptr;
    r->lpVtbl = &kUiaTextRangeVtbl;
    r->owner = owner;
    uia_addref(owner);
    r->start_u16 = (start_u16 <= end_u16) ? start_u16 : end_u16;
    r->end_u16 = (start_u16 <= end_u16) ? end_u16 : start_u16;
    r->ref_count = 1L;
    return r;
}

ULONG uia_text_range_release(WinUiaTextRange* r) noexcept {
    if (!r) return 0u;
    const LONG next = InterlockedDecrement(&r->ref_count);
    if (next == 0L) {
        if (r->owner) uia_release(r->owner);
        std::free(r);
    }
    return static_cast<ULONG>(next);
}

WinUiaTextRange* uia_as_range(ITextRangeProvider* p) noexcept {
    return (p && p->lpVtbl == &kUiaTextRangeVtbl) ? reinterpret_cast<WinUiaTextRange*>(p) : nullptr;
}

uint32_t uia_step_unit_byte(const char* s, uint32_t len, uint32_t pos, enum TextUnit unit, bool forward, bool* moved) noexcept {
    *moved = false;
    pos = utf8_clamp_boundary(s, len, pos);
    switch (unit) {
        case TextUnit_Document:
        case TextUnit_Page: {
            const uint32_t dst = forward ? len : 0u;
            *moved = (dst != pos);
            return dst;
        }
        case TextUnit_Line:
        case TextUnit_Paragraph: {
            if (forward) {
                if (pos >= len) return len;
                uint32_t p = pos;
                while (p < len && s[p] != '\n') ++p;
                if (p < len && s[p] == '\n') ++p;
                *moved = (p != pos);
                return p;
            } else {
                if (pos == 0u) return 0u;
                uint32_t p = pos;
                if (p > 0u && s[p - 1u] == '\n') --p;
                while (p > 0u && s[p - 1u] != '\n') --p;
                *moved = (p != pos);
                return p;
            }
        }
        case TextUnit_Word: {
            const uint32_t dst = forward ? utf8_word_right(s, len, pos) : utf8_word_left(s, len, pos);
            *moved = (dst != pos);
            return dst;
        }
        case TextUnit_Character:
        case TextUnit_Format:
        default: {
            const uint32_t dst = forward ? utf8_next_grapheme(s, len, pos) : utf8_prev_grapheme(s, len, pos);
            *moved = (dst != pos);
            return dst;
        }
    }
}

HRESULT STDMETHODCALLTYPE uia_tr_qi(ITextRangeProvider* This, REFIID riid, void** ppv) noexcept {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return E_NOINTERFACE;
    if (uia_guid_eq(riid, kIidIUnknown) || uia_guid_eq(riid, kIidUiaTextRange)) {
        *ppv = r;
        InterlockedIncrement(&r->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE uia_tr_addref(ITextRangeProvider* This) noexcept {
    WinUiaTextRange* r = uia_as_range(This);
    return r ? static_cast<ULONG>(InterlockedIncrement(&r->ref_count)) : 0u;
}

ULONG STDMETHODCALLTYPE uia_tr_release(ITextRangeProvider* This) noexcept {
    return uia_text_range_release(uia_as_range(This));
}

HRESULT STDMETHODCALLTYPE uia_tr_clone(ITextRangeProvider* This, ITextRangeProvider** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaTextRange* r = uia_as_range(This);
    if (!r || uia_is_detached(r->owner)) return kUiaElemNotAvail;
    WinUiaTextRange* copy = uia_create_text_range(r->owner, r->start_u16, r->end_u16);
    if (!copy) return E_OUTOFMEMORY;
    *pRetVal = reinterpret_cast<ITextRangeProvider*>(copy);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_tr_compare(ITextRangeProvider* This, ITextRangeProvider* range, BOOL* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = FALSE;
    WinUiaTextRange* a = uia_as_range(This);
    if (!a || uia_is_detached(a->owner)) return kUiaElemNotAvail;
    WinUiaTextRange* b = uia_as_range(range);
    if (!b) return S_OK;
    *pRetVal = (a->owner == b->owner && a->start_u16 == b->start_u16 && a->end_u16 == b->end_u16) ? TRUE : FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_tr_compare_endpoints(
    ITextRangeProvider* This,
    enum TextPatternRangeEndpoint endpoint,
    ITextRangeProvider* targetRange,
    enum TextPatternRangeEndpoint targetEndpoint,
    int* pRetVal) noexcept
{
    if (!pRetVal) return E_POINTER;
    *pRetVal = 0;
    WinUiaTextRange* a = uia_as_range(This);
    WinUiaTextRange* b = uia_as_range(targetRange);
    if (!a || uia_is_detached(a->owner)) return kUiaElemNotAvail;
    if (!b || a->owner != b->owner) return E_INVALIDARG;
    const uint32_t pa = (endpoint == TextPatternRangeEndpoint_Start) ? a->start_u16 : a->end_u16;
    const uint32_t pb = (targetEndpoint == TextPatternRangeEndpoint_Start) ? b->start_u16 : b->end_u16;
    *pRetVal = (pa < pb) ? -1 : ((pa > pb) ? 1 : 0);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_tr_expand(ITextRangeProvider* This, enum TextUnit unit) noexcept {
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return kUiaElemNotAvail;
    return uia_run_on_owner(r->owner, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(r->owner);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(r->owner, an, &snap);
        if (r->start_u16 > snap.total_u16) r->start_u16 = snap.total_u16;
        if (r->end_u16 > snap.total_u16) r->end_u16 = snap.total_u16;
        if (unit == TextUnit_Document || unit == TextUnit_Page) {
            r->start_u16 = 0u;
            r->end_u16 = snap.total_u16;
        } else if (snap.utf8_len == 0u) {
            r->start_u16 = 0u;
            r->end_u16 = 0u;
        } else {
            uint32_t b_start = uia_utf16_to_utf8_byte(snap.utf8, snap.utf8_len, r->start_u16);
            uint32_t a = b_start;
            uint32_t b = b_start;
            if (unit == TextUnit_Word) {
                utf8_word_bounds_at(snap.utf8, snap.utf8_len, b_start, &a, &b);
            } else if (unit == TextUnit_Line || unit == TextUnit_Paragraph) {
                uia_line_bounds_at(snap.utf8, snap.utf8_len, b_start, &a, &b);
            } else {
                if (b_start >= snap.utf8_len) {
                    a = utf8_prev_grapheme(snap.utf8, snap.utf8_len, snap.utf8_len);
                    b = snap.utf8_len;
                } else {
                    a = b_start;
                    b = utf8_next_grapheme(snap.utf8, snap.utf8_len, a);
                }
            }
            r->start_u16 = uia_utf8_byte_to_utf16(snap.utf8, snap.utf8_len, a);
            r->end_u16 = uia_utf8_byte_to_utf16(snap.utf8, snap.utf8_len, b);
        }
        uia_text_snapshot_free(&snap);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_tr_find_attribute(
    ITextRangeProvider* This,
    TEXTATTRIBUTEID attributeId,
    VARIANT val,
    BOOL backward,
    ITextRangeProvider** pRetVal) noexcept
{
    (void)attributeId;
    (void)val;
    (void)backward;
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaTextRange* r = uia_as_range(This);
    return (!r || uia_is_detached(r->owner)) ? kUiaElemNotAvail : S_OK;
}

HRESULT STDMETHODCALLTYPE uia_tr_find_text(
    ITextRangeProvider* This,
    BSTR text,
    BOOL backward,
    BOOL ignoreCase,
    ITextRangeProvider** pRetVal) noexcept
{
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return kUiaElemNotAvail;
    const UINT needle_len = text ? SysStringLen(text) : 0u;
    if (needle_len == 0u) return S_OK;
    return uia_run_on_owner(r->owner, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(r->owner);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(r->owner, an, &snap);
        const uint32_t s_u16 = (r->start_u16 <= snap.total_u16) ? r->start_u16 : snap.total_u16;
        const uint32_t e_u16 = (r->end_u16 <= snap.total_u16) ? r->end_u16 : snap.total_u16;
        const uint32_t b0 = uia_utf16_to_utf8_byte(snap.utf8, snap.utf8_len, s_u16);
        const uint32_t b1 = uia_utf16_to_utf8_byte(snap.utf8, snap.utf8_len, e_u16);
        const uint32_t slice_bytes = (b1 > b0) ? (b1 - b0) : 0u;
        if (slice_bytes > 0u) {
            const int wlen = MultiByteToWideChar(CP_UTF8, 0u, snap.utf8 + b0, static_cast<int>(slice_bytes), nullptr, 0);
            if (wlen >= static_cast<int>(needle_len)) {
                wchar_t stack_w[512];
                wchar_t* hay = (wlen <= static_cast<int>(ETCH_COUNTOF(stack_w)))
                    ? stack_w
                    : static_cast<wchar_t*>(std::malloc(static_cast<size_t>(wlen) * sizeof(wchar_t)));
                if (hay) {
                    MultiByteToWideChar(CP_UTF8, 0u, snap.utf8 + b0, static_cast<int>(slice_bytes), hay, wlen);
                    const auto eq_w = [ignoreCase](wchar_t ca, wchar_t cb) noexcept -> bool {
                        if (!ignoreCase) return ca == cb;
                        const wchar_t la = (ca >= L'A' && ca <= L'Z') ? static_cast<wchar_t>(ca + (L'a' - L'A')) : ca;
                        const wchar_t lb = (cb >= L'A' && cb <= L'Z') ? static_cast<wchar_t>(cb + (L'a' - L'A')) : cb;
                        return la == lb;
                    };
                    const uint32_t max_pos = static_cast<uint32_t>(wlen) - static_cast<uint32_t>(needle_len);
                    bool found = false;
                    uint32_t hit_pos = 0u;
                    if (!backward) {
                        for (uint32_t pos = 0u; pos <= max_pos; ++pos) {
                            bool match = true;
                            for (uint32_t j = 0u; j < needle_len; ++j) {
                                if (!eq_w(hay[pos + j], text[j])) { match = false; break; }
                            }
                            if (match) { found = true; hit_pos = pos; break; }
                        }
                    } else {
                        for (uint32_t k = max_pos + 1u; k > 0u; --k) {
                            const uint32_t pos = k - 1u;
                            bool match = true;
                            for (uint32_t j = 0u; j < needle_len; ++j) {
                                if (!eq_w(hay[pos + j], text[j])) { match = false; break; }
                            }
                            if (match) { found = true; hit_pos = pos; break; }
                        }
                    }
                    if (hay != stack_w) std::free(hay);
                    if (found) {
                        WinUiaTextRange* hit = uia_create_text_range(
                            r->owner, s_u16 + hit_pos, s_u16 + hit_pos + static_cast<uint32_t>(needle_len));
                        if (!hit) {
                            uia_text_snapshot_free(&snap);
                            return E_OUTOFMEMORY;
                        }
                        *pRetVal = reinterpret_cast<ITextRangeProvider*>(hit);
                    }
                }
            }
        }
        uia_text_snapshot_free(&snap);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_tr_get_attribute_value(
    ITextRangeProvider* This,
    TEXTATTRIBUTEID attributeId,
    VARIANT* pRetVal) noexcept
{
    if (!pRetVal) return E_POINTER;
    VariantInit(pRetVal);
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return kUiaElemNotAvail;
    return uia_run_on_owner(r->owner, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(r->owner);
        if (!an) return kUiaElemNotAvail;
        if (attributeId == UIA_IsReadOnlyAttributeId) {
            pRetVal->vt = VT_BOOL;
            pRetVal->boolVal = (uia_value_is_readonly(an) || an->role == A11yRole::StaticText) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
        if (attributeId == UIA_IsHiddenAttributeId) {
            pRetVal->vt = VT_BOOL;
            pRetVal->boolVal = VARIANT_FALSE;
            return S_OK;
        }
        if (g_win_uia.get_not_supported) {
            IUnknown* unk = nullptr;
            if (SUCCEEDED(g_win_uia.get_not_supported(&unk)) && unk) {
                pRetVal->vt = VT_UNKNOWN;
                pRetVal->punkVal = unk;
            }
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_tr_get_bounding_rects(ITextRangeProvider* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return kUiaElemNotAvail;
    return uia_run_on_owner(r->owner, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(r->owner);
        if (!an) return kUiaElemNotAvail;
        double sx = 1.0, sy = 1.0;
        uia_compute_client_scale(r->owner->slot, &sx, &sy);
        POINT tl = {0, 0};
        ClientToScreen(r->owner->slot->hwnd, &tl);
        const double vals[4] = {
            static_cast<double>(tl.x) + static_cast<double>(an->bounds.x) * sx,
            static_cast<double>(tl.y) + static_cast<double>(an->bounds.y) * sy,
            static_cast<double>(an->bounds.w) * sx,
            static_cast<double>(an->bounds.h) * sy,
        };
        SAFEARRAY* arr = SafeArrayCreateVector(VT_R8, 0, 4u);
        if (!arr) return E_OUTOFMEMORY;
        void* raw = nullptr;
        if (SUCCEEDED(SafeArrayAccessData(arr, &raw)) && raw) {
            std::memcpy(raw, vals, sizeof(vals));
            SafeArrayUnaccessData(arr);
        }
        *pRetVal = arr;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_tr_get_enclosing_element(ITextRangeProvider* This, IRawElementProviderSimple** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaTextRange* r = uia_as_range(This);
    if (!r || uia_is_detached(r->owner)) return kUiaElemNotAvail;
    *pRetVal = reinterpret_cast<IRawElementProviderSimple*>(&r->owner->simple);
    uia_addref(r->owner);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_tr_get_text(ITextRangeProvider* This, int maxLength, BSTR* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (maxLength < -1) return E_INVALIDARG;
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return kUiaElemNotAvail;
    return uia_run_on_owner(r->owner, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(r->owner);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(r->owner, an, &snap);
        const uint32_t s_u16 = (r->start_u16 <= snap.total_u16) ? r->start_u16 : snap.total_u16;
        uint32_t e_u16 = (r->end_u16 <= snap.total_u16) ? r->end_u16 : snap.total_u16;
        if (e_u16 < s_u16) e_u16 = s_u16;
        if (maxLength >= 0 && (e_u16 - s_u16) > static_cast<uint32_t>(maxLength)) {
            e_u16 = s_u16 + static_cast<uint32_t>(maxLength);
        }
        const uint32_t b0 = uia_utf16_to_utf8_byte(snap.utf8, snap.utf8_len, s_u16);
        const uint32_t b1 = uia_utf16_to_utf8_byte(snap.utf8, snap.utf8_len, e_u16);
        const uint32_t slice_len = (b1 > b0) ? (b1 - b0) : 0u;
        BSTR out = (slice_len > 0u)
            ? uia_alloc_bstr_utf8(Str{snap.utf8 + b0, slice_len})
            : SysAllocStringLen(L"", 0u);
        uia_text_snapshot_free(&snap);
        if (!out) return E_OUTOFMEMORY;
        *pRetVal = out;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_tr_move(ITextRangeProvider* This, enum TextUnit unit, int count, int* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = 0;
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return kUiaElemNotAvail;
    if (count == 0) return S_OK;
    return uia_run_on_owner(r->owner, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(r->owner);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(r->owner, an, &snap);
        const bool forward = (count > 0);
        const uint32_t steps = static_cast<uint32_t>(forward ? count : -count);
        const bool was_degenerate = (r->start_u16 == r->end_u16);
        uint32_t cur_byte = uia_utf16_to_utf8_byte(
            snap.utf8, snap.utf8_len, (r->start_u16 <= snap.total_u16) ? r->start_u16 : snap.total_u16);
        int moved_total = 0;
        for (uint32_t i = 0u; i < steps; ++i) {
            bool step_ok = false;
            cur_byte = uia_step_unit_byte(snap.utf8, snap.utf8_len, cur_byte, unit, forward, &step_ok);
            if (!step_ok) break;
            moved_total += forward ? 1 : -1;
        }
        const uint32_t new_u16 = uia_utf8_byte_to_utf16(snap.utf8, snap.utf8_len, cur_byte);
        r->start_u16 = new_u16;
        if (was_degenerate || snap.utf8_len == 0u) {
            r->end_u16 = new_u16;
        } else {
            uint32_t a = cur_byte, b = cur_byte;
            if (unit == TextUnit_Document || unit == TextUnit_Page) {
                a = 0u;
                b = snap.utf8_len;
            } else if (unit == TextUnit_Word) {
                utf8_word_bounds_at(snap.utf8, snap.utf8_len, cur_byte, &a, &b);
            } else if (unit == TextUnit_Line || unit == TextUnit_Paragraph) {
                uia_line_bounds_at(snap.utf8, snap.utf8_len, cur_byte, &a, &b);
            } else {
                b = utf8_next_grapheme(snap.utf8, snap.utf8_len, cur_byte);
            }
            r->start_u16 = uia_utf8_byte_to_utf16(snap.utf8, snap.utf8_len, a);
            r->end_u16 = uia_utf8_byte_to_utf16(snap.utf8, snap.utf8_len, b);
        }
        uia_text_snapshot_free(&snap);
        *pRetVal = moved_total;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_tr_move_endpoint_by_unit(
    ITextRangeProvider* This,
    enum TextPatternRangeEndpoint endpoint,
    enum TextUnit unit,
    int count,
    int* pRetVal) noexcept
{
    if (!pRetVal) return E_POINTER;
    *pRetVal = 0;
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return kUiaElemNotAvail;
    if (count == 0) return S_OK;
    return uia_run_on_owner(r->owner, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(r->owner);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(r->owner, an, &snap);
        const bool forward = (count > 0);
        const uint32_t steps = static_cast<uint32_t>(forward ? count : -count);
        uint32_t& ep = (endpoint == TextPatternRangeEndpoint_Start) ? r->start_u16 : r->end_u16;
        if (ep > snap.total_u16) ep = snap.total_u16;
        uint32_t cur_byte = uia_utf16_to_utf8_byte(snap.utf8, snap.utf8_len, ep);
        int moved_total = 0;
        for (uint32_t i = 0u; i < steps; ++i) {
            bool step_ok = false;
            cur_byte = uia_step_unit_byte(snap.utf8, snap.utf8_len, cur_byte, unit, forward, &step_ok);
            if (!step_ok) break;
            moved_total += forward ? 1 : -1;
        }
        ep = uia_utf8_byte_to_utf16(snap.utf8, snap.utf8_len, cur_byte);
        if (endpoint == TextPatternRangeEndpoint_Start && r->start_u16 > r->end_u16) {
            r->end_u16 = r->start_u16;
        } else if (endpoint == TextPatternRangeEndpoint_End && r->end_u16 < r->start_u16) {
            r->start_u16 = r->end_u16;
        }
        uia_text_snapshot_free(&snap);
        *pRetVal = moved_total;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_tr_move_endpoint_by_range(
    ITextRangeProvider* This,
    enum TextPatternRangeEndpoint endpoint,
    ITextRangeProvider* targetRange,
    enum TextPatternRangeEndpoint targetEndpoint) noexcept
{
    WinUiaTextRange* a = uia_as_range(This);
    WinUiaTextRange* b = uia_as_range(targetRange);
    if (!a || uia_is_detached(a->owner)) return kUiaElemNotAvail;
    if (!b || a->owner != b->owner) return E_INVALIDARG;
    const uint32_t src_pos = (targetEndpoint == TextPatternRangeEndpoint_Start) ? b->start_u16 : b->end_u16;
    if (endpoint == TextPatternRangeEndpoint_Start) {
        a->start_u16 = src_pos;
        if (a->start_u16 > a->end_u16) a->end_u16 = a->start_u16;
    } else {
        a->end_u16 = src_pos;
        if (a->end_u16 < a->start_u16) a->start_u16 = a->end_u16;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_tr_select(ITextRangeProvider* This) noexcept {
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return kUiaElemNotAvail;
    return uia_run_on_owner(r->owner, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(r->owner);
        if (!an) return kUiaElemNotAvail;
        WindowUiState* ui = uia_window_ui(r->owner->slot);
        WidgetState* ws = ui ? state_map_find(&ui->states, an->id) : nullptr;
        WidgetCold* c = ensure_widget_cold(ws);
        if (!c) return E_FAIL;
        UiaTextSnapshot snap = {};
        uia_capture_text(r->owner, an, &snap);
        const uint32_t s_u16 = (r->start_u16 <= snap.total_u16) ? r->start_u16 : snap.total_u16;
        const uint32_t e_u16 = (r->end_u16 <= snap.total_u16) ? r->end_u16 : snap.total_u16;
        uint32_t b_start = 0u;
        uint32_t b_end = 0u;
        if (snap.is_password && c->sel_text_ptr &&
            (c->sel_text_static || c->sel_text_epoch == r->owner->slot->ctx->arena_epoch))
        {
            b_start = uia_utf8_byte_at_codepoint(c->sel_text_ptr, c->sel_text_len, s_u16);
            b_end = uia_utf8_byte_at_codepoint(c->sel_text_ptr, c->sel_text_len, e_u16);
        } else {
            b_start = uia_utf16_to_utf8_byte(snap.utf8, snap.utf8_len, s_u16);
            b_end = uia_utf16_to_utf8_byte(snap.utf8, snap.utf8_len, e_u16);
        }
        c->edit_initialized = true;
        c->edit_sel_anchor = b_start;
        c->edit_cursor = b_end;
        c->edit_preferred_x = -1.0f;
        c->edit_wrap_affinity = false;
        if (an->role == A11yRole::CodeEditor) {
            c->editor_reveal = true;
        } else {
            c->edit_reveal_caret = true;
        }
        ui->redraw_budget = 2u;
        request_window_redraw(r->owner->slot->ctx, r->owner->slot->window);
        wake_up(r->owner->slot->ctx);
        uia_text_snapshot_free(&snap);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_tr_add_to_selection(ITextRangeProvider* This) noexcept {
    WinUiaTextRange* r = uia_as_range(This);
    return (!r || uia_is_detached(r->owner)) ? kUiaElemNotAvail : kUiaInvalidOp;
}

HRESULT STDMETHODCALLTYPE uia_tr_remove_from_selection(ITextRangeProvider* This) noexcept {
    WinUiaTextRange* r = uia_as_range(This);
    return (!r || uia_is_detached(r->owner)) ? kUiaElemNotAvail : kUiaInvalidOp;
}

HRESULT STDMETHODCALLTYPE uia_tr_scroll_into_view(ITextRangeProvider* This, BOOL alignToTop) noexcept {
    WinUiaTextRange* r = uia_as_range(This);
    if (!r) return kUiaElemNotAvail;
    return uia_run_on_owner(r->owner, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(r->owner);
        if (!an) return kUiaElemNotAvail;
        WindowUiState* ui = uia_window_ui(r->owner->slot);
        WidgetState* ws = ui ? state_map_find(&ui->states, an->id) : nullptr;
        if (WidgetCold* c = ensure_widget_cold(ws)) {
            if (an->role == A11yRole::CodeEditor && c->edit_text_buffer && c->sel_text_epoch == r->owner->slot->ctx->arena_epoch) {
                UiaTextSnapshot snap = {};
                uia_capture_text(r->owner, an, &snap);
                const uint32_t b_pos = uia_utf16_to_utf8_byte(snap.utf8, snap.utf8_len, r->start_u16);
                const TextLineCol lc = text_buffer_line_col(c->edit_text_buffer, b_pos);
                const double line_y = static_cast<double>(lc.line) * static_cast<double>(c->vlist_stride);
                const double target_y = alignToTop
                    ? line_y
                    : (line_y - std::fmax(0.0, static_cast<double>(ws->viewport_dim.y) - static_cast<double>(c->vlist_stride)));
                c->vscroll_pos_y = clamp_double(target_y, 0.0, c->vscroll_max_y);
                c->vscroll_vel_y = 0.0;
                uia_text_snapshot_free(&snap);
            } else {
                c->edit_reveal_caret = true;
            }
            ui->redraw_budget = 2u;
            request_window_redraw(r->owner->slot->ctx, r->owner->slot->window);
            wake_up(r->owner->slot->ctx);
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_tr_get_children(ITextRangeProvider* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaTextRange* r = uia_as_range(This);
    if (!r || uia_is_detached(r->owner)) return kUiaElemNotAvail;
    SAFEARRAY* arr = uia_make_unk_safearray(nullptr, 0u);
    if (!arr) return E_OUTOFMEMORY;
    *pRetVal = arr;
    return S_OK;
}

const ITextRangeProviderVtbl kUiaTextRangeVtbl = {
    uia_tr_qi,
    uia_tr_addref,
    uia_tr_release,
    uia_tr_clone,
    uia_tr_compare,
    uia_tr_compare_endpoints,
    uia_tr_expand,
    uia_tr_find_attribute,
    uia_tr_find_text,
    uia_tr_get_attribute_value,
    uia_tr_get_bounding_rects,
    uia_tr_get_enclosing_element,
    uia_tr_get_text,
    uia_tr_move,
    uia_tr_move_endpoint_by_unit,
    uia_tr_move_endpoint_by_range,
    uia_tr_select,
    uia_tr_add_to_selection,
    uia_tr_remove_from_selection,
    uia_tr_scroll_into_view,
    uia_tr_get_children,
};

HRESULT STDMETHODCALLTYPE uia_text_get_selection(ITextProvider* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(n, an, &snap);
        WinUiaTextRange* range = uia_create_text_range(n, snap.sel_start_u16, snap.sel_end_u16);
        uia_text_snapshot_free(&snap);
        if (!range) return E_OUTOFMEMORY;
        IUnknown* items[1] = {reinterpret_cast<IUnknown*>(range)};
        SAFEARRAY* arr = uia_make_unk_safearray(items, 1u);
        uia_text_range_release(range);
        if (!arr) return E_OUTOFMEMORY;
        *pRetVal = arr;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_text_get_visible_ranges(ITextProvider* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(n, an, &snap);
        WinUiaTextRange* range = uia_create_text_range(n, 0u, snap.total_u16);
        uia_text_snapshot_free(&snap);
        if (!range) return E_OUTOFMEMORY;
        IUnknown* items[1] = {reinterpret_cast<IUnknown*>(range)};
        SAFEARRAY* arr = uia_make_unk_safearray(items, 1u);
        uia_text_range_release(range);
        if (!arr) return E_OUTOFMEMORY;
        *pRetVal = arr;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_text_range_from_child(
    ITextProvider* This,
    IRawElementProviderSimple* childElement,
    ITextRangeProvider** pRetVal) noexcept
{
    (void)childElement;
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(n, an, &snap);
        WinUiaTextRange* range = uia_create_text_range(n, 0u, snap.total_u16);
        uia_text_snapshot_free(&snap);
        if (!range) return E_OUTOFMEMORY;
        *pRetVal = reinterpret_cast<ITextRangeProvider*>(range);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_text_range_from_point(
    ITextProvider* This,
    struct UiaPoint point,
    ITextRangeProvider** pRetVal) noexcept
{
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(n, an, &snap);
        double sx = 1.0, sy = 1.0;
        uia_compute_client_scale(n->slot, &sx, &sy);
        POINT tl = {0, 0};
        ClientToScreen(n->slot->hwnd, &tl);
        const double lx = (point.x - static_cast<double>(tl.x)) / sx;
        uint32_t pos_u16 = 0u;
        if (an->bounds.w > 1.0f && snap.total_u16 > 0u) {
            const double t = clamp_double((lx - static_cast<double>(an->bounds.x)) / static_cast<double>(an->bounds.w), 0.0, 1.0);
            pos_u16 = static_cast<uint32_t>(std::floor(t * static_cast<double>(snap.total_u16) + 0.5));
            if (pos_u16 > snap.total_u16) pos_u16 = snap.total_u16;
        }
        WinUiaTextRange* range = uia_create_text_range(n, pos_u16, pos_u16);
        uia_text_snapshot_free(&snap);
        if (!range) return E_OUTOFMEMORY;
        *pRetVal = reinterpret_cast<ITextRangeProvider*>(range);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_text_get_document_range(ITextProvider* This, ITextRangeProvider** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaTextSnapshot snap = {};
        uia_capture_text(n, an, &snap);
        WinUiaTextRange* range = uia_create_text_range(n, 0u, snap.total_u16);
        uia_text_snapshot_free(&snap);
        if (!range) return E_OUTOFMEMORY;
        *pRetVal = reinterpret_cast<ITextRangeProvider*>(range);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_text_get_supported_selection(ITextProvider* This, enum SupportedTextSelection* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    if (uia_is_detached(n)) return kUiaElemNotAvail;
    *pRetVal = SupportedTextSelection_Single;
    return S_OK;
}

// ============================================================================
// IScrollProvider
// ============================================================================

double uia_scroll_step_delta(enum ScrollAmount amount, double view_dim) noexcept {
    const double small_step = 40.0;
    const double large_step = (view_dim * 0.85 > small_step) ? (view_dim * 0.85) : small_step;
    switch (amount) {
        case ScrollAmount_SmallDecrement: return -small_step;
        case ScrollAmount_LargeDecrement: return -large_step;
        case ScrollAmount_SmallIncrement: return small_step;
        case ScrollAmount_LargeIncrement: return large_step;
        case ScrollAmount_NoAmount:
        default: return 0.0;
    }
}

HRESULT STDMETHODCALLTYPE uia_scroll_do(IScrollProvider* This, enum ScrollAmount horizontalAmount, enum ScrollAmount verticalAmount) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaScrollGeom g = {};
        if (!uia_read_scroll_geom(n, an, &g)) return kUiaInvalidOp;
        const bool req_h = (horizontalAmount != ScrollAmount_NoAmount);
        const bool req_v = (verticalAmount != ScrollAmount_NoAmount);
        if (!req_h && !req_v) return S_OK;
        if ((req_h && !g.h_scrollable) && (!req_v || !g.v_scrollable)) return kUiaInvalidOp;
        if ((req_v && !g.v_scrollable) && (!req_h || !g.h_scrollable)) return kUiaInvalidOp;
        bool changed = false;
        if (req_h && g.h_scrollable && g.h_ws) {
            const double next_x = clamp_double(g.h_pos + uia_scroll_step_delta(horizontalAmount, g.h_view), 0.0, g.h_max);
            g.h_ws->scroll_pos.x = static_cast<float>(next_x);
            g.h_ws->scroll_vel.x = 0.0f;
            changed = true;
        }
        if (req_v && g.v_scrollable && g.v_ws) {
            const double delta = uia_scroll_step_delta(verticalAmount, g.v_view);
            const double next_y = clamp_double(g.v_pos + delta, 0.0, g.v_max);
            if (g.is_vlist) {
                if (WidgetCold* c = ensure_widget_cold(g.v_ws)) {
                    c->vscroll_pos_y = next_y;
                    c->vscroll_vel_y = 0.0;
                    if (delta < 0.0) c->vlist_stuck_bottom = false;
                }
            } else {
                g.v_ws->scroll_pos.y = static_cast<float>(next_y);
                g.v_ws->scroll_vel.y = 0.0f;
                if (delta < 0.0) {
                    g.v_ws->scroll_stuck_bottom = false;
                } else if (delta > 0.0 && (g.v_max <= 0.5 || next_y >= g.v_max - 2.0)) {
                    g.v_ws->scroll_stuck_bottom = true;
                }
            }
            changed = true;
        }
        if (changed) {
            if (WindowUiState* ui = uia_window_ui(n->slot)) {
                ui->redraw_budget = 2u;
            }
            request_window_redraw(n->slot->ctx, n->slot->window);
            wake_up(n->slot->ctx);
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_scroll_set_percent(IScrollProvider* This, double horizontalPercent, double verticalPercent) noexcept {
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaScrollGeom g = {};
        if (!uia_read_scroll_geom(n, an, &g)) return kUiaInvalidOp;
        const bool set_h = (horizontalPercent != UIA_ScrollPatternNoScroll);
        const bool set_v = (verticalPercent != UIA_ScrollPatternNoScroll);
        if (set_h && (!std::isfinite(horizontalPercent) || horizontalPercent < 0.0 || horizontalPercent > 100.0)) return E_INVALIDARG;
        if (set_v && (!std::isfinite(verticalPercent) || verticalPercent < 0.0 || verticalPercent > 100.0)) return E_INVALIDARG;
        if ((set_h && !g.h_scrollable) || (set_v && !g.v_scrollable)) return kUiaInvalidOp;
        bool changed = false;
        if (set_h && g.h_ws) {
            g.h_ws->scroll_pos.x = static_cast<float>((horizontalPercent / 100.0) * g.h_max);
            g.h_ws->scroll_vel.x = 0.0f;
            changed = true;
        }
        if (set_v && g.v_ws) {
            const double target_y = (verticalPercent / 100.0) * g.v_max;
            if (g.is_vlist) {
                if (WidgetCold* c = ensure_widget_cold(g.v_ws)) {
                    c->vscroll_pos_y = target_y;
                    c->vscroll_vel_y = 0.0;
                    c->vlist_stuck_bottom = (verticalPercent >= 99.9);
                }
            } else {
                g.v_ws->scroll_pos.y = static_cast<float>(target_y);
                g.v_ws->scroll_vel.y = 0.0f;
                g.v_ws->scroll_stuck_bottom = (verticalPercent >= 99.9);
            }
            changed = true;
        }
        if (changed) {
            if (WindowUiState* ui = uia_window_ui(n->slot)) {
                ui->redraw_budget = 2u;
            }
            request_window_redraw(n->slot->ctx, n->slot->window);
            wake_up(n->slot->ctx);
        }
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_scroll_get_h_percent(IScrollProvider* This, double* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaScrollGeom g = {};
        uia_read_scroll_geom(n, an, &g);
        *pRetVal = uia_scroll_percent(g.h_scrollable, g.h_pos, g.h_max);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_scroll_get_v_percent(IScrollProvider* This, double* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaScrollGeom g = {};
        uia_read_scroll_geom(n, an, &g);
        *pRetVal = uia_scroll_percent(g.v_scrollable, g.v_pos, g.v_max);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_scroll_get_h_view(IScrollProvider* This, double* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaScrollGeom g = {};
        uia_read_scroll_geom(n, an, &g);
        *pRetVal = uia_scroll_view_size(g.h_scrollable, g.h_view, g.h_content);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_scroll_get_v_view(IScrollProvider* This, double* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaScrollGeom g = {};
        uia_read_scroll_geom(n, an, &g);
        *pRetVal = uia_scroll_view_size(g.v_scrollable, g.v_view, g.v_content);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_scroll_get_h_scrollable(IScrollProvider* This, BOOL* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaScrollGeom g = {};
        uia_read_scroll_geom(n, an, &g);
        *pRetVal = g.h_scrollable ? TRUE : FALSE;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_scroll_get_v_scrollable(IScrollProvider* This, BOOL* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        UiaScrollGeom g = {};
        uia_read_scroll_geom(n, an, &g);
        *pRetVal = g.v_scrollable ? TRUE : FALSE;
        return S_OK;
    });
}

// ============================================================================
// IGridProvider / IGridItemProvider / ITableProvider / ITableItemProvider
// ============================================================================

HRESULT STDMETHODCALLTYPE uia_grid_get_item(IGridProvider* This, int row, int column, IRawElementProviderSimple** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (row < 0 || column < 0) return E_INVALIDARG;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        const A11yNode* an = uia_live_node(n, &idx);
        if (!an) return kUiaElemNotAvail;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        const uint32_t cell_idx = uia_table_cell_at(span, idx, static_cast<uint32_t>(row), static_cast<uint32_t>(column));
        if (cell_idx >= span.size()) return E_INVALIDARG;
        WinUiaNode* dst = uia_get_or_create_node(n->slot, span[cell_idx].id, false);
        if (!dst) return E_OUTOFMEMORY;
        *pRetVal = reinterpret_cast<IRawElementProviderSimple*>(&dst->simple);
        uia_addref(dst);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_grid_get_row_count(IGridProvider* This, int* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = 0;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        const A11yNode* an = uia_live_node(n, &idx);
        if (!an) return kUiaElemNotAvail;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        uint32_t rows = 0u, cols = 0u;
        uia_table_dims(n->slot, span, idx, &rows, &cols);
        *pRetVal = static_cast<int>(rows);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_grid_get_col_count(IGridProvider* This, int* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = 0;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        const A11yNode* an = uia_live_node(n, &idx);
        if (!an) return kUiaElemNotAvail;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        uint32_t rows = 0u, cols = 0u;
        uia_table_dims(n->slot, span, idx, &rows, &cols);
        *pRetVal = static_cast<int>(cols);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_grid_item_get_row(IGridItemProvider* This, int* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = 0;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        if (!uia_live_node(n, &idx)) return kUiaElemNotAvail;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        uint32_t row = 0u;
        if (!uia_cell_coords(span, idx, nullptr, &row, nullptr)) return kUiaElemNotAvail;
        *pRetVal = static_cast<int>(row);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_grid_item_get_col(IGridItemProvider* This, int* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = 0;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        if (!uia_live_node(n, &idx)) return kUiaElemNotAvail;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        uint32_t col = 0u;
        if (!uia_cell_coords(span, idx, nullptr, nullptr, &col)) return kUiaElemNotAvail;
        *pRetVal = static_cast<int>(col);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_grid_item_get_row_span(IGridItemProvider* This, int* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    if (uia_is_detached(n)) return kUiaElemNotAvail;
    *pRetVal = 1;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_grid_item_get_col_span(IGridItemProvider* This, int* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    if (uia_is_detached(n)) return kUiaElemNotAvail;
    *pRetVal = 1;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_grid_item_get_grid(IGridItemProvider* This, IRawElementProviderSimple** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        if (!uia_live_node(n, &idx)) return kUiaElemNotAvail;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        uint32_t tbl_idx = UINT32_MAX;
        if (!uia_cell_coords(span, idx, &tbl_idx, nullptr, nullptr) || tbl_idx >= span.size()) return kUiaElemNotAvail;
        WinUiaNode* tbl = uia_get_or_create_node(n->slot, span[tbl_idx].id, false);
        if (!tbl) return E_OUTOFMEMORY;
        *pRetVal = reinterpret_cast<IRawElementProviderSimple*>(&tbl->simple);
        uia_addref(tbl);
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_table_get_row_headers(ITableProvider* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    if (uia_is_detached(n)) return kUiaElemNotAvail;
    SAFEARRAY* arr = uia_make_unk_safearray(nullptr, 0u);
    if (!arr) return E_OUTOFMEMORY;
    *pRetVal = arr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_table_get_col_headers(ITableProvider* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) return kUiaElemNotAvail;
        const WindowUiState* ui = uia_window_ui(n->slot);
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        IUnknown* hdrs[kMaxTableColumns] = {};
        uint32_t count = 0u;
        if (ui) {
            for (uint32_t c = 0u; c < kMaxTableColumns; ++c) {
                const uint32_t hid = uia_table_column_header_id(ui, an->id, c);
                if (hid == 0u) break;
                if (uia_find_index_by_id(span, hid) < span.size()) {
                    if (WinUiaNode* hn = uia_get_or_create_node(n->slot, hid, false)) {
                        hdrs[count++] = reinterpret_cast<IUnknown*>(&hn->simple);
                    }
                }
            }
        }
        SAFEARRAY* arr = uia_make_unk_safearray(hdrs, count);
        if (!arr) return E_OUTOFMEMORY;
        *pRetVal = arr;
        return S_OK;
    });
}

HRESULT STDMETHODCALLTYPE uia_table_get_major(ITableProvider* This, enum RowOrColumnMajor* pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    WinUiaNode* n = uia_owner(This);
    if (uia_is_detached(n)) return kUiaElemNotAvail;
    *pRetVal = RowOrColumnMajor_RowMajor;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_table_item_get_row_headers(ITableItemProvider* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    if (uia_is_detached(n)) return kUiaElemNotAvail;
    SAFEARRAY* arr = uia_make_unk_safearray(nullptr, 0u);
    if (!arr) return E_OUTOFMEMORY;
    *pRetVal = arr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE uia_table_item_get_col_headers(ITableItemProvider* This, SAFEARRAY** pRetVal) noexcept {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    WinUiaNode* n = uia_owner(This);
    return uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        uint32_t idx = 0u;
        if (!uia_live_node(n, &idx)) return kUiaElemNotAvail;
        const auto span = frame_a11y_nodes(n->slot->ctx, n->slot->window);
        uint32_t tbl_idx = UINT32_MAX, col = 0u;
        IUnknown* hdrs[1] = {};
        uint32_t count = 0u;
        if (uia_cell_coords(span, idx, &tbl_idx, nullptr, &col) && tbl_idx < span.size()) {
            const uint32_t hid = uia_table_column_header_id(uia_window_ui(n->slot), span[tbl_idx].id, col);
            if (hid != 0u && uia_find_index_by_id(span, hid) < span.size()) {
                if (WinUiaNode* hn = uia_get_or_create_node(n->slot, hid, false)) {
                    hdrs[count++] = reinterpret_cast<IUnknown*>(&hn->simple);
                }
            }
        }
        SAFEARRAY* arr = uia_make_unk_safearray(hdrs, count);
        if (!arr) return E_OUTOFMEMORY;
        *pRetVal = arr;
        return S_OK;
    });
}

const IRawElementProviderSimpleVtbl kUiaSimpleVtbl = {
    uia_com_qi<IRawElementProviderSimple>,
    uia_com_addref<IRawElementProviderSimple>,
    uia_com_release<IRawElementProviderSimple>,
    uia_get_provider_options,
    uia_get_pattern_provider,
    uia_get_property_value,
    uia_get_host_provider,
};

const IRawElementProviderFragmentVtbl kUiaFragmentVtbl = {
    uia_com_qi<IRawElementProviderFragment>,
    uia_com_addref<IRawElementProviderFragment>,
    uia_com_release<IRawElementProviderFragment>,
    uia_navigate,
    uia_get_runtime_id,
    uia_get_bounding_rect,
    uia_get_embedded_roots,
    uia_set_focus,
    uia_get_fragment_root,
};

const IRawElementProviderFragmentRootVtbl kUiaFragmentRootVtbl = {
    uia_com_qi<IRawElementProviderFragmentRoot>,
    uia_com_addref<IRawElementProviderFragmentRoot>,
    uia_com_release<IRawElementProviderFragmentRoot>,
    uia_elem_from_point,
    uia_get_focus,
};

const IInvokeProviderVtbl kUiaInvokeVtbl = {
    uia_com_qi<IInvokeProvider>,
    uia_com_addref<IInvokeProvider>,
    uia_com_release<IInvokeProvider>,
    uia_invoke,
};

const IValueProviderVtbl kUiaValueVtbl = {
    uia_com_qi<IValueProvider>,
    uia_com_addref<IValueProvider>,
    uia_com_release<IValueProvider>,
    uia_value_set,
    uia_value_get,
    uia_value_readonly,
};

const IRangeValueProviderVtbl kUiaRangeValueVtbl = {
    uia_com_qi<IRangeValueProvider>,
    uia_com_addref<IRangeValueProvider>,
    uia_com_release<IRangeValueProvider>,
    uia_range_set,
    uia_range_get,
    uia_range_readonly,
    uia_range_max,
    uia_range_min,
    uia_range_large,
    uia_range_small,
};

const IToggleProviderVtbl kUiaToggleVtbl = {
    uia_com_qi<IToggleProvider>,
    uia_com_addref<IToggleProvider>,
    uia_com_release<IToggleProvider>,
    uia_toggle,
    uia_toggle_state,
};

const IExpandCollapseProviderVtbl kUiaExpandCollapseVtbl = {
    uia_com_qi<IExpandCollapseProvider>,
    uia_com_addref<IExpandCollapseProvider>,
    uia_com_release<IExpandCollapseProvider>,
    uia_expand,
    uia_collapse,
    uia_expand_state,
};

const ISelectionItemProviderVtbl kUiaSelectionItemVtbl = {
    uia_com_qi<ISelectionItemProvider>,
    uia_com_addref<ISelectionItemProvider>,
    uia_com_release<ISelectionItemProvider>,
    uia_select_item,
    uia_select_add,
    uia_select_remove,
    uia_select_is_selected,
    uia_select_container,
};

const ITextProviderVtbl kUiaTextVtbl = {
    uia_com_qi<ITextProvider>,
    uia_com_addref<ITextProvider>,
    uia_com_release<ITextProvider>,
    uia_text_get_selection,
    uia_text_get_visible_ranges,
    uia_text_range_from_child,
    uia_text_range_from_point,
    uia_text_get_document_range,
    uia_text_get_supported_selection,
};

const IScrollProviderVtbl kUiaScrollVtbl = {
    uia_com_qi<IScrollProvider>,
    uia_com_addref<IScrollProvider>,
    uia_com_release<IScrollProvider>,
    uia_scroll_do,
    uia_scroll_set_percent,
    uia_scroll_get_h_percent,
    uia_scroll_get_v_percent,
    uia_scroll_get_h_view,
    uia_scroll_get_v_view,
    uia_scroll_get_h_scrollable,
    uia_scroll_get_v_scrollable,
};

const IGridProviderVtbl kUiaGridVtbl = {
    uia_com_qi<IGridProvider>,
    uia_com_addref<IGridProvider>,
    uia_com_release<IGridProvider>,
    uia_grid_get_item,
    uia_grid_get_row_count,
    uia_grid_get_col_count,
};

const IGridItemProviderVtbl kUiaGridItemVtbl = {
    uia_com_qi<IGridItemProvider>,
    uia_com_addref<IGridItemProvider>,
    uia_com_release<IGridItemProvider>,
    uia_grid_item_get_row,
    uia_grid_item_get_col,
    uia_grid_item_get_row_span,
    uia_grid_item_get_col_span,
    uia_grid_item_get_grid,
};

const ITableProviderVtbl kUiaTableVtbl = {
    uia_com_qi<ITableProvider>,
    uia_com_addref<ITableProvider>,
    uia_com_release<ITableProvider>,
    uia_table_get_row_headers,
    uia_table_get_col_headers,
    uia_table_get_major,
};

const ITableItemProviderVtbl kUiaTableItemVtbl = {
    uia_com_qi<ITableItemProvider>,
    uia_com_addref<ITableItemProvider>,
    uia_com_release<ITableItemProvider>,
    uia_table_item_get_row_headers,
    uia_table_item_get_col_headers,
};

WinUiaNode* uia_get_or_create_node(WinUiaSlot* slot, uint32_t elem_id, bool is_root) noexcept {
    if (!slot) {
        return nullptr;
    }
    if (is_root && slot->root) {
        return slot->root;
    }
    for (uint32_t i = 0u; i < slot->node_count; ++i) {
        WinUiaNode* c = slot->nodes[i];
        if (c && c->element_id == elem_id && c->is_root == is_root) {
            return c;
        }
    }
    if (slot->node_count == slot->node_cap) {
        const uint32_t cap = slot->node_cap ? slot->node_cap * 2u : 16u;
        void* p = std::realloc(slot->nodes, size_t(cap) * sizeof(WinUiaNode*));
        if (!p) return nullptr;
        slot->nodes = static_cast<WinUiaNode**>(p);
        slot->node_cap = cap;
    }
    auto* n = static_cast<WinUiaNode*>(std::calloc(1u, sizeof(WinUiaNode)));
    if (!n) return nullptr;
    n->simple = {&kUiaSimpleVtbl, n};
    n->fragment = {&kUiaFragmentVtbl, n};
    n->fragment_root = {&kUiaFragmentRootVtbl, n};
    n->invoke = {&kUiaInvokeVtbl, n};
    n->value = {&kUiaValueVtbl, n};
    n->range_value = {&kUiaRangeValueVtbl, n};
    n->toggle = {&kUiaToggleVtbl, n};
    n->expand_collapse = {&kUiaExpandCollapseVtbl, n};
    n->selection_item = {&kUiaSelectionItemVtbl, n};
    n->text = {&kUiaTextVtbl, n};
    n->scroll = {&kUiaScrollVtbl, n};
    n->grid = {&kUiaGridVtbl, n};
    n->grid_item = {&kUiaGridItemVtbl, n};
    n->table = {&kUiaTableVtbl, n};
    n->table_item = {&kUiaTableItemVtbl, n};
    n->slot = slot;
    n->hwnd = slot->hwnd;
    n->owner_tid = slot->owner_tid;
    n->element_id = elem_id;
    n->ref_count = 1L;
    n->is_root = is_root;
    n->detached = 0L;
    slot->nodes[slot->node_count++] = n;
    if (is_root) {
        slot->root = n;
    }
    return n;
}

uint64_t uia_hash_str(Str s) noexcept {
    return (s.chars && s.length > 0u) ? fnv1a_64(s.chars, s.length) : 0ull;
}

uint64_t uia_compute_struct_hash(std::span<const A11yNode> span) noexcept {
    uint64_t h = 14695981039346656037ULL;
    const uint32_t count = static_cast<uint32_t>(span.size());
    h ^= count;
    h *= 1099511628211ULL;
    for (uint32_t i = 0u; i < count; ++i) {
        const A11yNode& an = span[i];
        h ^= an.id;
        h *= 1099511628211ULL;
        h ^= an.parent_id;
        h *= 1099511628211ULL;
        h ^= static_cast<uint8_t>(an.role);
        h *= 1099511628211ULL;
    }
    return h;
}

const WinUiaTrackedNode* uia_tracked_find(const WinUiaSlot* uia, uint32_t elem_id) noexcept {
    if (!uia || !uia->prev_tracked || uia->prev_tracked_cap == 0u || elem_id == 0u) return nullptr;
    const uint32_t mask = uia->prev_tracked_cap - 1u;
    uint32_t slot = hash_u32(elem_id) & mask;
    for (uint32_t probe = 0u; probe < uia->prev_tracked_cap; ++probe) {
        const WinUiaTrackedNode& e = uia->prev_tracked[slot];
        if (e.id == elem_id) return &e;
        if (e.id == 0u) return nullptr;
        slot = (slot + 1u) & mask;
    }
    return nullptr;
}

void uia_tracked_rebuild(WinUiaSlot* uia, std::span<const A11yNode> span, uint32_t focus_id) noexcept {
    const uint32_t n_nodes = static_cast<uint32_t>(span.size());
    const uint32_t want = (n_nodes < kUiaMaxTrackedNodes) ? n_nodes : kUiaMaxTrackedNodes;
    uint32_t cap = 64u;
    while (cap < want * 2u && cap < 1024u) cap *= 2u;
    if (cap > uia->prev_tracked_cap) {
        auto* next = static_cast<WinUiaTrackedNode*>(std::calloc(cap, sizeof(WinUiaTrackedNode)));
        if (!next) return;
        std::free(uia->prev_tracked);
        uia->prev_tracked = next;
        uia->prev_tracked_cap = cap;
    } else if (uia->prev_tracked) {
        std::memset(uia->prev_tracked, 0, static_cast<size_t>(uia->prev_tracked_cap) * sizeof(WinUiaTrackedNode));
    }
    uia->prev_tracked_count = 0u;
    if (!uia->prev_tracked || uia->prev_tracked_cap == 0u) return;
    const uint32_t mask = uia->prev_tracked_cap - 1u;
    const auto insert = [&](const A11yNode& an) noexcept {
        if (an.id == 0u || uia->prev_tracked_count >= kUiaMaxTrackedNodes) return;
        uint32_t slot = hash_u32(an.id) & mask;
        for (uint32_t probe = 0u; probe < uia->prev_tracked_cap; ++probe) {
            WinUiaTrackedNode& e = uia->prev_tracked[slot];
            if (e.id == an.id) return;
            if (e.id == 0u) {
                e.id = an.id;
                e.state_flags = an.state_flags;
                e.label_hash = uia_hash_str(an.label);
                e.value_hash = uia_hash_str(an.value_text);
                e.label_len = an.label.length;
                e.value_len = an.value_text.length;
                e.role = an.role;
                ++uia->prev_tracked_count;
                return;
            }
            slot = (slot + 1u) & mask;
        }
    };
    // Always prioritize focused node and live-region nodes first.
    if (focus_id != 0u) {
        const uint32_t fi = uia_find_index_by_id(span, focus_id);
        if (fi < n_nodes) insert(span[fi]);
    }
    for (uint32_t i = 0u; i < n_nodes && uia->prev_tracked_count < kUiaMaxTrackedNodes; ++i) {
        if (uia_is_live_role(span[i].role)) insert(span[i]);
    }
    for (uint32_t i = 0u; i < n_nodes && uia->prev_tracked_count < kUiaMaxTrackedNodes; ++i) {
        if (uia_is_control_element(span, i)) insert(span[i]);
    }
}

void uia_refresh_reduced_motion(Context* ctx, SDL_Window* win, bool force_redraw) noexcept {
    if (!ctx) return;
    bool dirty = force_redraw;
    BOOL anim_enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0u, &anim_enabled, 0u)) {
        const bool next_reduced = (anim_enabled == FALSE);
        if (ctx->reduced_motion != next_reduced) {
            ctx->reduced_motion = next_reduced;
            dirty = true;
        }
    }
    if (dirty) {
        for (uint32_t i = 0u; i < ctx->claimed_window_cap; ++i) {
            if (ctx->claimed_windows[i]) {
                ctx->claimed_windows[i]->ui.redraw_budget = 2u;
            }
        }
        if (win) {
            request_window_redraw(ctx, win);
        } else {
            request_redraw(ctx);
        }
        wake_up(ctx);
    }
}

LRESULT CALLBACK etch_uia_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) noexcept {
    WinUiaSlot* slot = reinterpret_cast<WinUiaSlot*>(dwRefData);
    if (g_win_uia.dispatch_msg != 0u && msg == g_win_uia.dispatch_msg) {
        AcquireSRWLockExclusive(&g_win_uia.dispatch_lock);
        UiaDispatchCall* call = nullptr;
        const uintptr_t target_addr = static_cast<uintptr_t>(lParam);
        const uintptr_t target_seq = static_cast<uintptr_t>(wParam);
        for (UiaDispatchCall* it = g_win_uia.pending_calls; it; it = it->next) {
            if (reinterpret_cast<uintptr_t>(it) == target_addr && it->seq == target_seq) {
                call = it;
                break;
            }
        }
        if (!call || call->state != 0u) {
            ReleaseSRWLockExclusive(&g_win_uia.dispatch_lock);
            return 0;
        }
        if (!slot || uia_is_detached(call->node) || call->node->slot != slot) {
            call->state = 3u;
            WakeAllConditionVariable(&g_win_uia.dispatch_cv);
            ReleaseSRWLockExclusive(&g_win_uia.dispatch_lock);
            return 0;
        }
        call->state = 1u;
        ReleaseSRWLockExclusive(&g_win_uia.dispatch_lock);

        const HRESULT hr = call->fn(call->ctx);

        AcquireSRWLockExclusive(&g_win_uia.dispatch_lock);
        call->hr = hr;
        call->state = 2u;
        WakeAllConditionVariable(&g_win_uia.dispatch_cv);
        ReleaseSRWLockExclusive(&g_win_uia.dispatch_lock);
        return 1;
    }
    if (msg == WM_GETOBJECT && static_cast<LONG>(lParam) == static_cast<LONG>(UiaRootObjectId) &&
        slot && slot->ctx && slot->ctx->enable_os_a11y && ensure_win_uia_api())
    {
        if (!slot->root) {
            const uint32_t rid = id_index(id("etch_a11y_window"), SDL_GetWindowID(slot->window));
            slot->root = uia_get_or_create_node(slot, rid, true);
        }
        if (slot->root) {
            auto* root_simple = reinterpret_cast<IRawElementProviderSimple*>(&slot->root->simple);
            return g_win_uia.return_raw(hwnd, wParam, lParam, root_simple);
        }
    }
    if ((msg == WM_SETTINGCHANGE || msg == WM_THEMECHANGED) && slot && slot->ctx) {
        uia_refresh_reduced_motion(slot->ctx, slot->window, msg == WM_THEMECHANGED);
    }
    if ((msg == WM_DESTROY || msg == WM_NCDESTROY) && g_win_uia.ok && g_win_uia.return_raw) {
        g_win_uia.return_raw(hwnd, 0u, 0, nullptr);
    }
    if (msg == WM_NCDESTROY) {
        if (slot) slot->subclass_attached = false;
        RemoveWindowSubclass(hwnd, etch_uia_subclass_proc, uIdSubclass);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

#elif defined(ETCH_USE_ACCESSKIT) && ETCH_ENABLE_OS_A11Y

struct AccessKitSlot {
    Context* ctx;
    ClaimedWindowSlot* claimed;
    SDL_Window* window;
    void* adapter;
    uint32_t root_id;
    uint64_t prev_struct_hash;
    uint32_t prev_focus_id;
    bool initialized;
};

accesskit_role etch_to_accesskit_role(A11yRole role) noexcept {
    switch (role) {
        case A11yRole::Window:      return ACCESSKIT_ROLE_WINDOW;
        case A11yRole::Group:       return ACCESSKIT_ROLE_GENERIC_CONTAINER;
        case A11yRole::Region:      return ACCESSKIT_ROLE_REGION;
        case A11yRole::Toolbar:     return ACCESSKIT_ROLE_TOOLBAR;
        case A11yRole::MenuBar:     return ACCESSKIT_ROLE_MENU_BAR;
        case A11yRole::Menu:        return ACCESSKIT_ROLE_MENU;
        case A11yRole::MenuItem:    return ACCESSKIT_ROLE_MENU_ITEM;
        case A11yRole::TabList:     return ACCESSKIT_ROLE_TAB_LIST;
        case A11yRole::Tab:         return ACCESSKIT_ROLE_TAB;
        case A11yRole::Dialog:      return ACCESSKIT_ROLE_DIALOG;
        case A11yRole::ScrollArea:  return ACCESSKIT_ROLE_SCROLL_VIEW;
        case A11yRole::List:        return ACCESSKIT_ROLE_LIST;
        case A11yRole::ListItem:    return ACCESSKIT_ROLE_LIST_ITEM;
        case A11yRole::Table:       return ACCESSKIT_ROLE_TABLE;
        case A11yRole::Tree:        return ACCESSKIT_ROLE_TREE;
        case A11yRole::TreeItem:    return ACCESSKIT_ROLE_TREE_ITEM;
        case A11yRole::StaticText:  return ACCESSKIT_ROLE_LABEL;
        case A11yRole::Button:      return ACCESSKIT_ROLE_BUTTON;
        case A11yRole::Checkbox:    return ACCESSKIT_ROLE_CHECK_BOX;
        case A11yRole::Switch:      return ACCESSKIT_ROLE_SWITCH;
        case A11yRole::Slider:      return ACCESSKIT_ROLE_SLIDER;
        case A11yRole::SpinButton:  return ACCESSKIT_ROLE_SPIN_BUTTON;
        case A11yRole::ProgressBar: return ACCESSKIT_ROLE_PROGRESS_INDICATOR;
        case A11yRole::ComboBox:    return ACCESSKIT_ROLE_COMBO_BOX;
        case A11yRole::TextInput:   return ACCESSKIT_ROLE_TEXT_INPUT;
        case A11yRole::CodeEditor:  return ACCESSKIT_ROLE_MULTILINE_TEXT_INPUT;
        case A11yRole::Canvas:      return ACCESSKIT_ROLE_CANVAS;
        case A11yRole::Tooltip:     return ACCESSKIT_ROLE_TOOLTIP;
        default:                    return ACCESSKIT_ROLE_UNKNOWN;
    }
}

accesskit_tree_update* etch_accesskit_build_update(AccessKitSlot* ak) noexcept {
    if (!ak || !ak->ctx || !ak->window) return nullptr;
    const auto span = frame_a11y_nodes(ak->ctx, ak->window);
    if (span.empty()) return nullptr;
    const uint32_t count = static_cast<uint32_t>(span.size());
    const WindowUiState* ui = ak->claimed ? &ak->claimed->ui : window_ui(ak->ctx, ak->window);
    const accesskit_node_id root_id = static_cast<accesskit_node_id>(span[0].id);
    accesskit_node_id focus_id = root_id;
    if (ui && ui->focus_id != 0u) {
        for (uint32_t i = 0u; i < count; ++i) {
            if (span[i].id == ui->focus_id) {
                focus_id = static_cast<accesskit_node_id>(ui->focus_id);
                break;
            }
        }
    }
    accesskit_tree_update* update = accesskit_tree_update_with_capacity_and_focus(count, focus_id);
    if (!update) return nullptr;
    if (!ak->initialized || ak->root_id != span[0].id) {
        accesskit_tree_update_set_tree(update, accesskit_tree_new(root_id));
        ak->root_id = span[0].id;
        ak->initialized = true;
    }

    accesskit_node_id stack_children[128];
    for (uint32_t i = 0u; i < count; ++i) {
        const A11yNode& an = span[i];
        accesskit_node_builder* b = accesskit_node_builder_new(etch_to_accesskit_role(an.role));
        if (!b) continue;

        const accesskit_rect rect = {
            static_cast<double>(an.bounds.x),
            static_cast<double>(an.bounds.y),
            static_cast<double>(an.bounds.x + an.bounds.w),
            static_cast<double>(an.bounds.y + an.bounds.h),
        };
        accesskit_node_builder_set_bounds(b, rect);

        if (an.label.chars && an.label.length > 0u) {
            accesskit_node_builder_set_name_with_length(b, an.label.chars, an.label.length);
        }
        if (an.value_text.chars && an.value_text.length > 0u) {
            accesskit_node_builder_set_value_with_length(b, an.value_text.chars, an.value_text.length);
        }
        if (an.role == A11yRole::Slider || an.role == A11yRole::SpinButton || an.role == A11yRole::ProgressBar) {
            accesskit_node_builder_set_numeric_value(b, static_cast<double>(an.value_num));
            accesskit_node_builder_set_min_numeric_value(b, static_cast<double>(an.min_value));
            accesskit_node_builder_set_max_numeric_value(b, static_cast<double>(an.max_value));
        }
        if (an.role == A11yRole::Checkbox || an.role == A11yRole::Switch) {
            const accesskit_toggled t = ((an.state_flags & A11yState_Checked) != 0u)
                ? ACCESSKIT_TOGGLED_TRUE : ACCESSKIT_TOGGLED_FALSE;
            accesskit_node_builder_set_toggled(b, t);
        }
        if ((an.actions_mask & ((1u << static_cast<uint32_t>(A11yAction::Expand)) |
                                (1u << static_cast<uint32_t>(A11yAction::Collapse)))) != 0u)
        {
            accesskit_node_builder_set_expanded(b, (an.state_flags & A11yState_Expanded) != 0u);
        }
        if ((an.state_flags & A11yState_Selected) != 0u) {
            accesskit_node_builder_set_selected(b, true);
        }
        if ((an.state_flags & A11yState_Disabled) != 0u) {
            accesskit_node_builder_set_disabled(b);
        }
        if ((an.actions_mask & (1u << static_cast<uint32_t>(A11yAction::Press))) != 0u) {
            accesskit_node_builder_add_action(b, ACCESSKIT_ACTION_DEFAULT);
        }
        if ((an.actions_mask & (1u << static_cast<uint32_t>(A11yAction::Focus))) != 0u) {
            accesskit_node_builder_add_action(b, ACCESSKIT_ACTION_FOCUS);
        }
        if ((an.actions_mask & (1u << static_cast<uint32_t>(A11yAction::Increment))) != 0u) {
            accesskit_node_builder_add_action(b, ACCESSKIT_ACTION_INCREMENT);
        }
        if ((an.actions_mask & (1u << static_cast<uint32_t>(A11yAction::Decrement))) != 0u) {
            accesskit_node_builder_add_action(b, ACCESSKIT_ACTION_DECREMENT);
        }
        if ((an.actions_mask & (1u << static_cast<uint32_t>(A11yAction::SetValue))) != 0u) {
            accesskit_node_builder_add_action(b, ACCESSKIT_ACTION_SET_VALUE);
        }
        if ((an.actions_mask & (1u << static_cast<uint32_t>(A11yAction::Expand))) != 0u) {
            accesskit_node_builder_add_action(b, ACCESSKIT_ACTION_EXPAND);
        }
        if ((an.actions_mask & (1u << static_cast<uint32_t>(A11yAction::Collapse))) != 0u) {
            accesskit_node_builder_add_action(b, ACCESSKIT_ACTION_COLLAPSE);
        }

        uint32_t child_count = 0u;
        for (uint32_t c = an.first_child_idx; c != 0xFFFFFFFFu && c < count; c = span[c].next_sibling_idx) {
            ++child_count;
            if (child_count > count) break;
        }
        if (child_count > 0u) {
            accesskit_node_id* child_ids = (child_count <= 128u)
                ? stack_children
                : static_cast<accesskit_node_id*>(std::malloc(static_cast<size_t>(child_count) * sizeof(accesskit_node_id)));
            if (child_ids) {
                uint32_t ci = 0u;
                for (uint32_t c = an.first_child_idx; c != 0xFFFFFFFFu && c < count && ci < child_count; c = span[c].next_sibling_idx) {
                    child_ids[ci++] = static_cast<accesskit_node_id>(span[c].id);
                }
                accesskit_node_builder_set_children(b, ci, child_ids);
                if (child_ids != stack_children) {
                    std::free(child_ids);
                }
            }
        }
        accesskit_tree_update_push_node(update, static_cast<accesskit_node_id>(an.id), accesskit_node_builder_build(b));
    }
    return update;
}

accesskit_tree_update* etch_accesskit_initial_tree_cb(void* userdata) noexcept {
    return etch_accesskit_build_update(static_cast<AccessKitSlot*>(userdata));
}

void etch_accesskit_action_cb(accesskit_action_request* req, void* userdata) noexcept {
    auto* ak = static_cast<AccessKitSlot*>(userdata);
    if (!req) return;
    if (ak && ak->ctx && ak->window) {
        const uint32_t elem_id = static_cast<uint32_t>(req->target);
        switch (req->action) {
            case ACCESSKIT_ACTION_DEFAULT:
                perform_a11y_action(elem_id, A11yAction::Press, 0.0f, {}, ak->ctx, ak->window);
                break;
            case ACCESSKIT_ACTION_FOCUS:
                perform_a11y_action(elem_id, A11yAction::Focus, 0.0f, {}, ak->ctx, ak->window);
                break;
            case ACCESSKIT_ACTION_INCREMENT:
            case ACCESSKIT_ACTION_SCROLL_UP:
            case ACCESSKIT_ACTION_SCROLL_LEFT:
                perform_a11y_action(elem_id, A11yAction::Increment, 0.0f, {}, ak->ctx, ak->window);
                break;
            case ACCESSKIT_ACTION_DECREMENT:
            case ACCESSKIT_ACTION_SCROLL_DOWN:
            case ACCESSKIT_ACTION_SCROLL_RIGHT:
                perform_a11y_action(elem_id, A11yAction::Decrement, 0.0f, {}, ak->ctx, ak->window);
                break;
            case ACCESSKIT_ACTION_EXPAND:
                perform_a11y_action(elem_id, A11yAction::Expand, 0.0f, {}, ak->ctx, ak->window);
                break;
            case ACCESSKIT_ACTION_COLLAPSE:
                perform_a11y_action(elem_id, A11yAction::Collapse, 0.0f, {}, ak->ctx, ak->window);
                break;
            case ACCESSKIT_ACTION_SET_VALUE:
                if (req->data.has_value && req->data.value.tag == ACCESSKIT_ACTION_DATA_VALUE && req->data.value.value) {
                    const char* s = req->data.value.value;
                    perform_a11y_action(elem_id, A11yAction::SetValue, 0.0f, Str{s, static_cast<uint32_t>(std::strlen(s))}, ak->ctx, ak->window);
                } else if (req->data.has_value && req->data.value.tag == ACCESSKIT_ACTION_DATA_NUMERIC_VALUE) {
                    perform_a11y_action(elem_id, A11yAction::SetValue, static_cast<float>(req->data.value.numeric_value), {}, ak->ctx, ak->window);
                }
                break;
            default:
                break;
        }
    }
    accesskit_action_request_free(req);
}

void etch_accesskit_deactivate_cb(void* userdata) noexcept {
    auto* ak = static_cast<AccessKitSlot*>(userdata);
    if (ak) ak->initialized = false;
}

void accesskit_attach_window(Context* ctx, ClaimedWindowSlot* slot) noexcept {
    if (!ctx || !slot || !slot->window || ctx->headless || !ctx->enable_os_a11y || slot->os_a11y != nullptr) return;
    auto* ak = static_cast<AccessKitSlot*>(std::calloc(1u, sizeof(AccessKitSlot)));
    if (!ak) return;
    ak->ctx = ctx;
    ak->claimed = slot;
    ak->window = slot->window;
#if defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX
    void* nswin = SDL_GetPointerProperty(SDL_GetWindowProperties(slot->window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (nswin) {
        ak->adapter = accesskit_macos_subclassing_adapter_for_window(
            nswin, etch_accesskit_initial_tree_cb, ak, etch_accesskit_action_cb, ak);
    }
#elif defined(__linux__)
    ak->adapter = accesskit_unix_adapter_new(
        etch_accesskit_initial_tree_cb, ak, etch_accesskit_action_cb, ak, etch_accesskit_deactivate_cb, ak);
#endif
    if (!ak->adapter) {
        std::free(ak);
        return;
    }
    slot->os_a11y = ak;
}

void accesskit_detach_window(ClaimedWindowSlot* slot) noexcept {
    if (!slot || !slot->os_a11y) return;
    auto* ak = static_cast<AccessKitSlot*>(slot->os_a11y);
    slot->os_a11y = nullptr;
    if (ak->adapter) {
#if defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX
        accesskit_macos_subclassing_adapter_free(static_cast<accesskit_macos_subclassing_adapter*>(ak->adapter));
#elif defined(__linux__)
        accesskit_unix_adapter_free(static_cast<accesskit_unix_adapter*>(ak->adapter));
#endif
    }
    std::free(ak);
}

void accesskit_notify_commit(Context* ctx, ClaimedWindowSlot* slot) noexcept {
    if (!ctx || !slot || !slot->os_a11y) return;
    auto* ak = static_cast<AccessKitSlot*>(slot->os_a11y);
    if (!ak->adapter) return;
#if defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX
    accesskit_macos_queued_events* events = accesskit_macos_subclassing_adapter_update_if_active(
        static_cast<accesskit_macos_subclassing_adapter*>(ak->adapter), etch_accesskit_initial_tree_cb, ak);
    if (events) {
        accesskit_macos_queued_events_raise(events);
    }
#elif defined(__linux__)
    accesskit_unix_adapter_update_if_active(
        static_cast<accesskit_unix_adapter*>(ak->adapter), etch_accesskit_initial_tree_cb, ak);
#endif
}

#elif defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX && ETCH_ENABLE_OS_A11Y

struct MacOsA11ySlot;

struct MacOsA11yItem {
    MacOsA11ySlot* slot;
    ::id ns_elem;
    uint32_t element_id;
};

struct MacOsA11ySlot {
    Context* ctx;
    ClaimedWindowSlot* claimed;
    SDL_Window* window;
    ::id ns_window;
    ::id content_view;
    MacOsA11yItem* items;
    uint32_t item_count;
    uint32_t item_cap;
    uint64_t prev_struct_hash;
    uint32_t prev_focus_id;
    bool has_prev_snapshot;
};

struct MacOsA11yRuntime {
    bool init;
    bool ok;
    Class elem_cls;
    void (*post_notification)(::id, ::id);
    ::id notif_focus_changed;
    ::id notif_layout_changed;
    ::id notif_value_changed;
};

MacOsA11yRuntime g_macos_a11y = {};
const char kMacOsA11yAssocKey = 0;

::id macos_nsstring(const char* utf8, uint32_t len) noexcept {
    if (!utf8 || len == 0u) return nullptr;
    Class ns_str = objc_getClass("NSString");
    if (!ns_str) return nullptr;
    using AllocFn = ::id (*)(Class, SEL);
    using InitBytesFn = ::id (*)(::id, SEL, const void*, unsigned long, unsigned long);
    using AutoreleaseFn = ::id (*)(::id, SEL);
    ::id s = reinterpret_cast<AllocFn>(objc_msgSend)(ns_str, sel_registerName("alloc"));
    if (!s) return nullptr;
    constexpr unsigned long kNSUTF8StringEncoding = 4UL;
    s = reinterpret_cast<InitBytesFn>(objc_msgSend)(
        s, sel_registerName("initWithBytes:length:encoding:"), utf8, static_cast<unsigned long>(len), kNSUTF8StringEncoding);
    if (!s) return nullptr;
    return reinterpret_cast<AutoreleaseFn>(objc_msgSend)(s, sel_registerName("autorelease"));
}

::id macos_nsstring_cstr(const char* cstr) noexcept {
    return cstr ? macos_nsstring(cstr, static_cast<uint32_t>(std::strlen(cstr))) : nullptr;
}

CGRect macos_msg_rect(::id obj, SEL sel) noexcept {
    if (!obj) return CGRect{{0.0, 0.0}, {0.0, 0.0}};
#if defined(__aarch64__)
    using GetRectFn = CGRect (*)(::id, SEL);
    return reinterpret_cast<GetRectFn>(objc_msgSend)(obj, sel);
#else
    using GetRectStretFn = void (*)(CGRect*, ::id, SEL);
    CGRect out = {{0.0, 0.0}, {0.0, 0.0}};
    reinterpret_cast<GetRectStretFn>(objc_msgSend_stret)(&out, obj, sel);
    return out;
#endif
}

const char* etch_to_ns_a11y_role(A11yRole role) noexcept {
    switch (role) {
        case A11yRole::Window:      return "AXWindow";
        case A11yRole::Group:       return "AXGroup";
        case A11yRole::Region:      return "AXGroup";
        case A11yRole::Toolbar:     return "AXToolbar";
        case A11yRole::MenuBar:     return "AXMenuBar";
        case A11yRole::Menu:        return "AXMenu";
        case A11yRole::MenuItem:    return "AXMenuItem";
        case A11yRole::TabList:     return "AXTabGroup";
        case A11yRole::Tab:         return "AXRadioButton";
        case A11yRole::Dialog:      return "AXSheet";
        case A11yRole::ScrollArea:  return "AXScrollArea";
        case A11yRole::List:        return "AXList";
        case A11yRole::ListItem:    return "AXRow";
        case A11yRole::Table:       return "AXTable";
        case A11yRole::Tree:        return "AXOutline";
        case A11yRole::TreeItem:    return "AXRow";
        case A11yRole::StaticText:  return "AXStaticText";
        case A11yRole::Button:      return "AXButton";
        case A11yRole::Checkbox:    return "AXCheckBox";
        case A11yRole::Switch:      return "AXCheckBox";
        case A11yRole::Slider:      return "AXSlider";
        case A11yRole::SpinButton:  return "AXIncrementor";
        case A11yRole::ProgressBar: return "AXProgressIndicator";
        case A11yRole::ComboBox:    return "AXPopUpButton";
        case A11yRole::TextInput:   return "AXTextField";
        case A11yRole::CodeEditor:  return "AXTextArea";
        case A11yRole::Canvas:      return "AXImage";
        case A11yRole::Tooltip:     return "AXHelpTag";
        default:                    return "AXUnknown";
    }
}

MacOsA11yItem* macos_a11y_get_item(::id self) noexcept {
    return static_cast<MacOsA11yItem*>(objc_getAssociatedObject(self, &kMacOsA11yAssocKey));
}

const A11yNode* macos_a11y_live_node(MacOsA11yItem* item) noexcept {
    if (!item || !item->slot || !item->slot->ctx || !item->slot->window) return nullptr;
    return find_a11y_node(item->element_id, item->slot->ctx, item->slot->window);
}

signed char macos_a11y_is_elem(::id self, SEL) noexcept {
    MacOsA11yItem* item = macos_a11y_get_item(self);
    const A11yNode* an = macos_a11y_live_node(item);
    return (an != nullptr) ? static_cast<signed char>(1) : static_cast<signed char>(0);
}

signed char macos_a11y_is_enabled(::id self, SEL) noexcept {
    const A11yNode* an = macos_a11y_live_node(macos_a11y_get_item(self));
    return (an && (an->state_flags & A11yState_Disabled) == 0u) ? static_cast<signed char>(1) : static_cast<signed char>(0);
}

signed char macos_a11y_is_focused(::id self, SEL) noexcept {
    const A11yNode* an = macos_a11y_live_node(macos_a11y_get_item(self));
    return (an && (an->state_flags & A11yState_Focused) != 0u) ? static_cast<signed char>(1) : static_cast<signed char>(0);
}

::id macos_a11y_role(::id self, SEL) noexcept {
    const A11yNode* an = macos_a11y_live_node(macos_a11y_get_item(self));
    return macos_nsstring_cstr(an ? etch_to_ns_a11y_role(an->role) : "AXUnknown");
}

::id macos_a11y_label(::id self, SEL) noexcept {
    const A11yNode* an = macos_a11y_live_node(macos_a11y_get_item(self));
    if (!an) return nullptr;
    return macos_nsstring(an->label.chars, an->label.length);
}

::id macos_a11y_value(::id self, SEL) noexcept {
    const A11yNode* an = macos_a11y_live_node(macos_a11y_get_item(self));
    if (!an) return nullptr;
    if (an->value_text.chars && an->value_text.length > 0u) {
        return macos_nsstring(an->value_text.chars, an->value_text.length);
    }
    if (an->role == A11yRole::Checkbox || an->role == A11yRole::Switch) {
        Class ns_num = objc_getClass("NSNumber");
        using NumIntFn = ::id (*)(Class, SEL, int);
        const int v = ((an->state_flags & A11yState_Checked) != 0u) ? 1 : 0;
        return ns_num ? reinterpret_cast<NumIntFn>(objc_msgSend)(ns_num, sel_registerName("numberWithInt:"), v) : nullptr;
    }
    if (an->role == A11yRole::Slider || an->role == A11yRole::SpinButton || an->role == A11yRole::ProgressBar) {
        Class ns_num = objc_getClass("NSNumber");
        using NumDoubleFn = ::id (*)(Class, SEL, double);
        return ns_num ? reinterpret_cast<NumDoubleFn>(objc_msgSend)(ns_num, sel_registerName("numberWithDouble:"), static_cast<double>(an->value_num)) : nullptr;
    }
    return nullptr;
}

::id macos_a11y_identifier(::id self, SEL) noexcept {
    MacOsA11yItem* item = macos_a11y_get_item(self);
    if (!item) return nullptr;
    char buf[24];
    const int n = std::snprintf(buf, sizeof(buf), "etch_%08X", item->element_id);
    return (n > 0) ? macos_nsstring(buf, static_cast<uint32_t>(n)) : nullptr;
}

::id macos_a11y_parent(::id self, SEL) noexcept {
    MacOsA11yItem* item = macos_a11y_get_item(self);
    return (item && item->slot) ? item->slot->content_view : nullptr;
}

CGRect macos_a11y_frame(::id self, SEL) noexcept {
    MacOsA11yItem* item = macos_a11y_get_item(self);
    const A11yNode* an = macos_a11y_live_node(item);
    if (!an || !item || !item->slot || !item->slot->ns_window || !item->slot->content_view) {
        return CGRect{{0.0, 0.0}, {0.0, 0.0}};
    }
    const CGRect view_bounds = macos_msg_rect(item->slot->content_view, sel_registerName("bounds"));
    const CGRect local = {
        {static_cast<double>(an->bounds.x), view_bounds.size.height - static_cast<double>(an->bounds.y + an->bounds.h)},
        {static_cast<double>(an->bounds.w), static_cast<double>(an->bounds.h)},
    };
#if defined(__aarch64__)
    using ConvertRectFn = CGRect (*)(::id, SEL, CGRect);
    return reinterpret_cast<ConvertRectFn>(objc_msgSend)(item->slot->ns_window, sel_registerName("convertRectToScreen:"), local);
#else
    using ConvertRectStretFn = void (*)(CGRect*, ::id, SEL, CGRect);
    CGRect screen = {{0.0, 0.0}, {0.0, 0.0}};
    reinterpret_cast<ConvertRectStretFn>(objc_msgSend_stret)(&screen, item->slot->ns_window, sel_registerName("convertRectToScreen:"), local);
    return screen;
#endif
}

signed char macos_a11y_perform_press(::id self, SEL) noexcept {
    MacOsA11yItem* item = macos_a11y_get_item(self);
    if (!item || !item->slot || !item->slot->ctx || !item->slot->window) return 0;
    return perform_a11y_action(item->element_id, A11yAction::Press, 0.0f, {}, item->slot->ctx, item->slot->window) ? 1 : 0;
}

bool ensure_macos_a11y_runtime() noexcept {
    if (g_macos_a11y.init) return g_macos_a11y.ok;
    g_macos_a11y.init = true;
    Class super_cls = objc_getClass("NSObject");
    if (!super_cls) return false;
    Class cls = objc_getClass("EtchNSAccessibilityElement");
    if (!cls) {
        cls = objc_allocateClassPair(super_cls, "EtchNSAccessibilityElement", 0u);
        if (!cls) return false;
        class_addMethod(cls, sel_registerName("isAccessibilityElement"), reinterpret_cast<IMP>(macos_a11y_is_elem), "c@:");
        class_addMethod(cls, sel_registerName("isAccessibilityEnabled"), reinterpret_cast<IMP>(macos_a11y_is_enabled), "c@:");
        class_addMethod(cls, sel_registerName("isAccessibilityFocused"), reinterpret_cast<IMP>(macos_a11y_is_focused), "c@:");
        class_addMethod(cls, sel_registerName("accessibilityRole"), reinterpret_cast<IMP>(macos_a11y_role), "@@:");
        class_addMethod(cls, sel_registerName("accessibilityLabel"), reinterpret_cast<IMP>(macos_a11y_label), "@@:");
        class_addMethod(cls, sel_registerName("accessibilityValue"), reinterpret_cast<IMP>(macos_a11y_value), "@@:");
        class_addMethod(cls, sel_registerName("accessibilityIdentifier"), reinterpret_cast<IMP>(macos_a11y_identifier), "@@:");
        class_addMethod(cls, sel_registerName("accessibilityParent"), reinterpret_cast<IMP>(macos_a11y_parent), "@@:");
        class_addMethod(cls, sel_registerName("accessibilityFrame"), reinterpret_cast<IMP>(macos_a11y_frame), "{CGRect={CGPoint=dd}{CGSize=dd}}@:");
        class_addMethod(cls, sel_registerName("accessibilityPerformPress"), reinterpret_cast<IMP>(macos_a11y_perform_press), "c@:");
        objc_registerClassPair(cls);
    }
    g_macos_a11y.elem_cls = cls;
    g_macos_a11y.post_notification = reinterpret_cast<void (*)(::id, ::id)>(dlsym(RTLD_DEFAULT, "NSAccessibilityPostNotification"));
    auto load_const_id = [](const char* sym) noexcept -> ::id {
        ::id* p = static_cast<::id*>(dlsym(RTLD_DEFAULT, sym));
        return p ? *p : nullptr;
    };
    g_macos_a11y.notif_focus_changed = load_const_id("NSAccessibilityFocusedUIElementChangedNotification");
    g_macos_a11y.notif_layout_changed = load_const_id("NSAccessibilityLayoutChangedNotification");
    g_macos_a11y.notif_value_changed = load_const_id("NSAccessibilityValueChangedNotification");
    g_macos_a11y.ok = (g_macos_a11y.elem_cls != nullptr);
    return g_macos_a11y.ok;
}

void macos_a11y_clear_items(MacOsA11ySlot* mac) noexcept {
    if (!mac) return;
    using ReleaseFn = void (*)(::id, SEL);
    const SEL sel_rel = sel_registerName("release");
    for (uint32_t i = 0u; i < mac->item_count; ++i) {
        if (mac->items[i].ns_elem) {
            objc_setAssociatedObject(mac->items[i].ns_elem, &kMacOsA11yAssocKey, nullptr, OBJC_ASSOCIATION_ASSIGN);
            reinterpret_cast<ReleaseFn>(objc_msgSend)(mac->items[i].ns_elem, sel_rel);
            mac->items[i].ns_elem = nullptr;
        }
    }
    mac->item_count = 0u;
}

void macos_a11y_attach_window(Context* ctx, ClaimedWindowSlot* slot) noexcept {
    if (!ctx || !slot || !slot->window || ctx->headless || !ctx->enable_os_a11y || slot->os_a11y != nullptr) return;
    if (!ensure_macos_a11y_runtime()) return;
    void* nswin = SDL_GetPointerProperty(SDL_GetWindowProperties(slot->window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!nswin) return;
    using GetIdFn = ::id (*)(::id, SEL);
    ::id win_obj = reinterpret_cast<::id>(nswin);
    ::id view_obj = reinterpret_cast<GetIdFn>(objc_msgSend)(win_obj, sel_registerName("contentView"));
    if (!view_obj) return;
    auto* mac = static_cast<MacOsA11ySlot*>(std::calloc(1u, sizeof(MacOsA11ySlot)));
    if (!mac) return;
    mac->ctx = ctx;
    mac->claimed = slot;
    mac->window = slot->window;
    mac->ns_window = win_obj;
    mac->content_view = view_obj;
    slot->os_a11y = mac;
}

void macos_a11y_detach_window(ClaimedWindowSlot* slot) noexcept {
    if (!slot || !slot->os_a11y) return;
    auto* mac = static_cast<MacOsA11ySlot*>(slot->os_a11y);
    slot->os_a11y = nullptr;
    if (mac->content_view) {
        using SetIdFn = void (*)(::id, SEL, ::id);
        reinterpret_cast<SetIdFn>(objc_msgSend)(mac->content_view, sel_registerName("setAccessibilityChildren:"), nullptr);
    }
    macos_a11y_clear_items(mac);
    std::free(mac->items);
    std::free(mac);
}

void macos_a11y_notify_commit(Context* ctx, ClaimedWindowSlot* slot) noexcept {
    if (!ctx || !slot || !slot->os_a11y) return;
    auto* mac = static_cast<MacOsA11ySlot*>(slot->os_a11y);
    const auto span = frame_a11y_nodes(ctx, slot->window);
    const uint32_t count = static_cast<uint32_t>(span.size());

    uint64_t sh = 14695981039346656037ULL ^ count;
    sh *= 1099511628211ULL;
    for (uint32_t i = 0u; i < count; ++i) {
        sh ^= span[i].id;
        sh *= 1099511628211ULL;
        sh ^= static_cast<uint8_t>(span[i].role);
        sh *= 1099511628211ULL;
    }

    if (!mac->has_prev_snapshot || sh != mac->prev_struct_hash) {
        macos_a11y_clear_items(mac);
        if (count > mac->item_cap) {
            auto* next = static_cast<MacOsA11yItem*>(std::calloc(count, sizeof(MacOsA11yItem)));
            if (!next) return;
            std::free(mac->items);
            mac->items = next;
            mac->item_cap = count;
        }
        Class ns_marr = objc_getClass("NSMutableArray");
        using ArrCapFn = ::id (*)(Class, SEL, unsigned long);
        using AddObjFn = void (*)(::id, SEL, ::id);
        using AllocClsFn = ::id (*)(Class, SEL);
        using InitIdFn = ::id (*)(::id, SEL);
        using SetIdFn = void (*)(::id, SEL, ::id);
        ::id arr = ns_marr ? reinterpret_cast<ArrCapFn>(objc_msgSend)(ns_marr, sel_registerName("arrayWithCapacity:"), count) : nullptr;
        for (uint32_t i = 1u; i < count; ++i) {
            const A11yNode& an = span[i];
            if (an.role == A11yRole::Group && an.label.length == 0u && an.value_text.length == 0u && an.actions_mask == 0u) {
                continue;
            }
            ::id obj = reinterpret_cast<AllocClsFn>(objc_msgSend)(g_macos_a11y.elem_cls, sel_registerName("alloc"));
            if (!obj) continue;
            obj = reinterpret_cast<InitIdFn>(objc_msgSend)(obj, sel_registerName("init"));
            if (!obj) continue;
            MacOsA11yItem& it = mac->items[mac->item_count++];
            it.slot = mac;
            it.ns_elem = obj;
            it.element_id = an.id;
            objc_setAssociatedObject(obj, &kMacOsA11yAssocKey, reinterpret_cast<::id>(&it), OBJC_ASSOCIATION_ASSIGN);
            if (arr) {
                reinterpret_cast<AddObjFn>(objc_msgSend)(arr, sel_registerName("addObject:"), obj);
            }
        }
        if (mac->content_view && arr) {
            reinterpret_cast<SetIdFn>(objc_msgSend)(mac->content_view, sel_registerName("setAccessibilityChildren:"), arr);
        }
        if (mac->has_prev_snapshot && g_macos_a11y.post_notification && g_macos_a11y.notif_layout_changed) {
            g_macos_a11y.post_notification(mac->content_view, g_macos_a11y.notif_layout_changed);
        }
        mac->prev_struct_hash = sh;
        mac->has_prev_snapshot = true;
    }

    const uint32_t focus_id = slot->ui.focus_id;
    if (focus_id != mac->prev_focus_id) {
        mac->prev_focus_id = focus_id;
        if (focus_id != 0u && g_macos_a11y.post_notification && g_macos_a11y.notif_focus_changed) {
            for (uint32_t i = 0u; i < mac->item_count; ++i) {
                if (mac->items[i].element_id == focus_id && mac->items[i].ns_elem) {
                    g_macos_a11y.post_notification(mac->items[i].ns_elem, g_macos_a11y.notif_focus_changed);
                    break;
                }
            }
        }
    }
}

#elif defined(__linux__) && ETCH_ENABLE_OS_A11Y

struct DBusConnection;
struct DBusMessage;

struct DBusError {
    const char* name;
    const char* message;
    unsigned int dummy1 : 1;
    unsigned int dummy2 : 1;
    unsigned int dummy3 : 1;
    unsigned int dummy4 : 1;
    unsigned int dummy5 : 1;
    void* padding1;
};

struct DBusMessageIter {
    void* dummy1;
    void* dummy2;
    uint32_t dummy3;
    int dummy4;
    int dummy5;
    int dummy6;
    int dummy7;
    int dummy8;
    int dummy9;
    int dummy10;
    int dummy11;
    int pad1;
    void* pad2;
    void* pad3;
};

enum DBusHandlerResult : int {
    DBUS_HANDLER_RESULT_HANDLED = 0,
    DBUS_HANDLER_RESULT_NOT_YET_HANDLED = 1,
    DBUS_HANDLER_RESULT_NEED_MEMORY = 2,
};

using DBusHandleMessageFn = DBusHandlerResult (*)(DBusConnection*, DBusMessage*, void*);

struct LinuxDBusApi {
    void* so;
    bool loaded;
    bool ok;
    void (*error_init)(DBusError*);
    int (*error_is_set)(const DBusError*);
    void (*error_free)(DBusError*);
    DBusConnection* (*bus_get)(int, DBusError*);
    DBusConnection* (*conn_open)(const char*, DBusError*);
    int (*bus_register)(DBusConnection*, DBusError*);
    void (*conn_unref)(DBusConnection*);
    int (*conn_send)(DBusConnection*, DBusMessage*, uint32_t*);
    DBusMessage* (*conn_send_block)(DBusConnection*, DBusMessage*, int, DBusError*);
    void (*conn_flush)(DBusConnection*);
    int (*conn_dispatch)(DBusConnection*, int);
    int (*conn_add_filter)(DBusConnection*, DBusHandleMessageFn, void*, void (*)(void*));
    void (*conn_remove_filter)(DBusConnection*, DBusHandleMessageFn, void*);
    DBusMessage* (*msg_new_call)(const char*, const char*, const char*, const char*);
    DBusMessage* (*msg_new_return)(DBusMessage*);
    DBusMessage* (*msg_new_signal)(const char*, const char*, const char*);
    void (*msg_unref)(DBusMessage*);
    int (*msg_iter_init)(DBusMessage*, DBusMessageIter*);
    void (*msg_iter_init_append)(DBusMessage*, DBusMessageIter*);
    int (*msg_iter_append_basic)(DBusMessageIter*, int, const void*);
    int (*msg_iter_open_container)(DBusMessageIter*, int, const char*, DBusMessageIter*);
    int (*msg_iter_close_container)(DBusMessageIter*, DBusMessageIter*);
    int (*msg_iter_get_arg_type)(DBusMessageIter*);
    void (*msg_iter_get_basic)(DBusMessageIter*, void*);
    int (*msg_is_call)(DBusMessage*, const char*, const char*);
    const char* (*msg_get_path)(DBusMessage*);
};

LinuxDBusApi g_linux_dbus = {};

struct LinuxAtSpiSlot {
    Context* ctx;
    ClaimedWindowSlot* claimed;
    SDL_Window* window;
    DBusConnection* a11y_conn;
    uint64_t prev_struct_hash;
    uint32_t prev_focus_id;
    bool has_prev_snapshot;
};

template <typename Fn>
Fn linux_load_sym(void* so, const char* name) noexcept {
    return reinterpret_cast<Fn>(dlsym(so, name));
}

bool ensure_linux_dbus_api() noexcept {
    if (g_linux_dbus.loaded) return g_linux_dbus.ok;
    g_linux_dbus.loaded = true;
    g_linux_dbus.so = dlopen("libdbus-1.so.3", RTLD_LAZY | RTLD_LOCAL);
    if (!g_linux_dbus.so) return false;
    g_linux_dbus.error_init = linux_load_sym<decltype(g_linux_dbus.error_init)>(g_linux_dbus.so, "dbus_error_init");
    g_linux_dbus.error_is_set = linux_load_sym<decltype(g_linux_dbus.error_is_set)>(g_linux_dbus.so, "dbus_error_is_set");
    g_linux_dbus.error_free = linux_load_sym<decltype(g_linux_dbus.error_free)>(g_linux_dbus.so, "dbus_error_free");
    g_linux_dbus.bus_get = linux_load_sym<decltype(g_linux_dbus.bus_get)>(g_linux_dbus.so, "dbus_bus_get");
    g_linux_dbus.conn_open = linux_load_sym<decltype(g_linux_dbus.conn_open)>(g_linux_dbus.so, "dbus_connection_open");
    g_linux_dbus.bus_register = linux_load_sym<decltype(g_linux_dbus.bus_register)>(g_linux_dbus.so, "dbus_bus_register");
    g_linux_dbus.conn_unref = linux_load_sym<decltype(g_linux_dbus.conn_unref)>(g_linux_dbus.so, "dbus_connection_unref");
    g_linux_dbus.conn_send = linux_load_sym<decltype(g_linux_dbus.conn_send)>(g_linux_dbus.so, "dbus_connection_send");
    g_linux_dbus.conn_send_block = linux_load_sym<decltype(g_linux_dbus.conn_send_block)>(g_linux_dbus.so, "dbus_connection_send_with_reply_and_block");
    g_linux_dbus.conn_flush = linux_load_sym<decltype(g_linux_dbus.conn_flush)>(g_linux_dbus.so, "dbus_connection_flush");
    g_linux_dbus.conn_dispatch = linux_load_sym<decltype(g_linux_dbus.conn_dispatch)>(g_linux_dbus.so, "dbus_connection_read_write_dispatch");
    g_linux_dbus.conn_add_filter = linux_load_sym<decltype(g_linux_dbus.conn_add_filter)>(g_linux_dbus.so, "dbus_connection_add_filter");
    g_linux_dbus.conn_remove_filter = linux_load_sym<decltype(g_linux_dbus.conn_remove_filter)>(g_linux_dbus.so, "dbus_connection_remove_filter");
    g_linux_dbus.msg_new_call = linux_load_sym<decltype(g_linux_dbus.msg_new_call)>(g_linux_dbus.so, "dbus_message_new_method_call");
    g_linux_dbus.msg_new_return = linux_load_sym<decltype(g_linux_dbus.msg_new_return)>(g_linux_dbus.so, "dbus_message_new_method_return");
    g_linux_dbus.msg_new_signal = linux_load_sym<decltype(g_linux_dbus.msg_new_signal)>(g_linux_dbus.so, "dbus_message_new_signal");
    g_linux_dbus.msg_unref = linux_load_sym<decltype(g_linux_dbus.msg_unref)>(g_linux_dbus.so, "dbus_message_unref");
    g_linux_dbus.msg_iter_init = linux_load_sym<decltype(g_linux_dbus.msg_iter_init)>(g_linux_dbus.so, "dbus_message_iter_init");
    g_linux_dbus.msg_iter_init_append = linux_load_sym<decltype(g_linux_dbus.msg_iter_init_append)>(g_linux_dbus.so, "dbus_message_iter_init_append");
    g_linux_dbus.msg_iter_append_basic = linux_load_sym<decltype(g_linux_dbus.msg_iter_append_basic)>(g_linux_dbus.so, "dbus_message_iter_append_basic");
    g_linux_dbus.msg_iter_open_container = linux_load_sym<decltype(g_linux_dbus.msg_iter_open_container)>(g_linux_dbus.so, "dbus_message_iter_open_container");
    g_linux_dbus.msg_iter_close_container = linux_load_sym<decltype(g_linux_dbus.msg_iter_close_container)>(g_linux_dbus.so, "dbus_message_iter_close_container");
    g_linux_dbus.msg_iter_get_arg_type = linux_load_sym<decltype(g_linux_dbus.msg_iter_get_arg_type)>(g_linux_dbus.so, "dbus_message_iter_get_arg_type");
    g_linux_dbus.msg_iter_get_basic = linux_load_sym<decltype(g_linux_dbus.msg_iter_get_basic)>(g_linux_dbus.so, "dbus_message_iter_get_basic");
    g_linux_dbus.msg_is_call = linux_load_sym<decltype(g_linux_dbus.msg_is_call)>(g_linux_dbus.so, "dbus_message_is_method_call");
    g_linux_dbus.msg_get_path = linux_load_sym<decltype(g_linux_dbus.msg_get_path)>(g_linux_dbus.so, "dbus_message_get_path");
    g_linux_dbus.ok = (g_linux_dbus.error_init && g_linux_dbus.error_is_set && g_linux_dbus.error_free &&
                       g_linux_dbus.bus_get && g_linux_dbus.conn_open && g_linux_dbus.bus_register &&
                       g_linux_dbus.conn_unref && g_linux_dbus.conn_send && g_linux_dbus.conn_send_block &&
                       g_linux_dbus.conn_flush && g_linux_dbus.conn_dispatch && g_linux_dbus.conn_add_filter &&
                       g_linux_dbus.conn_remove_filter && g_linux_dbus.msg_new_call && g_linux_dbus.msg_new_return &&
                       g_linux_dbus.msg_new_signal && g_linux_dbus.msg_unref && g_linux_dbus.msg_iter_init &&
                       g_linux_dbus.msg_iter_init_append && g_linux_dbus.msg_iter_append_basic &&
                       g_linux_dbus.msg_iter_open_container && g_linux_dbus.msg_iter_close_container &&
                       g_linux_dbus.msg_iter_get_arg_type && g_linux_dbus.msg_iter_get_basic &&
                       g_linux_dbus.msg_is_call && g_linux_dbus.msg_get_path);
    return g_linux_dbus.ok;
}

uint32_t etch_to_atspi_role(A11yRole role) noexcept {
    // Standard AT-SPI2 AtspiRole enum values.
    switch (role) {
        case A11yRole::Window:      return 23u; // ATSPI_ROLE_FRAME
        case A11yRole::Group:       return 39u; // ATSPI_ROLE_PANEL
        case A11yRole::Region:      return 39u; // ATSPI_ROLE_PANEL
        case A11yRole::Toolbar:     return 63u; // ATSPI_ROLE_TOOL_BAR
        case A11yRole::MenuBar:     return 33u; // ATSPI_ROLE_MENU_BAR
        case A11yRole::Menu:        return 32u; // ATSPI_ROLE_MENU
        case A11yRole::MenuItem:    return 34u; // ATSPI_ROLE_MENU_ITEM
        case A11yRole::TabList:     return 37u; // ATSPI_ROLE_PAGE_TAB_LIST
        case A11yRole::Tab:         return 36u; // ATSPI_ROLE_PAGE_TAB
        case A11yRole::Dialog:      return 16u; // ATSPI_ROLE_DIALOG
        case A11yRole::ScrollArea:  return 49u; // ATSPI_ROLE_SCROLL_PANE
        case A11yRole::List:        return 30u; // ATSPI_ROLE_LIST
        case A11yRole::ListItem:    return 31u; // ATSPI_ROLE_LIST_ITEM
        case A11yRole::Table:       return 55u; // ATSPI_ROLE_TABLE
        case A11yRole::Tree:        return 65u; // ATSPI_ROLE_TREE
        case A11yRole::TreeItem:    return 66u; // ATSPI_ROLE_TREE_ITEM
        case A11yRole::StaticText:  return 29u; // ATSPI_ROLE_LABEL
        case A11yRole::Button:      return 43u; // ATSPI_ROLE_PUSH_BUTTON
        case A11yRole::Checkbox:    return 7u;  // ATSPI_ROLE_CHECK_BOX
        case A11yRole::Switch:      return 62u; // ATSPI_ROLE_TOGGLE_BUTTON
        case A11yRole::Slider:      return 51u; // ATSPI_ROLE_SLIDER
        case A11yRole::SpinButton:  return 52u; // ATSPI_ROLE_SPIN_BUTTON
        case A11yRole::ProgressBar: return 42u; // ATSPI_ROLE_PROGRESS_BAR
        case A11yRole::ComboBox:    return 11u; // ATSPI_ROLE_COMBO_BOX
        case A11yRole::TextInput:   return 61u; // ATSPI_ROLE_TEXT
        case A11yRole::CodeEditor:  return 61u; // ATSPI_ROLE_TEXT
        case A11yRole::Canvas:      return 6u;  // ATSPI_ROLE_CANVAS
        case A11yRole::Tooltip:     return 64u; // ATSPI_ROLE_TOOL_TIP
        default:                    return 0u;  // ATSPI_ROLE_INVALID
    }
}

void linux_atspi_emit_object_signal(
    DBusConnection* conn,
    uint32_t elem_id,
    const char* member,
    const char* detail,
    int32_t detail1,
    int32_t detail2) noexcept
{
    if (!conn || !g_linux_dbus.ok) return;
    char path[64];
    std::snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/etch_%08X", elem_id);
    DBusMessage* sig = g_linux_dbus.msg_new_signal(path, "org.a11y.atspi.Event.Object", member);
    if (!sig) return;
    DBusMessageIter it = {};
    DBusMessageIter var = {};
    DBusMessageIter dict = {};
    g_linux_dbus.msg_iter_init_append(sig, &it);
    const char* det = detail ? detail : "";
    g_linux_dbus.msg_iter_append_basic(&it, 's', &det);
    g_linux_dbus.msg_iter_append_basic(&it, 'i', &detail1);
    g_linux_dbus.msg_iter_append_basic(&it, 'i', &detail2);
    if (g_linux_dbus.msg_iter_open_container(&it, 'v', "i", &var)) {
        const int32_t zero = 0;
        g_linux_dbus.msg_iter_append_basic(&var, 'i', &zero);
        g_linux_dbus.msg_iter_close_container(&it, &var);
    }
    if (g_linux_dbus.msg_iter_open_container(&it, 'a', "{sv}", &dict)) {
        g_linux_dbus.msg_iter_close_container(&it, &dict);
    }
    g_linux_dbus.conn_send(conn, sig, nullptr);
    g_linux_dbus.msg_unref(sig);
}

DBusHandlerResult linux_atspi_filter(DBusConnection* conn, DBusMessage* msg, void* userdata) noexcept {
    auto* ats = static_cast<LinuxAtSpiSlot*>(userdata);
    if (!ats || !ats->ctx || !ats->window || !msg) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    const char* path = g_linux_dbus.msg_get_path(msg);
    constexpr const char* kPrefix = "/org/a11y/atspi/accessible/etch_";
    constexpr size_t kPrefixLen = 32u;
    if (!path || std::strncmp(path, kPrefix, kPrefixLen) != 0) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    uint32_t elem_id = 0u;
    if (std::sscanf(path + kPrefixLen, "%X", &elem_id) != 1) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (g_linux_dbus.msg_is_call(msg, "org.a11y.atspi.Accessible", "GetRole")) {
        const A11yNode* an = find_a11y_node(elem_id, ats->ctx, ats->window);
        const uint32_t role = an ? etch_to_atspi_role(an->role) : 0u;
        if (DBusMessage* rep = g_linux_dbus.msg_new_return(msg)) {
            DBusMessageIter it = {};
            g_linux_dbus.msg_iter_init_append(rep, &it);
            g_linux_dbus.msg_iter_append_basic(&it, 'u', &role);
            g_linux_dbus.conn_send(conn, rep, nullptr);
            g_linux_dbus.msg_unref(rep);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (g_linux_dbus.msg_is_call(msg, "org.a11y.atspi.Action", "DoAction")) {
        const uint32_t ok = perform_a11y_action(elem_id, A11yAction::Press, 0.0f, {}, ats->ctx, ats->window) ? 1u : 0u;
        if (DBusMessage* rep = g_linux_dbus.msg_new_return(msg)) {
            DBusMessageIter it = {};
            g_linux_dbus.msg_iter_init_append(rep, &it);
            g_linux_dbus.msg_iter_append_basic(&it, 'b', &ok);
            g_linux_dbus.conn_send(conn, rep, nullptr);
            g_linux_dbus.msg_unref(rep);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (g_linux_dbus.msg_is_call(msg, "org.a11y.atspi.Component", "GrabFocus")) {
        const uint32_t ok = perform_a11y_action(elem_id, A11yAction::Focus, 0.0f, {}, ats->ctx, ats->window) ? 1u : 0u;
        if (DBusMessage* rep = g_linux_dbus.msg_new_return(msg)) {
            DBusMessageIter it = {};
            g_linux_dbus.msg_iter_init_append(rep, &it);
            g_linux_dbus.msg_iter_append_basic(&it, 'b', &ok);
            g_linux_dbus.conn_send(conn, rep, nullptr);
            g_linux_dbus.msg_unref(rep);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void linux_atspi_attach_window(Context* ctx, ClaimedWindowSlot* slot) noexcept {
    if (!ctx || !slot || !slot->window || ctx->headless || !ctx->enable_os_a11y || slot->os_a11y != nullptr) return;
    if (!ensure_linux_dbus_api()) return;
    DBusError err = {};
    g_linux_dbus.error_init(&err);
    DBusConnection* session = g_linux_dbus.bus_get(0, &err); // DBUS_BUS_SESSION = 0
    if (!session || g_linux_dbus.error_is_set(&err)) {
        g_linux_dbus.error_free(&err);
        if (session) g_linux_dbus.conn_unref(session);
        return;
    }
    DBusMessage* req = g_linux_dbus.msg_new_call("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    DBusConnection* a11y_conn = nullptr;
    if (req) {
        DBusMessage* rep = g_linux_dbus.conn_send_block(session, req, 250, &err);
        g_linux_dbus.msg_unref(req);
        if (rep && !g_linux_dbus.error_is_set(&err)) {
            DBusMessageIter it = {};
            if (g_linux_dbus.msg_iter_init(rep, &it) && g_linux_dbus.msg_iter_get_arg_type(&it) == 's') {
                const char* addr = nullptr;
                g_linux_dbus.msg_iter_get_basic(&it, &addr);
                if (addr && addr[0] != '\0') {
                    a11y_conn = g_linux_dbus.conn_open(addr, &err);
                    if (a11y_conn && !g_linux_dbus.error_is_set(&err)) {
                        if (!g_linux_dbus.bus_register(a11y_conn, &err) || g_linux_dbus.error_is_set(&err)) {
                            g_linux_dbus.conn_unref(a11y_conn);
                            a11y_conn = nullptr;
                        }
                    } else if (a11y_conn) {
                        g_linux_dbus.conn_unref(a11y_conn);
                        a11y_conn = nullptr;
                    }
                }
            }
            g_linux_dbus.msg_unref(rep);
        }
        g_linux_dbus.error_free(&err);
    }
    g_linux_dbus.conn_unref(session);
    if (!a11y_conn) return;

    auto* ats = static_cast<LinuxAtSpiSlot*>(std::calloc(1u, sizeof(LinuxAtSpiSlot)));
    if (!ats) {
        g_linux_dbus.conn_unref(a11y_conn);
        return;
    }
    ats->ctx = ctx;
    ats->claimed = slot;
    ats->window = slot->window;
    ats->a11y_conn = a11y_conn;
    g_linux_dbus.conn_add_filter(a11y_conn, linux_atspi_filter, ats, nullptr);
    slot->os_a11y = ats;
}

void linux_atspi_detach_window(ClaimedWindowSlot* slot) noexcept {
    if (!slot || !slot->os_a11y) return;
    auto* ats = static_cast<LinuxAtSpiSlot*>(slot->os_a11y);
    slot->os_a11y = nullptr;
    if (ats->a11y_conn && g_linux_dbus.ok) {
        g_linux_dbus.conn_remove_filter(ats->a11y_conn, linux_atspi_filter, ats);
        g_linux_dbus.conn_unref(ats->a11y_conn);
    }
    std::free(ats);
}

void linux_atspi_notify_commit(Context* ctx, ClaimedWindowSlot* slot) noexcept {
    if (!ctx || !slot || !slot->os_a11y) return;
    auto* ats = static_cast<LinuxAtSpiSlot*>(slot->os_a11y);
    if (!ats->a11y_conn || !g_linux_dbus.ok) return;
    const auto span = frame_a11y_nodes(ctx, slot->window);
    const uint32_t count = static_cast<uint32_t>(span.size());
    uint64_t sh = 14695981039346656037ULL ^ count;
    sh *= 1099511628211ULL;
    for (uint32_t i = 0u; i < count; ++i) {
        sh ^= span[i].id;
        sh *= 1099511628211ULL;
        sh ^= static_cast<uint8_t>(span[i].role);
        sh *= 1099511628211ULL;
    }
    if (ats->has_prev_snapshot && sh != ats->prev_struct_hash && count > 0u) {
        linux_atspi_emit_object_signal(ats->a11y_conn, span[0].id, "ChildrenChanged", "add", 0, 0);
    }
    ats->prev_struct_hash = sh;
    ats->has_prev_snapshot = true;

    const uint32_t focus_id = slot->ui.focus_id;
    if (focus_id != ats->prev_focus_id) {
        if (ats->prev_focus_id != 0u) {
            linux_atspi_emit_object_signal(ats->a11y_conn, ats->prev_focus_id, "StateChanged", "focused", 0, 0);
        }
        ats->prev_focus_id = focus_id;
        if (focus_id != 0u) {
            linux_atspi_emit_object_signal(ats->a11y_conn, focus_id, "StateChanged", "focused", 1, 0);
        }
    }
    g_linux_dbus.conn_flush(ats->a11y_conn);
    g_linux_dbus.conn_dispatch(ats->a11y_conn, 0);
}

#endif // platform OS A11y backends

void os_a11y_attach_window(Context* ctx, ClaimedWindowSlot* slot) noexcept {
#if defined(_WIN32) && ETCH_ENABLE_OS_A11Y
    if (!ctx || !slot || !slot->window || ctx->headless || !ctx->enable_os_a11y || slot->os_a11y != nullptr) {
        return;
    }
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(slot->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd || !ensure_win_uia_api()) {
        return;
    }
    auto* uia = static_cast<WinUiaSlot*>(std::calloc(1u, sizeof(WinUiaSlot)));
    if (!uia) {
        return;
    }
    uia->ctx = ctx;
    uia->claimed = slot;
    uia->window = slot->window;
    uia->hwnd = hwnd;
    uia->owner_tid = GetWindowThreadProcessId(hwnd, nullptr);
    const uint32_t rid = id_index(id("etch_a11y_window"), SDL_GetWindowID(slot->window));
    uia_get_or_create_node(uia, rid, true);
    if (!SetWindowSubclass(hwnd, etch_uia_subclass_proc, kUiaSubclassId, reinterpret_cast<DWORD_PTR>(uia))) {
        for (uint32_t i = 0u; i < uia->node_count; ++i) {
            if (uia->nodes[i]) {
                InterlockedExchange(&uia->nodes[i]->detached, 1L);
                uia->nodes[i]->slot = nullptr;
                uia_release(uia->nodes[i]);
            }
        }
        std::free(uia->prev_tracked);
        std::free(uia->nodes);
        std::free(uia);
        return;
    }
    uia->subclass_attached = true;
    slot->os_a11y = uia;
#elif defined(ETCH_USE_ACCESSKIT) && ETCH_ENABLE_OS_A11Y
    accesskit_attach_window(ctx, slot);
#elif defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX && ETCH_ENABLE_OS_A11Y
    macos_a11y_attach_window(ctx, slot);
#elif defined(__linux__) && ETCH_ENABLE_OS_A11Y
    linux_atspi_attach_window(ctx, slot);
#else
    (void)ctx;
    (void)slot;
#endif
}

void os_a11y_detach_window(Context* ctx, ClaimedWindowSlot* slot) noexcept {
    (void)ctx;
#if defined(_WIN32) && ETCH_ENABLE_OS_A11Y
    if (!slot || !slot->os_a11y) {
        return;
    }
    auto* uia = static_cast<WinUiaSlot*>(slot->os_a11y);
    slot->os_a11y = nullptr;
    for (uint32_t i = 0u; i < uia->node_count; ++i) {
        WinUiaNode* n = uia->nodes[i];
        if (!n) continue;
        InterlockedExchange(&n->detached, 1L);
        n->slot = nullptr;
    }
    if (uia->hwnd && IsWindow(uia->hwnd)) {
        if (uia->subclass_attached) {
            RemoveWindowSubclass(uia->hwnd, etch_uia_subclass_proc, kUiaSubclassId);
            uia->subclass_attached = false;
        }
        if (g_win_uia.ok && g_win_uia.return_raw) {
            g_win_uia.return_raw(uia->hwnd, 0u, 0, nullptr);
        }
    }
    for (uint32_t i = 0u; i < uia->node_count; ++i) {
        if (uia->nodes[i]) {
            uia_release(uia->nodes[i]);
        }
    }
    std::free(uia->prev_tracked);
    std::free(uia->nodes);
    std::free(uia);
#elif defined(ETCH_USE_ACCESSKIT) && ETCH_ENABLE_OS_A11Y
    accesskit_detach_window(slot);
#elif defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX && ETCH_ENABLE_OS_A11Y
    macos_a11y_detach_window(slot);
#elif defined(__linux__) && ETCH_ENABLE_OS_A11Y
    linux_atspi_detach_window(slot);
#else
    (void)slot;
#endif
}

void os_a11y_notify_commit(Context* ctx) noexcept {
#if defined(_WIN32) && ETCH_ENABLE_OS_A11Y
    if (!ctx || !ctx->enable_os_a11y || ctx->headless || !ctx->active_window) {
        return;
    }
    ClaimedWindowSlot* slot = find_claimed_slot(ctx, ctx->active_window);
    if (!slot || !slot->os_a11y) {
        return;
    }
    auto* uia = static_cast<WinUiaSlot*>(slot->os_a11y);
    const WindowUiState& ui = slot->ui;
    uint32_t write = 0u;
    for (uint32_t i = 0u; i < uia->node_count; ++i) {
        WinUiaNode* n = uia->nodes[i];
        bool alive = n && n->is_root;
        if (n && !n->is_root) {
            const auto* entry = a11y_index(ctx, n->element_id);
            alive = entry && entry->build;
        }
        if (alive) {
            uia->nodes[write++] = n;
        } else if (n) {
            InterlockedExchange(&n->detached, 1L);
            n->slot = nullptr;
            uia_release(n);
        }
    }
    uia->node_count = write;

    const bool can_raise = g_win_uia.ok && g_win_uia.clients_listening && g_win_uia.clients_listening() && g_win_uia.raise_event;
    if (can_raise && ui.frame_a11y_actions) {
        for (uint32_t i = 0u; i < ui.frame_a11y_action_count; ++i) {
            const PendingA11yAction& act = ui.frame_a11y_actions[i];
            if (!act.consumed || act.action != A11yAction::Press) {
                continue;
            }
            const A11yNode* an = find_a11y_node(act.id, ctx, slot->window);
            if (uia_supports_invoke(an)) {
                WinUiaNode* inv = uia_get_or_create_node(uia, act.id, false);
                if (inv) {
                    g_win_uia.raise_event(reinterpret_cast<IRawElementProviderSimple*>(&inv->simple), UIA_Invoke_InvokedEventId);
                }
            }
        }
    }

    if (ui.focus_id != ui.prev_a11y_focus_id) {
        slot->ui.prev_a11y_focus_id = ui.focus_id;
        if (ui.focus_id != 0u && can_raise && find_a11y_node(ui.focus_id, ctx, slot->window) != nullptr) {
            WinUiaNode* fn = uia_get_or_create_node(uia, ui.focus_id, false);
            if (fn) {
                g_win_uia.raise_event(reinterpret_cast<IRawElementProviderSimple*>(&fn->simple), UIA_AutomationFocusChangedEventId);
            }
        }
    }

    if (!can_raise) {
        uia->has_prev_snapshot = false;
        return;
    }

    const auto span = frame_a11y_nodes(ctx, slot->window);
    const uint32_t count = static_cast<uint32_t>(span.size());
    const uint64_t struct_hash = uia_compute_struct_hash(span);

    if (uia->has_prev_snapshot) {
        if ((struct_hash != uia->prev_struct_hash || count != uia->prev_node_count) &&
            g_win_uia.raise_structure_changed && uia->root)
        {
            int rid[2] = {UiaAppendRuntimeId, static_cast<int>(uia->root->element_id)};
            g_win_uia.raise_structure_changed(
                reinterpret_cast<IRawElementProviderSimple*>(&uia->root->simple),
                StructureChangeType_ChildrenInvalidated,
                rid,
                2);
        }

        uint32_t raised_events = 0u;
        for (uint32_t i = 0u; i < count && raised_events < kUiaMaxPropEventsPerCommit; ++i) {
            const A11yNode& an = span[i];
            const WinUiaTrackedNode* prev = uia_tracked_find(uia, an.id);
            if (!prev) continue;

            const uint64_t label_h = uia_hash_str(an.label);
            const uint64_t val_h = uia_hash_str(an.value_text);
            const bool label_changed = (label_h != prev->label_hash || an.label.length != prev->label_len);
            const bool val_changed = (val_h != prev->value_hash || an.value_text.length != prev->value_len);
            const uint32_t state_diff = an.state_flags ^ prev->state_flags;
            const bool toggle_changed = uia_supports_toggle(&an) && ((state_diff & A11yState_Checked) != 0u);
            const bool expand_changed = uia_supports_expand_collapse(&an) && ((state_diff & A11yState_Expanded) != 0u);
            const bool sel_changed = uia_supports_selection_item(&an) && ((state_diff & A11yState_Selected) != 0u);
            const bool live_changed = uia_is_live_role(an.role) && (label_changed || val_changed);

            if (!label_changed && !val_changed && !toggle_changed && !expand_changed && !sel_changed && !live_changed) {
                continue;
            }
            WinUiaNode* node = uia_get_or_create_node(uia, an.id, i == 0u);
            if (!node) continue;
            auto* prov = reinterpret_cast<IRawElementProviderSimple*>(&node->simple);

            if (g_win_uia.raise_prop_changed) {
                if (label_changed && raised_events < kUiaMaxPropEventsPerCommit) {
                    VARIANT ov = {};
                    VARIANT nv = {};
                    VariantInit(&ov);
                    VariantInit(&nv);
                    nv.vt = VT_BSTR;
                    nv.bstrVal = uia_alloc_bstr_utf8(an.label);
                    g_win_uia.raise_prop_changed(prov, UIA_NamePropertyId, ov, nv);
                    VariantClear(&nv);
                    ++raised_events;
                }
                if (val_changed && uia_supports_value(&an) && raised_events < kUiaMaxPropEventsPerCommit) {
                    VARIANT ov = {};
                    VARIANT nv = {};
                    VariantInit(&ov);
                    VariantInit(&nv);
                    nv.vt = VT_BSTR;
                    nv.bstrVal = uia_alloc_bstr_utf8(an.value_text);
                    g_win_uia.raise_prop_changed(prov, UIA_ValueValuePropertyId, ov, nv);
                    VariantClear(&nv);
                    ++raised_events;
                }
                if (toggle_changed && raised_events < kUiaMaxPropEventsPerCommit) {
                    auto to_ts = [](uint32_t sf) noexcept -> LONG {
                        return ((sf & A11yState_Checked) != 0u) ? static_cast<LONG>(ToggleState_On) : static_cast<LONG>(ToggleState_Off);
                    };
                    VARIANT ov = {};
                    VARIANT nv = {};
                    VariantInit(&ov);
                    VariantInit(&nv);
                    ov.vt = VT_I4;
                    ov.lVal = to_ts(prev->state_flags);
                    nv.vt = VT_I4;
                    nv.lVal = to_ts(an.state_flags);
                    g_win_uia.raise_prop_changed(prov, UIA_ToggleToggleStatePropertyId, ov, nv);
                    ++raised_events;
                }
                if (expand_changed && raised_events < kUiaMaxPropEventsPerCommit) {
                    VARIANT ov = {};
                    VARIANT nv = {};
                    VariantInit(&ov);
                    VariantInit(&nv);
                    ov.vt = VT_I4;
                    ov.lVal = ((prev->state_flags & A11yState_Expanded) != 0u)
                        ? static_cast<LONG>(ExpandCollapseState_Expanded)
                        : static_cast<LONG>(ExpandCollapseState_Collapsed);
                    nv.vt = VT_I4;
                    nv.lVal = ((an.state_flags & A11yState_Expanded) != 0u)
                        ? static_cast<LONG>(ExpandCollapseState_Expanded)
                        : static_cast<LONG>(ExpandCollapseState_Collapsed);
                    g_win_uia.raise_prop_changed(prov, UIA_ExpandCollapseExpandCollapseStatePropertyId, ov, nv);
                    ++raised_events;
                }
                if (sel_changed && raised_events < kUiaMaxPropEventsPerCommit) {
                    VARIANT ov = {};
                    VARIANT nv = {};
                    VariantInit(&ov);
                    VariantInit(&nv);
                    ov.vt = VT_BOOL;
                    ov.boolVal = ((prev->state_flags & A11yState_Selected) != 0u) ? VARIANT_TRUE : VARIANT_FALSE;
                    nv.vt = VT_BOOL;
                    nv.boolVal = ((an.state_flags & A11yState_Selected) != 0u) ? VARIANT_TRUE : VARIANT_FALSE;
                    g_win_uia.raise_prop_changed(prov, UIA_SelectionItemIsSelectedPropertyId, ov, nv);
                    ++raised_events;
                }
            }
            if (live_changed && raised_events < kUiaMaxPropEventsPerCommit) {
                g_win_uia.raise_event(prov, UIA_LiveRegionChangedEventId);
                ++raised_events;
            }
        }
    }

    uia->has_prev_snapshot = true;
    uia->prev_node_count = count;
    uia->prev_struct_hash = struct_hash;
    uia_tracked_rebuild(uia, span, ui.focus_id);
#elif (defined(ETCH_USE_ACCESSKIT) || (defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX) || defined(__linux__)) && ETCH_ENABLE_OS_A11Y
    if (!ctx || !ctx->enable_os_a11y || ctx->headless || !ctx->active_window) {
        return;
    }
    ClaimedWindowSlot* slot = find_claimed_slot(ctx, ctx->active_window);
    if (!slot || !slot->os_a11y) {
        return;
    }
#    if defined(ETCH_USE_ACCESSKIT)
    accesskit_notify_commit(ctx, slot);
#    elif defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX
    macos_a11y_notify_commit(ctx, slot);
#    elif defined(__linux__)
    linux_atspi_notify_commit(ctx, slot);
#    endif
#else
    (void)ctx;
#endif
}

} // namespace

void set_os_a11y_enabled(Context* ctx_arg, bool enabled) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx) {
        return;
    }
    ctx->enable_os_a11y = enabled;
    if (ctx->headless) {
        return;
    }
    for (uint32_t i = 0u; i < ctx->claimed_window_cap; ++i) {
        ClaimedWindowSlot* slot = ctx->claimed_windows[i];
        if (!slot || !slot->window) {
            continue;
        }
        if (enabled) {
            os_a11y_attach_window(ctx, slot);
        } else {
            os_a11y_detach_window(ctx, slot);
        }
    }
}

bool os_a11y_enabled(const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    return ctx && ctx->enable_os_a11y;
}

bool os_a11y_active(const Context* ctx_arg, SDL_Window* window) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx || !ctx->enable_os_a11y || ctx->headless) {
        return false;
    }
    SDL_Window* target = window ? window : (ctx->active_window ? ctx->active_window : ctx->window);
    const ClaimedWindowSlot* slot = find_claimed_slot(ctx, target);
    return slot && slot->os_a11y != nullptr;
}

void set_window_refresh_callback(Context* ctx_arg, WindowRefreshFn fn, void* user_data) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx) {
        return;
    }
    ctx->c_refresh_fn = nullptr;
    ctx->c_refresh_user_data = nullptr;
    ctx->refresh_fn = fn;
    ctx->refresh_user_data = fn ? user_data : nullptr;
    sync_window_event_watch(ctx);
}

bool set_window_fullscreen(bool fullscreen, SDL_Window* window) noexcept {
    Context* ctx = current_context();
    SDL_Window* target = window ? window : (ctx ? (ctx->active_window ? ctx->active_window : ctx->window) : nullptr);
    if (!target || (ctx && ctx->headless && !window)) {
        return false;
    }
    const bool ok = SDL_SetWindowFullscreen(target, fullscreen);
    if (ok) {
        SDL_SyncWindow(target);
    }
    if (ok && ctx) {
        WindowUiState* ui = window_ui(ctx, target);
        if (ui) {
            sync_window_metrics_from_os(ui, target);
            ui->redraw_budget = 2u;
        }
        request_window_redraw(ctx, target);
    }
    return ok;
}

bool is_window_fullscreen(const Context* ctx_arg, SDL_Window* window) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    SDL_Window* target = window ? window : (ctx ? (ctx->active_window ? ctx->active_window : ctx->window) : nullptr);
    if (!target || (ctx && ctx->headless && !window)) {
        return false;
    }
    return (SDL_GetWindowFlags(target) & SDL_WINDOW_FULLSCREEN) != 0u;
}

Padding window_safe_insets(const Context* ctx_arg, SDL_Window* window) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    SDL_Window* target = window ? window : (ctx ? (ctx->active_window ? ctx->active_window : ctx->window) : nullptr);
    const WindowUiState* ui = ctx ? window_ui(ctx, target) : nullptr;
    Padding insets = {};
    if (target && (!ctx || !ctx->headless || window != nullptr)) {
        int win_w = 0;
        int win_h = 0;
        SDL_Rect safe = {};
        if (SDL_GetWindowSize(target, &win_w, &win_h) && win_w > 0 && win_h > 0 &&
            SDL_GetWindowSafeArea(target, &safe))
        {
            float density = SDL_GetWindowPixelDensity(target);
            if (density <= 0.0f) {
                density = 1.0f;
            }
            const float dpi = (ui && ui->dpi_scale > 0.0f) ? ui->dpi_scale : density;
            const float scale = density / dpi;
            const int rx = win_w - (safe.x + safe.w);
            const int by = win_h - (safe.y + safe.h);
            insets = {
                .left = static_cast<float>(safe.x > 0 ? safe.x : 0) * scale,
                .right = static_cast<float>(rx > 0 ? rx : 0) * scale,
                .top = static_cast<float>(safe.y > 0 ? safe.y : 0) * scale,
                .bottom = static_cast<float>(by > 0 ? by : 0) * scale,
            };
        }
#if defined(__APPLE__) && defined(__MACH__) && TARGET_OS_OSX
        const SDL_WindowFlags wflags = SDL_GetWindowFlags(target);
        if (ui && ui->chrome_desc.enabled && ui->chrome_desc.macos_traffic_lights &&
            (wflags & SDL_WINDOW_FULLSCREEN) == 0u &&
            insets.left < ui->chrome_desc.traffic_light_inset_px)
        {
            insets.left = ui->chrome_desc.traffic_light_inset_px;
        }
#endif
    }
    return insets;
}

} // namespace etch
