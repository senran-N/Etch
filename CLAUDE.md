# CLAUDE.md — Etch v2.0 Architecture Constitution & AI Coding Redlines

## 1. Project Identity & End-State Goals
- **Codename**: `Etch` (Namespace `etch::`, Macro prefix `ETCH_`, Library `etch` / `libetch`)
- **Permanent architectural invariants**:
  - Orthodox C++20 zero-overhead purity, built as the single translation unit `etch_unity.cpp`.
  - Steady-state zero per-frame heap allocation: `etch::Arena` has O(1) reset; explicit/geometric capacity reserve occurs only at documented overflow boundaries.
  - 64-byte `alignas(16)` `etch::Prim`; 1–3 GPU draw calls per window UI pass, exactly 1 at idle without blur/external textures.
  - 0% CPU/GPU at idle and < 0.2 ms layout.
  - HarfBuzz + Slug GPU Bezier text/vector rendering, zero scalable font bitmap atlases, zero-copy `SDL_GPUTexture*` viewports, and mechanism-first anti-over-defensive rigor.
- **Non-Goals ("Three Don'ts")**:
  1. No HTML/CSS parser.
  2. No bloated kitchen-sink business widget library.
  3. No mobile targets during v0.1–v1.0; focus exclusively on Desktop (Windows / macOS / Linux).

---

## 2. Locked Technology Stack
- **Language**: "Orthodox C+" — C-style subset of C++20:
  - Allowed: C++20 designated initializers (`.radius = 8.0f`), `std::span`, `constexpr`, simple math vector operator overloads, POD aggregate structs.
- **Window / Input / GPU**: `SDL3` + `SDL_GPU` (Vulkan / Metal / DX12) + `SDL_shadercross` (single **HLSL** shader source compiled offline or loaded as SPIR-V / DXIL / MSL).
- **Layout**: `third_party/clay.h` (pure C single-header Flexbox layout).
- **Text Pipeline**:
  - Phase 1 (v0.1–v0.3, retired): `third_party/stb_truetype.h` bitmap glyph atlas.
  - Phase 2 (v0.6–v1.6, active): Multi-font slots (`kFontSans`/`kFontBold`/`kFontMono`/user slots), cluster-safe HarfBuzz OpenType shaping (`kern`/`liga`/`calt`) with automatic fallback chain, inline rich `text_spans` (`TextSpan`/`RichTextDesc`), SVG-to-Slug vector icons (`register_svg_icon` / `icon`), analytic `RingArc` & immediate box-local `draw_*` (`draw_text`/`draw_image`/`measure_text`), native RGBA/PNG/BMP GPU texture loader (`create_texture_rgba`/`load_texture_file`/`load_texture_memory`), frameless custom window chrome & multi-window swapchains (`claim_window_swapchain`/`set_window_chrome`), zero-heap drag-and-drop & canvas marquee multi-selection, pro desktop shortcuts/popover clamping/UTF-8 undo-redo, and Public Domain / MIT `Slug` quadratic Bezier GPU curve evaluation (`src/etch_slug.cpp`).
- **Etch v2.0 workbench**: isolated multi-window UI frames, spatial OS file/text drop, modal focus scopes, keyboard focus rings, and system theme; binary-split multi-tab `DockSpace`, resizable/sortable virtual `Table`, and collapsible hierarchical `Tree`, demonstrated by Etch Studio 2.0.
- **Etch v2.0 completeness**: selectable static `text` / `text_spans` share UTF-8-safe selection with `text_input`; query/copy via `selected_text`, `has_text_selection`, `clear_text_selection`, Ctrl+C and Ctrl+A. Selection highlights stay in the existing UI batch without layout wrappers.
- CPU/GPU `Prim` buffers reserve `max_prims_per_frame` initially (64–4,194,304), then double elastically at overflow; `FrameStats::prim_capacity` reports the CPU reserve. Steady-state frames retain storage without heap churn.
- `begin_collapsible` / `end_collapsible` and `ETCH_COLLAPSIBLE` animate clipped body height with a spring, omit settled closed content, and let settled UI sleep.
- **Memory & State**:
  - Frame temporaries: `etch::Arena` linear allocator, $O(1)$ reset at frame end.
  - Cross-frame state: flat hash table keyed by `uint32_t` semantic ID (focus, hover, scroll inertia, spring animation).

---

## 3. Build Layout (Unity Build)
```text
etch/
├── CLAUDE.md                 # This constitution
├── CMakeLists.txt            # Minimal CMake compiling etch_unity.cpp & examples
├── third_party/              # Vendored headers, HarfBuzz (headers/prebuilt or system), Slug shaders, SDL3 SDK
│   ├── harfbuzz/
│   └── slug/
├── shaders/
│   ├── etch_kawase.hlsl      # Dual-pass Kawase downsample/upsample backdrop blur & present shader
│   └── etch_uber.hlsl        # SDF rounded rect/border/gradient/shadow/Slug Bezier text uber shader
├── src/
│   ├── etch.h                # Single public header: Arena, Prim & declarative UI API
│   ├── etch_arena.cpp
│   ├── etch_slug.cpp         # HarfBuzz shaping & Slug quadratic Bezier contour/band extractor
│   ├── etch_layout.cpp
│   ├── etch_gpu.cpp
│   └── etch_unity.cpp        # Unity build translation unit including all engine .cpp files
└── examples/
    ├── sandbox.cpp           # Core interactive & "--headless" regression harness
    └── etch_studio.cpp       # Etch Studio 2.0 flagship DockSpace, resizable/sortable Table, hierarchical Tree, render-graph, vector dials, custom chrome, DND & 1M workbench + "--headless"
```

---

## 4. The Four AI Coding Redlines (Non-Negotiable)
1. **C++ Feature Blacklist**:
   - **FORBIDDEN**: Exceptions (`try`/`catch`/`throw`), RTTI (`dynamic_cast`/`typeid`), OOP virtual widget inheritance hierarchies, complex template metaprogramming.
   - **FORBIDDEN in per-frame hot paths**: `std::vector`, `std::shared_ptr`, `std::function`, `std::string`, or any general-purpose heap allocation. Allocate frame data strictly from `etch::Arena`.
2. **Zero API Hallucination**:
   - `SDL_GPU` and `clay.h` evolve rapidly. **NEVER guess function signatures, enum values, or struct fields from memory.**
   - Before writing calls in `etch_layout.cpp` or `etch_gpu.cpp`, read the actual local `SDL3/SDL_gpu.h` and `third_party/clay.h` headers in the repository.
3. **Strict 16-Byte CPU-GPU Struct Alignment & Linear Color Pipeline**:
   - Every struct uploaded to an HLSL `StructuredBuffer` or Uniform Buffer MUST carry explicit `alignas(16)` and `static_assert(sizeof(T) % 16 == 0)`.
   - Fields inside GPU structs are restricted to `Vec4`, `Uint4`, or explicitly aligned 32-bit scalars. **NEVER put a 12-byte `float3` / `Vec3` in a GPU struct.**
   - `etch::Prim` is strictly **64 bytes** (`4 x 16B`: `bounds`, `clip`, `payload`, `style`) with `alignas(16)` so `Prim[]` can pack directly into a shared `SDL_GPUTransferBuffer` after 32-byte `GlobalsUniform` with zero copy overhead.
   - `etch_uber.hlsl` binds 4 fixed sampler slots (`Slot 0` solid white fallback, `Slot 1..3` external textures/backdrop blur) plus 4 fragment `StructuredBuffer` storage slots (`t4..t7`: Slug curves & band directory) to stay within 1–3 draw calls without Bindless, and decodes sRGB `RGBA8` colors to linear space for physically accurate blending and stem darkening.
4. **Headless Visual Verification Loop**:
   - `sandbox --headless` must render a test frame offscreen without creating a visible window and write `frame_check.png` via `third_party/stb_image_write.h`.
   - After modifying shaders, batching, or layout code, rebuild, run `sandbox --headless`, and inspect `frame_check.png` for clipping offsets, alignment bugs, or AA artifacts.

---

## 5. Dual-Engine Collaboration (Antigravity Architect × Codex C++ Specialist)
- **Role Split**: Antigravity acts as the chief graphics/UI architect and anti-over-defensive gatekeeper; local `codex-cli` acts as a high-precision C++20/systems auditor and specialist for dense algorithmic modules (e.g., UTF-8/IME state machines, HarfBuzz shaping, and v0.6 Slug GPU Bezier winding/banding).
- **Invocation**:
  - Audits: `$prompt | codex exec -C d:\Myproject\Etch -s read-only -` (must explicitly instruct Codex to obey `AGENTS.md` and reject redundant checks/RAII bloat).
  - Pre-commit diffs: `codex review --uncommitted`.
- **Gatekeeping Rule**: Never blindly accept defensive boilerplate from Codex; land only concrete fixes for C++ UB, lifetime/rehash invalidation, UTF-8/IME state bugs, or `SDL_GPU` contract issues.

---

## 6. Git Commit Discipline
- Commit promptly (`git status` → `git add` → atomic `git commit`) after every verified sub-milestone or cohesive feature passes Release build (`0` warnings), `--headless` visual/behavioral checks, and FlowGuard `flow_verify`.
- Never let multi-stage changes accumulate uncommitted across milestones, and never commit compiled binaries, SDK `.lib`/`.dll`/`.pdb` blobs, or temporary screenshots.

- **Etch v1.6 vectors**: transient cycled GPU Slug filled polygons/paths/gradient area series and analytic 2D rotation, with frame-arena scratch and no CPU triangulation.
