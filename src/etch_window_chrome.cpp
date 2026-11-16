// Native maximize hit testing is independent of the optional accessibility bridge.
#if defined(_WIN32)
#include <windows.h>
#include <dwmapi.h>
#endif

namespace etch {
namespace {
#if defined(_WIN32)
constexpr const wchar_t* kChromeSlotProperty=L"Etch.ChromeSlot";
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
LRESULT CALLBACK chrome_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept {
    auto* slot=reinterpret_cast<ClaimedWindowSlot*>(GetPropW(hwnd,kChromeSlotProperty));
    const WNDPROC original=reinterpret_cast<WNDPROC>(slot->native_chrome_proc);
    if (msg==WM_NCHITTEST) {
        POINT point={static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
        ScreenToClient(hwnd,&point);
        const float scale=SDL_GetWindowPixelDensity(slot->window)/slot->ui.dpi_scale;
        if (evaluate_window_hit(slot->chrome_context,{point.x*scale,point.y*scale},slot->window)==WindowHit::Maximize) return HTMAXBUTTON;
    }
    POINT max_point={static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
    bool over_custom_max=false;
    if((msg==WM_NCMOUSEMOVE || msg==WM_NCLBUTTONDOWN || msg==WM_NCLBUTTONDBLCLK) && wparam==HTMAXBUTTON && slot->ui.chrome_desc.enabled) {
        ScreenToClient(hwnd,&max_point);
        const float scale=SDL_GetWindowPixelDensity(slot->window)/slot->ui.dpi_scale;
        over_custom_max=evaluate_window_hit(slot->chrome_context,{max_point.x*scale,max_point.y*scale},slot->window)==WindowHit::Maximize;
    }
    if (msg==WM_NCMOUSEMOVE && (over_custom_max || slot->native_max_hover)) {
        POINT point={static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
        ScreenToClient(hwnd,&point);chrome_mouse_event(slot,msg,point);
        slot->native_max_hover=over_custom_max;
        TRACKMOUSEEVENT track={sizeof(TRACKMOUSEEVENT),TME_LEAVE|TME_NONCLIENT,hwnd,0};
        TrackMouseEvent(&track);
        return DefWindowProcW(hwnd,msg,wparam,lparam);
    }
    if (msg==WM_NCMOUSELEAVE && slot->native_max_hover) {
        slot->native_max_hover=false;chrome_mouse_event(slot,msg,{-1,-1});
    }
    if ((msg==WM_NCLBUTTONDOWN || msg==WM_NCLBUTTONDBLCLK) && over_custom_max) {
        POINT point={static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
        ScreenToClient(hwnd,&point);slot->native_max_press=true;
        SetCapture(hwnd);chrome_mouse_event(slot,msg,point);return 0;
    }
    if ((msg==WM_LBUTTONUP || msg==WM_NCLBUTTONUP) && slot->native_max_press) {
        POINT point={static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
        if (msg==WM_NCLBUTTONUP) ScreenToClient(hwnd,&point);
        slot->native_max_press=false;ReleaseCapture();chrome_mouse_event(slot,msg,point);return 0;
    }
    if (msg==WM_CAPTURECHANGED && slot->native_max_press) {
        slot->native_max_press=false;slot->ui.event_pointer_down=false;
        slot->ui.prev_pointer_down=false;slot->ui.active_id=0;slot->ui.redraw_budget=2;
    }
    if (msg==WM_NCDESTROY) {
        RemovePropW(hwnd,kChromeSlotProperty);slot->native_chrome_proc=nullptr;
    }
    return CallWindowProcW(original,hwnd,msg,wparam,lparam);
}

#endif

void apply_win32_chrome(Context* ctx, ClaimedWindowSlot* slot) noexcept {
#if defined(_WIN32)
    HWND hwnd=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(slot->window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
    if (!hwnd) return;
    if (!slot->native_chrome_proc) {
        slot->chrome_context=ctx;
        if (!SetPropW(hwnd,kChromeSlotProperty,reinterpret_cast<HANDLE>(slot))) return;
        slot->native_chrome_proc=reinterpret_cast<void*>(SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(chrome_window_proc)));
        if (!slot->native_chrome_proc) { RemovePropW(hwnd,kChromeSlotProperty);return; }
    }
    const DWM_WINDOW_CORNER_PREFERENCE corners=slot->ui.chrome_desc.enabled?DWMWCP_ROUND:DWMWCP_DEFAULT;
    DwmSetWindowAttribute(hwnd,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    const Color bg=active_theme(ctx).bg_canvas;
    const BOOL dark=int(bg.r)+int(bg.g)+int(bg.b)<384;
    DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
#else
    (void)ctx;(void)slot;
#endif
}
void detach_win32_chrome(ClaimedWindowSlot* slot) noexcept {
#if defined(_WIN32)
    if (!slot->native_chrome_proc) return;
    HWND hwnd=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(slot->window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
    if (hwnd) {
        SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(slot->native_chrome_proc));
        RemovePropW(hwnd,kChromeSlotProperty);
    }
    slot->native_chrome_proc=nullptr;
#else
    (void)slot;
#endif
}
} // namespace
} // namespace etch
