# AGENTS.md — Etch Engineering Contract for Codex & Automated Agents

Read `CLAUDE.md` first. Every code change, architecture review, or optimization in this repository MUST obey the rules below.

## 1. Orthodox C++20 & GPU Architecture Constraints
- Allowed C++20 subset: designated initializers, `std::span`, `constexpr`, basic operator overloads on `Vec2`/`Vec4`, and simple POD/aggregate structs.
- Forbidden: exceptions (`try`/`catch`/`throw`), RTTI (`dynamic_cast`/`typeid`), virtual widget inheritance trees, heavy templates/metaprogramming, and per-frame heap containers (`std::vector`, `std::string`, `std::function`, `std::shared_ptr`).
- Every GPU buffer struct (`etch::Prim` = 64 B, `GlobalsUniform` = 32 B, auxiliary style/clip/gradient/shadow/transform structs) must remain `alignas(16)` with `static_assert(sizeof(T) % 16 == 0)`. Never use 12-byte `float3`/`Vec3`.
- **Lean Multi-Pipeline GPU + Indexed Aux Buffers**:
  - Keep `etch::Prim` 64 B for fast common primitives, and use indexed frame-arena GPU auxiliary buffers (`ClipShapeGpu`, `ShadowLayerGpu`, `GradientStopGpu`, `Transform2DGpu`) for rich visual effects (multi-layer/inset shadows, multi-stop gradients, 2D subtree affine transforms, vibrancy blur) instead of crippling expressiveness or bloating `Prim`.
  - Keep shaders small and modular to minimize GPU driver PSO compilation memory and VGPR pressure. Do NOT expand `etch_uber.hlsl` into a larger monolith.
- **Adaptive Hybrid Text Architecture**:
  - Default & HiDPI (`dpi_scale >= 1.25`): single-channel `R8_UNORM` 4× X-phase hinted glyph atlas with polarity-calibrated stem darkening (1 draw call across all text colors, 1/4 VRAM, cross-platform consistent).
  - Low-DPI (`1.0x`) standard RGB LCD opt-in: optional 3-channel LCD subpixel path when explicitly requested; never regress standard multi-color UIs back to 40+ draw calls.
  - Large (> 24 px), scaled, rotated text, SVG icons, and dynamic vectors: Slug GPU quadratic Beziers.
  - Enforce bounded atlas memory with LRU eviction and small stripe uploads (no unbounded growth, no full-atlas persistent staging buffers).
- **Strict Memory, VRAM & Power Discipline**:
  - Prefer low-power GPU by default (`SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN = true`), with opt-out via `ContextDesc`.
  - Use small initial reserves with geometric growth (`max_prims = 2048`, `frame_arena = 512 KiB`, hot/cold `WidgetState` split), slice/ring GPU uploads (never `cycle = true` on full multi-megabyte buffers for tiny frames), and automatic idle release for all transient offscreen render targets (`depth_tex`, `scene_tex`, `blur_ping/pong`, `filter_slices`).
- **Owned 2-Pass Layout & Occlusion Hitbox Architecture**:
  - Maintain Etch's immediate-mode ergonomics backed by `src/etch_layout_engine.cpp` (Flexbox + 2D Grid + Anchor), dirty-subtree replay caching, and an inverse-transformed occlusion hitbox tree (including full 2D affine transforms).

## 2. Anti-Over-Defensive Programming Mandate (Strict)
You are valued for C++ systems precision, NOT defensive boilerplate. Apply rigor selectively:
1. **Validate untrusted caller inputs ONCE at public `etch::` / `etch_` API boundaries** (`create_context`, `text`, `text_input`, `begin_frame`, etc.).
2. **Inside anonymous-namespace helpers** in `src/etch_*.cpp`, **trust established caller invariants** — do NOT add redundant `if (!ptr)` or repetitive clamping where the caller already guarantees validity.
3. **NEVER propose or insert**:
   - `uint64_t` overflow-checking ceremonies for GPU-bounded viewport/texture dimensions,
   - RAII wrapper classes, `std::optional`/`std::variant`/`std::expected`, or verbose helper layers,
   - Extra heap allocations or fallback copies inside hot paths.
4. **Focus strictly on real, high-signal defects**:
   - C++ undefined behavior (pointer formation past one-past-end, data races, strict aliasing, uninitialized reads),
   - Dangling pointers / lifetime hazards (stack strings outliving a frame, `StateMap` rehash pointer invalidation, `Arena` mark/reset misuse),
   - UTF-8 codepoint boundary splits and SDL3/IME state-machine desyncs,
   - `SDL_GPU` lifetime, transfer buffer cycling/sizing, pipeline/blend state, VRAM leaks, and synchronization contracts.

## 3. Verification Discipline (Functional + Memory + Visual Craft)
- All changes must compile with `/W4 /permissive- /GR- /EHs-c- /utf-8` (0 warnings) and pass `sandbox.exe --headless`, `etch_studio.exe --headless`, and `etch_showcase.exe --headless` on both `direct3d12` and `vulkan`.
- Run `ctest --test-dir build --output-on-failure`, including `c99_abi_smoke`, `repo_guardrails` and, when installation is enabled, `downstream_consumers`.
- **Inspect actual runtime metrics & pixels** on rendering/memory changes:
  - Verify single-window Peak Working Set / Private Commit stays within budget and standard UI Draw Calls remain in single digits (`≤ 8` DC).
  - Visually inspect rendered PNGs under ignored `.ci/` for real aesthetic quality (dark-on-light & light-on-dark text weight/contrast, crisp downscaled textures, shadow depth, vibrancy blur clarity, scrollbar refinement, alignment/spacing).
- Before committing, review all uncommitted diffs thoroughly and verify clean builds & tests. After committing, `git status --porcelain` must be empty; keep transient screenshots strictly under ignored `.ci/`.

## 4. Architecture, Packaging & Anti-Toy Guardrails
- No hard total or per-file line-count caps. Keep modules cohesive and split by responsibility; support both modular compilation and optional unity build (`ETCH_UNITY_BUILD`).
- Keep zero `.h`/`.hpp` files in `examples/`; shared regression checks belong in `tests/` and engine internals in `src/`.
- Public headers expose only the public API: no `inject_` hooks, internal testing headers, HarfBuzz/stb/private headers, or forbidden STL headers. `include/etch/etch_c.h` must remain strict C99 and include only `<stdbool.h>`, `<stddef.h>`, and `<stdint.h>`, with `ETCH_API` export macros and versioned `struct_size` ABI fields on public descriptors. Keep private include paths private in both installed and `add_subdirectory` consumers.
- Follow SemVer 2.0.0. `project(etch VERSION ...)` and public `ETCH_VERSION_*` macros in both `include/etch/etch.h` and `include/etch/etch_c.h` are the matching Single Source of Truth, with a matching changelog heading. Compose runtime version labels from `ETCH_VERSION_STRING`; never hardcode version literals in source/tests.
- Use out-of-source builds. Builds, installs, tests and reviews must preserve worktree cleanliness: no root PNGs, no source-tree `*.vectors.png`, no tracked build artifacts. Keep local CI caches in ignored `/.ci/`.
- Keep `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md`, `CHANGELOG.md`, `CLAUDE.md`, and `AGENTS.md` nonempty and accurate. Cover every `third_party/*` dependency in `THIRD_PARTY_NOTICES.md` (dotfiles and the local `dxc` tool directory are excluded).
- Verify positive and negative cases: valid repositories and both downstream consumption paths must pass, while deliberate rule violations in isolated temporary fixtures must fail with the offending file and rule. Never inject failures into tracked source files.
