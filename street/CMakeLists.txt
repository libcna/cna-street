# The street itself. Almost everything lives in a static library so the unit
# tests can link the same code the application runs; the executable target is a
# thin main().
file(GLOB_RECURSE CNA_STREET_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM CNA_STREET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")

add_library(cna_street_core STATIC ${CNA_STREET_SOURCES})
add_library(CnaStreet::Core ALIAS cna_street_core)

target_include_directories(cna_street_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cna_street_core
    PUBLIC  CNA
    PRIVATE CnaStreet::Warnings)

if(TARGET SharpRuntime::Text.Json)
    target_link_libraries(cna_street_core PUBLIC SharpRuntime::Text.Json)
endif()

target_compile_definitions(cna_street_core PUBLIC
    CNA_STREET_VERSION="${PROJECT_VERSION}"
    CNA_STREET_RENDERER_NAME="${CNA_STREET_RENDERER}")

add_executable(cna-street src/Main.cpp)
target_link_libraries(cna-street PRIVATE CnaStreet::Core CnaStreet::Warnings)
if(TARGET SDL3::SDL3main)
    target_link_libraries(cna-street PRIVATE SDL3::SDL3main)
endif()

# The assets directory is data, not compiled input: point the running binary at
# the source tree in a development build and let packaging override it. In the
# browser it is a path in Emscripten's virtual filesystem.
if(EMSCRIPTEN)
    set(_cna_street_default_asset_dir "/assets")
else()
    set(_cna_street_default_asset_dir "${CMAKE_SOURCE_DIR}/assets")
endif()
set(CNA_STREET_ASSET_DIR "${_cna_street_default_asset_dir}" CACHE PATH
    "Directory the application loads its assets from")
target_compile_definitions(cna_street_core PUBLIC
    CNA_STREET_DEFAULT_ASSET_DIR="${CNA_STREET_ASSET_DIR}")

if(EMSCRIPTEN)
    # Game::Run() is CNA's blocking loop, which the browser can only host
    # through Asyncify. Only the settings file is packaged: the compiled
    # content is gigabytes, and the demo generates every surface without it,
    # exactly as it does from a fresh clone.
    set_target_properties(cna-street PROPERTIES SUFFIX ".html")
    target_link_libraries(cna-street PRIVATE CNA::EmscriptenAsyncify)
    target_link_options(cna-street PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sMAXIMUM_MEMORY=4GB
        -sSTACK_SIZE=8MB
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        --preload-file "${CMAKE_SOURCE_DIR}/assets/config@/assets/config")
    return()
endif()


# --- offline asset bake ------------------------------------------------------
# Headless: no GraphicsDevice, no display. It is both the source stage of the
# content pipeline and the only way to look at a generated texture on a machine
# with no GPU.
add_executable(bake-assets "${CMAKE_SOURCE_DIR}/tools/bake/BakeAssets.cpp")
target_link_libraries(bake-assets PRIVATE CnaStreet::Core CnaStreet::Warnings)

# --- screenshot comparison ---------------------------------------------------
# The other half of `--capture`: scripts/check-screenshots.sh renders every named
# viewpoint and hands each pair to this. Deliberately standalone -- it needs a
# PNG *decoder*, which nothing else in the project does, and linking the whole
# street into a tool that reads two files would be the wrong shape.
add_executable(compare-images "${CMAKE_SOURCE_DIR}/tools/compare/CompareImages.cpp")
target_link_libraries(compare-images PRIVATE CnaStreet::Warnings)
target_include_directories(compare-images SYSTEM PRIVATE "${CNA_ROOT_DIR}/third_party/stb")
