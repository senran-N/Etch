# Etch

Etch is a Pure-GPU UI Engine for desktop applications on Windows, Linux, and
macOS. It combines immediate UI construction, Clay layout, Unicode text
handling, a pure C99 ABI facade (`<etch/etch_c.h>`), and SDL3 GPU rendering. The
sandbox is the regression harness; Etch Studio demonstrates docking, tables,
trees, themes, and a virtual editor.

The historical audit closure and current verification boundaries are tracked in
[CLOSURE.md](CLOSURE.md); the v0.12 corrections are in [AUDIT_v0.12.md](AUDIT_v0.12.md). Descriptor additions require recompiling
consumers. Mark titlebar layout containers with `window_drag_passthrough=true`
and register custom maximize controls with `window_maximize_button_region(id)`.

## Architecture

- Orthodox C++20, without exceptions or RTTI, plus a zero-heap pure C99 ABI
  header ([include/etch/etch_c.h](include/etch/etch_c.h)). The engine compiles as
  one translation unit, [src/etch_unity.cpp](src/etch_unity.cpp), with cohesive core modules and no hard code-size limits.
- GPU geometry uses 64-byte, `alignas(16)` `etch::Prim` (`EtchPrim`) records.
  UI geometry, glyphs, and small sprite thumbnails/images packed into the
  dynamic sprite atlas batch into 1–3 draw calls per window UI pass through
  [etch_uber.hlsl](shaders/etch_uber.hlsl); [etch_kawase.hlsl](shaders/etch_kawase.hlsl)
  handles backdrop blur and presentation. Native RGBA/PNG/JPEG/GIF/TGA/BMP
  decoding feeds both sprites and zero-copy `SDL_GPUTexture*` slots.
- HarfBuzz shapes text; [src/etch_slug.cpp](src/etch_slug.cpp) extracts Slug
  GPU Bezier contours and bands, [src/etch_emoji.cpp](src/etch_emoji.cpp) provides
  color emoji fallbacks (`COLR v0`, `CBDT`, `sbix`), and multiline `text_input` /
  `code_editor` support soft word-wrapping plus semantic `Theme` tokens.
- [src/etch_a11y.cpp](src/etch_a11y.cpp) and [src/etch_a11y_os.cpp](src/etch_a11y_os.cpp)
  expose zero-heap A11y snapshots/actions, smooth live window resize,
  multi-monitor DPI sync, and optional OS accessibility bridges (Windows UIA).
- `etch::Arena` supplies frame temporaries with O(1) reset and zero steady-state
  per-frame heap churn. Initialization and explicit/geometric capacity growth
  may allocate.
- Headless contexts defer GPU initialization to `ensure_gpu_device`; CPU
  layout work needs no GPU. Rendering, texture operations, or requesting the
  device initialize it lazily.
- Backend selection order: `ContextDesc::gpu_driver`, `ETCH_GPU_DRIVER`,
  `SDL_GPU_DRIVER`, then SDL's default. Use `direct3d12` or `vulkan` on Windows,
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
The Linux [CI workflow](.github/workflows/ci.yml) includes the source-build path.

macOS:

```sh
brew install sdl3 harfbuzz ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix)" -DETCH_BUILD_EXAMPLES=ON -DETCH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
ETCH_GPU_DRIVER=metal ./build/sandbox --headless
```

## Verification

CI builds all targets and runs these seven suites without requiring a GPU:

```sh
ctest --test-dir build --output-on-failure -R "^(repo_guardrails|downstream_consumers|arena_and_elasticity|unicode_bidi_grapheme|layout_clay_flex_grid|fenwick_virtual_list_1m|c99_abi_smoke)$"
```

With tests and installation enabled, full CTest runs twelve suites, including GPU
invariants. `repo_guardrails` checks architecture,
versions, pure C99 header includes, artifacts and OSS notices;
`downstream_consumers` installs Etch and builds/runs strict C++20 and pure C99
public-API consumers using both `find_package` and `add_subdirectory`. The five
core CPU suites have a 5s timeout, guardrails 10s, and downstream consumers 120s.
CI also requires a clean working tree and zero PNG files in the repository root.
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
find_package(Etch 0.18 CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Etch::etch)
```

Keep SDL3 and HarfBuzz discoverable via the dependency package manager or
`CMAKE_PREFIX_PATH`. The installed package exports `Etch::etch` through
`EtchConfig.cmake`; version compatibility is within the same minor release.

## License and attribution

Etch is [MIT licensed](LICENSE), Copyright (c) 2026 Etch Contributors.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for bundled dependencies
and the full license-file references. Include those applicable licenses and
notices with redistributed software; `cmake --install` currently installs
the library, headers, and CMake package, so package notices alongside them.

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

`kFontMedium` (14) and `kFontSemiBold` (15) lazily probe static faces; Windows
SemiBold probes `seguisb.ttf`. Missing faces render with Sans, and
`is_font_loaded(ctx, slot)` reports whether the requested face actually loaded.
Applications can load their own 500/600 faces into these slots. Automatic custom
font allocation uses slots 3–63, skipping reserved slots 14 and 15; up to 64 fonts
and 1024 vector icons are supported. Variable-font axis selection is not implemented.
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

Release candidate scope: Etch 0.18 pairs with Inlay 0.6 using public C++20/C99
APIs. Windows Vulkan and D3D12 are the local verification targets. Metal and
Linux Vulkan Wayland/X11 still require real-hardware CI sign-off; this is a
pre-1.0 release. Idle UI batching/sleep and bounded frame-arena use are covered
by the CPU/GPU regression suites. Inlay remains a separate optional consumer.
