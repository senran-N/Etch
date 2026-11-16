// Platform window chrome, smooth live-resize event watch, and optional OS accessibility bridge.
// Compiled via src/etch_unity.cpp.

#ifndef ETCH_ENABLE_OS_A11Y
#    define ETCH_ENABLE_OS_A11Y 1
#endif

#if defined(__APPLE__) && defined(__MACH__)
#    include <TargetConditionals.h>
#    if TARGET_OS_OSX
#        include <objc/message.h>
#        include <objc/runtime.h>
#    endif
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
    if (!ctx || !event || !SDL_IsMainThread() || !ctx->refresh_fn || ctx->in_frame || ctx->in_refresh_callback) {
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
    const bool want = !ctx->headless && ctx->refresh_fn != nullptr;
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

constexpr wchar_t kUiaSlotPropName[] = L"EtchOsA11ySlot";
constexpr HRESULT kUiaElemNotAvail = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);

struct UiaDispatchCall;

struct WinUiaApi {
    HMODULE dll = nullptr;
    LRESULT(WINAPI* return_raw)(HWND, WPARAM, LPARAM, IRawElementProviderSimple*) = nullptr;
    HRESULT(WINAPI* host_from_hwnd)(HWND, IRawElementProviderSimple**) = nullptr;
    HRESULT(WINAPI* raise_event)(IRawElementProviderSimple*, EVENTID) = nullptr;
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
    WinUiaSlot* slot;
    HWND hwnd;
    DWORD owner_tid;
    uint32_t element_id;
    LONG ref_count;
    bool is_root;
    volatile LONG detached;
};

struct WinUiaSlot {
    Context* ctx;
    ClaimedWindowSlot* claimed;
    SDL_Window* window;
    HWND hwnd;
    DWORD owner_tid;
    WNDPROC orig_wndproc;
    WinUiaNode* root;
    WinUiaNode** nodes;
    uint32_t node_count;
    uint32_t node_cap;
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
    if (!wants_invoke && !wants_value && !wants_range && !wants_toggle && !wants_expand && !wants_select) {
        return E_NOINTERFACE;
    }
    void* iface = nullptr;
    const HRESULT hr = uia_run_on_owner(n, [&]() noexcept -> HRESULT {
        const A11yNode* an = uia_live_node(n);
        if (!an) {
            return E_NOINTERFACE;
        }
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
    const WindowUiState* ui = slot->claimed ? &slot->claimed->ui : window_ui(slot->ctx, slot->window);
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
        const A11yNode* an = uia_live_node(n);
        if (!an) return uia_is_detached(n) ? kUiaElemNotAvail : S_OK;
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
        const A11yNode* an = uia_live_node(n);
        if (!an) return uia_is_detached(n) ? kUiaElemNotAvail : S_OK;

        switch (propertyId) {
            case UIA_ControlTypePropertyId:
                pRetVal->vt = VT_I4;
                pRetVal->lVal = uia_control_type(an->role);
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
                pRetVal->boolVal = VARIANT_TRUE;
                break;
            case UIA_IsContentElementPropertyId:
                pRetVal->vt = VT_BOOL;
                pRetVal->boolVal = VARIANT_TRUE;
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
        if (!an) return uia_is_detached(n) ? kUiaElemNotAvail : S_OK;
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
        if (!an) return uia_is_detached(n) ? kUiaElemNotAvail : S_OK;
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
        uint32_t hit_idx = 0u;
        for (uint32_t i = 0u; i < span.size(); ++i) {
            if (span[i].bounds.w > 0.0f && span[i].bounds.h > 0.0f && span[i].bounds.contains(pt)) {
                hit_idx = i;
            }
        }
        if (!span.empty()) {
            WinUiaNode* dst = uia_get_or_create_node(root->slot, span[hit_idx].id, hit_idx == 0u);
            if (dst) {
                *pRetVal = reinterpret_cast<IRawElementProviderFragment*>(&dst->fragment);
                uia_addref(dst);
            }
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

LRESULT CALLBACK etch_uia_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    WinUiaSlot* slot = reinterpret_cast<WinUiaSlot*>(GetPropW(hwnd, kUiaSlotPropName));
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
    if ((msg == WM_DESTROY || msg == WM_NCDESTROY) && g_win_uia.ok && g_win_uia.return_raw) {
        g_win_uia.return_raw(hwnd, 0u, 0, nullptr);
    }
    return (slot && slot->orig_wndproc)
        ? CallWindowProcW(slot->orig_wndproc, hwnd, msg, wParam, lParam)
        : DefWindowProcW(hwnd, msg, wParam, lParam);
}

#endif // defined(_WIN32) && ETCH_ENABLE_OS_A11Y

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
    SetPropW(hwnd, kUiaSlotPropName, reinterpret_cast<HANDLE>(uia));
    const LONG_PTR prev = SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(etch_uia_wndproc));
    uia->orig_wndproc = reinterpret_cast<WNDPROC>(prev);
    slot->os_a11y = uia;
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
        if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(uia->hwnd, GWLP_WNDPROC)) == etch_uia_wndproc && uia->orig_wndproc) {
            SetWindowLongPtrW(uia->hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(uia->orig_wndproc));
        }
        if (GetPropW(uia->hwnd, kUiaSlotPropName) == reinterpret_cast<HANDLE>(uia)) {
            RemovePropW(uia->hwnd, kUiaSlotPropName);
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
    std::free(uia->nodes);
    std::free(uia);
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
            const auto* entry=a11y_index(ctx,n->element_id);
            alive=entry && entry->build;
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
    if (can_raise) {
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
        if (ui.focus_id != 0u && can_raise) {
            WinUiaNode* fn = uia_get_or_create_node(uia, ui.focus_id, false);
            if (fn) {
                g_win_uia.raise_event(reinterpret_cast<IRawElementProviderSimple*>(&fn->simple), UIA_AutomationFocusChangedEventId);
            }
        }
    }
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
