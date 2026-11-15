# Changelog

All notable changes to Etch are documented here. This file follows
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

## [0.6.0] - 2026-11-15

### Added

- Added the Slot 0 Dynamic Sprite Cache GPU atlas with generational handles and extruded borders.
- Added static JPEG (baseline and progressive), GIF and TGA decoding alongside PNG/BMP.
- Added cross-platform OpenType sbix and CBDT/CBLC color emoji, including outline-less fonts.

## [0.5.0] - 2026-11-15

### Added

- Establish 0.5.0 as the SemVer baseline, shared by CMake and the public header
  and checked by the test executable. During 0.y.z development, new subsystems
  or breaking refinements increment MINOR; compatible fixes increment PATCH.
- Runtime GPU backend selection through `ContextDesc::gpu_driver`, then
  `ETCH_GPU_DRIVER`, then `SDL_GPU_DRIVER`, with SDL automatic selection last.
- Out-of-source CMake builds, the `Etch::etch` target, install/export rules,
  and `EtchConfig.cmake` for `find_package(Etch 0.5 CONFIG REQUIRED)`.
- MIT license, verified third-party notices including Slug credit, engineering
  README, and Windows/Linux/macOS CI for four CPU regression suites.

### Changed

- Defer headless GPU creation to `ensure_gpu_device` when a GPU operation is
  first requested; CPU layout tests can run without a GPU device.
- Load built-in bold and monospace fonts lazily on first use; initialize the
  preferred/default sans font at context creation.
- Isolate the public API in `include/etch/etch.h` and internal test hooks in
  `src/etch_testing.h`.
- Decompose layout into five modules: `etch_render_cmd.cpp`,
  `etch_text_input.cpp`, `etch_layout.cpp`, `etch_containers.cpp`, and
  `etch_frame.cpp`, all included by the single engine translation unit.
- Consolidate shared regression checks under `tests/checks_*.h`, including
  desktop, editor, accessibility, and sandbox headless checks.
- Default examples, tests, and installation to OFF when consumed as a CMake
  subproject, keeping consumer include paths and compiler options isolated.
