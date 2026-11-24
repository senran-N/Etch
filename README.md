# Etch

Etch is a Pure-GPU UI Engine for desktop applications on Windows, Linux, and
macOS. It combines immediate UI construction, an owned 2-pass Flexbox / 2D Grid /
Anchor layout engine (`src/etch_layout_engine.cpp`) with dirty-subtree replay
caching and inverse-transformed occlusion hitboxes, full Unicode/HarfBuzz text
shaping, a versioned pure C99 ABI facade (`<etch/etch_c.h>`), and SDL3 GPU
rendering. `etch_showcase` demonstrates visual craft and materials; `sandbox` is
the regression harness; `etch_studio` demonstrates docking, node graphs, tables,
trees, themes, and a virtualized code editor.

The historical audit closure and current verification boundaries are tracked in
[CLOSURE.md](CLOSURE.md); the v0.12 corrections are in [AUDIT_v0.12.md](AUDIT_v0.12.md). Descriptor additions require recompiling
consumers. Mark titlebar layout containers with `window_drag_passthrough=true`
and register custom maximize controls with `window_maximize_button_region(id)`.

## Architecture

- Orthodox C++20, without exceptions or RTTI, plus a zero-heap versioned pure C99 ABI
  header ([include/etch/etch_c.h](include/etch/etch_c.h)). Supports both single-TU unity
  compilation ([src/etch_unity.cpp](src/etch_unity.cpp)) and modular multi-TU builds
  (`-DETCH_UNITY_BUILD=OFF`).
- **Lean Multi-Pipeline GPU + Indexed Aux Buffers**: 64-byte, `alignas(16)` `etch::Prim`
  (`EtchPrim`) records paired with indexed frame-arena GPU storage buffers
  (`ClipShapeGpu`, `GradientStopGpu`) and six focused low-VGPR micro-shaders with unified
  lazy PSO caching, hardware `BlendMode` (`Normal`/`Additive`/`Multiply`/`Screen`), and
  composable `AlphaMaskDesc` (`Rounded`/`LinearGradient`/`RadialSpotlight`/`Texture`):
  - [etch_quad.hlsl](shaders/etch_quad.hlsl): Early-Z opaque quads, rounded/superellipse
    boxes, disjoint-area hollow borders (4-strip non-occluding batch coalescing),
    multi-layer elevation & analytic inset shadows, and 2-to-8 stop Linear/Radial/Conic
    Oklab gradients.
  - [etch_subpixel.hlsl](shaders/etch_subpixel.hlsl): Adaptive small hinted text (`<= 24 px`)
    defaulting to a bounded `R8_UNORM` 4x X-phase grayscale atlas with polarity-calibrated
    stem darkening (single draw call across all text colors, 1/4 VRAM, text wipe, blend
    modes & alpha masks), with explicit opt-in 3-channel ClearType RGB LCD blending
    (`set_lcd_subpixel_text`).
  - [etch_slug.hlsl](shaders/etch_slug.hlsl): Slug GPU quadratic Bezier vectors, large &
    rotated text, variable font outlines (`hb_font_draw_glyph()`), and SVG icons.
  - [etch_image.hlsl](shaders/etch_image.hlsl): Mipmapped textures (`ImageFit::Fill`/
    `Contain`/`Cover`), optical vibrancy `BlurGlass` (saturation, brightness, micro-grain),
    `LayerBlur` compositing, and `PunchHole` alpha cutouts.
  - [etch_fx.hlsl](shaders/etch_fx.hlsl): Lazily instantiated special FX (`CanvasGrid`,
    `BezierWire`, `RingArc`, `MeshGrad`, animated `BorderBeam`).
  - [etch_kawase.hlsl](shaders/etch_kawase.hlsl): Dual-Kawase backdrop/layer blur, bloom,
    and presentation.
- **Strict Memory & Power Discipline**: Prefers low-power integrated GPU adapters by
  default (`ContextDesc::prefer_high_performance_gpu = false`), platform-stripped shader
  arrays, lazy shader/PSO creation, hot/cold `WidgetState` split, lean `512 KiB` default
  frame arena and `2048`-prim initial reserves with geometric growth, ring-slice GPU
  uploads, bounded LRU glyph atlas reclamation, and automatic idle release for offscreen
  render/staging targets.
- HarfBuzz shapes text with per-call OpenType features (`OpenTypeFlags`, `FontFeature`)
  and deduplicated variable font variations (`create_font_variation`, `FontAxisValue`);
  [src/etch_slug.cpp](src/etch_slug.cpp) extracts Slug GPU Bezier contours and bands via
  `hb_font_draw_glyph()`, [src/etch_emoji.cpp](src/etch_emoji.cpp) provides color emoji
  fallbacks (`COLR v0`, `CBDT`, `sbix`) and hinted small-text rasterization (including
  DirectWrite variable axes), and multiline `text_input` / `code_editor` support soft
  word-wrapping plus modern semantic `Theme` tokens and easing/spring motion
  (`eval_easing`, `tween`, `stagger_delay`, 2D subtree `LayerDesc::rotation_rad`).
- [src/etch_a11y.cpp](src/etch_a11y.cpp) and [src/etch_a11y_os.cpp](src/etch_a11y_os.cpp)
  expose zero-heap A11y snapshots/actions, smooth live window resize,
  multi-monitor DPI sync, and optional OS accessibility bridges (Windows UIA).
- `etch::Arena` supplies frame temporaries with O(1) reset and zero steady-state
  per-frame heap churn. Initialization and explicit/geometric capacity growth
  may allocate.
- Headless contexts defer GPU initialization to `ensure_gpu_device`; CPU
  layout work needs no GPU. Rendering, texture operations, or requesting the
  device initialize it lazily.
- `gpu_status()` and `gpu_driver_name()` expose the lazy device state to hosts.
  A newly created headless context reports `Uninitialized`, a usable device
  reports `Ready`, and a failed creation reports `Failed` with an empty driver
  name. Between frames, `retry_gpu_initialization()` lets a host retry that
  failure on the same context, optionally selecting another driver.
- Use `ContextDesc::gpu_driver` to select a backend per context and
  `ETCH_GPU_DRIVER` to set the default. SDL's process-wide `SDL_GPU_DRIVER` hint
  takes priority during device creation. Use `direct3d12` or `vulkan` on Windows,
  `vulkan` on Linux, and `metal` on macOS.

## Build

Requires CMake 3.24+, a C++20 compiler, Ninja, SDL3 **3.4.0+** (PNG APIs),
HarfBuzz, and a system font. CMake prefers the local SDL SDK and HarfBuzz
archive/source tree under `third_party/`, then installed packages. Fresh
checkouts need system dependencies; local SDK binaries are ignored by Git.
Checked-in DXIL, SPIR-V, and MSL headers allow builds without shader tools.
Optional DXC and SDL_shadercross regenerate shaders offline when available.

Windows: run in an **x64 Native Tools Command Prompt for VS 2022** with
Ninja and vcpkg installed (`VCPKG_ROOT` points to vcpkg):

```bat
vcpkg install sdl3:x64-windows harfbuzz:x64-windows
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DETCH_BUILD_EXAMPLES=ON -DETCH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build\sandbox.exe --headless
```

MSVC targets use `/W4 /permissive- /GR- /EHs-c- /utf-8`.

Linux (Debian/Ubuntu with SDL3 3.4.0+ packaged):

```sh
sudo apt-get update
sudo apt-get install cmake ninja-build pkg-config libsdl3-dev libharfbuzz-dev fonts-dejavu-core
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DETCH_BUILD_EXAMPLES=ON -DETCH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
ETCH_GPU_DRIVER=vulkan ./build/sandbox --headless
```

If SDL3 is absent or older, build a compatible SDL3 release with CMake and
pass its installation prefix using `-DCMAKE_PREFIX_PATH=/path/to/sdl3`.

macOS:

```sh
brew install sdl3 harfbuzz ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix)" -DETCH_BUILD_EXAMPLES=ON -DETCH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
ETCH_GPU_DRIVER=metal ./build/sandbox --headless
```

## Verification

These seven suites run without requiring a GPU:

```sh
ctest --test-dir build --output-on-failure -R "^(repo_guardrails|downstream_consumers|arena_and_elasticity|unicode_bidi_grapheme|layout_clay_flex_grid|fenwick_virtual_list_1m|c99_abi_smoke)$"
```

With tests and installation enabled, full CTest runs twelve suites, including GPU
invariants. `repo_guardrails` checks architecture,
versions, pure C99 header includes, artifacts and OSS notices;
`downstream_consumers` installs Etch and builds/runs strict C++20 and pure C99
public-API consumers using both `find_package` and `add_subdirectory`. The five
core CPU suites have a 5s timeout, guardrails 10s, and downstream consumers 120s.
Verification also requires a clean working tree and zero PNG files in the repository root.
`--headless` renders
offscreen and writes `frame_check.png` in the working directory; it still
requires a supported GPU device/driver. Check the image as well as assertions.
For Windows backend verification in PowerShell:

```powershell
$env:ETCH_GPU_DRIVER = 'vulkan'
ctest --test-dir build --output-on-failure
.\build\sandbox.exe --headless
$env:ETCH_GPU_DRIVER = 'direct3d12'
ctest --test-dir build --output-on-failure
.\build\sandbox.exe --headless
Remove-Item Env:ETCH_GPU_DRIVER
```

## Consume with CMake

Add Etch as a subdirectory and include `<etch/etch.h>` (C++20) or `<etch/etch_c.h>` (pure C99):

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_app LANGUAGES C CXX)
add_subdirectory(external/Etch)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Etch::etch)

add_executable(my_c_app main.c)
set_target_properties(my_c_app PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(my_c_app PRIVATE Etch::etch)
```

`ETCH_BUILD_EXAMPLES` and `ETCH_BUILD_TESTS` default to OFF for subprojects
(ON at the top level). `ETCH_INSTALL` follows the same default. The target
exposes the public include directory, SDL3, and C++20; engine internals and
Orthodox compiler flags remain private.

MSVC consumers on non-UTF-8 system code pages should compile their own sources
with `target_compile_options(my_app PRIVATE /utf-8)` to read the public header
without encoding warnings.

Or install the standalone Release build above to a local prefix:

```sh
cmake --install build --prefix "$PWD/install"
cmake -S /path/to/consumer -B consumer-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/install"
cmake --build consumer-build --parallel
```

In Windows PowerShell use `--prefix "$PWD/install"` and
`-DCMAKE_PREFIX_PATH="$PWD/install"` as well; pass the vcpkg toolchain when
configuring the consumer. If installation was disabled, reconfigure Etch with
`-DETCH_INSTALL=ON` first. The consumer's `CMakeLists.txt` uses:

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_app LANGUAGES C CXX)
find_package(Etch 0.29 CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Etch::etch)
```

Keep SDL3 and HarfBuzz discoverable via the dependency package manager or
`CMAKE_PREFIX_PATH`. The installed package exports `Etch::etch` through
`EtchConfig.cmake`; version compatibility is within the same minor release.

## License and attribution

Etch is [MIT licensed](LICENSE), Copyright (c) 2026 Etch Contributors.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for bundled dependencies
and the full license-file references. `cmake --install` installs `LICENSE` and
`THIRD_PARTY_NOTICES.md` under `${CMAKE_INSTALL_DOCDIR}` alongside the library,
headers, and CMake package; include them when redistributing software.

Slug is available under MIT or Apache-2.0. Its README states that the patent
has been dedicated to the public domain and requires credit for distribution:
**Slug shader code Copyright 2017 by Eric Lengyel.**

### Desktop layout and raster precision

`create_desktop_window("Chisel", 1360, 840, SDL_WINDOW_RESIZABLE)` creates a
high-density window from logical dimensions, fitting its client area to the primary
usable display. Pass SDL events to `process_event` and use `begin_window_frame`;
helper-created normal windows resize proportionally when the display scale changes.
Maximized/fullscreen windows retain OS-managed sizes. Use `create_desktop_window_on_display` for a specific SDL display ID (0 means primary),
or `create_desktop_window_at_pointer` for the cursor display.
`set_desktop_window_min_size(window, logical_w, logical_h)` stores logical minima
and reapplies them on DPI changes without repeated rounding drift. Existing raw minima
set with `SDL_SetWindowMinimumSize` scale proportionally on helper-created windows.
Destroy with `SDL_DestroyWindow`.

`FrameDesc::pixel_snap` / `WindowFrameDesc::pixel_snap` default to true. Rectangle
edges, images, glass bounds, layout clip bounds and border widths snap after DPI conversion; subpixel
rectangles keep their original bounds when snapping would collapse them. Text, rotated shapes, shadows,
and soft rectangles retain fractional positioning. Set false for continuous animation.
`RectDrawDesc::softness` is a physical-pixel AA width (minimum 0.5), for solid and
gradient axis-aligned rectangles. `border_align` selects Inside (default), Center or
Outside for boxes and immediate rectangles; layout dimensions retain their original size.

Use `.shrink_width = true` on a Fit/Grow content box in a toolbar to let it shrink
below its intrinsic text width and clip overflow; give a trailing close button a
fixed width. `shrink_height` applies the same rule to vertical workspaces, preserving
a fixed footer below a flexible panel. Fixed and Percent items retain their explicit sizing rules.
`TextDrawDesc{..., .max_width_px = 100, .ellipsis = true}` limits each drawn line in
logical pixels using the shared cluster-safe BiDi text engine. With ellipsis, alignment
uses the elided line width. Without ellipsis, alignment positions the original line:
right alignment keeps the trailing portion and center alignment keeps its middle.
Width zero is unlimited. `spring(id, target, initial_value)` returns the
initial value on the first frame and animates toward the target on later frames.
C99 equivalents use the same descriptor fields plus `etch_create_desktop_window`
and `etch_spring_from`; initialize descriptors with their `*_default()` functions.

### Surface and typography controls

`BoxDesc::continuous_corners = true` and `RectDrawDesc::continuous_corners = true`
use exponent-4 superellipse corner segments with zero curvature at straight joins.
This is a gradient-normalized distance approximation, not an exact Apple corner profile.
Box fills, borders, shadows and child clips share the mode. Nonuniform and nested
rounded clips preserve all four corner radii through an indexed GPU clip chain, including
zero-radius child clips inside a rounded parent. Circular corners remain the default.

For a downward card shadow use `shadow_offset = {0, 12}`, `shadow_blur = 20`,
`shadow_spread = -6`; box values are logical pixels. Low-level `Prim::make_shadow`
accepts offset/spread in physical pixels. A spread that collapses the shadow omits it.

`kFontMedium` (14), `kFontSemiBold` (15), and `kFontItalic` (16) lazily probe system faces. Windows
SemiBold probes `seguisb.ttf`. Missing faces render with Sans, and
`is_font_loaded(ctx, slot)` reports whether the requested face actually loaded.
Applications can load their own faces into those slots. Automatic allocation
uses free custom slots from 3 through 63, skipping slots 14, 15, and 16. Etch
supports up to 64 font slots and 1024 vector icons. Variable-font axis selection
is supported via `create_font_variation` (`etch_create_font_variation` in C99)
with deduplicated axis slots, HarfBuzz outline shaping, and platform hinted
rasterization (DirectWrite `IDWriteFontFace5`, CoreText, and FreeType via
`ETCH_USE_FREETYPE=ON` on Linux). The hinted glyph atlas performs shelf-aware
LRU reclamation in steady state and hot-glyph compaction on burst shrink-back.
Before starting a frame, call `set_font_tabular_numbers(ctx, font_id, true)` to
request OpenType `tnum` consistently in measured and rendered text. The font must
provide the feature; it is not synthesized. Load a separate custom slot if prose
and numeric columns need different numeral settings for the same face.

Tables accept `show_column_lines = false` and `header_font_size_px = 14` for a
line-free header with existing configurable zebra rows. C99 exposes the
corresponding descriptors and font API.

### Desktop navigation and texture uploads

Tables share a horizontal viewport between header and rows. `horizontal_scroll`
and `keyboard_navigation` default to true; `show_scrollbar = false` hides both thumbs.
Use `TableSelection` with caller-owned focus/anchor rows and a bitmap of at least
`ceil(item_count / 64)` words for Shift ranges, Primary toggles and Primary+A.
Arrow keys, PageUp/PageDown and Home/End reveal the focused row. Initialize focus
and anchor to `UINT32_MAX` for no selection. The existing `selected_row` API remains available; an explicit `selection` takes precedence
for highlighting, so a cleared bitmap bit stays unselected even when legacy focus is retained.

`SplitDesc::divider_size_px` sets visual thickness; `divider_hit_px` defaults to 8
for a thin divider that remains easy to drag. Tab focus enables arrow-key resizing
with `keyboard_step_px` (default 8), and Home/End obey pane minima. Combo popups
virtualize visible rows and reveal keyboard selection, including PageUp/PageDown and Home/End.

A batch accepts eight distinct external textures plus the atlas. Texture creation
and updates copy caller pixels immediately into a reusable cycled staging buffer,
then queue their GPU copies together. Etch flushes the upload command buffer before
rendering. If another renderer consumes a texture first, call `flush_texture_uploads(ctx)`
(`etch_flush_texture_uploads` in C99) on the owning GPU thread before submitting its commands.
Destroying a texture flushes pending uploads; destroying the context cancels them.

Use `render_headless_pixels` or `etch_render_headless_pixels` to capture a finished
frame or rectangle in memory. The call waits for GPU completion and lends sRGB
RGBA8 rows to the callback. Use the supplied pitch to address successive rows.
The PNG writer uses the same readback path.

Use `render_to_texture` or `etch_render_to_texture` after finishing a frame to
composite Etch into another GPU view. Create a single-sampled 2D texture with
`SDL_GPU_TEXTUREUSAGE_COLOR_TARGET` on the device returned by `gpu_device`.
The target format is the SDL GPU format value. Supported formats are RGBA8 or
BGRA8, including their sRGB formats, and RGBA16F. The submission is asynchronous
on the owning GPU thread. Submit the host's sampling pass after Etch's call and
give the texture sampler usage if it will be sampled.

v0.13 extends C++/C99 descriptors and changes packed `Prim` header bindings while
preserving its 64-byte size. Recompile downstream consumers and use matching shader
headers. DXIL, SPIR-V and offline Metal fallbacks are regenerated from the same HLSL.

### Perceptual color and motion

`Theme::from_seed(seed, dark_mode)` builds a contrast-tested palette; `Color::oklch`
and `Color::lerp_oklab` provide perceptual color control. `ETCH_LAYER` changes
opacity, translation and scale without adding layout boxes. `ETCH_PRESENCE(id, mounted)`
keeps content mounted through its exit fade. Both scopes balance on break and return.
Use `BoxDesc::animate_layout` for FLIP and a stable `hero_id` for transitions between
parents. Visual transforms preserve layout geometry; interaction coordinates remain
in layout space. `set_time_scale(ctx, 0)` pauses springs; F10 switches between 1x
and 0.2x while the F12 HUD is open. Windows reduced-motion preferences settle
springs immediately when `respect_reduced_motion` is enabled.

### Mesh gradients, border beams and scroll fades

`draw_mesh_rect` blends four corner colors in Oklab (or linear RGB with
`oklab=false`), with optional sRGB grain and an explicit warp phase.
`draw_border_beam` paints a conic beam and glow along rounded or
continuous corners. Set `BoxDesc::border_beam` and `top_rim_alpha` for the same
effect after a box's children. `angle_rad` sets a stationary angle; enabling
`beam_active` advances it at `speed` radians per second. `symmetric_dual` adds an
opposite beam; `base_color` supplies the unswept border. Animated beams honor time
scaling and reduced motion, and stationary beams let the frame sleep.

`scroll_fade_px` on boxes, virtual lists, tables and code editors masks scrollable
content edges through the indexed clip buffer. The top/left mask disappears at
scroll zero and the bottom/right mask disappears at the end; backgrounds and
scrollbars retain their opacity. The fade uses a smoothstep ramp in logical
pixels and adds no draw calls.

Text buffers expose revision-bound diagnostics, UTF-8 search (`text_buffer_find`,
`text_buffer_find_all`), and atomic multi-range edits (`text_buffer_apply_edits`).
Diagnostics copy messages cleanly, remain visible only for the revision that
produced them, and render highest-severity row and gutter markers in the code
editor. `code_editor` also supports inline ghost-text completion (`Tab` accept /
`Escape` dismiss), search highlights, line folding, and multi-cursor atomic edits.

For desktop resilience, tooling, and accessibility, Etch provides in-place GPU
device recovery (`mark_gpu_device_lost`, `recreate_gpu_device`,
`gpu_device_generation`, `set_gpu_device_reset_callback`), non-blocking async
readback (`begin_async_readback`, `poll_async_readback`), pure-CPU thread-safe
image/SVG decoding (`decode_image_memory`, `rasterize_svg_memory`), structured
frame/memory tracing with JSON export and P50/P95/P99 percentiles
(`set_trace_enabled`, `trace_export_json`, `trace_compute_percentiles`),
semantic A11y roles/scroll actions and live announcements (`emit_a11y_node`,
`announce_a11y`, `ScrollUp`/`ScrollDown`), OS appearance/i18n queries and
opt-in automatic propagation (`system_preferences`, `apply_system_preferences`),
extended pen/touch/pinch gesture-phase `pointer_state`, scoped and chorded
application commands with runtime rebinding (`register_command`,
`rebind_command_shortcut`, `find_command_shortcut_conflict`), pixel-free
semantic automation with full input replay and failure reports
(`automation_find_nodes`, `automation_assert_node`, `automation_replay_frame`,
`automation_export_failure_report_json`), dock layout JSON persistence and
cross-window tear-off/redock (`dock_save_layout_json`, `dock_load_layout_json`,
`dock_detach_pane`, `dock_transfer_pane`, `dock_redock_window_panes`), table
frozen columns and column reordering (`table_set_frozen_columns`,
`table_set_column_order`), and cross-app multi-MIME drag and clipboard
negotiation (`set_clipboard_mime`, `get_clipboard_mime`) — all mirrored in
`<etch/etch_c.h>`.

`punch_hole` erases previously drawn color and alpha inside a rounded shape.
Draw overlays afterward to keep them above the transparent opening. Hole runs
use a lazily created destination-erase pipeline; normal/hole/normal ordering
uses three draws, while frames without holes retain the normal batch path.
`softness` is a physical-pixel feather width, including the outer half of the
edge. All these APIs have C99 equivalents.

`update_texture_region_rgba` queues an RGBA8 mip-zero subregion without cycling
the destination, preserving surrounding texels. The caller supplies a region
inside the texture and a byte row pitch (zero means tightly packed); source
pixels may be reused immediately after the call. `flush_texture_uploads` submits
queued uploads for consumers outside Etch's frame rendering.

## Materials and workbench primitives

`etch_showcase --headless output.png` renders the dashboard, shared hero pill,
procedural aurora, agent nodes, terminal and snap sheet. Run without arguments
for the interactive demo. UI primitives remain batched; shader surfaces and
opt-in bloom use additional material/postprocessing passes.

Create shader surfaces outside a UI frame, evaluate once during their owner
window's frame, then draw the borrowed RGBA16F texture. Keep the surface alive
until rendering finishes. Revision/parameter changes invalidate its cache;
continuous surfaces honor `max_fps`, reduced motion, paused time and hidden or
minimized windows. Destroy surfaces outside frames. The exported CMake helper
`etch_compile_hlsl_surface(TARGET app SOURCE /absolute/aurora.hlsl NAME aurora)`
requires DXC and optionally shadercross for Metal. Fragment input is TEXCOORD0;
uniform b0/space3 contains viewport/time and four float4 parameters (80 bytes).

`terminal_grid` consumes a physical row ring of caller-owned Unicode cells;
ANSI parsing belongs to the application. Width 2 marks a wide leading cell and
width 0 its continuation. `begin_sheet`/`end_sheet` supports bottom, left and
right edges, normalized snap fractions and velocity-aware release projection.
`WindowBackdrop` selects Mica/Acrylic on supported Windows versions; create an
SDL transparent window for alpha composition. Other platforms retain their
normal SDL window behavior.

Portal hosts can query `element_geometry(id, &geometry)` and
`screen_to_element_local(id, screen_position, &local_position)` (C99 equivalents
`etch_element_geometry` / `etch_screen_to_element_local`). Named boxes expose the
most recent frame composition; queries during layout construction read the prior
frame. Coordinates are logical screen pixels and local units before Canvas zoom
and visual layers. Captured drags may map points outside the box. Visible bounds
intersect ancestor clip bounding rectangles; rounded masks and sibling overlap
still require the host's hit-testing policy. Outputs remain unchanged on failure.

Release candidate scope: Etch 0.29 pairs with Inlay 0.14 using public C++20/C99
APIs (`SameMinorVersion` package compatibility). Windows Vulkan and D3D12 are
the locally verified Tier-1 targets. Metal and
Linux Vulkan Wayland/X11 backends (including FreeType hinting, Fontconfig/CoreText
system font lookup, macOS `NSAccessibility` hierarchy, and Linux AT-SPI2 / optional
AccessKit C bridges) are implemented in-tree and still await real-hardware CI
sign-off; this is a pre-1.0 release. Idle UI batching/sleep and bounded frame-arena
use are covered by the CPU/GPU regression suites. Inlay remains a separate optional consumer.
