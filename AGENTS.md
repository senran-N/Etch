# AGENTS.md — Etch Engineering Contract for Codex & Automated Agents

Read `CLAUDE.md` first. Every code change, review, or algorithm prototype in this repository MUST obey the rules below.

## 1. Orthodox C++20 Constraints (The Non-Negotiable Soul)
- Allowed C++20 subset: designated initializers, `std::span`, `constexpr`, basic operator overloads on `Vec2`/`Vec4`, and simple POD/aggregate structs.
- Forbidden: exceptions (`try`/`catch`/`throw`), RTTI (`dynamic_cast`/`typeid`), virtual widget inheritance trees, heavy templates/metaprogramming, and per-frame heap containers (`std::vector`, `std::string`, `std::function`, `std::shared_ptr`).
- Every GPU buffer struct (`etch::Prim` = 64 B, `GlobalsUniform` = 32 B, specialized pipeline instance structs) must remain `alignas(16)` with `static_assert(sizeof(T) % 16 == 0)`. Never use 12-byte `float3`/`Vec3`.
- **Multi-Pipeline GPU & Hybrid Text Architecture**:
  - Prefer specialized lightweight GPU pipelines (Opaque/Alpha Quad+Border+Shadow, Hardware LCD Subpixel Text via `CONSTANT_COLOR` + `ONE_MINUS_SRC_COLOR`, Slug GPU Bezier Text/Vector, Image/Blur) with Early-Z depth rejection and spatial non-overlapping batch reordering to minimize Render Pass switches, VGPR pressure, and overdraw.
  - Use Hybrid Text rendering: axis-aligned small text (≤ 24 px, unscaled/unrotated) renders via OS ClearType/hinted RGB LCD subpixel multi-page atlas with 4× X subpixel variants; large, scaled, rotated text and SVG/dynamic vectors render via Slug GPU quadratic Beziers.
- **Owned 2-Pass Layout & Occlusion Hitbox Architecture**:
  - Maintain Etch's ergonomic immediate-mode API (never introduce retained OOP/Entity state machines), backed by an owned Orthodox C++20 2-pass Flexbox + 2D Grid + Anchor layout engine (`etch_layout_engine.cpp`), dirty-subtree layout/prim replay caching, and an inverse-transformed occlusion hitbox tree.

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
   - `SDL_GPU` lifetime, transfer buffer sizing, pipeline/blend state, and synchronization contracts.

## 3. Verification Discipline
- All fixes must compile with `/W4 /permissive- /GR- /EHs-c- /utf-8` (0 warnings) and pass `sandbox.exe --headless` visual & assertion checks on both D3D12 and Vulkan.
- Run `ctest --test-dir build --output-on-failure`, including `c99_abi_smoke`, `repo_guardrails` and, when installation is enabled, `downstream_consumers` (12+ suites total). The core CPU suites have a 5s timeout; repository checks have 10s and downstream consumer checks (covering both C++20 `main.cpp` and C99 `main_c.c`) have 120s.
- Before committing, review all uncommitted diffs thoroughly (run `codex review --uncommitted` when available) and verify clean builds & tests. After committing verified work, `git status --porcelain` must be empty; keep transient screenshots strictly under ignored `.ci/`, never in repository roots.

## 4. Architecture, Packaging & Anti-Toy Guardrails
- No hard total or per-file line-count caps. Keep modules cohesive and split by responsibility; support both modular compilation and optional unity build (`ETCH_UNITY_BUILD`).
- Keep zero `.h`/`.hpp` files in `examples/`; shared regression checks belong in `tests/` and engine internals in `src/`.
- Public headers expose only the public API: no `inject_` hooks, internal testing headers, HarfBuzz/stb/private headers, or forbidden STL headers. `include/etch/etch_c.h` must remain strict C99 and include only `<stdbool.h>`, `<stddef.h>`, and `<stdint.h>`, with `ETCH_API` export macros and versioned `struct_size` ABI fields on public descriptors. Keep private include paths private in both installed and `add_subdirectory` consumers.
- Follow SemVer 2.0.0. `project(etch VERSION ...)` and public `ETCH_VERSION_*` macros in both `include/etch/etch.h` and `include/etch/etch_c.h` are the matching Single Source of Truth, with a matching changelog heading. Compose runtime version labels from `ETCH_VERSION_STRING`; never hardcode version literals in source/tests.
- Use out-of-source builds. Builds, installs, tests and reviews must preserve worktree cleanliness: no root PNGs, no source-tree `*.vectors.png`, no tracked build artifacts. Keep local CI caches in ignored `/.ci/`.
- Keep `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md`, `CHANGELOG.md`, `CLAUDE.md`, and `AGENTS.md` nonempty. Cover every `third_party/*` dependency in `THIRD_PARTY_NOTICES.md` (dotfiles and the local `dxc` tool directory are excluded).
- Verify positive and negative cases: valid repositories and both downstream consumption paths must pass, while deliberate rule violations in isolated temporary fixtures must fail with the offending file and rule. Never inject failures into tracked source files.
