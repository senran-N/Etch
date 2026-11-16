# Etch v0.10.0

Etch is a Pure-GPU UI Engine for desktop applications on Windows, Linux, and
macOS. It combines immediate UI construction, Clay layout, Unicode text
handling, a pure C99 ABI facade (`<etch/etch_c.h>`), and SDL3 GPU rendering. The
sandbox is the regression harness; Etch Studio demonstrates docking, tables,
trees, themes, and a virtual editor.

## Architecture

- Orthodox C++20, without exceptions or RTTI, plus a zero-heap pure C99 ABI
  header ([include/etch/etch_c.h](include/etch/etch_c.h)). The engine compiles as
  one translation unit, [src/etch_unity.cpp](src/etch_unity.cpp), keeping total
  core source under 30,000 lines.
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

With tests and installation enabled, full CTest runs eleven suites, including GPU
invariants. `repo_guardrails` checks architecture, 30,000-line core LOC ceiling,
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
find_package(Etch 0.9 CONFIG REQUIRED)
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
Maximized/fullscreen windows retain OS-managed sizes. Destroy with `SDL_DestroyWindow`.

`FrameDesc::pixel_snap` / `WindowFrameDesc::pixel_snap` default to true. Rectangle
edges and border widths snap after DPI conversion; text, rotated shapes, shadows,
and soft rectangles retain fractional positioning. Set false for continuous animation.
`RectDrawDesc::softness` is a physical-pixel AA width (minimum 0.5), for solid and
gradient axis-aligned rectangles. Borders occupy the inside of the rectangle.

Use `.shrink_width = true` on a Fit/Grow content box in a toolbar to let it shrink
below its intrinsic text width and clip overflow; give a trailing close button a
fixed width. Fixed and Percent items retain their explicit sizing rules.
`TextDrawDesc{..., .max_width_px = 100, .ellipsis = true}` limits each drawn line in
logical pixels using the shared cluster-safe text engine. Alignment uses the limited
line width; width zero is unlimited. `spring(id, target, initial_value)` returns the
initial value on the first frame and animates toward the target on later frames.
C99 equivalents use the same descriptor fields plus `etch_create_desktop_window`
and `etch_spring_from`; initialize descriptors with their `*_default()` functions.
