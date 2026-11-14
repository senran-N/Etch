cmake_minimum_required(VERSION 3.24)
project(etch VERSION 1.6.0 LANGUAGES C CXX)

option(ETCH_BUILD_EXAMPLES "Build Etch sandbox and etch_studio examples" ON)

# CMake's default /EHsc conflicts with the explicit no-exceptions contract.
if (MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif ()

# Enforce Orthodox C+ (C-style C++20 without exceptions or RTTI) on engine & example targets.
function(etch_configure_orthodox_cpp target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR (NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT AND MSVC))
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /GR- /EHs-c-)
        target_compile_definitions(${target} PRIVATE
            _HAS_EXCEPTIONS=0
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    else ()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
    endif ()
endfunction()

# Prefer a vendored SDL3 SDK or source tree under third_party/SDL3 before falling back to system packages.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3/cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3")
    find_package(SDL3 CONFIG REQUIRED)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3/CMakeLists.txt")
    add_subdirectory(third_party/SDL3 EXCLUDE_FROM_ALL)
else ()
    find_package(SDL3 CONFIG REQUIRED)
endif ()

# Auto-discover DXC (vendored SPIR-V-capable DXC first, then Windows SDK / PATH)
# to compile shaders/etch_uber.hlsl offline into DXIL & SPIR-V C headers.
find_program(ETCH_DXC_EXE
    NAMES dxc dxc.exe
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dxc/bin/x64"
    NO_DEFAULT_PATH
)
if (NOT ETCH_DXC_EXE)
    set(_etch_dxc_hints "")
    if (WIN32)
        file(GLOB _etch_win_sdk_bins LIST_DIRECTORIES true "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
        if (_etch_win_sdk_bins)
            list(SORT _etch_win_sdk_bins COMPARE NATURAL ORDER DESCENDING)
            list(APPEND _etch_dxc_hints ${_etch_win_sdk_bins})
        endif ()
    endif ()
    find_program(ETCH_DXC_EXE
        NAMES dxc dxc.exe
        HINTS ${_etch_dxc_hints}
    )
endif ()

set(ETCH_UBER_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/shaders/etch_uber.hlsl")
set(ETCH_KAWASE_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/shaders/etch_kawase.hlsl")
set(ETCH_SHADER_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders_gen")
set(ETCH_SHADER_HEADERS
    "${ETCH_SHADER_GEN_DIR}/etch_uber_vs_dxil.h"
    "${ETCH_SHADER_GEN_DIR}/etch_uber_ps_dxil.h"
    "${ETCH_SHADER_GEN_DIR}/etch_uber_vs_spv.h"
    "${ETCH_SHADER_GEN_DIR}/etch_uber_ps_spv.h"
    "${ETCH_SHADER_GEN_DIR}/etch_kawase_vs_dxil.h"
    "${ETCH_SHADER_GEN_DIR}/etch_kawase_ps_dxil.h"
    "${ETCH_SHADER_GEN_DIR}/etch_kawase_vs_spv.h"
    "${ETCH_SHADER_GEN_DIR}/etch_kawase_ps_spv.h"
)

if (ETCH_DXC_EXE)
    add_custom_command(
        OUTPUT  "${ETCH_SHADER_GEN_DIR}/etch_uber_vs_dxil.h"
        COMMAND "${ETCH_DXC_EXE}" -T vs_6_0 -E vs_main -O3 -WX -Ges -Vn g_etch_uber_vs_dxil
                -Fh "${ETCH_SHADER_GEN_DIR}/etch_uber_vs_dxil.h" "${ETCH_UBER_SHADER}"
        DEPENDS "${ETCH_UBER_SHADER}"
        COMMENT "Compiling etch_uber.hlsl (vs_6_0 DXIL)"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${ETCH_SHADER_GEN_DIR}/etch_uber_ps_dxil.h"
        COMMAND "${ETCH_DXC_EXE}" -T ps_6_0 -E ps_main -O3 -WX -Ges -Vn g_etch_uber_ps_dxil
                -Fh "${ETCH_SHADER_GEN_DIR}/etch_uber_ps_dxil.h" "${ETCH_UBER_SHADER}"
        DEPENDS "${ETCH_UBER_SHADER}"
        COMMENT "Compiling etch_uber.hlsl (ps_6_0 DXIL)"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${ETCH_SHADER_GEN_DIR}/etch_kawase_vs_dxil.h"
        COMMAND "${ETCH_DXC_EXE}" -T vs_6_0 -E vs_main -O3 -WX -Ges -Vn g_etch_kawase_vs_dxil
                -Fh "${ETCH_SHADER_GEN_DIR}/etch_kawase_vs_dxil.h" "${ETCH_KAWASE_SHADER}"
        DEPENDS "${ETCH_KAWASE_SHADER}"
        COMMENT "Compiling etch_kawase.hlsl (vs_6_0 DXIL)"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${ETCH_SHADER_GEN_DIR}/etch_kawase_ps_dxil.h"
        COMMAND "${ETCH_DXC_EXE}" -T ps_6_0 -E ps_main -O3 -WX -Ges -Vn g_etch_kawase_ps_dxil
                -Fh "${ETCH_SHADER_GEN_DIR}/etch_kawase_ps_dxil.h" "${ETCH_KAWASE_SHADER}"
        DEPENDS "${ETCH_KAWASE_SHADER}"
        COMMENT "Compiling etch_kawase.hlsl (ps_6_0 DXIL)"
        VERBATIM
    )

    execute_process(
        COMMAND "${ETCH_DXC_EXE}" -spirv "-fspv-target-env=vulkan1.0" -T vs_6_0 -E vs_main
                -Fo "${CMAKE_CURRENT_BINARY_DIR}/_etch_spv_probe.spv" "${ETCH_UBER_SHADER}"
        RESULT_VARIABLE _etch_spv_probe_res
        OUTPUT_QUIET
        ERROR_QUIET
    )
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/_etch_spv_probe.spv")
    if (_etch_spv_probe_res EQUAL 0)
        add_custom_command(
            OUTPUT  "${ETCH_SHADER_GEN_DIR}/etch_uber_vs_spv.h"
            COMMAND "${ETCH_DXC_EXE}" -spirv "-fspv-target-env=vulkan1.0" -T vs_6_0 -E vs_main -O3 -WX -Ges
                    -Vn g_etch_uber_vs_spv -Fh "${ETCH_SHADER_GEN_DIR}/etch_uber_vs_spv.h" "${ETCH_UBER_SHADER}"
            DEPENDS "${ETCH_UBER_SHADER}"
            COMMENT "Compiling etch_uber.hlsl (vs_6_0 SPIR-V)"
            VERBATIM
        )
        add_custom_command(
            OUTPUT  "${ETCH_SHADER_GEN_DIR}/etch_uber_ps_spv.h"
            COMMAND "${ETCH_DXC_EXE}" -spirv "-fspv-target-env=vulkan1.0" -T ps_6_0 -E ps_main -O3 -WX -Ges
                    -Vn g_etch_uber_ps_spv -Fh "${ETCH_SHADER_GEN_DIR}/etch_uber_ps_spv.h" "${ETCH_UBER_SHADER}"
            DEPENDS "${ETCH_UBER_SHADER}"
            COMMENT "Compiling etch_uber.hlsl (ps_6_0 SPIR-V)"
            VERBATIM
        )
        add_custom_command(
            OUTPUT  "${ETCH_SHADER_GEN_DIR}/etch_kawase_vs_spv.h"
            COMMAND "${ETCH_DXC_EXE}" -spirv "-fspv-target-env=vulkan1.0" -T vs_6_0 -E vs_main -O3 -WX -Ges
                    -Vn g_etch_kawase_vs_spv -Fh "${ETCH_SHADER_GEN_DIR}/etch_kawase_vs_spv.h" "${ETCH_KAWASE_SHADER}"
            DEPENDS "${ETCH_KAWASE_SHADER}"
            COMMENT "Compiling etch_kawase.hlsl (vs_6_0 SPIR-V)"
            VERBATIM
        )
        add_custom_command(
            OUTPUT  "${ETCH_SHADER_GEN_DIR}/etch_kawase_ps_spv.h"
            COMMAND "${ETCH_DXC_EXE}" -spirv "-fspv-target-env=vulkan1.0" -T ps_6_0 -E ps_main -O3 -WX -Ges
                    -Vn g_etch_kawase_ps_spv -Fh "${ETCH_SHADER_GEN_DIR}/etch_kawase_ps_spv.h" "${ETCH_KAWASE_SHADER}"
            DEPENDS "${ETCH_KAWASE_SHADER}"
            COMMENT "Compiling etch_kawase.hlsl (ps_6_0 SPIR-V)"
            VERBATIM
        )
    endif ()
endif ()

# Optional cross-platform shader compiler CLI (needed later when targeting macOS MSL).
find_program(ETCH_SHADERCROSS_EXE
    NAMES shadercross
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_shadercross/bin"
)

# Prefer a vendored prebuilt archive/source tree under third_party/harfbuzz before falling back to system packages.
set(ETCH_HB_LIB_TARGET "")
if (WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/lib/harfbuzz.lib")
    add_library(harfbuzz_static STATIC IMPORTED GLOBAL)
    set_target_properties(harfbuzz_static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/lib/harfbuzz.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/include;${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/include/harfbuzz"
    )
    set(ETCH_HB_LIB_TARGET harfbuzz_static)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/lib/libharfbuzz.a")
    add_library(harfbuzz_static STATIC IMPORTED GLOBAL)
    set_target_properties(harfbuzz_static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/lib/libharfbuzz.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/include;${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/include/harfbuzz"
    )
    set(ETCH_HB_LIB_TARGET harfbuzz_static)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/CMakeLists.txt")
    add_subdirectory(third_party/harfbuzz EXCLUDE_FROM_ALL)
    set(ETCH_HB_LIB_TARGET harfbuzz)
else ()
    find_package(harfbuzz CONFIG QUIET)
    if (TARGET harfbuzz::harfbuzz)
        set(ETCH_HB_LIB_TARGET harfbuzz::harfbuzz)
    else ()
        find_package(PkgConfig QUIET)
        if (PkgConfig_FOUND)
            pkg_check_modules(HARFBUZZ QUIET IMPORTED_TARGET harfbuzz)
            if (TARGET PkgConfig::HARFBUZZ)
                set(ETCH_HB_LIB_TARGET PkgConfig::HARFBUZZ)
            endif ()
        endif ()
        if (NOT ETCH_HB_LIB_TARGET)
            find_library(ETCH_HARFBUZZ_LIBRARY NAMES harfbuzz libharfbuzz)
            find_path(ETCH_HARFBUZZ_INCLUDE_DIR NAMES hb.h PATH_SUFFIXES harfbuzz)
            if (ETCH_HARFBUZZ_LIBRARY AND ETCH_HARFBUZZ_INCLUDE_DIR)
                add_library(harfbuzz_system UNKNOWN IMPORTED GLOBAL)
                set_target_properties(harfbuzz_system PROPERTIES
                    IMPORTED_LOCATION "${ETCH_HARFBUZZ_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${ETCH_HARFBUZZ_INCLUDE_DIR}"
                )
                set(ETCH_HB_LIB_TARGET harfbuzz_system)
            endif ()
        endif ()
    endif ()
endif ()

if (NOT ETCH_HB_LIB_TARGET)
    message(FATAL_ERROR "HarfBuzz not found. Provide third_party/harfbuzz/lib/harfbuzz.lib (or libharfbuzz.a), third_party/harfbuzz source, or install system HarfBuzz (vcpkg/brew/apt).")
endif ()

# Unity Build static library: compiles src/etch_unity.cpp as a single translation unit.
add_library(etch STATIC
    src/etch_unity.cpp
    src/etch.h
    ${ETCH_SHADER_HEADERS}
)
add_library(Etch::etch ALIAS etch)
set_source_files_properties(src/etch_unity.cpp PROPERTIES
    OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/etch.h;${CMAKE_CURRENT_SOURCE_DIR}/src/etch_arena.cpp;${CMAKE_CURRENT_SOURCE_DIR}/src/etch_slug.cpp;${CMAKE_CURRENT_SOURCE_DIR}/src/etch_layout.cpp;${CMAKE_CURRENT_SOURCE_DIR}/src/etch_gpu.cpp;${CMAKE_CURRENT_SOURCE_DIR}/third_party/clay.h;${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb_truetype.h;${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb_image_write.h"
)
etch_configure_orthodox_cpp(etch)
target_include_directories(etch
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders_gen"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)
target_link_libraries(etch
    PUBLIC  SDL3::SDL3
    PRIVATE ${ETCH_HB_LIB_TARGET}
)

if (ETCH_BUILD_EXAMPLES)
    add_executable(sandbox
        examples/sandbox.cpp
    )
    set_source_files_properties(examples/sandbox.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/etch.h"
    )
    etch_configure_orthodox_cpp(sandbox)
    target_include_directories(sandbox PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    target_link_libraries(sandbox PRIVATE etch)

    if (WIN32)
        add_custom_command(TARGET sandbox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:sandbox>>,copy_if_different,true>
                    $<TARGET_RUNTIME_DLLS:sandbox>
                    $<$<BOOL:$<TARGET_RUNTIME_DLLS:sandbox>>:$<TARGET_FILE_DIR:sandbox>>
            COMMAND_EXPAND_LISTS
            COMMENT "Syncing runtime DLLs for sandbox"
        )
    endif ()

    add_executable(etch_studio
        examples/etch_studio.cpp
    )
    set_source_files_properties(examples/etch_studio.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/etch.h"
    )
    etch_configure_orthodox_cpp(etch_studio)
    target_include_directories(etch_studio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    target_link_libraries(etch_studio PRIVATE etch)

    if (WIN32)
        add_custom_command(TARGET etch_studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:etch_studio>>,copy_if_different,true>
                    $<TARGET_RUNTIME_DLLS:etch_studio>
                    $<$<BOOL:$<TARGET_RUNTIME_DLLS:etch_studio>>:$<TARGET_FILE_DIR:etch_studio>>
            COMMAND_EXPAND_LISTS
            COMMENT "Syncing runtime DLLs for etch_studio"
        )
    endif ()
endif ()
