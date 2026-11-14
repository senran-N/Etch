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
2. **Inside anonymous-namespace helpers** in `src/etch_arena.cpp`, `src/etch_layout.cpp`, and `src/etch_gpu.cpp`, **trust established caller invariants** — do NOT add redundant `if (!ptr)` or repetitive clamping where the caller already guarantees validity.
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
