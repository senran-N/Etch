// Desktop logical-size windows. SDL properties follow the window lifetime.
namespace etch {
namespace {
constexpr const char* kDesktopScale = "etch.desktop.coordinate_scale";
float desktop_coordinate_scale(SDL_Window* window) noexcept {
    return SDL_GetWindowDisplayScale(window) / SDL_GetWindowPixelDensity(window);
}
void sync_desktop_window(SDL_Window* window) noexcept {
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    const float old_scale = SDL_GetFloatProperty(props, kDesktopScale, 0);
    if (old_scale == 0) return;
    const float scale = desktop_coordinate_scale(window);
    if (std::fabs(scale - old_scale) < 0.0001f) return;
    SDL_SetFloatProperty(props, kDesktopScale, scale);
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if ((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) != 0) return;
#if defined(SDL_PLATFORM_WINDOWS)
    // SDL 3.4+ accepts the OS-suggested DPI-scaled rectangle for resizable windows.
    // Applying our ratio again would double-scale the client area.
    if ((flags & SDL_WINDOW_RESIZABLE) != 0 && SDL_GetVersion() >= SDL_VERSIONNUM(3, 4, 0)) return;
#endif
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    SDL_SetWindowSize(window, static_cast<int>(std::round(w * scale / old_scale)),
        static_cast<int>(std::round(h * scale / old_scale)));
}
}
SDL_Window* create_desktop_window(const char* title, float logical_w, float logical_h, uint64_t flags) noexcept {
    if (!title || !std::isfinite(logical_w) || !std::isfinite(logical_h) || logical_w < 1 || logical_h < 1 || logical_w > 32768 || logical_h > 32768) return nullptr;
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (!display) return nullptr;
    SDL_Rect usable = {};
    if (!SDL_GetDisplayUsableBounds(display, &usable)) return nullptr;
    float estimate = SDL_GetDisplayContentScale(display);
    if (estimate <= 0) estimate = 1;
    // Hidden until per-window density is known; macOS window coordinates are points.
    SDL_Window* window = SDL_CreateWindow(title, static_cast<int>(std::fmin(logical_w * estimate, static_cast<float>(usable.w))),
        static_cast<int>(std::fmin(logical_h * estimate, static_cast<float>(usable.h))), flags | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!window) return nullptr;
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    const float scale = desktop_coordinate_scale(window);
    int top=0, left=0, bottom=0, right=0;
    SDL_GetWindowBordersSize(window,&top,&left,&bottom,&right);
    usable.w -= left + right; usable.h -= top + bottom;
    const float ratio = std::fmin(1.0f, std::fmin(usable.w / (logical_w * scale), usable.h / (logical_h * scale)));
    const int w = static_cast<int>(std::fmax(1.0f, std::round(logical_w * scale * ratio)));
    const int h = static_cast<int>(std::fmax(1.0f, std::round(logical_h * scale * ratio)));
    if (!SDL_SetWindowSize(window, w, h) || !SDL_SetFloatProperty(SDL_GetWindowProperties(window), kDesktopScale, scale)) {
        SDL_DestroyWindow(window); return nullptr;
    }
    if ((flags & SDL_WINDOW_HIDDEN) == 0) SDL_ShowWindow(window);
    return window;
}
} // namespace etch
