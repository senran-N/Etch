# AGENTS.md — Etch Engineering Contract for Codex & Automated Agents

Read `CLAUDE.md` first. Every code change, review, or algorithm prototype in this repository MUST obey the rules below.

## 1. Orthodox C++20 Constraints
- Allowed C++20 subset: designated initializers, `std::span`, `constexpr`, basic operator overloads on `Vec2`/`Vec4`, and simple POD/aggregate structs.
- Forbidden: exceptions (`try`/`catch`/`throw`), RTTI (`dynamic_cast`/`typeid`), virtual widget inheritance trees, heavy templates/metaprogramming, and per-frame heap containers (`std::vector`, `std::string`, `std::function`, `std::shared_ptr`).
- Every GPU buffer struct (`etch::Prim` = 64 B, `GlobalsUniform` = 32 B) must remain `alignas(16)` with `static_assert(sizeof(T) % 16 == 0)`. Never use 12-byte `float3`/`Vec3`.
- Keep all UI geometry and glyphs batched into 1–3 GPU draw calls via `shaders/etch_uber.hlsl`.

## 2. Anti-Over-Defensive Programming Mandate (Strict)
You are valued for C++ systems precision, NOT defensive boilerplate. Apply rigor selectively:
1. **Validate untrusted caller inputs ONCE at public `etch::` API boundaries** (`create_context`, `text`, `text_input`, `begin_frame`, etc.).
2. **Inside anonymous-namespace helpers** in `src/etch_*.cpp`, **trust established caller invariants** — do NOT add redundant `if (!ptr)` or repetitive clamping where the caller already guarantees validity.
3. **NEVER propose or insert**:
   - `uint64_t` overflow-checking ceremonies for GPU-bounded viewport/texture dimensions,
   - RAII wrapper classes, `std::optional`/`std::variant`/`std::expected`, or verbose helper layers,
   - Extra heap allocations or fallback copies inside hot paths.
4. **Focus strictly on real, high-signal defects**:
   - C++ undefined behavior (pointer formation past one-past-end, data races, strict aliasing, uninitialized reads),
   - Dangling pointers / lifetime hazards (stack strings outliving a frame, `StateMap` rehash pointer invalidation, `Arena` mark/reset misuse),
   - UTF-8 codepoint boundary splits and SDL3/IME state-machine desyncs,
   - `SDL_GPU` lifetime, transfer buffer sizing, and synchronization contracts.

## 3. Verification Discipline
- All fixes must compile with `/W4 /permissive- /GR- /EHs-c- /utf-8` (0 warnings) and pass `sandbox.exe --headless` visual & assertion checks on both D3D12 and Vulkan.
- Run `ctest --test-dir build --output-on-failure`, including `repo_guardrails` and, when installation is enabled, `downstream_consumers`. The four core CPU suites have a 5s timeout; repository checks have 10s and downstream consumer checks have 120s.
- Before committing, run `codex review --uncommitted` and FlowGuard `flow_verify`. After committing verified work, `git status --porcelain` must be empty; remove transient screenshots from the repository root even if ignored.

## 4. Architecture, Packaging & Anti-Toy Guardrails
- File ceilings: `src/*.cpp <= 4000` lines, `include/etch/*.h <= 2500` lines, `examples/*.cpp <= 3200` lines. Split cohesive modules before exceeding these caps.
- Keep zero `.h`/`.hpp` files in `examples/`; shared regression checks belong in `tests/` and engine internals in `src/`.
- Public headers expose only the public API: no `inject_` hooks, internal testing headers, Clay/stb/HarfBuzz includes, or forbidden STL headers. Keep private include paths private in both installed and `add_subdirectory` consumers.
- Follow SemVer 2.0.0. `project(etch VERSION ...)` and public `ETCH_VERSION_*` macros are the matching Single Source of Truth, with a matching changelog heading. Compose runtime version labels from `ETCH_VERSION_STRING`; never hardcode version literals in source/tests.
- Use out-of-source builds. Builds, installs, tests and reviews must preserve worktree cleanliness: no root PNGs, no source-tree `*.vectors.png`, no tracked build artifacts. Keep local CI caches in ignored `/.ci/`.
- Keep `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md`, `CHANGELOG.md`, `CLAUDE.md`, and `AGENTS.md` nonempty. Cover every `third_party/*` dependency in `THIRD_PARTY_NOTICES.md` (dotfiles and the local `dxc` tool directory are excluded).
- Verify positive and negative cases: valid repositories and both downstream consumption paths must pass, while deliberate rule violations in isolated temporary fixtures must fail with the offending file and rule. Never inject failures into tracked source files.
