# Changelog

All notable changes to Etch are documented here. This file follows
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

## [0.8.0] - 2026-11-16

### Added
- **Smooth Live Resize**: Added `WindowRefreshFn`, `ContextDesc::on_window_refresh` / `refresh_user_data`, and `set_window_refresh_callback()` backed by an engine-managed `SDL_AddEventWatch` hook with main-thread and re-entrancy guards so modal OS edge-dragging renders continuously without black borders or jitter across primary and tear-off windows.
- **Fullscreen, Safe Area & macOS Chrome**: Added `set_window_fullscreen()`, `is_window_fullscreen()`, `window_safe_insets()`, `WindowChromeDesc::macos_traffic_lights`, and `traffic_light_inset_px` with fullscreen border/caption hit suppression and native Cocoa titlebar/traffic-light styling on macOS.
- **Optional Native OS Accessibility Bridge (`ETCH_ENABLE_OS_A11Y`)**: Added `src/etch_a11y_os.cpp` and public controls (`ContextDesc::enable_os_a11y`, `set_os_a11y_enabled()`, `os_a11y_enabled()`, `os_a11y_active()`) bridging `A11yNode` trees and `perform_a11y_action()` to Win32 UI Automation COM providers (`WM_GETOBJECT`) with `UIAutomationCore.dll` dynamic loading and zero virtual inheritance.

### Fixed
- **Multi-Monitor DPI & Swapchain Sync**: Immediately resynchronized `viewport_w`, `viewport_h`, `dpi_scale`, and IME cursor rects on `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`, `DISPLAY_CHANGED`, `PIXEL_SIZE_CHANGED`, and `METAL_VIEW_RESIZED`, initialized claimed swapchain metrics per window, converted root `A11yRole::Window` bounds to logical pixels, and bound `target_ui` cleanly inside `render_window()` with swapchain size settlement redraws.

## [0.7.0] - 2026-11-15

### Added
- **Semantic Theme & Design Tokens**: Added `Theme`, `ThemeId`, built-in presets `Theme::dark()`, `Theme::light()`, `Theme::high_contrast()`, and `Theme::industrial()`, context/scoped APIs (`set_theme()`, `theme()`, `push_theme()`, `pop_theme()`, `ETCH_THEME`), sentinel tokens `Color::theme_auto()` and `Corners::theme_auto()`, and `resolve_theme_color()` / `resolve_theme_corners()` across widgets, containers, scrollbars, selection highlights, syntax colors, and window clear color.
- **Editable Soft Word-Wrap (`word_wrap`)**: Added `word_wrap` to `TextInputDesc` (when `multiline = true`) and `CodeEditorDesc`, supporting UTF-8 cluster-safe word/CJK/emergency line breaking, visual-line navigation (`Up`/`Down`/`Home`/`End`/`PageUp`/`PageDown`) with boundary affinity, pointer hit-testing and drag selection across wrapped lines, inline IME composition placement, and sparse Fenwick-cached O(log N) scrolling for 1,000,000-line `code_editor` buffers.

## [0.6.0] - 2026-11-15

### Added

- Added the Slot 0 Dynamic Sprite Cache GPU atlas with generational handles and extruded borders.
- Added static JPEG (baseline and progressive), GIF and TGA decoding alongside PNG/BMP.
- Added cross-platform OpenType sbix and CBDT/CBLC color emoji, including outline-less fonts.

## [0.5.0] - 2026-11-15

### Added

- Establish 0.5.0 as the SemVer baseline, shared by CMake and the public header
  and checked by the test executable. During 0.y.z development, new subsystems
  or breaking refinements increment MINOR; compatible fixes increment PATCH.
- Runtime GPU backend selection through `ContextDesc::gpu_driver`, then
  `ETCH_GPU_DRIVER`, then `SDL_GPU_DRIVER`, with SDL automatic selection last.
- Out-of-source CMake builds, the `Etch::etch` target, install/export rules,
  and `EtchConfig.cmake` for `find_package(Etch 0.5 CONFIG REQUIRED)`.
- MIT license, verified third-party notices including Slug credit, engineering
  README, and Windows/Linux/macOS CI for four CPU regression suites.

### Changed

- Defer headless GPU creation to `ensure_gpu_device` when a GPU operation is
  first requested; CPU layout tests can run without a GPU device.
- Load built-in bold and monospace fonts lazily on first use; initialize the
  preferred/default sans font at context creation.
- Isolate the public API in `include/etch/etch.h` and internal test hooks in
  `src/etch_testing.h`.
- Decompose layout into five modules: `etch_render_cmd.cpp`,
  `etch_text_input.cpp`, `etch_layout.cpp`, `etch_containers.cpp`, and
  `etch_frame.cpp`, all included by the single engine translation unit.
- Consolidate shared regression checks under `tests/checks_*.h`, including
  desktop, editor, accessibility, and sandbox headless checks.
- Default examples, tests, and installation to OFF when consumed as a CMake
  subproject, keeping consumer include paths and compiler options isolated.
