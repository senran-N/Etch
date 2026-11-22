# CLAUDE.md — Etch Architecture Constitution

## 1. Project Identity & Strategic Positioning
- **Codename**: `Etch` (Namespace `etch::`, C ABI prefix `etch_` / `Etch`, Macro prefix `ETCH_`, Library `etch` / `libetch`).
- **North-Star Positioning**:
  - **Self-controlled, zero-Electron, C99-embeddable, high-aesthetic native GPU UI kernel** for desktop applications and professional tools (Windows / macOS / Linux).
  - **Clean Ecosystem Split with `Inlay`**:
    - **`Etch`** owns the ultra-light, high-visual-ceiling GPU rendering, layout, text, input/IME, A11y, motion, and foundational native controls with a versioned pure C99 ABI (`<etch/etch_c.h>`).
    - **`Inlay`** (separate sibling repo, strict public-API consumer) owns the high-velocity declarative HMR authoring layer (`inlay_ui` + design-token shorthands) and zero-browser native rich content (`inlay_doc`, `inlay_math`, `inlay_diagram`, `inlay_portal`).
- **Permanent Architectural Invariants**:
  1. **Orthodox C++20 Core + Versioned Pure C99 ABI**: Zero exceptions, zero RTTI, zero OOP virtual widget trees, zero per-frame STL heap containers. Every public feature maintains strict C++20 / C99 parity (`ETCH_API`, `ETCH_ABI_VERSION`, versioned `uint32_t struct_size` descriptors).
  2. **True "Zero-Electron" Memory, VRAM & Power Discipline** (Non-Negotiable):
     - Steady-state zero per-frame heap allocation (`etch::Arena` $O(1)$ reset), **paired with small-start geometric growth and active memory bounds** — NEVER mask heap churn with wasteful multi-megabyte upfront reserves.
     - Default GPU device creation MUST prefer low-power / integrated GPUs (`SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN = true`) so desktop UIs do not needlessly wake discrete GPUs or pay 70+ MB discrete-driver tax; expose an explicit opt-in (`prefer_high_performance_gpu`) for heavy compute/3D hosts.
     - Initial reserves stay lean (`max_prims_per_frame = 2048` growing geometrically, `frame_arena_bytes = 512 KiB` default with bounded growth, lazy right-sized `ShapeCache`, hot/cold split for `WidgetState`).
     - GPU uploads use **slice-sized / ring sub-allocation** — NEVER call `cycle = true` on a full-capacity multi-megabyte buffer when only a few kilobytes of primitives are submitted.
     - All transient offscreen render targets (`depth_tex`, `scene_tex`, `blur_ping/pong`, `filter_slices`) MUST release automatically after idle frames (e.g., 120 unused frames).
     - All glyph and sprite atlases MUST enforce hard memory caps with LRU eviction and small stripe staging buffers (never retain full-atlas persistent staging copies or unbounded 4096×4096 growth).
     - **Hard Verification Budgets**:
       - CPU-only core: `< 15 MB` Peak Working Set.
       - Single-window desktop showcase: target `< 50 MB` Working Set (iGPU) / `< 75 MB` (dGPU), `0%` CPU/GPU at idle, `< 0.5 ms` layout, **`≤ 8` Draw Calls** for standard multi-color UIs.
  3. **Modular Build & Unity Release Support**: Support both incremental multi-TU compilation (`ETCH_UNITY_BUILD=OFF`) and unity LTO builds (`ETCH_UNITY_BUILD=ON`).
  4. **Cross-Platform Desktop Accessibility**: Production Windows UIA (`ITextProvider`/`IScrollProvider`/`IGridProvider`) + macOS NSAccessibility + Linux AT-SPI2 / AccessKit bridge.
- **Non-Goals**:
  1. No embedded browser HTML/CSS engine or retained DOM/Entity object hierarchy inside Etch.
  2. No mobile targets during v0.x–v1.0; focus exclusively on Desktop (Windows / macOS / Linux).
  3. No kitchen-sink business widget bloat inside Etch — keep Etch's widget set crisp, foundational, and theme-driven; complex composite/rapid-iteration UI patterns belong in `Inlay` or the host.

---

## 2. Rendering, Visual Expressiveness & Text Architecture

### 2.1 High-Aesthetic GPU Pipeline (64 B Fast-Path `Prim` + Frame-Aux Style Buffers)
- Keep `etch::Prim` strictly **64 bytes, `alignas(16)`** (`static_assert(sizeof(T) % 16 == 0)`, never 12-byte `float3`/`Vec3`) so common rects, glyphs, borders, and images stay maximally compact.
- **DO NOT** cripple visual expressiveness to cram advanced styling into 64 bytes. Follow the proven indexed `ClipShapeGpu` pattern: attach lightweight frame-arena GPU auxiliary storage buffers when needed:
  1. **Multi-Layer & Inset Shadows (`ShadowLayerGpu`)**: Support stacked ambient + key elevation shadows and inner specular/inset shadows per surface.
  2. **Multi-Stop Gradients (`GradientStopGpu`)**: Support 2–8 color stops for linear, radial, and conic gradients in linear or perceptual Oklab space with IGN dithering.
  3. **Optical Vibrancy Backdrop Blur (`etch_kawase.hlsl` + blur composite)**: True macOS/Fluent-grade translucency combining Kawase blur with configurable **saturation boost, luminance/contrast lift, subtle micro-grain, and hairline specular rim** (never flat muddy gray blur).
  4. **Full 2D Subtree Affine Transforms & Alpha Masks**: `LayerDesc` supports `translate`, `scale`, `rotation_rad`, `pivot`, `opacity`, `blur_px`, blend modes, and arbitrary rounded/gradient/texture alpha masks, with exact inverse-transformed occlusion hitboxes.
  5. **Mipmapped Textures & `ImageFit`**: Support GPU mipmap generation (`num_levels > 1`) for crisp downscaled avatars/thumbnails/illustrations and native `Cover`/`Contain`/`Fill` fitting.
  6. **Custom Real-Time Materials**: `ShaderSurface` (RGBA16F, optional feedback ping-pong, up to 4 samplers) + zero-copy external `SDL_GPUTexture*` slots + `PunchHole` alpha cutouts for native underlay portals.
- **Focused Lean Shaders (Minimize PSO Bloat)**:
  - Keep GPU shaders split into small, focused pipelines (`etch_quad`, `etch_text`, `etch_image_slug`, `etch_fx`, `etch_kawase`) rather than compiling one massive 1000+ line uber-shader into a dozen heavy PSO variants.
  - Use Early-Z depth rejection (when beneficial and memory-budgeted) and spatial non-overlapping batch coalescing to keep typical UI draw calls in single digits.

### 2.2 Adaptive Hybrid Text Pipeline (Quality + Single-Draw-Call Batching)
- **Shaping & Internationalization**: HarfBuzz OpenType shaping (`kern`/`liga`/`calt`/`tnum` + configurable feature tags) with right-sized 2-bank `ShapeCache`, cluster-safe single-pass editor coloring, UAX #9 BiDi, UAX #29 grapheme clusters, UTF-8 CJK + Kinsoku line breaking, and COLR/CBDT/sbix color emoji.
- **Small Axis-Aligned UI/Code Text (≤ 24 px, unscaled/unrotated)**:
  - **Default & HiDPI (`dpi_scale >= 1.25`) Path**: OS-hinted **single-channel `R8_UNORM` grayscale glyph atlas with 4× horizontal subpixel phase** and **polarity-calibrated stem darkening** (balanced dark-on-light and light-on-dark contrast). Renders **all text colors in a single draw call** with 4× smaller VRAM footprint and zero color fringing on Retina/OLED/rotated displays across Windows (DirectWrite), macOS (CoreText), and Linux (FreeType/hinted rasterizer).
  - **Optional Low-DPI (`1.0x`) Standard RGB LCD Path**: Opt-in 3-channel LCD subpixel pipeline (`CONSTANT_COLOR` + `ONE_MINUS_SRC_COLOR`) only when explicitly requested on 100% non-HiDPI RGB LCD displays, never as the unbatched global default.
  - **Mandatory Bounded Atlas**: Hard VRAM/RAM cap with frame-stamped LRU eviction; never keep an unbounded monotonic atlas or full-atlas persistent transfer buffer.
- **Large (> 24 px), Scaled, Rotated Text & Vector Icons**: MIT/Apache-2.0 `Slug` GPU quadratic Bezier evaluation (`src/etch_slug.cpp`) + dynamic GPU vectors + multi-layer SVG icons.

### 2.3 Layout, Motion & Visual Craft Standards
- **Owned 2-Pass Layout Engine (`src/etch_layout_engine.cpp`)**: Orthodox C++20 Flexbox (`Row`/`Col`, wrap, `flex_grow`/`flex_shrink`, `Justify`, `AlignSelf`, `margin`, `aspect_ratio`) + 2D Grid (`col_tracks`/`row_tracks`, spans) + floating Anchor/Flip positioning + dirty-subtree replay cache (`CachedSubtree`) + inverse-transformed true-occlusion hitbox tree.
- **Motion System**: Analytic damped springs (`SpringSpec`, FLIP `animate_layout`, `hero_id`, `presence`) PLUS zero-heap **Cubic-Bezier easing curves, timed transitions, and stagger helpers**, honoring `time_scale` and OS reduced-motion preferences.
- **Modern Desktop Visual Craft Baseline** (Enforced on Default Themes & Built-in Controls):
  - Default `Theme::dark()` and `Theme::light()` follow restrained modern desktop craft (Linear / macOS / shadcn aesthetic): neutral balanced grays, Elevation 0/1/2/3 surface & multi-shadow tokens, subtle 1px translucent borders/top-edge specular rims, clear typographic scale and line-height rhythm.
  - Default scrollbars are **auto-hiding overlay slim thumbs** (subtle on idle, gently widening on hover/drag), never heavy permanent gray bars.
  - Decode sRGB `RGBA8` colors to linear space before blending, gradients, and stem darkening.

---

## 3. Core Rules & Redlines (Non-Negotiable)
1. **Orthodox C++20 Feature Blacklist**:
   - **FORBIDDEN**: Exceptions (`try`/`catch`/`throw`), RTTI (`dynamic_cast`/`typeid`), OOP virtual widget inheritance hierarchies, heavy template metaprogramming.
   - **FORBIDDEN in per-frame hot paths**: `std::vector`, `std::shared_ptr`, `std::function`, `std::string`, or general-purpose heap allocation. Allocate frame temporaries strictly from `etch::Arena`.
2. **Zero API Hallucination**:
   - Never guess `SDL_GPU` or OS SDK signatures/enums from memory; read local headers (`third_party/SDL3/include/SDL3/SDL_gpu.h`, Windows/macOS SDK headers) first.
3. **Strict 16-Byte GPU Alignment**:
   - Every GPU uniform/storage struct MUST carry `alignas(16)` and `static_assert(sizeof(T) % 16 == 0)`. Never use 12-byte `float3`/`Vec3`.
4. **Visual, Memory & Dual-Backend Verification Loop**:
   - Every rendering, memory, shader, text, or layout change MUST pass Release build (`0` warnings), full CTest on both `direct3d12` and `vulkan`, **memory/draw-call budget verification**, and **human-quality visual inspection** of headless PNGs saved under ignored `.ci/` (verify contrast, light/dark text crispness, alignment, shadows, blur vibrancy, and absence of clipping/aliasing defects — never stop at "pixels non-zero").

---

## 4. Git, Documentation & SemVer Discipline
- Keep `CLAUDE.md`, `AGENTS.md`, `README.md`, and `CHANGELOG.md` synchronized with actual code; never leave obsolete architecture claims (e.g., removed dependencies or old version numbers) in docs.
- Follow SemVer 2.0.0 (`CMakeLists.txt`, `include/etch/etch.h`, `include/etch/etch_c.h`, and `CHANGELOG.md` are the matching Single Source of Truth).
- Commit atomically after verified milestones; never commit compiled binaries, SDK blobs, or root PNG screenshots (`git status --porcelain` must be empty).
