// Native maximize hit testing is independent of the optional accessibility bridge.
#if defined(_WIN32)
#include <cwchar>
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#endif

namespace etch {
namespace {
#if defined(_WIN32)
constexpr UINT_PTR kChromeSubclassId = 0x45544348u;
constexpr LONG_PTR kNativeFrameFixedStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr LONG_PTR kNativeFrameStyle = kNativeFrameFixedStyle | WS_THICKFRAME | WS_MAXIMIZEBOX;
constexpr LONG_PTR kNativeFrameMask = kNativeFrameStyle | WS_POPUP;
void chrome_mouse_event(ClaimedWindowSlot* slot, UINT msg, POINT point) noexcept {
    SDL_Event event={};
    const Uint32 window_id=SDL_GetWindowID(slot->window);
    if (msg==WM_NCMOUSEMOVE || msg==WM_NCMOUSELEAVE) {
        event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=window_id;
        event.motion.x=static_cast<float>(point.x);event.motion.y=static_cast<float>(point.y);
    } else {
        const bool down=msg==WM_NCLBUTTONDOWN || msg==WM_NCLBUTTONDBLCLK;
        event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID=window_id;event.button.button=SDL_BUTTON_LEFT;event.button.down=down;
        event.button.clicks=msg==WM_NCLBUTTONDBLCLK?2:1;
        event.button.x=static_cast<float>(point.x);event.button.y=static_cast<float>(point.y);
    }
    SDL_PushEvent(&event);
}
bool uses_system_backdrop(WindowBackdrop backdrop) noexcept {
    return backdrop == WindowBackdrop::SystemMica || backdrop == WindowBackdrop::SystemAcrylic;
}
float chrome_client_to_logical_scale(const ClaimedWindowSlot* slot) noexcept {
    const float density = SDL_GetWindowPixelDensity(slot->window);
    const float dpi = (slot->ui.dpi_scale > 0.0f) ? slot->ui.dpi_scale : 1.0f;
    return ((density > 0.0f) ? density : 1.0f) / dpi;
}
void apply_win32_dwm_margins(HWND hwnd, const ClaimedWindowSlot* slot) noexcept {
    if (uses_system_backdrop(slot->ui.chrome_desc.backdrop)) {
        const MARGINS sheet = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &sheet);
    } else if (slot->native_frame_applied) {
        const MARGINS thin = {1, 1, 1, 1};
        DwmExtendFrameIntoClientArea(hwnd, &thin);
    } else {
        const MARGINS none = {0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(hwnd, &none);
    }
}
LRESULT CALLBACK chrome_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) noexcept {
    auto* slot = reinterpret_cast<ClaimedWindowSlot*>(dwRefData);
    if (!slot || !slot->window) {
        if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, chrome_subclass_proc, uIdSubclass);
        return DefSubclassProc(hwnd, msg, wparam, lparam);
    }
    if (slot->native_frame_applied && msg == WM_ERASEBKGND) {
        return 1;
    }
    if (msg == WM_ENTERSIZEMOVE) {
        slot->in_win32_sizemove = true;
        slot->in_win32_sizing = false;
    } else if (msg == WM_SIZING) {
        slot->in_win32_sizing = true;
    } else if (msg == WM_MOVING) {
        slot->in_win32_sizing = false;
    } else if (msg == WM_EXITSIZEMOVE) {
        slot->in_win32_sizemove = false;
        slot->in_win32_sizing = false;
    }
    if (slot->native_frame_applied && msg == WM_NCCALCSIZE && wparam && !IsIconic(hwnd)) {
        if (IsZoomed(hwnd)) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            HMONITOR mon = MonitorFromRect(&params->rgrc[0], MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitor = {};
            monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(mon, &monitor)) {
                RECT rc = monitor.rcWork;
                // Preserve 1px clearance on auto-hide taskbar edge so the taskbar can wake on hover.
                APPBARDATA abd = {};
                abd.cbSize = sizeof(abd);
                if ((SHAppBarMessage(ABM_GETSTATE, &abd) & ABS_AUTOHIDE) != 0 &&
                    SHAppBarMessage(ABM_GETTASKBARPOS, &abd) != 0 &&
                    MonitorFromRect(&abd.rc, MONITOR_DEFAULTTONEAREST) == mon)
                {
                    if (abd.uEdge == ABE_BOTTOM && rc.bottom == monitor.rcMonitor.bottom) rc.bottom -= 1;
                    else if (abd.uEdge == ABE_TOP && rc.top == monitor.rcMonitor.top) rc.top += 1;
                    else if (abd.uEdge == ABE_LEFT && rc.left == monitor.rcMonitor.left) rc.left += 1;
                    else if (abd.uEdge == ABE_RIGHT && rc.right == monitor.rcMonitor.right) rc.right -= 1;
                }
                params->rgrc[0] = rc;
            }
        }
        return 0;
    }
    if ((slot->native_frame_applied || uses_system_backdrop(slot->ui.chrome_desc.backdrop)) &&
        (msg == WM_ACTIVATE || msg == WM_DWMCOMPOSITIONCHANGED))
    {
        apply_win32_dwm_margins(hwnd, slot);
    }
    if (msg == WM_NCHITTEST) {
        POINT point = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        ScreenToClient(hwnd, &point);
        const float scale = chrome_client_to_logical_scale(slot);
        if (evaluate_window_hit(slot->chrome_context, {point.x * scale, point.y * scale}, slot->window) == WindowHit::Maximize) return HTMAXBUTTON;
    }
    POINT max_point = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
    bool over_custom_max = false;
    if ((msg == WM_NCMOUSEMOVE || msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK || msg == WM_NCLBUTTONUP) &&
        wparam == HTMAXBUTTON && slot->ui.chrome_desc.enabled)
    {
        ScreenToClient(hwnd, &max_point);
        const float scale = chrome_client_to_logical_scale(slot);
        over_custom_max = evaluate_window_hit(slot->chrome_context, {max_point.x * scale, max_point.y * scale}, slot->window) == WindowHit::Maximize;
    }
    if (msg == WM_NCMOUSEMOVE && !slot->native_max_press && (over_custom_max || slot->native_max_hover)) {
        POINT point = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        ScreenToClient(hwnd, &point);
        chrome_mouse_event(slot, msg, point);
        slot->native_max_hover = over_custom_max;
        if (over_custom_max) {
            TRACKMOUSEEVENT track = {sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
            TrackMouseEvent(&track);
        }
        return DefSubclassProc(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_NCMOUSELEAVE) {
        if (!slot->native_max_press && slot->native_max_hover) {
            slot->native_max_hover = false;
            POINT cur = {};
            RECT rc = {};
            bool inject_leave = true;
            if (GetCursorPos(&cur) && ScreenToClient(hwnd, &cur) && GetClientRect(hwnd, &rc)) {
                if (PtInRect(&rc, cur) != FALSE) {
                    const float scale = chrome_client_to_logical_scale(slot);
                    const bool still_over_max =
                        evaluate_window_hit(slot->chrome_context, {cur.x * scale, cur.y * scale}, slot->window) == WindowHit::Maximize;
                    // Only clear hover when the cursor left the client rect or remained over the maximize rect
                    // (e.g., Windows 11 Snap Layout flyout intercepted the pointer). Adjacent client UI receives WM_MOUSEMOVE.
                    inject_leave = still_over_max;
                }
            }
            if (inject_leave) {
                chrome_mouse_event(slot, msg, {-1, -1});
            }
        }
        return DefSubclassProc(hwnd, msg, wparam, lparam);
    }
    if ((msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK) && over_custom_max) {
        POINT point = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        ScreenToClient(hwnd, &point);
        slot->native_max_hover = false;
        slot->native_max_press = true;
        if (GetActiveWindow() != hwnd) {
            SetActiveWindow(hwnd);
            if (GetActiveWindow() != hwnd && IsWindowVisible(hwnd)) {
                SetForegroundWindow(hwnd);
            }
        }
        SetCapture(hwnd);
        chrome_mouse_event(slot, msg, point);
        return 0;
    }
    if ((msg == WM_LBUTTONUP || msg == WM_NCLBUTTONUP) && slot->native_max_press) {
        POINT point = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        if (msg == WM_NCLBUTTONUP) ScreenToClient(hwnd, &point);
        slot->native_max_press = false;
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        chrome_mouse_event(slot, msg, point);
        return 0;
    }
    if (msg == WM_NCLBUTTONUP && over_custom_max) {
        return 0;
    }
    if (msg == WM_CAPTURECHANGED && slot->native_max_press && reinterpret_cast<HWND>(lparam) != hwnd) {
        slot->native_max_press = false; slot->ui.event_pointer_down = false;
        slot->ui.prev_pointer_down = false; slot->ui.active_id = 0; slot->ui.redraw_budget = 2;
    }
    if (msg == WM_NCDESTROY) {
        slot->in_win32_sizemove = false;
        slot->in_win32_sizing = false;
        slot->chrome_subclass_attached = false;
        RemoveWindowSubclass(hwnd, chrome_subclass_proc, uIdSubclass);
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

#endif

void apply_win32_chrome(Context* ctx, ClaimedWindowSlot* slot) noexcept {
#if defined(_WIN32)
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(slot->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    slot->chrome_context = ctx;
    if (!slot->chrome_subclass_attached) {
        if (!SetWindowSubclass(hwnd, chrome_subclass_proc, kChromeSubclassId, reinterpret_cast<DWORD_PTR>(slot))) return;
        slot->chrome_subclass_attached = true;
    }
    const WindowBackdrop backdrop = slot->ui.chrome_desc.backdrop;
    if (!slot->dwm_attrs_initialized || slot->applied_backdrop != backdrop) {
        const DWM_SYSTEMBACKDROP_TYPE material = backdrop == WindowBackdrop::SystemMica ? DWMSBT_MAINWINDOW :
            backdrop == WindowBackdrop::SystemAcrylic ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &material, sizeof(material));
        slot->applied_backdrop = backdrop;
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(slot->window);
    const bool native = slot->ui.chrome_desc.enabled && (flags & SDL_WINDOW_BORDERLESS) != 0 && (flags & SDL_WINDOW_FULLSCREEN) == 0;
    const bool native_changed = (native != slot->native_frame_applied);
    if (native_changed) {
        if (native) {
            slot->native_chrome_style = uintptr_t(GetWindowLongPtrW(hwnd, GWL_STYLE) & kNativeFrameMask);
            slot->native_frame_applied = true;
            const LONG_PTR desired = ((flags & SDL_WINDOW_RESIZABLE) != 0) ? kNativeFrameStyle : kNativeFrameFixedStyle;
            SetWindowLongPtrW(hwnd, GWL_STYLE, (GetWindowLongPtrW(hwnd, GWL_STYLE) & ~kNativeFrameMask) | desired);
        } else {
            slot->native_frame_applied = false;
            SetWindowLongPtrW(hwnd, GWL_STYLE, (GetWindowLongPtrW(hwnd, GWL_STYLE) & ~kNativeFrameMask) | LONG_PTR(slot->native_chrome_style));
        }
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    if (native && (!slot->dwm_attrs_initialized || native_changed)) {
        const DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
        DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    }
    if (!slot->dwm_attrs_initialized || native_changed ||
        slot->applied_margins_native != slot->native_frame_applied ||
        slot->applied_margins_backdrop != backdrop) {
        apply_win32_dwm_margins(hwnd, slot);
        slot->applied_margins_native = slot->native_frame_applied;
        slot->applied_margins_backdrop = backdrop;
    }
    const DWM_WINDOW_CORNER_PREFERENCE corners = slot->ui.chrome_desc.enabled ? DWMWCP_ROUND : DWMWCP_DEFAULT;
    const uint32_t corners_u32 = static_cast<uint32_t>(corners);
    if (!slot->dwm_attrs_initialized || slot->applied_corners != corners_u32) {
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
        slot->applied_corners = corners_u32;
    }
    const Color bg = ctx->base_theme.bg_canvas;
    const bool dark_bool = (int(bg.r) + int(bg.g) + int(bg.b)) < 384;
    if (!slot->dwm_attrs_initialized || slot->applied_dark != dark_bool) {
        const BOOL dark = dark_bool ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        slot->applied_dark = dark_bool;
    }
    const Color border = Color::blend_over(bg, ctx->base_theme.border_subtle);
    const COLORREF border_ref = RGB(border.r, border.g, border.b);
    const uint32_t border_u32 = static_cast<uint32_t>(border_ref);
    if (!slot->dwm_attrs_initialized || slot->applied_border_ref != border_u32) {
        DwmSetWindowAttribute(hwnd, static_cast<DWMWINDOWATTRIBUTE>(34), &border_ref, sizeof(border_ref));
        slot->applied_border_ref = border_u32;
    }
    slot->dwm_attrs_initialized = true;
#else
    (void)ctx; (void)slot;
#endif
}
void detach_win32_chrome(ClaimedWindowSlot* slot) noexcept {
#if defined(_WIN32)
    if (!slot->chrome_subclass_attached) return;
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(slot->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd && IsWindow(hwnd)) {
        if (slot->native_frame_applied) {
            slot->native_frame_applied = false;
            SetWindowLongPtrW(hwnd, GWL_STYLE, (GetWindowLongPtrW(hwnd, GWL_STYLE) & ~kNativeFrameMask) | LONG_PTR(slot->native_chrome_style));
            const MARGINS margins = {}; DwmExtendFrameIntoClientArea(hwnd, &margins);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        RemoveWindowSubclass(hwnd, chrome_subclass_proc, kChromeSubclassId);
    }
    slot->chrome_subclass_attached = false;
    slot->in_win32_sizemove = false;
    slot->in_win32_sizing = false;
    slot->dwm_attrs_initialized = false;
#else
    (void)slot;
#endif
}
bool win32_is_zoomed(SDL_Window* window) noexcept {
#if defined(_WIN32)
    if (!window) return false;
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    return hwnd && IsWindow(hwnd) && IsZoomed(hwnd) != FALSE;
#else
    (void)window;
    return false;
#endif
}
void win32_maximize_window(SDL_Window* window) noexcept {
#if defined(_WIN32)
    if (!window) return;
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd) && !IsZoomed(hwnd)) {
        ShowWindow(hwnd, SW_MAXIMIZE);
    }
#else
    (void)window;
#endif
}
void win32_restore_window(SDL_Window* window) noexcept {
#if defined(_WIN32)
    if (!window) return;
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd) && IsZoomed(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
#else
    (void)window;
#endif
}

#if defined(_WIN32)
bool wstr_icontains(const wchar_t* hay, const wchar_t* needle) noexcept {
    if (!hay || !needle || !needle[0]) return false;
    return std::wcsstr(hay, needle) != nullptr;
}
void wstr_tolower(const wchar_t* src, wchar_t* dst, size_t cap) noexcept {
    size_t i = 0;
    for (; i + 1u < cap && src[i]; ++i) {
        const wchar_t c = src[i];
        dst[i] = (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
    }
    dst[i] = L'\0';
}
bool wstr_ends_with_font_ext(const wchar_t* lower_s) noexcept {
    const size_t len = std::wcslen(lower_s);
    if (len < 4u) return false;
    const wchar_t* tail = lower_s + (len - 4u);
    return std::wcscmp(tail, L".ttf") == 0 || std::wcscmp(tail, L".ttc") == 0 || std::wcscmp(tail, L".otf") == 0;
}
#endif

bool win32_find_system_font_path(Str family_name, FontWeight weight, char* out_path, size_t out_cap) noexcept {
#if defined(_WIN32)
    if (!family_name.chars || family_name.length == 0u || !out_path || out_cap == 0u) return false;
    wchar_t fam_raw[128] = {};
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, family_name.chars, static_cast<int>(family_name.length), fam_raw, 127);
    if (wlen <= 0) return false;
    fam_raw[wlen] = L'\0';
    wchar_t fam_lower[128] = {};
    wstr_tolower(fam_raw, fam_lower, 128u);
    const size_t fam_len = std::wcslen(fam_lower);
    if (fam_len == 0u) return false;

    const HKEY roots[2] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    int best_score = -100000;
    wchar_t best_data[512] = {};
    bool found = false;

    for (HKEY root : roots) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &key) != ERROR_SUCCESS) {
            continue;
        }
        for (DWORD idx = 0u; ; ++idx) {
            wchar_t val_name[256] = {};
            DWORD val_len = 256u;
            wchar_t data[512] = {};
            DWORD data_bytes = sizeof(data) - sizeof(wchar_t);
            DWORD type = 0u;
            const LONG rc = RegEnumValueW(key, idx, val_name, &val_len, nullptr, &type, reinterpret_cast<LPBYTE>(data), &data_bytes);
            if (rc == ERROR_NO_MORE_ITEMS) break;
            if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || data_bytes < 4u) continue;
            data[data_bytes / sizeof(wchar_t)] = L'\0';

            wchar_t val_lower[256] = {};
            wchar_t data_lower[512] = {};
            wstr_tolower(val_name, val_lower, 256u);
            wstr_tolower(data, data_lower, 512u);
            if (!wstr_ends_with_font_ext(data_lower)) continue;

            const wchar_t* hit = std::wcsstr(val_lower, fam_lower);
            if (!hit) continue;

            int score = 20;
            if (hit == val_lower) {
                score += 40;
                const wchar_t after = val_lower[fam_len];
                if (after == L'\0' || after == L' ' || after == L'(' || after == L'&') score += 30;
            }
            const bool has_bold = wstr_icontains(val_lower, L"bold");
            const bool has_semibold = wstr_icontains(val_lower, L"semibold") || wstr_icontains(val_lower, L"semi bold") || wstr_icontains(val_lower, L"demi");
            const bool has_medium = wstr_icontains(val_lower, L"medium");
            const bool has_italic = wstr_icontains(val_lower, L"italic") || wstr_icontains(val_lower, L"oblique");
            const bool has_light = wstr_icontains(val_lower, L"light") || wstr_icontains(val_lower, L"thin");
            const bool has_black = wstr_icontains(val_lower, L"black") || wstr_icontains(val_lower, L"heavy");
            const bool has_mono = wstr_icontains(val_lower, L"mono") || wstr_icontains(val_lower, L"code");

            switch (weight) {
                case FontWeight::Bold:
                    if (has_bold && !has_semibold) score += 45;
                    else if (has_semibold) score += 10;
                    if (has_italic) score -= 25;
                    if (has_light) score -= 20;
                    break;
                case FontWeight::SemiBold:
                    if (has_semibold) score += 45;
                    else if (has_bold) score += 15;
                    if (has_italic) score -= 25;
                    break;
                case FontWeight::Medium:
                    if (has_medium) score += 45;
                    else if (has_semibold) score += 15;
                    if (has_italic) score -= 25;
                    break;
                case FontWeight::Italic:
                    if (has_italic) score += 45;
                    if (has_bold) score -= 15;
                    break;
                case FontWeight::Mono:
                    if (has_mono) score += 30;
                    if (has_bold || has_italic) score -= 20;
                    break;
                case FontWeight::Regular:
                default:
                    if (!has_bold && !has_semibold && !has_medium && !has_italic && !has_light && !has_black) score += 35;
                    else score -= 25;
                    break;
            }
            if (!found || score > best_score) {
                best_score = score;
                std::wcsncpy(best_data, data, 511u);
                best_data[511] = L'\0';
                found = true;
            }
        }
        RegCloseKey(key);
    }
    if (!found) return false;
    wchar_t expanded[512] = {};
    const DWORD exp_n = ExpandEnvironmentStringsW(best_data, expanded, 512u);
    const wchar_t* final_w = (exp_n > 0u && exp_n <= 512u) ? expanded : best_data;
    const int n = WideCharToMultiByte(CP_UTF8, 0, final_w, -1, out_path, static_cast<int>(out_cap), nullptr, nullptr);
    return n > 0;
#else
    (void)family_name; (void)weight; (void)out_path; (void)out_cap;
    return false;
#endif
}
} // namespace
} // namespace etch
