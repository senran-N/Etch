# CLAUDE.md — Etch Architecture Constitution (v0.11.0)

## 1. Project Identity & End-State Goals
- **Codename**: `Etch` (Namespace `etch::`, C ABI prefix `etch_` / `Etch`, Macro prefix `ETCH_`, Library `etch` / `libetch`)
- **Permanent architectural invariants**:
  - Orthodox C++20 zero-overhead core with a zero-heap pure C99 ABI facade (`include/etch/etch_c.h` + `src/etch_c.cpp`), built as the single translation unit `etch_unity.cpp`.
  - Total core engine LOC (`include/etch/*.h`, `src/*.cpp`, `src/*.h`, `shaders/*.hlsl`) permanently locked at **<= 30,000 lines**.
  - Steady-state zero per-frame heap allocation: `etch::Arena` has O(1) reset; explicit/geometric capacity reserve occurs only at documented overflow boundaries.
  - 64-byte `alignas(16)` `etch::Prim` (`EtchPrim`); 1–3 GPU draw calls per window UI pass, exactly 1 at idle without blur/external textures.
  - 0% CPU/GPU at idle and < 0.2 ms layout.
  - HarfBuzz + Slug GPU Bezier text/vector rendering, dynamic sprite atlas + decoded image loading, zero scalable font bitmap atlases, zero-copy `SDL_GPUTexture*` viewports, optional OS accessibility bridges, and mechanism-first anti-over-defensive rigor.
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
  - Phase 2 (v0.6–v1.6, active): Multi-font slots (`kFontSans`/`kFontBold`/`kFontMono`/user slots), cluster-safe HarfBuzz OpenType shaping (`kern`/`liga`/`calt`) with automatic fallback chain, inline rich `text_spans` (`TextSpan`/`RichTextDesc`), SVG-to-Slug vector icons (`register_svg_icon` / `icon`), analytic `RingArc` & immediate box-local `draw_*` (`draw_text`/`draw_image`/`measure_text`), native RGBA/PNG/BMP GPU texture loader (`create_texture_rgba`/`load_texture_file`/`load_texture_memory`), frameless custom window chrome & multi-window swapchains (`claim_window_swapchain`/`set_window_chrome`), zero-heap drag-and-drop & canvas marquee multi-selection, pro desktop shortcuts/popover clamping/UTF-8 undo-redo, and MIT / Apache-2.0 `Slug` quadratic Bezier GPU curve evaluation (`src/etch_slug.cpp`).
- **Workbench & Multi-Window**: isolated multi-window UI frames, spatial OS file/text drop, modal focus scopes, keyboard focus rings, and system theme; binary-split multi-tab `DockSpace`, resizable/sortable virtual `Table`, and collapsible hierarchical `Tree`, demonstrated by Etch Studio.
- **Text Selection & Collapsibles**: selectable static `text` / `text_spans` share UTF-8-safe selection with `text_input`; query/copy via `selected_text`, `has_text_selection`, `clear_text_selection`, Ctrl+C and Ctrl+A. Selection highlights stay in the existing UI batch without layout wrappers.
- CPU/GPU `Prim` buffers reserve `max_prims_per_frame` initially (64–4,194,304), then double elastically at overflow; `FrameStats::prim_capacity` reports the CPU reserve. Steady-state frames retain storage without heap churn.
- `begin_collapsible` / `end_collapsible` and `ETCH_COLLAPSIBLE` animate clipped body height with a spring, omit settled closed content, and let settled UI sleep.
- **Memory & State**:
  - Frame temporaries: `etch::Arena` linear allocator, $O(1)$ reset at frame end.
  - Cross-frame state: flat hash table keyed by `uint32_t` semantic ID (focus, hover, scroll inertia, spring animation).

---

## 3. Build Layout (Unity Build)
```text
etch/
|-- CLAUDE.md                 # Architecture constitution
|-- CMakeLists.txt            # Out-of-source build, Etch::etch, install/export
|-- cmake/
|   |-- check_repo_guardrails.cmake # Architecture, version and OSS checks
|   |-- check_downstream_consumers.cmake # Installed/subproject consumer checks
|   |-- embed_msl.cmake       # Embed offline Metal source as C byte arrays
|   `-- EtchConfig.cmake.in   # Installed dependency discovery and targets
|-- include/
|   `-- etch/
|       |-- etch.h            # Public C++20 API: Arena, Prim, Theme and declarative UI
|       `-- etch_c.h          # Public pure C99 ABI header (<stdbool.h>, <stddef.h>, <stdint.h> only)
|-- third_party/              # Clay/stb headers, SDL3, HarfBuzz, Slug notices/code
|-- shaders/
|   |-- etch_kawase.hlsl      # Kawase blur and presentation
|   `-- etch_uber.hlsl        # Batched UI geometry and Slug Bezier glyphs
|-- src/
|   |-- etch_testing.h        # Internal regression hooks, excluded from public API
|   |-- etch_arena.cpp        # Arena, shared internal state and POD definitions
|   |-- etch_theme.cpp        # Semantic Theme tokens and system theme probe
|   |-- etch_image.cpp        # Dynamic sprite atlas and PNG/JPEG/GIF/TGA/BMP decoder
|   |-- etch_bidi.cpp         # Unicode UAX #9 bidi and grapheme itemization
|   |-- etch_slug.cpp         # HarfBuzz shaping and Slug contours/bands
|   |-- etch_emoji.cpp        # COLR v0, CBDT and sbix color emoji fallbacks
|   |-- etch_render_cmd.cpp   # Render command generation and clipping
|   |-- etch_text_input.cpp   # UTF-8 editing, soft word-wrap, selection and IME
|   |-- etch_layout.cpp       # Clay integration and core layout API
|   |-- etch_containers.cpp   # Virtual lists, grids, table, tree and canvas
|   |-- etch_frame.cpp        # Frame lifecycle, input, focus and interaction
|   |-- etch_widgets.cpp      # Desktop widgets and docking
|   |-- etch_editor.cpp       # Text buffer and virtual code editor
|   |-- etch_a11y.cpp         # Accessibility snapshots and automation
|   |-- etch_a11y_os.cpp      # Live resize, multi-monitor DPI and OS A11y bridges
|   |-- etch_gpu.cpp          # Lazy SDL GPU setup, backend selection and rendering
|   |-- etch_c.cpp            # Pure C99 ABI implementation facade
|   |-- shaders_gen/          # Checked-in fallback shader headers
|   `-- etch_unity.cpp        # Only engine translation unit; includes modules
|-- examples/
|   |-- sandbox.cpp           # Interactive and --headless regression harness
|   `-- etch_studio.cpp       # Desktop workbench and --headless demo
`-- tests/
    |-- checks_desktop.h      # Shared desktop/widget regressions
    |-- checks_editor.h       # Shared editor regressions
    |-- checks_a11y.h         # Shared accessibility regressions
    |-- checks_media_emoji.h  # Dynamic sprite cache and color emoji checks
    |-- checks_theme_wrap.h   # Semantic theme and soft word-wrap checks
    |-- checks_os_window_a11y.h # Live resize, DPI and OS A11y bridge checks
    |-- checks_sandbox_headless.h # Shared sandbox offscreen assertions
    |-- consumer_smoke/main.cpp # Public C++20 consumer smoke
    |-- consumer_smoke/main_c.c # Public C99 consumer smoke
    |-- etch_c_abi_tests.c    # Standalone C99 ABI suite (`c99_abi_smoke`)
    `-- etch_tests.cpp        # Eight C++ suites; eleven CTest suites total
```

The former layout implementation is split across `etch_render_cmd.cpp`,
`etch_text_input.cpp`, `etch_layout.cpp`, `etch_containers.cpp`, and
`etch_frame.cpp`; these remain part of the single unity translation unit.
Examples, tests, and installation default to OFF in subprojects. Generated
shader headers live in the build directory; checked-in headers are fallbacks.
With tests and installation enabled, CTest runs eleven suites (eight C++ suites,
pure C99 `c99_abi_smoke`, `repo_guardrails`, and C++/C99 `downstream_consumers`).

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

- **Dynamic GPU Vectors**: transient cycled GPU Slug filled polygons/paths/gradient area series and analytic 2D rotation, with frame-arena scratch and no CPU triangulation.

## Core Engine Modules
- `etch_widgets.cpp`: desktop controls, menus, popups and modal scopes.
- `etch_editor.cpp`: gap-buffer documents, indexed lines and virtual code editor.
- `etch_a11y.cpp`: frame-arena semantic builders, per-window arena snapshots and bounded queued automation actions. Snapshots survive other window builds and remain readable until their own next end_frame.
- `src/shaders_gen/*_msl.h`: offline Metal 2.1 source as NUL-terminated byte arrays. SDL Metal uniform slots precede storage slots.
- `tests/etch_tests.cpp`: eight standalone CTest regression suites; no exceptions, RTTI or heap containers.

---

## 7. Semantic Versioning Discipline (SemVer 2.0.0)
- **Pre-1.0 (`0.y.z`)**: Etch remains in `0.y.z` until external production adoption and real-hardware CI sign-off across Windows (D3D12/Vulkan), macOS (Metal), and Linux (Vulkan Wayland/X11). New subsystems or breaking refinements increment `MINOR` (`0.y.0`); backward-compatible fixes and optimizations increment `PATCH` (`0.y.z`).
- **Post-1.0 (`1.0.0+`)**: `MAJOR` increments **exclusively** on incompatible public API/ABI changes in `include/etch/etch.h`; backward-compatible features increment `MINOR`; bugfixes increment `PATCH`. Never use `MAJOR` bumps as internal milestone codenames.
- **Single Source of Truth**: `CMakeLists.txt` (`project(etch VERSION ...)`), `include/etch/etch.h`, and `include/etch/etch_c.h` (`ETCH_VERSION_*`) must always match (verified by `repo_guardrails` and compile-time/CTest checks in `tests/etch_tests.cpp` and `tests/etch_c_abi_tests.c`), and formal releases carry an annotated `vX.Y.Z` Git tag.
