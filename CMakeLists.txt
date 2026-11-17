cmake_minimum_required(VERSION 3.24)
project(etch VERSION 0.16.0 LANGUAGES C CXX)

option(ETCH_BUILD_EXAMPLES "Build Etch sandbox and etch_studio examples" ${PROJECT_IS_TOP_LEVEL})
option(ETCH_BUILD_TESTS "Build Etch CTest suite" ${PROJECT_IS_TOP_LEVEL})
option(ETCH_INSTALL "Enable Etch install and package export targets" ${PROJECT_IS_TOP_LEVEL})
option(ETCH_COMPILE_SHADERS "Compile HLSL shaders offline when DXC/shadercross are available" ON)
option(ETCH_ENABLE_OS_A11Y "Enable optional native OS accessibility bridge" ON)

include(GNUInstallDirs)

# Strip the conflicting default in this directory only; preserve parent/cache flags.
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

set(ETCH_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders_gen")
file(MAKE_DIRECTORY "${ETCH_SHADER_OUT_DIR}")
set(ETCH_GENERATED_SHADER_HEADERS "")

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

find_program(ETCH_SHADERCROSS_EXE NAMES shadercross
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_shadercross/bin")

include(cmake/EtchShaderTools.cmake)

if (ETCH_COMPILE_SHADERS AND ETCH_DXC_EXE)
    execute_process(
        COMMAND "${ETCH_DXC_EXE}" -spirv "-fspv-target-env=vulkan1.0" -T vs_6_0 -E vs_main
                -Fo "${CMAKE_CURRENT_BINARY_DIR}/_etch_spv_probe.spv"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/etch_uber.hlsl"
        RESULT_VARIABLE _etch_spv_probe_res
        OUTPUT_QUIET ERROR_QUIET)
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/_etch_spv_probe.spv")
    foreach(shader etch_uber etch_kawase)
        set(_etch_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.hlsl")
        foreach(stage vs ps)
            set(_etch_base "${ETCH_SHADER_OUT_DIR}/${shader}_${stage}")
            if (stage STREQUAL "vs")
                set(_etch_stage vertex)
            else ()
                set(_etch_stage fragment)
            endif ()
            add_custom_command(
                OUTPUT "${_etch_base}_dxil.h"
                COMMAND "${ETCH_DXC_EXE}" -T ${stage}_6_0 -E ${stage}_main -O3 -WX -Ges
                        -Vn g_${shader}_${stage}_dxil -Fh "${_etch_base}_dxil.h" "${_etch_hlsl}"
                DEPENDS "${_etch_hlsl}"
                COMMENT "Compiling ${shader} (${stage} DXIL)"
                VERBATIM)
            list(APPEND ETCH_GENERATED_SHADER_HEADERS "${_etch_base}_dxil.h")
            if (_etch_spv_probe_res EQUAL 0)
                add_custom_command(
                    OUTPUT "${_etch_base}_spv.h"
                    COMMAND "${ETCH_DXC_EXE}" -spirv "-fspv-target-env=vulkan1.0"
                            -T ${stage}_6_0 -E ${stage}_main -O3 -WX -Ges
                            -Vn g_${shader}_${stage}_spv -Fh "${_etch_base}_spv.h" "${_etch_hlsl}"
                    DEPENDS "${_etch_hlsl}"
                    COMMENT "Compiling ${shader} (${stage} SPIR-V)"
                    VERBATIM)
                list(APPEND ETCH_GENERATED_SHADER_HEADERS "${_etch_base}_spv.h")
                if (ETCH_SHADERCROSS_EXE)
                    add_custom_command(
                        OUTPUT "${_etch_base}.spv"
                        COMMAND "${ETCH_DXC_EXE}" -spirv "-fspv-target-env=vulkan1.0"
                                -T ${stage}_6_0 -E ${stage}_main -O3 -WX -Ges
                                -Fo "${_etch_base}.spv" "${_etch_hlsl}"
                        DEPENDS "${_etch_hlsl}"
                        VERBATIM)
                    add_custom_command(
                        OUTPUT "${_etch_base}.msl"
                        COMMAND "${ETCH_SHADERCROSS_EXE}" "${_etch_base}.spv"
                                -s SPIRV -d MSL -t ${_etch_stage}
                                --msl-version 2.1.0 -e ${stage}_main -o "${_etch_base}.msl"
                        DEPENDS "${_etch_base}.spv"
                        VERBATIM)
                    add_custom_command(
                        OUTPUT "${_etch_base}_msl.h"
                        COMMAND "${CMAKE_COMMAND}" "-DIN=${_etch_base}.msl"
                                "-DOUT=${_etch_base}_msl.h" "-DVAR=g_${shader}_${stage}_msl"
                                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_msl.cmake"
                        DEPENDS "${_etch_base}.msl" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_msl.cmake"
                        VERBATIM)
                    list(APPEND ETCH_GENERATED_SHADER_HEADERS "${_etch_base}_msl.h")
                endif ()
            endif ()
        endforeach ()
    endforeach ()
endif ()
if (ETCH_GENERATED_SHADER_HEADERS)
    add_custom_target(etch_shaders DEPENDS ${ETCH_GENERATED_SHADER_HEADERS})
endif ()

# Archive dependencies use build paths locally and relocatable installed paths.
set(ETCH_HB_LIB_TARGET "")
set(ETCH_HB_INCLUDE_DIRS "")
set(ETCH_HB_PREBUILT_LIB "")
if (WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/lib/harfbuzz.lib")
    set(ETCH_HB_PREBUILT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/lib/harfbuzz.lib")
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/lib/libharfbuzz.a")
    set(ETCH_HB_PREBUILT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/lib/libharfbuzz.a")
endif ()
if (ETCH_HB_PREBUILT_LIB)
    set(ETCH_INSTALL_HB_MODE "VENDORED")
    get_filename_component(_etch_hb_lib_name "${ETCH_HB_PREBUILT_LIB}" NAME)
    set(ETCH_HB_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/include/harfbuzz")
    set(ETCH_HB_LIB_TARGET
        "$<BUILD_INTERFACE:${ETCH_HB_PREBUILT_LIB}>$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${_etch_hb_lib_name}>")
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/CMakeLists.txt")
    add_subdirectory(third_party/harfbuzz EXCLUDE_FROM_ALL)
    # Installed consumers resolve HarfBuzz separately, avoiding an unexported target.
    set(ETCH_INSTALL_HB_MODE "CONFIG")
    set(ETCH_HB_LIB_TARGET "$<BUILD_INTERFACE:harfbuzz>$<INSTALL_INTERFACE:harfbuzz::harfbuzz>")
else ()
    find_package(harfbuzz CONFIG QUIET)
    if (TARGET harfbuzz::harfbuzz)
        set(ETCH_INSTALL_HB_MODE "CONFIG")
        set(ETCH_HB_LIB_TARGET harfbuzz::harfbuzz)
    else ()
        find_package(PkgConfig QUIET)
        if (PkgConfig_FOUND)
            pkg_check_modules(HARFBUZZ QUIET IMPORTED_TARGET harfbuzz)
            if (TARGET PkgConfig::HARFBUZZ)
                set(ETCH_INSTALL_HB_MODE "PKGCONFIG")
                set(ETCH_HB_LIB_TARGET PkgConfig::HARFBUZZ)
            endif ()
        endif ()
        if (NOT ETCH_HB_LIB_TARGET)
            find_library(ETCH_HARFBUZZ_LIBRARY NAMES harfbuzz libharfbuzz)
            find_path(ETCH_HARFBUZZ_INCLUDE_DIR NAMES hb.h PATH_SUFFIXES harfbuzz)
            if (ETCH_HARFBUZZ_LIBRARY AND ETCH_HARFBUZZ_INCLUDE_DIR)
                set(ETCH_INSTALL_HB_MODE "SYSTEM_PATH")
                set(ETCH_HB_INCLUDE_DIRS "${ETCH_HARFBUZZ_INCLUDE_DIR}")
                set(ETCH_HB_LIB_TARGET "${ETCH_HARFBUZZ_LIBRARY}")
            endif ()
        endif ()
    endif ()
endif ()
if (NOT ETCH_HB_LIB_TARGET)
    message(FATAL_ERROR "HarfBuzz not found. Provide a vendored archive/source tree or install system HarfBuzz.")
endif ()

add_library(etch STATIC src/etch_unity.cpp)
add_library(Etch::etch ALIAS etch)
etch_configure_orthodox_cpp(etch)
target_include_directories(etch
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE "${ETCH_SHADER_OUT_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders_gen"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
            ${ETCH_HB_INCLUDE_DIRS})
target_link_libraries(etch PUBLIC SDL3::SDL3 PRIVATE ${ETCH_HB_LIB_TARGET})
target_compile_definitions(etch PRIVATE ETCH_ENABLE_OS_A11Y=$<BOOL:${ETCH_ENABLE_OS_A11Y}>)
if (WIN32)
    target_link_libraries(etch PRIVATE user32 dwmapi)
endif ()
if (WIN32 AND ETCH_ENABLE_OS_A11Y)
    target_link_libraries(etch PRIVATE oleaut32)
endif ()
if (APPLE)
    target_link_libraries(etch PRIVATE objc)
endif ()
if (TARGET etch_shaders)
    add_dependencies(etch etch_shaders)
endif ()

# Explicit dependencies also cover localized MSVC/Ninja include scanning.
file(GLOB _etch_engine_dependencies CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders_gen/*.h")
file(GLOB _etch_check_dependencies CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/checks_*.h")
set(_etch_common_dependencies
    "${CMAKE_CURRENT_SOURCE_DIR}/include/etch/etch.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/etch/etch_c.h"
    ${_etch_engine_dependencies} ${ETCH_GENERATED_SHADER_HEADERS})
set_source_files_properties(src/etch_unity.cpp PROPERTIES OBJECT_DEPENDS "${_etch_common_dependencies}")
set_source_files_properties(examples/sandbox.cpp examples/etch_studio.cpp tests/etch_tests.cpp
    PROPERTIES OBJECT_DEPENDS "${_etch_common_dependencies};${_etch_check_dependencies}")

if (ETCH_BUILD_EXAMPLES)
    add_executable(etch_showcase examples/etch_showcase.cpp)
    etch_configure_orthodox_cpp(etch_showcase)
    target_link_libraries(etch_showcase PRIVATE etch)
    target_include_directories(etch_showcase PRIVATE src/shaders_gen)
    if (ETCH_COMPILE_SHADERS AND ETCH_DXC_EXE)
        etch_compile_hlsl_surface(TARGET etch_showcase SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/etch_surface_demo.hlsl" NAME etch_surface_demo)
    endif ()
    if (WIN32)
        add_custom_command(TARGET etch_showcase POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:etch_showcase>>,copy_if_different,true>
                    $<TARGET_RUNTIME_DLLS:etch_showcase>
                    $<$<BOOL:$<TARGET_RUNTIME_DLLS:etch_showcase>>:$<TARGET_FILE_DIR:etch_showcase>>
            COMMAND_EXPAND_LISTS VERBATIM)
    endif ()
    add_executable(sandbox
        examples/sandbox.cpp
    )
    etch_configure_orthodox_cpp(sandbox)
    target_include_directories(sandbox PRIVATE src tests "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
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
    etch_configure_orthodox_cpp(etch_studio)
    target_include_directories(etch_studio PRIVATE src tests "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
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

if (ETCH_BUILD_TESTS)
    enable_testing()
    add_executable(etch_tests tests/etch_tests.cpp)
    if (ETCH_COMPILE_SHADERS AND ETCH_DXC_EXE)
        etch_compile_hlsl_surface(TARGET etch_tests SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/etch_surface_demo.hlsl" NAME etch_surface_demo)
    endif ()
    etch_configure_orthodox_cpp(etch_tests)
    target_link_libraries(etch_tests PRIVATE etch)
    target_include_directories(etch_tests PRIVATE "${ETCH_SHADER_OUT_DIR}" src src/shaders_gen tests)
    if (TARGET etch_shaders)
        add_dependencies(etch_tests etch_shaders)
    endif ()
    target_compile_definitions(etch_tests PRIVATE
        ETCH_CMAKE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        ETCH_CMAKE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        ETCH_CMAKE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        ETCH_ENABLE_OS_A11Y=$<BOOL:${ETCH_ENABLE_OS_A11Y}>
    )
    if (WIN32)
        target_link_libraries(etch_tests PRIVATE user32 ole32 oleaut32)
        add_custom_command(TARGET etch_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:etch_tests>>,copy_if_different,true>
                    $<TARGET_RUNTIME_DLLS:etch_tests>
                    $<$<BOOL:$<TARGET_RUNTIME_DLLS:etch_tests>>:$<TARGET_FILE_DIR:etch_tests>>
            COMMAND_EXPAND_LISTS)
    endif ()
    foreach(suite motion_layers_presence arena_and_elasticity unicode_bidi_grapheme layout_clay_flex_grid fenwick_virtual_list_1m widgets_and_dockspace text_buffer_and_code_editor a11y_semantic_tree headless_gpu_invariants)
        add_test(NAME ${suite} COMMAND etch_tests --suite=${suite})
    endforeach()

    add_executable(etch_c_abi_tests tests/etch_c_abi_tests.c)
    set_target_properties(etch_c_abi_tests PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        OBJECT_DEPENDS "${_etch_common_dependencies}"
    )
    if (CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR (NOT CMAKE_C_COMPILER_FRONTEND_VARIANT AND MSVC))
        target_compile_options(etch_c_abi_tests PRIVATE /W4 /WX /permissive- /utf-8)
    else ()
        target_compile_options(etch_c_abi_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif ()
    target_link_libraries(etch_c_abi_tests PRIVATE etch)
    target_compile_definitions(etch_c_abi_tests PRIVATE
        ETCH_CMAKE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        ETCH_CMAKE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        ETCH_CMAKE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    )
    if (WIN32)
        target_link_libraries(etch_c_abi_tests PRIVATE user32 ole32 oleaut32)
        add_custom_command(TARGET etch_c_abi_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:etch_c_abi_tests>>,copy_if_different,true>
                    $<TARGET_RUNTIME_DLLS:etch_c_abi_tests>
                    $<$<BOOL:$<TARGET_RUNTIME_DLLS:etch_c_abi_tests>>:$<TARGET_FILE_DIR:etch_c_abi_tests>>
            COMMAND_EXPAND_LISTS)
    endif ()
    add_test(NAME c99_abi_smoke COMMAND etch_c_abi_tests)

    set_tests_properties(arena_and_elasticity unicode_bidi_grapheme layout_clay_flex_grid fenwick_virtual_list_1m c99_abi_smoke
        PROPERTIES TIMEOUT 5)
    add_test(NAME repo_guardrails COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_repo_guardrails.cmake")
    set_tests_properties(repo_guardrails PROPERTIES TIMEOUT 10)
    if (ETCH_INSTALL)
        set(_etch_consumer_cfg "${CMAKE_CURRENT_BINARY_DIR}/etch_consumer_test_cfg.cmake")
        file(WRITE "${_etch_consumer_cfg}"
            "set(ETCH_TEST_REPO_ROOT [=[${CMAKE_CURRENT_SOURCE_DIR}]=])\n"
            "set(ETCH_TEST_BUILD_DIR [=[${CMAKE_CURRENT_BINARY_DIR}]=])\n"
            "set(ETCH_TEST_INSTALL_INCLUDEDIR [=[${CMAKE_INSTALL_INCLUDEDIR}]=])\n"
            "set(ETCH_TEST_GENERATOR [=[${CMAKE_GENERATOR}]=])\n"
            "set(ETCH_TEST_GENERATOR_PLATFORM [=[${CMAKE_GENERATOR_PLATFORM}]=])\n"
            "set(ETCH_TEST_GENERATOR_TOOLSET [=[${CMAKE_GENERATOR_TOOLSET}]=])\n"
            "set(ETCH_TEST_MAKE_PROGRAM [=[${CMAKE_MAKE_PROGRAM}]=])\n"
            "set(ETCH_TEST_C_COMPILER [=[${CMAKE_C_COMPILER}]=])\n"
            "set(ETCH_TEST_CXX_COMPILER [=[${CMAKE_CXX_COMPILER}]=])\n"
            "set(ETCH_TEST_TOOLCHAIN_FILE [=[${CMAKE_TOOLCHAIN_FILE}]=])\n"
            "set(ETCH_TEST_VCPKG_TRIPLET [=[${VCPKG_TARGET_TRIPLET}]=])\n"
            "set(ETCH_TEST_PREFIX_PATH [=[${CMAKE_PREFIX_PATH}]=])\n"
            "set(ETCH_TEST_DXC_EXE [=[${ETCH_DXC_EXE}]=])\n"
            "set(ETCH_TEST_SHADERCROSS_EXE [=[${ETCH_SHADERCROSS_EXE}]=])\n"
            "set(ETCH_TEST_SDL3_DIR [=[${SDL3_DIR}]=])\n"
            "set(ETCH_TEST_HARFBUZZ_DIR [=[${harfbuzz_DIR}]=])\n"
            "set(ETCH_TEST_OSX_ARCHITECTURES [=[${CMAKE_OSX_ARCHITECTURES}]=])\n"
            "set(ETCH_TEST_OSX_SYSROOT [=[${CMAKE_OSX_SYSROOT}]=])\n"
            "set(ETCH_TEST_OSX_DEPLOYMENT_TARGET [=[${CMAKE_OSX_DEPLOYMENT_TARGET}]=])\n"
            "set(ETCH_TEST_VERSION_MM [=[${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}]=])\n")
        add_test(NAME downstream_consumers COMMAND "${CMAKE_COMMAND}" "-DCFG_FILE=${_etch_consumer_cfg}"
            "-DCONFIG=$<CONFIG>" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_downstream_consumers.cmake")
        set_tests_properties(downstream_consumers PROPERTIES TIMEOUT 120)
    endif ()
endif()

if (ETCH_INSTALL)
    include(CMakePackageConfigHelpers)
    install(TARGETS etch EXPORT EtchTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/etch" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    if (ETCH_INSTALL_HB_MODE STREQUAL "VENDORED")
        install(FILES "${ETCH_HB_PREBUILT_LIB}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif ()
    configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EtchConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/EtchConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Etch")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/EtchConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
    install(EXPORT EtchTargets FILE EtchTargets.cmake NAMESPACE Etch::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Etch")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/EtchConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/EtchConfigVersion.cmake"
        "cmake/EtchShaderTools.cmake" "cmake/embed_msl.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Etch")
endif ()
