# The content pipeline.
#
# Two stages, both offline and both headless. `bake-assets --content` writes the
# material catalogue's surfaces as PNG, using the catalogue itself so the names
# and the parameters are exactly the ones the runtime will ask for. CNA's own
# `cna_tool_source_to_cnb` then compiles each PNG into a `.cnb`, which is what
# `ContentManager::Load<Texture2D>` reads at start-up.
#
# The target is not part of ALL, and that is deliberate: the demo runs perfectly
# well without it by generating every surface at start-up. A content build turns
# a nine-second start into a one-second one; it is an optimisation, not a
# dependency, and making it mandatory would mean a fresh clone could not run.

set(CNA_STREET_CONTENT_SEED "20260903" CACHE STRING
    "Procedural seed the content build bakes. Must match the seed the demo runs
     with, or the compiled surfaces are a different street's")
set(CNA_STREET_CONTENT_DIR "${CMAKE_SOURCE_DIR}/assets/content" CACHE PATH
    "Where the compiled .cnb assets are written")
set(CNA_STREET_CONTENT_STAGING "${CMAKE_BINARY_DIR}/content-source" CACHE PATH
    "Where the content pipeline's intermediate PNGs are written")

if(TARGET cna_tool_source_to_cnb)
    add_custom_target(content
        COMMAND "${CMAKE_COMMAND}"
                -D "BAKE=$<TARGET_FILE:bake-assets>"
                -D "COMPILER=$<TARGET_FILE:cna_tool_source_to_cnb>"
                -D "STAGING=${CNA_STREET_CONTENT_STAGING}"
                -D "OUTPUT=${CNA_STREET_CONTENT_DIR}"
                -D "SEED=${CNA_STREET_CONTENT_SEED}"
                -P "${CMAKE_CURRENT_LIST_DIR}/CnaStreetBuildContent.cmake"
        DEPENDS bake-assets cna_tool_source_to_cnb
        COMMENT "cna-street: compiling the content pipeline"
        VERBATIM
        USES_TERMINAL)
else()
    message(STATUS "cna-street: cna_tool_source_to_cnb is not available; "
                   "the 'content' target is not defined")
endif()
