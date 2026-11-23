# Changelog

All notable changes to Etch are documented here. This file follows
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- Load static SVG and WebP images, rasterize SVGs directly into the upload
  bitmap and transfer ownership of memory-backed SVG fonts to the renderer.
- Render completed frames into caller-owned, same-device SDL GPU textures.
- Read full frames or pixel rectangles into a synchronous sRGB RGBA8 callback.
  Query the lazy device initialization state and active driver from C or C++,
  and retry failed initialization on the same context.
- Keep long and multiple SDL text commits intact within the input queue limit.
- Allocate editor undo storage on the first recorded edit and grow it up to
  1,024 operations and 4 MiB of text per buffer.
- Query native editor revisions and convert between UTF-8 byte offsets and
  zero-based UTF-16 language-service positions directly on the gap buffer.
- Release idle transient render targets while the event loop is waiting and
  report incomplete GPU batch construction as a render failure.
- Build against a native SDL3 package on Linux and macOS and exercise the
  platform backends in the GPU test suites.

## [0.28.0] - 2026-11-23

- Decompose the legacy monolithic `etch_uber.hlsl` into six focused, low-VGPR GPU
  micro-shaders (`etch_quad.hlsl`, `etch_subpixel.hlsl`, `etch_slug.hlsl`,
  `etch_image.hlsl`, `etch_fx.hlsl`, `etch_kawase.hlsl`) backed by a unified lazy PSO
  cache (`PsoEntry`) and platform-stripped shader data (`src/etch_shaders_data.cpp`
  shared across both modular and unity builds), cutting PSO compilation overhead and
  runtime driver memory.
- Add hardware **Blend Modes** (`BlendMode::Normal`, `Additive`, `Multiply`, `Screen`)
  on `LayerDesc`, `BoxDesc`, and `ImageDesc` (retaining `LayerDesc::blend_plus_lighter`
  as a backward-compatible alias for `BlendMode::Additive`).
- Add composable **Alpha Masks** (`AlphaMaskKind::Rounded`, `LinearGradient`,
  `RadialSpotlight`, `Texture` sampling `.a`, plus `invert`) on `LayerDesc::alpha_mask`
  evaluated uniformly across all UI micro-shaders via the indexed `ClipShapeGpu` chain.
- Add per-text **OpenType features** (`OpenTypeFlags` `NoLiga`/`TabularNums`/
  `SlashedZero`/`SmallCaps`/`OldstyleNums`/`Fractions`/`StylisticSet01`/`StylisticSet02`,
  custom `FontFeature` spans, `make_opentype_tag`) with `ShapeCache` and layout
  text-cache key isolation, plus **Variable Font variations** (`create_font_variation`,
  `FontAxisValue`, `kAxisWeight`/`kAxisWidth`/`kAxisOptical`/`kAxisSlant`/`kAxisItalic`,
  deduplicated variation slots), HarfBuzz `hb_font_draw_glyph()` Slug curve extraction,
  and DirectWrite `IDWriteFontFace5` variable-axis hinted rasterization.
- Reduce idle RAM & draw-call overhead: split `WidgetState` into hot/cold storage
  (`WidgetCold`), move deep table/split/A11y stacks to lazy allocation, lower default
  `ContextDesc::frame_arena_bytes` to `512 KiB`, and add 4-strip hollow-border overlap
  awareness so clipped bordered card grids coalesce cleanly into single-digit draw calls.
- Maintain full pure C99 ABI parity (`include/etch/etch_c.h`): `EtchBlendMode`,
  `EtchAlphaMaskKind`, `EtchAlphaMaskDesc`, `etch_alpha_mask_desc_default()`,
  `EtchOpenTypeFlags`, `ETCH_OT_TAG`, `ETCH_AXIS_*`, `EtchFontFeature`,
  `EtchFontAxisValue`, and `etch_create_font_variation()`.

## [0.27.0] - 2026-11-22

- Add multi-layer elevation shadows (`ShadowLayer`, `Elevation` presets `Sm`/`Md`/`Lg`/`Xl`,
  up to 4 stacked layers per `BoxDesc`) and analytic rounded/continuous SDF **inset shadows**
  (`ShadowLayer::inset`, `BoxDesc::shadow_inset`, `Prim::make_shadow(..., inset = true)`) with
  zero `Prim` bloat and single-draw-call `QuadAlpha` batch coalescing.
- Add 2-to-8 stop **Linear, Radial, and Conic Gradients** (`GradientKind`, `GradientStop`,
  `GradientDesc`, `BoxDesc::gradient`, `RectDrawDesc::gradient`) with optional perceptual
  Oklab interpolation and IGN dithering backed by a 16-byte-aligned frame-aux GPU storage
  buffer (`GradientStopGpu`).
- Upgrade `BlurGlass` with optical vibrancy controls (`BoxDesc::backdrop_saturation`,
  `backdrop_brightness`, `backdrop_noise`) packed into existing 64-byte `Prim` payload with
  full backward compatibility.
- Add full 2D subtree affine rotation (`LayerDesc::rotation_rad`) with composed 2x3 layer
  transforms, automatic Slug vector routing for rotated text, and exact inverse-rotated
  occlusion hitboxes.
- Add hardware texture mipmap generation (`TextureDesc::generate_mipmaps` via
  `SDL_GenerateMipmapsForGPUTexture` + trilinear `max_lod = 16.0` linear sampler) and
  `ImageFit` (`Fill`/`Contain`/`Cover`) on `ImageDesc` and `ImageDrawDesc`.
- Add easing curves and staggered transitions (`EasingKind`, `EasingSpec`, `eval_easing`,
  `tween`, `stagger_delay`) with full pure C99 ABI parity (`include/etch/etch_c.h`).
- Upgrade default `Theme::dark()` and `Theme::light()` to modern restrained neutral desktop
  palettes, refine scrollbars to sleek auto-expanding overlay pill thumbs (transparent track
  by default, honoring explicit non-zero `scrollbar_track`), and polish default desktop
  controls with subtle elevation shadows and specular top highlights.

## [0.26.0] - 2026-11-22

- Default small hinted text (<= 24 px) to single-channel `R8_UNORM` 4x X-phase grayscale
  atlas (`GrayscaleText` pipeline with polarity-calibrated stem darkening), collapsing
  multi-color UI text into a single draw call across all colors while cutting glyph atlas
  VRAM by 4x and adding cross-platform `stb_truetype` subpixel rasterization fallback;
  retain hardware 3-channel ClearType RGB subpixel text as explicit opt-in via
  `ContextDesc::lcd_subpixel_text` and `set_lcd_subpixel_text` / `lcd_subpixel_text`
  (plus pure C99 ABI parity `etch_set_lcd_subpixel_text` / `etch_lcd_subpixel_text`).
- Add bounded hinted glyph atlas memory with steady 1024x1024 cap, safe single-frame
  2048x1024 burst, and start-of-frame LRU cache reclamation with `uv_epoch` subtree
  invalidation so long-running desktop sessions stay bounded in VRAM.
- Prefer low-power integrated GPU adapters by default via `SDL_CreateGPUDeviceWithProperties`
  (`SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN = true`) with explicit discrete-GPU
  opt-in via `ContextDesc::prefer_high_performance_gpu` (`EtchContextDesc::prefer_high_performance_gpu`).
- Shrink default RAM/VRAM footprint: lazy shader & PSO compilation on first use, lean
  initial reserves (`max_prims_per_frame = 2048`, `frame_arena_bytes = 1 MiB`, 4x leaner
  `ShapeCache` and Slug storage caps), ring-slice GPU upload transfers, and automatic idle
  release for transient atlas transfer buffers, depth targets, and blur render targets.

## [0.25.0] - 2026-11-22

- Replace the monolithic GPU ubershader path with specialized lightweight pipelines
  (`etch_quad.hlsl`, `etch_subpixel.hlsl`, `etch_uber.hlsl`), hardware 3-channel
  ClearType RGB subpixel text (`CONSTANT_COLOR` + `ONE_MINUS_SRC_COLOR` with a 4x
  X-phase linear RGBA atlas), Early-Z `D32_FLOAT` depth rejection, 32x32 spatial
  non-overlapping batch coalescing, 16x16 `BlurGlass` backdrop sharing, and exact
  disjoint-area Porter-Duff `over(border, fill)` compositing.
- Replace `third_party/clay.h` with an owned Orthodox C++20 2-pass Flexbox, 2D Grid,
  and Anchor layout engine (`src/etch_layout_engine.cpp`), dirty-subtree
  `Prim + Hitbox + A11y` replay caching (`begin_cached_subtree` /
  `end_cached_subtree` / `invalidate_cached_subtree` with `GlyphAtlas::uv_epoch`
  invalidation), and an inverse-transformed true-occlusion hitbox tree
  (`HitboxBehavior`, `topmost_hovered_id`, `clicked_without_drag`).
- Upgrade the pure C99 ABI (`include/etch/etch_c.h`) with `ETCH_ABI_VERSION`,
  `ETCH_API` export macros, `etch_abi_version()`, and versioned `uint32_t struct_size`
  fields on public descriptors with zero-copy binary-layout verification, plus optional
  modular multi-TU compilation (`ETCH_UNITY_BUILD=OFF`).
- Upgrade Windows UIA accessibility (`src/etch_a11y_os.cpp`) with COM POD
  `ITextProvider`/`ITextRangeProvider`, `IScrollProvider`,
  `IGridProvider`/`ITableProvider`/`IGridItemProvider`/`ITableItemProvider`, live UIA
  structure/property/live-region events, hitbox-backed hit testing, and clean control
  tree filtering alongside cross-platform A11y bridges.

## [0.24.0] - 2026-11-20

- Add cross-platform asynchronous native OS folder and file dialogs (`FileDialogFilter`,
  `FileDialogResult`, `open_folder_dialog`, `open_file_dialog`, `open_save_file_dialog`,
  `poll_file_dialog`, and pure C99 ABI parity `etch_open_folder_dialog` /
  `etch_open_file_dialog` / `etch_open_save_file_dialog` / `etch_poll_file_dialog`),
  backed by `SDL_ShowOpenFolderDialog`, `SDL_ShowOpenFileDialog`, and
  `SDL_ShowSaveFileDialog` with a refcounted thread-safe completion queue that copies
  filter slices safely and calls `wake_up` so 0%-idle event loops resume immediately.
- Extend `SheetDesc` (`EtchSheetDesc`) for desktop drawers with `open_state`,
  `dismiss_on_backdrop`, `dismiss_on_escape`, `show_handle`, `backdrop_color`,
  `border_color`, `border_width`, `corner_radius_px`, `inset_px`, and `backdrop_blur`.

## [0.23.0] - 2026-11-20

- Add smart sticky-bottom streaming scroll to `VirtualListDesc::stick_to_bottom`
  (`EtchVirtualListDesc::stick_to_bottom`), `VirtualListSlice::at_bottom`
  (`EtchVirtualListSlice::at_bottom`), `virtual_list_is_at_bottom`
  (`etch_virtual_list_is_at_bottom`), and `virtual_list_scroll_to_bottom`
  (`etch_virtual_list_scroll_to_bottom`). Scrolling up detaches immediately; reaching
  the bottom re-attaches cleanly with zero frame-bounce on variable-height growth.
- Add inline predictive ghost text to `TextInputDesc` (`ghost_text`, `ghost_color`,
  `accept_ghost_on_tab`) and `TextInputResult::ghost_accepted` (plus pure C99 ABI
  parity), rendering non-mutating continuation glyphs at the caret with Tab acceptance
  that preserves focus when a ghost suggestion is active.
- Add per-line diff/review decoration and gutter click reporting to `CodeEditorDesc`
  (`CodeLineDecoration`, `CodeLineDecorateFn`, `line_decorator`,
  `line_decorator_user_data`, `gutter_tag_width_px`) and `CodeEditorResult`
  (`hovered_line`, `clicked_gutter_line`), with full C99 ABI trampoline parity.
- Add cross-platform clipboard image retrieval (`ClipboardImageData`,
  `get_clipboard_image` / `etch_get_clipboard_image`) backed by `SDL_GetClipboardData`
  into caller/frame `Arena` memory without heap leaks.

## [0.22.1] - 2026-11-19

- Fix `src/etch_gpu.cpp` mid-frame `filter_slices` release bug when `LayerDesc::blur_px`
  and `BoxDesc::backdrop_blur` are used together on the first frame or after a resize:
  keep `release_blur_targets` scoped to backdrop/scene targets (`scene_tex`,
  `blur_ping_tex`, `blur_pong_tex`) and call `release_filter_targets` explicitly
  alongside it in `release_window` and `destroy_context`.

## [0.22.0] - 2026-11-19

- Bind `push_layer` `pivot_norm` to the first top-level box under explicit layers so
  `scale` transforms pivot in-place around the target box rather than window `(0, 0)`.
- Overhaul `LayerDesc::blur_px` filter targets to `R8G8B8A8_UNORM_SRGB` (50% VRAM
  reduction), 8 lazy slices, local scissored Kawase downsample passes, 120-frame
  idle target release, and preserved child `blend_plus_lighter` inside filter groups.
- Scope `Clay_SetCullingEnabled(false)` in `push_layer` to layers with non-identity
  translation or scale.
- Polish Windows frameless window chrome (`WM_ERASEBKGND` suppression during native
  frame resize/show, borderless usable-bounds fix) and add `is_window_minimized`
  (`etch_is_window_minimized`).
- Add `BoxDesc::edge_fade` (`EtchBoxDesc::edge_fade`) static per-side edge fade masks
  for clipped containers.
- Add native `TextDrawDesc` (`EtchTextDrawDesc`) `shadow_color`, `shadow_offset`, and
  allocation-free radial multi-tap `shadow_blur` halo rendering.
- Add `FontWeight` (`EtchFontWeight`) and cross-platform `load_system_font`
  (`etch_load_system_font`) system font resolver with clean fallbacks.
- Add `ShaderSurfaceCreateDesc::enable_feedback` (`EtchShaderSurfaceCreateDesc::enable_feedback`)
  ping-pong self-reference history buffer for procedural shader surfaces.

## [0.21.0] - 2026-11-19

- Add `LayerDesc::blur_px` (`EtchLayerDesc::blur_px`) optical layer defocus blur via
  non-overlapping offscreen `R16G16B16A16_FLOAT` slices and a continuous Dual-Kawase
  mip pyramid (`FrameStats::blur_pass_count` incremented for filter passes).
- Add `LayerDesc::blend_plus_lighter` (`EtchLayerDesc::blend_plus_lighter`) additive
  RGB (`SRC=ONE, DST=ONE`) + valid alpha (`SRC=ONE, DST=ONE_MINUS_SRC_ALPHA`) blending.
- Add sub-pixel horizontal feathered text wipe (`has_wipe`, `color_end`, `wipe_x`,
  `wipe_feather_px`) on `TextDrawDesc`, `TextSpan`, `EtchTextDrawDesc`, and
  `EtchTextSpan` for both `SlugGlyph` and raster `Glyph` primitives.
- Extend `ShaderSurfaceCreateDesc` / `ShaderSurfaceEvalDesc` (and C99 equivalents) with
  `num_samplers` (`0..4`), `sampler_mirror_repeat`, and `textures[4]`.
- Add `continuous_corners` superellipse squircle corners on `ImageDesc`, `ImageDrawDesc`,
  `EtchImageDesc`, and `EtchImageDrawDesc`.

## [0.20.0] - 2026-11-19

- Add `TextInputDesc::suppress_popup_keys` (`EtchTextInputDesc::suppress_popup_keys`)
  to suppress `Tab` focus advancement, `Up`/`Down` caret movement, and `Return`
  newline/submit while keeping `key_pressed` readable for autocomplete popovers.
- Add public `scroll_delta()`, `scroll_max(id)`, `request_text_input(id, rect)`,
  and `typed_text(id)` C++20/C99 primitives for custom scrollable/terminal widgets.
- Make `virtual_list_invalidate_heights` order-independent with
  `virtual_list_scroll_to_row` and `virtual_list_set_item_height` across pre- and
  post-`begin_virtual_list` call sites.

## [0.19.0] - 2026-11-18

- Compile SVG strokes into Slug vector icons with configurable caps and joins.
- Add per-side and dashed box borders, including borders on clipped containers.
- Add a lazy system italic font slot, inline rich-text icons and hoverable pills,
  and UTF-8 byte-range highlights for text inputs.

## [0.18.1] - 2026-11-18

- Align the README package example with Etch 0.18 and correct its scroll fade
  documentation.

## [0.18.0] - 2026-11-18

- Publish the pre-1.0 release candidate for public C++20/C99 consumers and Inlay.
- Audit portal geometry lifetimes, batched idle rendering, package exports and
  repository guardrail fixtures on Windows Vulkan and D3D12.
- Retain pre-1.0 status pending external adoption and real-hardware Metal and
  Linux Vulkan sign-off.

## [0.17.0] - 2026-11-17

- Add C++ and C99 element geometry snapshots and screen-to-local mapping for
  windowless portal input and IME positioning, including Canvas zoom, composed
  visual layers, opacity and ancestor clip bounds.

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
