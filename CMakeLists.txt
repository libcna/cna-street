# =============================================================================
# cna-street -- a realistic 3D city street rendered with the CNA framework.
# =============================================================================
# Layout note: application sources live under street/ rather than the
# conventional src/ + include/ at the repository root. CNA's module-layout
# validator tests ${CMAKE_SOURCE_DIR}/src, which is *this* project's root when
# CNA is consumed with add_subdirectory(), and aborts the configure with a
# message about CNA's own history. See docs/cna-findings.md CNA-F2.
cmake_minimum_required(VERSION 3.23)

project(CnaStreet
    VERSION 0.1.0
    DESCRIPTION "A realistic 3D city street built on the CNA framework"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# The renderer CNA compiles in. OPENGL33 is the default here rather than CNA's
# own Linux default (OPENGLES3) because the street's material set uses desktop
# GL 3.3 shading; every renderer in CNA's 2d3d set is selectable.
set(CNA_STREET_RENDERER "OPENGL33" CACHE STRING
    "CNA graphics renderer to build against (OPENGL33, OPENGLES3, VULKAN, SOFTWARE, ...)")

option(CNA_STREET_BUILD_TESTS "Build cna-street's unit tests" ON)

include(CnaStreetWarnings)
include(CnaStreetDependencies)

message(STATUS "cna-street: ${PROJECT_VERSION}, renderer ${CNA_STREET_RENDERER}, "
               "${CMAKE_BUILD_TYPE}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "cna-street: CNA at ${CNA_ROOT_DIR}")

add_subdirectory("${CNA_ROOT_DIR}" cna EXCLUDE_FROM_ALL)

# CNA's SDL comes from a vendored submodule built as part of the framework; a
# few ancillary upstream targets still expect the config packages to be found.
foreach(_sdl_package IN ITEMS SDL3)
    if(NOT TARGET ${_sdl_package}::${_sdl_package})
        find_package(${_sdl_package} QUIET CONFIG)
    endif()
endforeach()

add_subdirectory(street)

if(CNA_STREET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
