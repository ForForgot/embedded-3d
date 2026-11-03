cmake_minimum_required(VERSION 3.16)
project(embd3d VERSION 1.0.1 LANGUAGES C)

option(EMBD3D_BUILD_EXAMPLES "Build the demo programs" ON)
option(EMBD3D_BUILD_TESTS    "Build the unit tests and the tier matrix" ON)

# --------------------------------------------------------------- library ---
set(EMBD3D_SOURCES
    src/embd3d_vec3.c
    src/embd3d_mat4.c
    src/embd3d_framebuffer.c
    src/embd3d_raster.c
    src/embd3d_fixed.c
    src/embd3d_render.c
)

add_library(embd3d STATIC ${EMBD3D_SOURCES})
target_include_directories(embd3d PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include/embd3d>
)
target_compile_features(embd3d PUBLIC c_std_99)

if(NOT MSVC)
    target_compile_options(embd3d PRIVATE -Wall -Wextra -Wpedantic)
    # libm is needed for sqrt/sin/cos/tan where it is not part of libc
    target_link_libraries(embd3d PUBLIC m)
endif()

# ---------------------------------------------------------------- tests ----
if(EMBD3D_BUILD_TESTS)
    enable_testing()

    add_executable(test_embd3d tests/test_embd3d.c)
    target_link_libraries(test_embd3d PRIVATE embd3d)
    add_test(NAME embd3d.unit COMMAND test_embd3d)

    # The library is configured per target, so the same suite is built once per
    # tier: a feature combination that stops compiling or passing fails CI.
    # Format: <name>|<definition>|<definition>...
    set(EMBD3D_TIER_CONFIGS
        "float_only|EMBD3D_ENABLE_FIXED=0"
        "fixed_only|EMBD3D_ENABLE_FLOAT=0"
        "no_lighting|EMBD3D_ENABLE_LIGHTING=0"
        "no_full_clip|EMBD3D_ENABLE_FULL_CLIP=0"
        "no_cache|EMBD3D_ENABLE_VERTEX_CACHE=0"
        "avr_minimal|EMBD3D_ENABLE_FLOAT=0|EMBD3D_ENABLE_LIGHTING=0|EMBD3D_ENABLE_FULL_CLIP=0|EMBD3D_ENABLE_VERTEX_CACHE=0|EMBD3D_DEPTH_BITS=8|EMBD3D_COLOR_FORMAT=1"
        "m0_build|EMBD3D_ENABLE_FLOAT=0|EMBD3D_ENABLE_LIGHTING=0|EMBD3D_DEPTH_BITS=8"
        "double_scalars|EMBD3D_REAL=double"
        "subpixel5|EMBD3D_SUBPIXEL_BITS=5"
        "validated|EMBD3D_VALIDATE_MESH=1"
    )

    foreach(cfg IN LISTS EMBD3D_TIER_CONFIGS)
        string(REPLACE "|" ";" cfg_list "${cfg}")
        list(GET cfg_list 0 cfg_name)
        list(REMOVE_AT cfg_list 0)

        add_executable(test_embd3d_${cfg_name} tests/test_embd3d.c ${EMBD3D_SOURCES})
        target_include_directories(test_embd3d_${cfg_name} PRIVATE src)
        target_compile_definitions(test_embd3d_${cfg_name} PRIVATE ${cfg_list})
        if(NOT MSVC)
            target_compile_options(test_embd3d_${cfg_name} PRIVATE -Wall -Wextra)
            target_link_libraries(test_embd3d_${cfg_name} PRIVATE m)
        endif()
        add_test(NAME embd3d.${cfg_name} COMMAND test_embd3d_${cfg_name})
    endforeach()
endif()

# ------------------------------------------------------------- examples ----
if(EMBD3D_BUILD_EXAMPLES)
    add_executable(embd3d_demo examples/demo/main.c)
    target_link_libraries(embd3d_demo PRIVATE embd3d)
endif()

# -------------------------------------------------------------- install ----
include(GNUInstallDirs)
install(TARGETS embd3d EXPORT embd3dTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/embd3d)
install(FILES
    src/embd3d.h
    src/embd3d_config.h
    src/embd3d_vec3.h
    src/embd3d_mat4.h
    src/embd3d_framebuffer.h
    src/embd3d_raster.h
    src/embd3d_mesh.h
    src/embd3d_fixed.h
    src/embd3d_render.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/embd3d)
install(EXPORT embd3dTargets
        FILE embd3dConfig.cmake
        NAMESPACE embd3d::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/embd3d)
