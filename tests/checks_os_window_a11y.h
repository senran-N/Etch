#pragma once
#include "etch/etch.h"
#include "checks_media_emoji.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <objbase.h>
#    include <oleauto.h>
#    include <UIAutomation.h>
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

namespace os_window_a11y_checks {

struct RefreshCheckState {
    etch::Context* ctx = nullptr;
    SDL_Window* expected_win = nullptr;
    uint32_t calls = 0u;
    bool wrong_window = false;
    bool reentered = false;
    bool push_nested_inside_cb = true;
};

inline void refresh_cb(etch::Context* ctx, SDL_Window* win, float dt_seconds, void* userdata) noexcept {
    (void)dt_seconds;
    auto* st = static_cast<RefreshCheckState*>(userdata);
    if (!st) {
        return;
    }
    if (win != st->expected_win || ctx != st->ctx) {
        st->wrong_window = true;
    }
    ++st->calls;
    if (st->push_nested_inside_cb) {
        st->push_nested_inside_cb = false;
        const uint32_t before = st->calls;
        SDL_Event nested = {};
        nested.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
        nested.window.windowID = SDL_GetWindowID(win);
        SDL_PushEvent(&nested);
        if (st->calls != before) {
            st->reentered = true;
        }
    }
}

#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y

struct UiaWorkerData {
    HWND hwnd = nullptr;
    uint32_t btn_id = 0u;
    uint32_t chk_id = 0u;
    uint32_t sld_id = 0u;
    uint32_t txt_id = 0u;
    uint32_t pwd_id = 0u;
    uint32_t col_id = 0u;
    HANDLE ev_phase1_done = nullptr;
    HANDLE ev_phase2_go = nullptr;
    bool phase1_ok = false;
    bool phase2_ok = false;
    HRESULT stale_hr = S_OK;
    int fail_line = 0;
    volatile LONG stage = 0;
};

inline bool bstr_equals_w(BSTR b, const wchar_t* w) noexcept {
    if (!b || !w) {
        return false;
    }
    return std::wcscmp(b, w) == 0;
}

inline IUIAutomationElement* find_uia_by_id(
    IUIAutomationElement* root,
    IUIAutomationTreeWalker* walker,
    uint32_t target_id) noexcept
{
    if (!root || !walker || target_id == 0u) {
        return nullptr;
    }
    wchar_t expected[32] = {};
    std::swprintf(expected, 32u, L"etch_%08X", target_id);

    constexpr uint32_t kMaxQueue = 128u;
    IUIAutomationElement* queue[kMaxQueue] = {};
    uint32_t head = 0u;
    uint32_t tail = 0u;
    root->AddRef();
    queue[tail++] = root;

    IUIAutomationElement* found = nullptr;
    while (head < tail) {
        IUIAutomationElement* cur = queue[head++];
        BSTR auto_id = nullptr;
        if (SUCCEEDED(cur->get_CurrentAutomationId(&auto_id)) && auto_id) {
            if (std::wcscmp(auto_id, expected) == 0) {
                SysFreeString(auto_id);
                cur->AddRef();
                found = cur;
                break;
            }
            SysFreeString(auto_id);
        }
        IUIAutomationElement* child = nullptr;
        if (SUCCEEDED(walker->GetFirstChildElement(cur, &child)) && child) {
            while (child) {
                IUIAutomationElement* next = nullptr;
                walker->GetNextSiblingElement(child, &next);
                if (tail < kMaxQueue) {
                    queue[tail++] = child;
                } else {
                    child->Release();
                }
                child = next;
            }
        }
        cur->Release();
    }
    while (head < tail) {
        if (queue[head]) {
            queue[head]->Release();
        }
        ++head;
    }
    return found;
}

inline DWORD WINAPI uia_worker_proc(LPVOID param) noexcept {
    auto* d = static_cast<UiaWorkerData*>(param);
    if (!d) {
        return 1u;
    }
    const HRESULT hr_co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr_co)) {
        d->fail_line = __LINE__;
        if (d->ev_phase1_done) SetEvent(d->ev_phase1_done);
        return 1u;
    }

    auto fail_at = [&](int line) noexcept {
        if (d->fail_line == 0) {
            d->fail_line = line;
        }
        d->phase1_ok = false;
    };

    IUIAutomation* uia = nullptr;
    IUIAutomationElement* root = nullptr;
    IUIAutomationTreeWalker* walker = nullptr;
    IUIAutomationElement* e_btn = nullptr;
    IUIAutomationElement* e_chk = nullptr;
    IUIAutomationElement* e_sld = nullptr;
    IUIAutomationElement* e_txt = nullptr;
    IUIAutomationElement* e_pwd = nullptr;
    IUIAutomationElement* e_col = nullptr;
    IUIAutomationInvokePattern* stale_invoke = nullptr;

    InterlockedExchange(&d->stage, 1);
    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, reinterpret_cast<void**>(&uia));
    if (FAILED(hr) || !uia) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 2);
    hr = uia->ElementFromHandle(d->hwnd, &root);
    if (FAILED(hr) || !root) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 3);
    {
        CONTROLTYPEID ct = 0;
        if (FAILED(root->get_CurrentControlType(&ct)) || ct != UIA_WindowControlTypeId) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        BSTR name = nullptr;
        if (FAILED(root->get_CurrentName(&name)) || !bstr_equals_w(name, L"Etch UIA Test")) {
            if (name) SysFreeString(name);
            fail_at(__LINE__);
            goto done_phase1;
        }
        SysFreeString(name);

        BSTR fw = nullptr;
        if (FAILED(root->get_CurrentFrameworkId(&fw)) || !bstr_equals_w(fw, L"Etch")) {
            if (fw) SysFreeString(fw);
            fail_at(__LINE__);
            goto done_phase1;
        }
        SysFreeString(fw);

        RECT rc = {};
        if (FAILED(root->get_CurrentBoundingRectangle(&rc)) || (rc.right - rc.left) <= 0 || (rc.bottom - rc.top) <= 0) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 4);
    hr = uia->get_RawViewWalker(&walker);
    if (FAILED(hr) || !walker) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 5);
    e_btn = find_uia_by_id(root, walker, d->btn_id);
    e_chk = find_uia_by_id(root, walker, d->chk_id);
    e_sld = find_uia_by_id(root, walker, d->sld_id);
    e_txt = find_uia_by_id(root, walker, d->txt_id);
    e_pwd = find_uia_by_id(root, walker, d->pwd_id);
    e_col = find_uia_by_id(root, walker, d->col_id);
    InterlockedExchange(&d->stage, 6);
    if (!e_btn || !e_chk || !e_sld || !e_txt || !e_pwd || !e_col) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 61);
    if (FAILED(e_btn->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern, reinterpret_cast<void**>(&stale_invoke))) || !stale_invoke) {
        fail_at(__LINE__);
        goto done_phase1;
    }
    if (FAILED(stale_invoke->Invoke())) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 62);
    {
        IUIAutomationTogglePattern* p_toggle = nullptr;
        if (FAILED(e_chk->GetCurrentPatternAs(UIA_TogglePatternId, IID_IUIAutomationTogglePattern, reinterpret_cast<void**>(&p_toggle))) || !p_toggle) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        ToggleState ts = ToggleState_Indeterminate;
        const bool ts_ok = SUCCEEDED(p_toggle->get_CurrentToggleState(&ts)) && ts == ToggleState_Off;
        const HRESULT hr_tog = ts_ok ? p_toggle->Toggle() : E_FAIL;
        p_toggle->Release();
        if (!ts_ok || FAILED(hr_tog)) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 63);
    {
        IUIAutomationRangeValuePattern* p_range = nullptr;
        const HRESULT hr_pat = e_sld->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_IUIAutomationRangeValuePattern, reinterpret_cast<void**>(&p_range));
        if (FAILED(hr_pat) || !p_range) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        InterlockedExchange(&d->stage, 631);
        double rmin = -1.0, rmax = -1.0;
        const HRESULT hr_min = p_range->get_CurrentMinimum(&rmin);
        InterlockedExchange(&d->stage, 632);
        const HRESULT hr_max = p_range->get_CurrentMaximum(&rmax);
        InterlockedExchange(&d->stage, 633);
        const bool bounds_ok = SUCCEEDED(hr_min) && SUCCEEDED(hr_max) &&
            std::fabs(rmin - 0.0) < 0.01 && std::fabs(rmax - 100.0) < 0.01;
        const HRESULT hr_set = bounds_ok ? p_range->SetValue(75.0) : E_FAIL;
        InterlockedExchange(&d->stage, 634);
        p_range->Release();
        if (!bounds_ok || FAILED(hr_set)) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 64);
    {
        IUIAutomationValuePattern* p_val = nullptr;
        if (FAILED(e_txt->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, reinterpret_cast<void**>(&p_val))) || !p_val) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        BOOL focused = FALSE;
        BSTR cur_val = nullptr;
        const bool focus_ok = SUCCEEDED(e_txt->get_CurrentHasKeyboardFocus(&focused)) && focused == TRUE;
        const bool val_ok = SUCCEEDED(p_val->get_CurrentValue(&cur_val)) && bstr_equals_w(cur_val, L"hello");
        if (cur_val) SysFreeString(cur_val);
        BSTR next_val = SysAllocString(L"updated_\u4E2D\u6587");
        const HRESULT hr_set = (focus_ok && val_ok && next_val) ? p_val->SetValue(next_val) : E_FAIL;
        if (next_val) SysFreeString(next_val);
        wchar_t over_buf[1100];
        for (size_t oi = 0u; oi < 1099u; ++oi) over_buf[oi] = L'x';
        over_buf[1099] = L'\0';
        BSTR over_val = SysAllocString(over_buf);
        const HRESULT hr_over = over_val ? p_val->SetValue(over_val) : E_FAIL;
        if (over_val) SysFreeString(over_val);
        p_val->Release();
        if (!focus_ok || !val_ok || FAILED(hr_set) || SUCCEEDED(hr_over)) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 65);
    {
        BOOL is_pw = FALSE;
        if (FAILED(e_pwd->get_CurrentIsPassword(&is_pw)) || is_pw != TRUE) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        IUIAutomationValuePattern* p_pwd = nullptr;
        if (FAILED(e_pwd->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, reinterpret_cast<void**>(&p_pwd))) || !p_pwd) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        BSTR pw_val = nullptr;
        const bool masked_ok = SUCCEEDED(p_pwd->get_CurrentValue(&pw_val)) && pw_val &&
            std::wcscmp(pw_val, L"s3cr3t") != 0 && pw_val[0] == L'\u2022';
        if (pw_val) SysFreeString(pw_val);
        p_pwd->Release();
        if (!masked_ok) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 66);
    {
        IUIAutomationExpandCollapsePattern* p_ec = nullptr;
        if (FAILED(e_col->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_IUIAutomationExpandCollapsePattern, reinterpret_cast<void**>(&p_ec))) || !p_ec) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        ExpandCollapseState ecs = ExpandCollapseState_LeafNode;
        const bool ecs_ok = SUCCEEDED(p_ec->get_CurrentExpandCollapseState(&ecs)) && ecs == ExpandCollapseState_Collapsed;
        const HRESULT hr_exp = ecs_ok ? p_ec->Expand() : E_FAIL;
        p_ec->Release();
        if (!ecs_ok || FAILED(hr_exp)) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    d->phase1_ok = true;

done_phase1:
    if (e_col) e_col->Release();
    if (e_pwd) e_pwd->Release();
    if (e_txt) e_txt->Release();
    if (e_sld) e_sld->Release();
    if (e_chk) e_chk->Release();
    if (e_btn) e_btn->Release();
    if (walker) walker->Release();
    if (root) root->Release();
    if (uia) uia->Release();

    if (d->ev_phase1_done) {
        SetEvent(d->ev_phase1_done);
    }
    if (d->ev_phase2_go) {
        WaitForSingleObject(d->ev_phase2_go, 5000u);
    }
    if (d->phase1_ok && stale_invoke) {
        d->stale_hr = stale_invoke->Invoke();
        d->phase2_ok = FAILED(d->stale_hr);
        if (!d->phase2_ok && d->fail_line == 0) {
            d->fail_line = __LINE__;
        }
    }
    if (stale_invoke) {
        stale_invoke->Release();
    }
    CoUninitialize();
    return 0u;
}

inline bool pump_until_event(HANDLE ev, DWORD timeout_ms) noexcept {
    const bool bounded = (timeout_ms != INFINITE);
    const ULONGLONG deadline = bounded ? (GetTickCount64() + static_cast<ULONGLONG>(timeout_ms)) : 0ull;
    for (;;) {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0u, 0u, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (WaitForSingleObject(ev, 0u) == WAIT_OBJECT_0) {
            return true;
        }
        DWORD rem = INFINITE;
        if (bounded) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline) {
                return false;
            }
            rem = static_cast<DWORD>(deadline - now);
        }
        const DWORD wait_res = MsgWaitForMultipleObjectsEx(1u, &ev, rem, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait_res == WAIT_OBJECT_0) {
            return true;
        }
        if (wait_res != WAIT_OBJECT_0 + 1u) {
            return false;
        }
    }
}

#endif // defined(_WIN32) && ETCH_ENABLE_OS_A11Y

} // namespace os_window_a11y_checks

inline bool verify_os_window_a11y_checks() noexcept {
    using namespace etch;
    using namespace os_window_a11y_checks;
    Context* previous = current_context();
    bool ok = true;
    const auto check = [&](bool pass, int line) noexcept {
        if (!pass) {
            ok = false;
            std::fprintf(stderr, "v080 os/window/a11y check line %d failed (%s)\n", line, SDL_GetError());
        }
    };

    // 1. Headless root A11yRole::Window logical bounds across DPI scales.
    {
        Context* hc = create_context({.headless = true});
        if (!hc) {
            return false;
        }
        set_current_context(hc);
        const float scales[] = {1.0f, 1.5f, 2.0f};
        for (float scale : scales) {
            begin_frame(hc, {.viewport_w = 1200u, .viewport_h = 900u, .dpi_scale = scale});
            box({.id = id("root_probe"), .width = Sizing::fixed(100.0f), .height = Sizing::fixed(40.0f)});
            end_frame(hc);
            const auto nodes = frame_a11y_nodes(hc);
            check(!nodes.empty() && nodes[0].role == A11yRole::Window, __LINE__);
            if (!nodes.empty()) {
                check(std::fabs(nodes[0].bounds.x) < 0.01f && std::fabs(nodes[0].bounds.y) < 0.01f, __LINE__);
                check(std::fabs(nodes[0].bounds.w - (1200.0f / scale)) < 0.05f, __LINE__);
                check(std::fabs(nodes[0].bounds.h - (900.0f / scale)) < 0.05f, __LINE__);
            }
        }
        destroy_context(hc);
        set_current_context(nullptr);
    }

    // 2. Multi-monitor DPI transition (1.0x -> 2.0x -> 1.25x): logical layout invariance, click hit accuracy, bitmap emoji PPEM promotion.
    {
        Context* dc = create_context({.headless = true});
        if (!dc) {
            set_current_context(previous);
            return false;
        }
        set_current_context(dc);
        const auto sbix = media_emoji_checks::font(false);
        const uint16_t fs = load_font_memory(dc, sbix.data, sbix.size);
        check(fs != kInvalidFont, __LINE__);

        uint32_t btn_hits = 0u;
        const auto run_dpi_frame = [&](uint32_t vw, uint32_t vh, float dpi, Vec2 ptr, bool down) noexcept {
            begin_frame(dc, {
                .viewport_w = vw,
                .viewport_h = vh,
                .dpi_scale = dpi,
                .dt_seconds = 1.0f / 60.0f,
                .pointer_pos = ptr,
                .pointer_down = down,
            });
            begin_box({
                .width = Sizing::fixed(320.0f),
                .height = Sizing::fixed(120.0f),
                .background = Color::rgba8(0u, 0u, 0u, 255u),
            });
            if (button({.id = id("dpi_btn"), .label = str_lit("Click"), .width = Sizing::fixed(100.0f), .height = Sizing::fixed(32.0f)})) {
                ++btn_hits;
            }
            draw_text({.text = str_lit("\xF0\x9F\x98\x80"), .pos = {10.0f, 70.0f}, .font_id = fs, .size = 14.0f});
            end_box();
            end_frame(dc);
        };

        const char* png_1x = "_etch_v080_dpi1.png";
        const char* png_2x = "_etch_v080_dpi2.png";
        run_dpi_frame(320u, 120u, 1.0f, {-1.0f, -1.0f}, false);
        const Rect b1 = element_bounds(id("dpi_btn"));
        check(b1.w > 10.0f && b1.h > 10.0f && frame_stats(dc).draw_call_count <= 3u, __LINE__);
        check(render_headless_png(dc, 320u, 120u, png_1x), __LINE__);
        if (SDL_Surface* s1 = SDL_LoadPNG(png_1x)) {
            check(media_emoji_checks::pixel(s1, 16, 76, 0x00FFFFu), __LINE__);
            SDL_DestroySurface(s1);
        } else {
            check(false, __LINE__);
        }
        std::remove(png_1x);

        const Vec2 btn_center = {b1.x + b1.w * 0.5f, b1.y + b1.h * 0.5f};
        run_dpi_frame(640u, 240u, 2.0f, {-1.0f, -1.0f}, false);
        const Rect b2 = element_bounds(id("dpi_btn"));
        check(std::fabs(b2.x - b1.x) < 0.5f && std::fabs(b2.y - b1.y) < 0.5f &&
              std::fabs(b2.w - b1.w) < 0.5f && std::fabs(b2.h - b1.h) < 0.5f, __LINE__);
        run_dpi_frame(640u, 240u, 2.0f, btn_center, true);
        run_dpi_frame(640u, 240u, 2.0f, btn_center, false);
        check(btn_hits == 1u && frame_stats(dc).draw_call_count <= 3u, __LINE__);
        check(render_headless_png(dc, 640u, 240u, png_2x), __LINE__);
        if (SDL_Surface* s2 = SDL_LoadPNG(png_2x)) {
            check(media_emoji_checks::pixel(s2, 32, 152, 0xFF00FFu), __LINE__);
            SDL_DestroySurface(s2);
        } else {
            check(false, __LINE__);
        }
        std::remove(png_2x);

        run_dpi_frame(400u, 150u, 1.25f, {-1.0f, -1.0f}, false);
        const Rect b125 = element_bounds(id("dpi_btn"));
        check(std::fabs(b125.x - b1.x) < 0.5f && std::fabs(b125.y - b1.y) < 0.5f &&
              std::fabs(b125.w - b1.w) < 0.5f && std::fabs(b125.h - b1.h) < 0.5f, __LINE__);
        run_dpi_frame(400u, 150u, 1.25f, btn_center, true);
        run_dpi_frame(400u, 150u, 1.25f, btn_center, false);
        check(btn_hits == 2u, __LINE__);

        destroy_context(dc);
        set_current_context(nullptr);
    }

    // 3, 4, 5. Window chrome/fullscreen/safe-area, refresh callback & re-entrancy, and Win32 UIA bridge.
    const Padding empty_insets = window_safe_insets(nullptr, nullptr);
    check(empty_insets.top == 0.0f && empty_insets.right == 0.0f &&
          empty_insets.bottom == 0.0f && empty_insets.left == 0.0f, __LINE__);

    SDL_Window* win = create_desktop_window("Etch UIA Test", 480, 320, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (win) {
        int pw=0, ph=0;
        SDL_GetWindowSizeInPixels(win,&pw,&ph);
        const float scale=SDL_GetWindowDisplayScale(win);
        check(std::fabs(pw/scale-480)<2 && std::fabs(ph/scale-320)<2, __LINE__);
        check((SDL_GetWindowFlags(win)&SDL_WINDOW_HIDDEN)!=0, __LINE__);
    }
    check(win != nullptr, __LINE__);
    Context* wctx = win ? create_context({.window = win, .headless = false, .enable_os_a11y = true}) : nullptr;
    check(wctx != nullptr && gpu_device(wctx) != nullptr, __LINE__);
    if (!wctx || !gpu_device(wctx)) {
        if (wctx) destroy_context(wctx);
        if (win) SDL_DestroyWindow(win);
        set_current_context(previous);
        return false;
    }
    set_current_context(wctx);

    // Simulate an old monitor scale through the helper's window-local state.
    SDL_SetWindowMinimumSize(win,120,80);
    const float coordinate_scale=SDL_GetWindowDisplayScale(win)/SDL_GetWindowPixelDensity(win);
    SDL_SetFloatProperty(SDL_GetWindowProperties(win),"etch.desktop.coordinate_scale",coordinate_scale*.5f);
    SDL_Event dpi_event={};dpi_event.type=SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED;dpi_event.window.windowID=SDL_GetWindowID(win);
    process_event(wctx,dpi_event);
    int min_w=0,min_h=0;SDL_GetWindowMinimumSize(win,&min_w,&min_h);
    check(min_w==240 && min_h==160,__LINE__);
    process_event(wctx,dpi_event);SDL_GetWindowMinimumSize(win,&min_w,&min_h);
    check(min_w==240 && min_h==160,__LINE__); // Duplicate events must not rescale twice.
    SDL_SetWindowMinimumSize(win,0,0);

    // 3a. Chrome + safe insets + windowed vs fullscreen hit-testing
    set_window_chrome({
        .enabled = true,
        .resizable = true,
        .border_thickness = 8.0f,
        .corner_size = 16.0f,
        .macos_traffic_lights = true,
        .traffic_light_inset_px = -10.0f,
    }, win);
    begin_window_frame(wctx, win, 0.0f);
    window_drag_region({0.0f, 0.0f, 480.0f, 32.0f});
    box({.width = Sizing::fixed(480.0f), .height = Sizing::fixed(320.0f)});
    end_window_frame(wctx);

    const Padding win_insets = window_safe_insets(wctx, win);
    check(std::isfinite(win_insets.top) && win_insets.top >= 0.0f &&
          std::isfinite(win_insets.right) && win_insets.right >= 0.0f &&
          std::isfinite(win_insets.bottom) && win_insets.bottom >= 0.0f &&
          std::isfinite(win_insets.left) && win_insets.left >= 0.0f, __LINE__);
    check(evaluate_window_hit(wctx, {2.0f, 80.0f}, win) == WindowHit::ResizeLeft, __LINE__);
    check(evaluate_window_hit(wctx, {120.0f, 16.0f}, win) == WindowHit::Caption, __LINE__);

    const bool fs_ok = set_window_fullscreen(true, win);
    if (fs_ok && is_window_fullscreen(wctx, win)) {
        check(evaluate_window_hit(wctx, {2.0f, 80.0f}, win) == WindowHit::Client, __LINE__);
        check(evaluate_window_hit(wctx, {120.0f, 16.0f}, win) == WindowHit::Client, __LINE__);
        check(set_window_fullscreen(false, win), __LINE__);
        check(!is_window_fullscreen(wctx, win), __LINE__);
        check(evaluate_window_hit(wctx, {2.0f, 80.0f}, win) == WindowHit::ResizeLeft, __LINE__);
    }
    if (SDL_Window* fs_win = SDL_CreateWindow("Etch FS Test", 320, 200, SDL_WINDOW_HIDDEN | SDL_WINDOW_FULLSCREEN)) {
        begin_window_frame(wctx, fs_win, 0.0f);
        if (frame_in_progress(wctx)) {
            window_drag_region({0.0f, 0.0f, 320.0f, 32.0f});
            box({.width = Sizing::fixed(320.0f), .height = Sizing::fixed(200.0f)});
            end_window_frame(wctx);
            set_window_chrome({.enabled = true, .resizable = true, .border_thickness = 8.0f, .corner_size = 16.0f}, fs_win);
            if (is_window_fullscreen(wctx, fs_win)) {
                check(evaluate_window_hit(wctx, {2.0f, 80.0f}, fs_win) == WindowHit::Client, __LINE__);
                check(evaluate_window_hit(wctx, {120.0f, 16.0f}, fs_win) == WindowHit::Client, __LINE__);
            }
            release_window(wctx, fs_win);
        }
        SDL_DestroyWindow(fs_win);
    }

    // 4. Refresh callback + re-entrancy protection
    {
        RefreshCheckState rstate = {.ctx = wctx, .expected_win = win};
        set_window_refresh_callback(wctx, refresh_cb, &rstate);
        const SDL_WindowID wid = SDL_GetWindowID(win);
        auto push_win_ev = [&](Uint32 type) noexcept {
            SDL_Event ev = {};
            ev.type = type;
            ev.window.windowID = wid;
            SDL_PushEvent(&ev);
        };
        push_win_ev(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        check(rstate.calls == 1u && !rstate.wrong_window && !rstate.reentered, __LINE__);

        // Must ignore while in_frame is true.
        begin_window_frame(wctx, win, 0.0f);
        box({.width = Sizing::fixed(100.0f), .height = Sizing::fixed(40.0f)});
        push_win_ev(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        end_window_frame(wctx);
        check(rstate.calls == 1u, __LINE__);

        // Render settled frames to clear redraw budget, then test DISPLAY_SCALE_CHANGED.
        for (int i = 0; i < 3; ++i) {
            begin_window_frame(wctx, win, 0.0f);
            box({.width = Sizing::fixed(100.0f), .height = Sizing::fixed(40.0f)});
            end_window_frame(wctx);
        }
        push_win_ev(SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED);
        check(rstate.calls == 2u && needs_window_redraw(wctx, win), __LINE__);

        set_window_refresh_callback(wctx, nullptr, nullptr);
        push_win_ev(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        check(rstate.calls == 2u, __LINE__);

        SDL_Event drain = {};
        while (SDL_PollEvent(&drain)) {}
    }

    // 5. OS Accessibility Bridge verification
#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y
    {
        check(os_a11y_enabled(wctx) && os_a11y_active(wctx, win), __LINE__);
        uint32_t uia_btn_hits = 0u;
        bool uia_chk_val = false;
        float uia_sld_val = 20.0f;
        char uia_txt_buf[64] = "hello";
        char uia_pwd_buf[64] = "s3cr3t";
        const uint32_t btn_id = id("uia_btn");
        const uint32_t chk_id = id("uia_chk");
        const uint32_t sld_id = id("uia_sld");
        const uint32_t txt_id = id("uia_txt");
        const uint32_t pwd_id = id("uia_pwd");
        const uint32_t col_id = id("uia_col");

        const auto uia_step = [&]() noexcept {
            begin_window_frame(wctx, win, 0.0f);
            begin_box({.id = id("uia_panel"), .axis = Axis::Col, .width = Sizing::fixed(480.0f), .height = Sizing::fixed(320.0f)});
            if (button({.id = btn_id, .label = str_lit("Run Action")})) ++uia_btn_hits;
            checkbox({.id = chk_id, .label = str_lit("Enable Sync"), .checked = &uia_chk_val});
            slider({.id = sld_id, .value = &uia_sld_val, .min_value = 0.0f, .max_value = 100.0f, .step = 1.0f});
            text_input({.id = txt_id, .buffer = uia_txt_buf, .capacity = sizeof(uia_txt_buf)});
            text_input({.id = pwd_id, .buffer = uia_pwd_buf, .capacity = sizeof(uia_pwd_buf), .password = true});
            if (begin_collapsible({.id = col_id, .title = str_lit("Details"), .default_open = false}).visible) {
                end_collapsible();
            }
            end_box();
            end_window_frame(wctx);
        };

        uia_step();
        check(perform_a11y_action(txt_id, A11yAction::Focus, 0.0f, {}, wctx, win), __LINE__);
        uia_step();

        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        check(hwnd != nullptr, __LINE__);

        UiaWorkerData wd = {
            .hwnd = hwnd,
            .btn_id = btn_id,
            .chk_id = chk_id,
            .sld_id = sld_id,
            .txt_id = txt_id,
            .pwd_id = pwd_id,
            .col_id = col_id,
            .ev_phase1_done = CreateEventW(nullptr, TRUE, FALSE, nullptr),
            .ev_phase2_go = CreateEventW(nullptr, TRUE, FALSE, nullptr),
        };
        HANDLE th = (wd.ev_phase1_done && wd.ev_phase2_go && hwnd)
            ? CreateThread(nullptr, 0u, uia_worker_proc, &wd, 0u, nullptr)
            : nullptr;
        check(th != nullptr, __LINE__);
        if (th) {
            const bool phase1_waited = pump_until_event(wd.ev_phase1_done, 5000u);
            check(phase1_waited, __LINE__);
            if (phase1_waited) {
                check(wd.phase1_ok, __LINE__);
                if (!wd.phase1_ok) {
                    std::fprintf(stderr, "v080 Win32 UIA worker phase1 failed at worker line %d (stage=%ld)\n", wd.fail_line, wd.stage);
                }

                uia_step();
                uia_step();
                check(uia_btn_hits == 1u, __LINE__);
                check(uia_chk_val == true, __LINE__);
                check(std::fabs(uia_sld_val - 75.0f) < 0.05f, __LINE__);
                check(std::strcmp(uia_txt_buf, "updated_\xE4\xB8\xAD\xE6\x96\x87") == 0, __LINE__);
                check(is_collapsible_open(col_id), __LINE__);

                set_os_a11y_enabled(wctx, false);
                check(!os_a11y_enabled(wctx) && !os_a11y_active(wctx, win), __LINE__);
                set_os_a11y_enabled(wctx, true);
                check(os_a11y_enabled(wctx) && os_a11y_active(wctx, win), __LINE__);
            }

            release_window(wctx, win);
            check(!os_a11y_active(wctx, win), __LINE__);

            SetEvent(wd.ev_phase2_go);
            const bool worker_done = pump_until_event(th, 5000u);
            check(worker_done, __LINE__);
            if (!worker_done) {
                // Drain until the worker exits so stack-local `wd` and event handles stay valid.
                pump_until_event(th, INFINITE);
            }
            if (phase1_waited && worker_done) {
                check(wd.phase2_ok, __LINE__);
                if (!wd.phase2_ok) {
                    std::fprintf(stderr, "v080 Win32 UIA worker phase2 failed (fail_line=%d, stage=%ld, hr=0x%08lX)\n",
                        wd.fail_line, wd.stage, static_cast<unsigned long>(wd.stale_hr));
                }
            }
            CloseHandle(th);
        }
        if (wd.ev_phase1_done) CloseHandle(wd.ev_phase1_done);
        if (wd.ev_phase2_go) CloseHandle(wd.ev_phase2_go);
    }
#else
    {
        check(!os_a11y_active(wctx, win), __LINE__);
        const bool was_enabled = os_a11y_enabled(wctx);
        set_os_a11y_enabled(wctx, false);
        check(!os_a11y_enabled(wctx) && !os_a11y_active(wctx, win), __LINE__);
        set_os_a11y_enabled(wctx, was_enabled);
        check(os_a11y_enabled(wctx) == was_enabled && !os_a11y_active(wctx, win), __LINE__);
    }
#endif

    destroy_context(wctx);
    SDL_DestroyWindow(win);
    set_current_context(previous);
    return ok;
}
