include_guard(GLOBAL)

# etch_compile_hlsl_surface(TARGET app SOURCE aurora.hlsl NAME aurora [ENTRYPOINT ps_main])
# Adds private generated aurora_{dxil,spv,msl}.h headers with g_aurora_* arrays.
# Surface shaders use TEXCOORD0 and one 80-byte fragment uniform block in space3.
function(etch_compile_hlsl_surface)
    cmake_parse_arguments(SURFACE "" "TARGET;SOURCE;NAME;ENTRYPOINT" "" ${ARGN})
    if (NOT TARGET "${SURFACE_TARGET}" OR NOT EXISTS "${SURFACE_SOURCE}" OR
        NOT SURFACE_NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "etch_compile_hlsl_surface requires TARGET, absolute SOURCE and identifier NAME")
    endif ()
    if (NOT SURFACE_ENTRYPOINT)
        set(SURFACE_ENTRYPOINT ps_main)
    endif ()
    find_program(ETCH_DXC_EXE NAMES dxc dxc.exe
        HINTS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/dxc/bin/x64")
    if (NOT ETCH_DXC_EXE)
        message(FATAL_ERROR "etch_compile_hlsl_surface requires DXC; set ETCH_DXC_EXE")
    endif ()
    find_program(ETCH_SHADERCROSS_EXE NAMES shadercross)
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}/${SURFACE_NAME}_shader")
    file(MAKE_DIRECTORY "${_dir}")
    set(_base "${_dir}/${SURFACE_NAME}")
    add_custom_command(OUTPUT "${_base}_dxil.h"
        COMMAND "${ETCH_DXC_EXE}" -T ps_6_0 -E "${SURFACE_ENTRYPOINT}" -O3 -WX -Ges
            -Vn "g_${SURFACE_NAME}_dxil" -Fh "${_base}_dxil.h" "${SURFACE_SOURCE}"
        DEPENDS "${SURFACE_SOURCE}" VERBATIM)
    set(_headers "${_base}_dxil.h")
    # Windows SDK DXC can emit DXIL without the optional SPIR-V backend.
    # The bundled demo retains checked-in SPIR-V/MSL headers in that case.
    if (DEFINED _etch_spv_probe_res)
        set(_surface_spv_result "${_etch_spv_probe_res}")
    else ()
        execute_process(COMMAND "${ETCH_DXC_EXE}" -T ps_6_0 -E "${SURFACE_ENTRYPOINT}"
            -spirv -fspv-target-env=vulkan1.0 -Fo "${_base}_probe.spv" "${SURFACE_SOURCE}"
            RESULT_VARIABLE _surface_spv_result OUTPUT_QUIET ERROR_QUIET)
    endif ()
    if (_surface_spv_result EQUAL 0)
        add_custom_command(OUTPUT "${_base}_spv.h" "${_base}.spv"
            COMMAND "${ETCH_DXC_EXE}" -T ps_6_0 -E "${SURFACE_ENTRYPOINT}" -O3 -WX -Ges
                -spirv -fspv-target-env=vulkan1.0 -Vn "g_${SURFACE_NAME}_spv" -Fh "${_base}_spv.h"
                -Fo "${_base}.spv" "${SURFACE_SOURCE}"
            DEPENDS "${SURFACE_SOURCE}" VERBATIM)
        list(APPEND _headers "${_base}_spv.h")
    endif ()
    if (ETCH_SHADERCROSS_EXE AND _surface_spv_result EQUAL 0)
        add_custom_command(OUTPUT "${_base}_msl.h" "${_base}.msl"
            COMMAND "${ETCH_SHADERCROSS_EXE}" "${_base}.spv" -s SPIRV -d MSL -t fragment
                --msl-version 2.1.0 -e "${SURFACE_ENTRYPOINT}" -o "${_base}.msl"
            COMMAND "${CMAKE_COMMAND}" "-DIN=${_base}.msl" "-DOUT=${_base}_msl.h"
                "-DVAR=g_${SURFACE_NAME}_msl" -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed_msl.cmake"
            DEPENDS "${_base}.spv" VERBATIM)
        list(APPEND _headers "${_base}_msl.h")
    endif ()
    target_sources(${SURFACE_TARGET} PRIVATE ${_headers})
    target_include_directories(${SURFACE_TARGET} BEFORE PRIVATE "${_dir}")
endfunction()
