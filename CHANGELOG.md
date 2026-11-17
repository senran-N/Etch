# Changelog

All notable changes to Etch are documented here. This file follows
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

## [0.16.1] - 2026-11-17

- Expose public C++/C99 font ascent, descent and line-height metrics for baseline-aligned native consumer layout.

## [0.16.0] - 2026-11-17

- Preserve variable-list measured heights across append/truncate, defer callback invalidation until the updated descriptor, and retain explicit-height/scroll ordering for streamed document consumers.

### Added
- Cached procedural shader surfaces with generation handles, bounded frame rates, window ownership and C99 parity.
- Exported `etch_compile_hlsl_surface` CMake helper for DXIL, SPIR-V and optional offline Metal headers.
- Opt-in emissive threshold Kawase bloom and Windows Mica/Acrylic backdrop selection.
- Caller-owned terminal cell rings with ANSI/true-color runs and spring cursors, and draggable bottom/side snap sheets.
- Headless material/workbench showcase and GPU cache, bloom, palette and sheet regressions.

## [0.15.0] - 2026-11-17

### Added
- Four-corner Oklab/linear mesh gradients with optional grain and warp phase.
- Rounded and continuous-corner conic border beams, glow and box top rims; animated beams honor reduced motion and time scaling.
- Ordered alpha punch holes with lazy destination-erase pipelines and complete feathered edges.
- Scroll-edge smoothstep masks through indexed clip chains for boxes, virtual lists, tables and code editors.
- Queued RGBA8 texture subregion uploads preserving pixels outside each rectangle.
- C99 descriptors and APIs for every feature, pixel regressions and installed/subproject consumer coverage.

## [0.14.0] - 2026-11-17

### Added
- Oklch colors, Oklab interpolation and contrast-tested seed themes with C99 parity.
- Arena-backed visual layer chains, opacity scopes and animated presence with balanced early exits.
- FLIP layout springs and shared hero identities across parents; inline hover and press transitions.
- Spring presets, animation time scaling and Windows reduced-motion preference support.
- F10 slow motion toggle in the F12 inspector; motion lifecycle and 5,000-item state growth regressions.

## [0.13.0] - 2026-11-16

### Added
- Symmetric vertical shrink constraints, horizontal scrollbar interaction, accessible split hit regions and keyboard resizing.
- Explicit display selection and logical minimum-size desktop window helpers.
- Virtualized combo popups with keyboard selection reveal, page navigation and Home/End.
- Shared horizontal table scrolling, keyboard row navigation and caller-owned multi-selection bitmaps.
- Eight external texture slots per batch, 64 font slots and 1024 vector-icon slots.
- Inside/center/outside borders and GPU clip chains preserving nested asymmetric rounded corners.

### Changed
- Texture uploads share a queued command buffer and reusable cycled staging storage; `flush_texture_uploads` supports consumers outside Etch rendering.
- Axis-aligned image and glass bounds join rectangle/clip pixel snapping; text clipping uses glyph-local coordinates.
- Borderless Windows chrome enables a native DWM frame while preserving unrelated runtime window styles on disable/release.
- C99 descriptors and exports mirror every added public API. Regenerated DXIL, SPIR-V and offline Metal shader fallbacks share the updated bindings.
- Historical audit implementation, regression evidence and remaining platform acceptance are recorded in `CLOSURE.md`.

## [0.12.0] - 2026-11-16

### Added
- Layout-only pointer-passthrough boxes, table row event results and optional caller-owned row selection with hover overlays.
- Post-layout drawing callbacks receive current-frame bounds; table grid tracks resolve stretch widths in the same layout pass.
- Button content alignment, semantic `on_primary` / `on_error` tokens, pressed feedback and hand cursors for desktop controls; combo chevrons use vector lines.
- Text-input and code-editor context editing menus, optional input clear affordance and Escape-to-blur without a selection.
- Trigger-attached popups with current-frame positioning and width; multiline input wheel scrolling and a draggable vertical thumb.
- Win32 native maximize-button hit regions and DWM corner/dark-mode preferences, independent of the accessibility bridge.
- Source-over color compositing in both C++ and C99 APIs; geometric drop-batch storage and sprite-atlas compaction at saturation.

### Fixed
- Read-only inputs never modify caller memory; finite negative pointer coordinates and popup anchors remain valid.
- Modal dismissal yields to active popups; closing a popup preserves independent siblings.
- A11y semantic and UIA liveness queries use a frame-local ID index; single-line layout reuses whole-line measurement caches.
- Rich-text ellipsis reserves the active span's metrics and trims pill backgrounds; paired brackets share script/font selection.
- Slider fills, tab/dock active rules and tree guides draw with current-frame bounds; dock bodies grow on the first frame.
- Syntax highlighting supports long lines through frame-arena token storage, omitting default-color punctuation/whitespace tokens.
- Texture uploads reuse a cycled transfer buffer; Kawase passes scissor to an expanded glass region.
- Undo restoration validates snapshot hashes before touching caller memory.
- High Contrast/Industrial foreground tokens and checkbox/switch marks respect semantic contrast colors.
- Whole-line no-wrap shaping metrics agree with ellipsis detection; explicit ellipsis and shrink items compress inside clipped containers.
- Virtual-list viewport changes request settling redraws and non-focusable pointer targets are excluded from caption dragging by default.

### Changed
- Removed hard total and per-file LOC caps at the user's request. ABI, packaging, architecture and regression checks remain required. Recompile consumers for expanded descriptors.

## [0.11.0] - 2026-11-16

### Added
- Opt-in exponent-4 continuous corners for boxes, immediate rectangles, shadows and matching uniform-radius child clipping, within the existing 64-byte primitive and GPU batch.
- Negative/positive `BoxDesc::shadow_spread`; low-level `Prim::make_shadow` accepts physical-pixel offset and spread. Existing box shadow offsets remain logical pixels.
- Lazy `kFontMedium` / `kFontSemiBold` static faces (slots 14/15, including Windows `seguisb.ttf`). Missing faces fall back to Sans; explicit font loading into either slot is supported. Automatic custom allocation now uses slots 3–13.
- `set_font_tabular_numbers` enables HarfBuzz `tnum` for a loaded font outside a frame, with measurement-cache invalidation and matching C99 API.
- `TableDesc::show_column_lines` and `header_font_size_px`, mirrored in C99. Recompile consumers for expanded descriptors.

### Fixed
- Wrapped-editor row-height caches invalidate when font metrics or numeral features change.
- Small dark Slug glyphs receive optical coverage compensation, alongside the existing light-glyph compensation.

## [0.10.1] - 2026-11-16

### Fixed
- Continuous antialiasing at flat RingArc radial cuts.
- Shared physical-pixel alignment for layout geometry and clip bounds; subpixel rectangles retain their positive dimensions if snapping would collapse them.
- Width-limited immediate text without ellipsis preserves right/center alignment of the full line, clipping the corresponding visible portion.
- Desktop windows probe density at a small hidden size, recenter after sizing, and scale existing SDL minimum dimensions when DPI changes.

## [0.10.0] - 2026-11-16

### Added
- Logical desktop window creation with usable-area sizing and opt-in per-monitor size updates.
- Physical pixel snapping (frame switch), immediate text width/ellipsis, rectangle softness,
  spring initial values, and flexible boxes that can shrink below their content width.
- Matching C99 descriptors and window/spring entry points. Recompile consumers for the expanded descriptors.

### Fixed
- Consistent inner/outer border AA and coverage-partitioned transparent border blending.
- Sorted right-aligned table headers reserve arrow space; anonymous column caches reset when column counts change.
- Icon-only buttons center their icon.

## [0.9.1] - 2026-11-16

### Fixed
- **Linux GCC Compatibility & Warning Cleanup**: Added missing `<cstdlib>` (plus self-contained `<cstdio>` / `<cstring>` / `<cmath>`) includes across `examples/etch_studio.cpp` and `tests/checks_*.h` for strict `libstdc++` builds, and eliminated GCC `-Wall -Wextra -Wpedantic` warnings across `src/` and `tests/` (`-Wclass-memaccess`, `-Wmisleading-indentation`, `-Wextra` conditional enum mismatch, and `-Wmissing-field-initializers` on `SDL_GPU*` structs and `third_party/` headers).

### Changed
- **Documentation Sync**: Updated `README.md`, `CLAUDE.md`, and version metadata to `0.9.1`, clarifying that the active OS accessibility bridge targets Windows UIA.

## [0.9.0] - 2026-11-16

### Added
- **Pure C99 ABI (`<etch/etch_c.h>` & `src/etch_c.cpp`)**: Added zero-allocation, strict ISO C99 public ABI header `include/etch/etch_c.h` (coexisting cleanly with C++20 `<etch/etch.h>`, restricted strictly to `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`) and unity implementation `src/etch_c.cpp`, exposing `EtchContext`, `EtchArena`, `EtchTheme`, layout containers, widgets, sprites/textures, 2D vector drawing, `EtchTextBuffer`/`etch_code_editor`, 1M-row virtual lists, `DockSpace`/`Table`/`Tree`/`Canvas`, multi-window chrome/refresh callbacks, and A11y inspection/action APIs for C, Rust, Zig, Go, C#, Python, and Lua bindings.
- **C99 Conformance & Core Size Lock**: Added strict C99 verification executable `tests/etch_c_abi_tests.c` (CTest `c99_abi_smoke`), C99 downstream consumer verification `tests/consumer_smoke/main_c.c` under both `find_package(Etch)` and `add_subdirectory(...)`, `etch_c.h` version/header-purity checks, and a hard `<= 30,000` line total core engine ceiling in `cmake/check_repo_guardrails.cmake`.

### Changed
- **Pre-1.0 Governance Sync**: Updated `README.md`, `CLAUDE.md`, `AGENTS.md`, and `.github/workflows/ci.yml` to reflect `v0.6.0`–`v0.9.0` capabilities, 11 CTest verification suites, and the `<= 30,000` core LOC lock.

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
