# ---------------------------------------------------------------------------
# Locating and configuring the upstream checkouts cna-street builds against.
# ---------------------------------------------------------------------------
# CNA installs and exports no CMake package, so find_package(CNA) does not
# exist: the framework is consumed with add_subdirectory() from a sibling
# checkout. CNA in turn resolves sharp-runtime, easy-gl and meta-gl relative to
# *its own* source root, so the whole set has to live side by side.
#
# Everything here is a cache variable with a sibling default. Nothing in this
# project ever hardcodes a developer's absolute path.
include_guard(GLOBAL)

set(CNA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cna" CACHE PATH
    "Path to the CNA framework checkout (a sibling of this repository by default)")

function(cna_street_require_checkout root name clone_url branch)
    if(EXISTS "${root}/CMakeLists.txt")
        return()
    endif()
    message(FATAL_ERROR
        "cna-street: ${name} not found at '${root}'.\n"
        "It is a separate repository, not part of this project. Fetch every\n"
        "dependency at once with\n"
        "    scripts/fetch-dependencies.sh\n"
        "or clone it yourself with\n"
        "    git clone --branch ${branch} ${clone_url} ${root}\n"
        "and pass -DCNA_ROOT_DIR=/path/to/cna if it does not sit beside this\n"
        "repository. README.md \"Prerequisites\" lists the full set.")
endfunction()

cna_street_require_checkout("${CNA_ROOT_DIR}" "the CNA framework"
    "https://github.com/openeggbert/cna.git" "next")
cna_street_require_checkout("${CNA_ROOT_DIR}/../sharp-runtime" "sharp-runtime"
    "https://github.com/openeggbert/sharp-runtime.git" "next")

# The GL renderer family is implemented on top of easy-gl, which itself expects
# meta-gl beside it. Check here rather than letting CNA fail later with a
# message that names CNA's own layout instead of this project's bootstrap.
if(CNA_STREET_RENDERER MATCHES "^(OPENGL33|OPENGLES3|OPENGLES2)$")
    cna_street_require_checkout("${CNA_ROOT_DIR}/../easy-gl" "easy-gl"
        "https://github.com/openeggbert/easy-gl.git" "develop")
    cna_street_require_checkout("${CNA_ROOT_DIR}/../meta-gl" "meta-gl"
        "https://github.com/openeggbert/meta-gl.git" "develop")
endif()

# --- CNA build options -----------------------------------------------------
# CNA_CNAEXT is the one that matters: without it every CNA/Graphics/*.hpp header
# compiles to nothing (they are wrapped in `#ifdef CNA_CNAEXT`) and the whole
# modern rendering surface this project is built on disappears. It defaults OFF
# upstream, so it is forced here rather than left to the caller.
set(CNA_CNAEXT           ON  CACHE BOOL "Enable CNA's extended graphics layer" FORCE)
set(CNA_BUILD_TESTS      OFF CACHE BOOL "Build CNA's own tests"                FORCE)
set(CNA_BUILD_EXAMPLES   OFF CACHE BOOL "Build CNA's example applications"     FORCE)
set(CNA_ENABLE_NET       OFF CACHE BOOL "Build CNA networking"                 FORCE)
set(CNA_ENABLE_VIDEO     OFF CACHE STRING "Enable FFmpeg video playback"       FORCE)
set(EASYGL_BUILD_TESTS    OFF CACHE BOOL "Build easy-gl tests"    FORCE)
set(EASYGL_BUILD_EXAMPLES OFF CACHE BOOL "Build easy-gl examples" FORCE)
set(METAGL_BUILD_TESTS    OFF CACHE BOOL "Build meta-gl tests"    FORCE)
set(BUILD_TESTING_SAVED "${BUILD_TESTING}")

if(CNA_STREET_RENDERER AND NOT CNA_STREET_RENDERER STREQUAL "")
    set(CNA_GRAPHICS_RENDERER "${CNA_STREET_RENDERER}" CACHE STRING
        "Graphics renderer CNA compiles in" FORCE)
endif()

# --- Upstream workaround 1: SDL's prebuilt cache ---------------------------
# CNA caches its vendored SDL build inside its own source checkout by default,
# which writes into a dependency this project treats as read-only. Keep it in
# our build tree, keyed by toolchain so two toolchains do not fight. See
# docs/cna-findings.md CNA-F3.
if(NOT DEFINED CNA_SDL_PREBUILT_ROOT)
    set(CNA_SDL_PREBUILT_ROOT
        "${CMAKE_BINARY_DIR}/cna-sdl-prebuilt-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_CXX_COMPILER_ID}"
        CACHE PATH "Persistent CNA SDL build cache, kept outside the CNA checkout")
endif()

# --- Upstream workaround 2: vendored single-header include paths -----------
# CNA's content module adds its vendored headers as
# ${CMAKE_SOURCE_DIR}/third_party/..., which resolves into the *consumer's* tree
# when CNA is a subdirectory, so the build dies with "cgltf.h: No such file".
# INCLUDE_DIRECTORIES is inherited by subdirectories, so setting the correct
# paths before add_subdirectory() repairs the search without touching CNA.
# See docs/cna-findings.md CNA-F1.
foreach(_vendored IN ITEMS cgltf stb)
    if(EXISTS "${CNA_ROOT_DIR}/third_party/${_vendored}")
        include_directories("${CNA_ROOT_DIR}/third_party/${_vendored}")
    endif()
endforeach()

# --- sharp-runtime component selection -------------------------------------
# CNA declares the closure it needs itself; cna-street additionally uses
# System.Text.Json for its data-driven scene description, so that component has
# to be enabled *before* sharp-runtime is added or its target will not exist.
include("${CNA_ROOT_DIR}/cmake/SharpRuntimeConsumption.cmake" OPTIONAL)
if(DEFINED CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS)
    set(_street_components ${CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS} Text.Json Numerics)
    list(REMOVE_DUPLICATES _street_components)
    set(SHARP_RUNTIME_COMPONENTS "${_street_components}" CACHE STRING
        "Sharp Runtime components required by CNA and cna-street" FORCE)
endif()
