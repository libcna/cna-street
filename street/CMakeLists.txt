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
# the source tree in a development build and let packaging override it.
set(CNA_STREET_ASSET_DIR "${CMAKE_SOURCE_DIR}/assets" CACHE PATH
    "Directory the application loads its assets from")
target_compile_definitions(cna_street_core PUBLIC
    CNA_STREET_DEFAULT_ASSET_DIR="${CNA_STREET_ASSET_DIR}")


# --- offline asset bake ------------------------------------------------------
# Headless: no GraphicsDevice, no display. It is both the source stage of the
# content pipeline and the only way to look at a generated texture on a machine
# with no GPU.
add_executable(bake-assets "${CMAKE_SOURCE_DIR}/tools/bake/BakeAssets.cpp")
target_link_libraries(bake-assets PRIVATE CnaStreet::Core CnaStreet::Warnings)
