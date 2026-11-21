# CLAUDE.md — Etch Architecture Constitution (v0.25+ Blueprint)

## 1. Project Identity & End-State Goals
- **Codename**: `Etch` (Namespace `etch::`, C ABI prefix `etch_` / `Etch`, Macro prefix `ETCH_`, Library `etch` / `libetch`)
- **Differentiating Core Identity**:
  - ImGui-level immediate-mode ergonomics + Orthodox C++20 zero-heap core + versioned pure C99 ABI embeddable from any language.
  - Hybrid GPU rendering engine: specialized lightweight GPU pipelines + hardware LCD subpixel text for razor-sharp small UI/code text + Slug GPU quadratic Beziers for infinite-zoom typography, rotation, SVG icons, and dynamic paths + custom real-time `ShaderSurface` materials.
  - Owned 2-pass Flexbox + 2D Grid + Anchor layout kernel with dirty-subtree caching and inverse-transformed occlusion hitboxes (zero 1-frame layout lag, zero retained Entity/OOP state-machine bloat).
- **Permanent Architectural Invariants**:
  - Orthodox C++20 zero-overhead core with a zero-heap, versioned pure C99 ABI facade (`include/etch/etch_c.h` + `src/etch_c.cpp`, `ETCH_API` exports, `uint32_t struct_size` descriptors).
  - Modular source organization supporting both incremental multi-TU builds and `ETCH_UNITY_BUILD` for LTO releases.
  - Steady-state zero per-frame heap allocation: `etch::Arena` has $O(1)$ reset; explicit/geometric capacity reserve occurs only at documented overflow boundaries.
  - All GPU instance/uniform structs are `alignas(16)` with `static_assert(sizeof(T) % 16 == 0)`. Specialized pipelines + Early-Z depth rejection + spatial non-overlapping batch reordering minimize GPU Render Pass switches and fragment overdraw.
  - 0% CPU/GPU at idle and sub-millisecond layout.
  - Cross-platform OS accessibility (full Windows UIA `ITextProvider`/`IScrollProvider`/`IGridProvider` + macOS NSAccessibility + Linux AT-SPI2 / AccessKit bridge).
- **Non-Goals ("Three Don'ts")**:
  1. No browser HTML/CSS parser or retained DOM/Entity object hierarchy.
  2. No bloated kitchen-sink business widget library.
  3. No mobile targets during v0.1–v1.0; focus exclusively on Desktop (Windows / macOS / Linux).

---

## 2. Technology Stack
- **Language**: "Orthodox C+" — C-style subset of C++20:
  - Allowed: C++20 designated initializers (`.radius = 8.0f`), `std::span`, `constexpr`, simple math vector operator overloads, POD aggregate structs.
- **Window / Input / GPU**: `SDL3` + `SDL_GPU` (Vulkan / Metal / DX12) + `DXC` offline HLSL → DXIL / SPIR-V + checked-in MSL 2.1 fallbacks.
  - **Specialized GPU Pipelines**:
    1. `etch_quad.hlsl`: Lightweight Opaque & Alpha Quad / Border / Gradient / Shadow pipeline with Early-Z depth (`D32_FLOAT` / `D16_UNORM`) and proper `over(fill, border)` compositing + IGN dithering.
    2. `etch_subpixel.hlsl`: Hardware 3-channel LCD Subpixel Text pipeline using `SDL_GPU_BLENDFACTOR_CONSTANT_COLOR` + `SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR` (exact ClearType/RGB subpixel compositing on D3D12, Vulkan, and Metal in a single pass) with 4× subpixel X-phase glyph variants and gamma/contrast stem darkening.
    3. `etch_uber.hlsl`: Slug GPU quadratic Bezier glyphs/vectors, external textures/sprite atlas, analytic `RingArc`/`BezierWire`, `Grid`, `BlurGlass`, `LayerBlur`, and `PunchHole`.
    4. `etch_kawase.hlsl`: Multi-pass Kawase backdrop/layer blur with monotonic scissor margins.
  - **Spatial Batching**: Frame-arena overlap layering reorders non-overlapping primitives of matching pipeline/material to maximize batching and minimize pipeline/pass switches without violating visual painter's order.
- **Layout & Interaction**:
  - **Owned 2-Pass Layout Engine (`src/etch_layout_engine.cpp`)**: Orthodox C++20 Flexbox (`Row`/`Col` wrap, `flex_grow`/`flex_shrink`, `SpaceBetween/Around/Evenly`, `AlignSelf`, `margin`, `aspect_ratio`) + 2D Grid (column & row tracks, `col_span`/`row_span`) + floating Anchor/Flip positioning + true float coordinates + geometrically growing node arena (no fixed element cap).
  - **Dirty Subtree Caching**: Frame-coherent `CachedSubtree` replays cached layout boxes, primitives, hitboxes, and A11y nodes in $O(1)$ when `(content_hash, constraints, theme_epoch, dpi)` are unchanged.
  - **True Occlusion Hitbox Tree**: Inverse-transformed `Hitbox` stack with `HitboxBehavior` (`Normal`, `BlockPointerExceptScroll`, `BlockPointer`), single topmost hover target, and separate click vs drag arbitration.
- **Text Pipeline (Hybrid Best-of-Both-Worlds)**:
  - **HarfBuzz** OpenType shaping (`kern`/`liga`/`calt`) with fixed-capacity 2-bank `ShapeCache`, cluster-safe single-pass CodeEditor token coloring, UAX #9 BiDi, UAX #29 grapheme clusters, and UTF-8 CJK + Kinsoku line breaking.
  - **Small Axis-Aligned Text (≤ 24 px, unscaled/unrotated)**: OS-hinted RGB LCD subpixel glyph atlas (DirectWrite ClearType / CoreText / FreeType) rendered through `etch_subpixel.hlsl`.
  - **Large / Scaled / Rotated Text & Vectors**: MIT / Apache-2.0 `Slug` GPU quadratic Bezier curve evaluation (`src/etch_slug.cpp`) + dynamic GPU vectors + SVG icons (`register_svg_icon`) + COLR/CBDT/sbix color emoji.
- **Memory & State**:
  - Frame temporaries: `etch::Arena` linear allocator, $O(1)$ reset at frame end.
  - Cross-frame state: generational `StateMap` keyed by non-zero scoped `uint32_t` ID (`push_id`/`pop_id`), with automatic stale-entry pruning (`state_map_prune`) and consuming keyboard/shortcut routing.

---

## 3. Core Rules & Redlines (Non-Negotiable)
1. **C++ Feature Blacklist**:
   - **FORBIDDEN**: Exceptions (`try`/`catch`/`throw`), RTTI (`dynamic_cast`/`typeid`), OOP virtual widget inheritance hierarchies, complex template metaprogramming.
   - **FORBIDDEN in per-frame hot paths**: `std::vector`, `std::shared_ptr`, `std::function`, `std::string`, or general-purpose heap allocation. Allocate frame data strictly from `etch::Arena`.
2. **Zero API Hallucination**:
   - Never guess `SDL_GPU` or OS SDK signatures/enums from memory; read the local headers first.
3. **Strict 16-Byte GPU Struct Alignment & Linear Color Pipeline**:
   - Every GPU uniform/storage struct MUST carry `alignas(16)` and `static_assert(sizeof(T) % 16 == 0)`. Never use 12-byte `float3`/`Vec3`.
   - Decode sRGB `RGBA8` colors to linear space before blending, gradients, and stem darkening.
4. **Headless Visual Verification Loop**:
   - Every shader, batching, text, or layout change MUST pass Release build (0 warnings), CTest on both `direct3d12` and `vulkan`, and visual inspection of headless PNGs saved under ignored `.ci/`.

---

## 4. Git Commit & SemVer Discipline
- Commit promptly (`git status` → `git add` → atomic `git commit`) after every verified milestone passes Release build (`0` warnings) and dual-backend CTest/headless checks.
- Never commit compiled binaries, SDK blobs, or root PNG screenshots.
- Follow SemVer 2.0.0 (`CMakeLists.txt`, `include/etch/etch.h`, `include/etch/etch_c.h`, and `CHANGELOG.md` are the matching Single Source of Truth).
