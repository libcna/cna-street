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
set(CNA_STREET_EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/assets/external/downloads" CACHE PATH
    "Where scripts/fetch-assets.sh puts the external glTF sources")

# The manifest's local names, so the compiled asset is called what the runtime
# asks for. Read at configure time through scripts/manifest-tool.py, which is
# the one place that knows the manifest's shape: an asset may be a single .glb
# or a .gltf with a buffer and a folder of images, and the surfaces declared
# beside them are not models at all. Without Python there are no imported
# models and no scanned surfaces, and the build says so once rather than
# guessing at JSON with a regular expression.
set(CNA_STREET_MODEL_NAMES "")
set(CNA_STREET_MANIFEST "${CMAKE_SOURCE_DIR}/assets/external/manifest.json")
find_program(CNA_STREET_PYTHON NAMES python3 python)
if(EXISTS "${CNA_STREET_MANIFEST}")
    if(CNA_STREET_PYTHON)
        execute_process(
            COMMAND "${CNA_STREET_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/manifest-tool.py" models
            OUTPUT_VARIABLE _models
            RESULT_VARIABLE _models_result
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_models_result EQUAL 0)
            string(REPLACE "\n" ";" CNA_STREET_MODEL_NAMES "${_models}")
        else()
            message(WARNING "cna-street: scripts/manifest-tool.py could not read the manifest; "
                            "no imported models will be compiled")
        endif()
    else()
        message(STATUS "cna-street: python3 not found; the content build will compile the "
                       "generated surfaces only, with no imported models or scanned surfaces")
    endif()
endif()

if(TARGET cna_tool_source_to_cnb)
    # The licence gate. Not part of `content` -- a build that cannot reach
    # Python should still compile textures -- but it is the thing to run before
    # trusting the manifest, and it found an undeclared 7.8 MB model sitting in
    # the fetched set on its first run.
    if(CNA_STREET_PYTHON)
        add_custom_target(validate-assets
            COMMAND "${CNA_STREET_PYTHON}"
                    "${CMAKE_SOURCE_DIR}/scripts/validate-assets.py"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "cna-street: checking every external asset's licence and digest"
            VERBATIM)
    endif()

    add_custom_target(content
        COMMAND "${CMAKE_COMMAND}"
                -D "BAKE=$<TARGET_FILE:bake-assets>"
                -D "COMPILER=$<TARGET_FILE:cna_tool_source_to_cnb>"
                -D "STAGING=${CNA_STREET_CONTENT_STAGING}"
                -D "OUTPUT=${CNA_STREET_CONTENT_DIR}"
                -D "SEED=${CNA_STREET_CONTENT_SEED}"
                -D "GLTF_COMPILER=$<IF:$<TARGET_EXISTS:cna_tool_gltf_to_cnb>,$<TARGET_FILE:cna_tool_gltf_to_cnb>,>"
                -D "GLTF_SOURCE=${CNA_STREET_EXTERNAL_DIR}"
                -D "MODEL_NAMES=${CNA_STREET_MODEL_NAMES}"
                -D "MANIFEST=${CNA_STREET_MANIFEST}"
                -D "PYTHON=${CNA_STREET_PYTHON}"
                -D "PREPARE=${CMAKE_SOURCE_DIR}/scripts/prepare-surfaces.py"
                -P "${CMAKE_CURRENT_LIST_DIR}/CnaStreetBuildContent.cmake"
        DEPENDS bake-assets cna_tool_source_to_cnb
                $<$<TARGET_EXISTS:cna_tool_gltf_to_cnb>:cna_tool_gltf_to_cnb>
        COMMENT "cna-street: compiling the content pipeline"
        VERBATIM
        USES_TERMINAL)
else()
    message(STATUS "cna-street: cna_tool_source_to_cnb is not available; "
                   "the 'content' target is not defined")
endif()
