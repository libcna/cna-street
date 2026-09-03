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
# asks for. Read at configure time, which is the right time: the manifest
# changes when somebody adds an asset, and that is a configure-worthy event.
set(CNA_STREET_MODEL_NAMES "")
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/external/manifest.json")
    # Zipped rather than matched as one pattern: CMake's regex engine has no
    # non-greedy repetition, so "name ... file" across a JSON object cannot be
    # written as a single match. Every asset carries exactly one of each, in
    # that order, so the two lists line up.
    file(READ "${CMAKE_SOURCE_DIR}/assets/external/manifest.json" _manifest)
    string(REGEX MATCHALL "\"name\"[ \t]*:[ \t]*\"[^\"]+\"" _names "${_manifest}")
    string(REGEX MATCHALL "\"file\"[ \t]*:[ \t]*\"[^\"]+\"" _files "${_manifest}")
    list(LENGTH _names _name_count)
    list(LENGTH _files _file_count)
    if(_name_count EQUAL _file_count)
        math(EXPR _last "${_name_count} - 1")
        if(_last GREATER_EQUAL 0)
            foreach(_i RANGE ${_last})
                list(GET _names ${_i} _n)
                list(GET _files ${_i} _f)
                string(REGEX MATCH "\"([^\"]+)\"$" _m "${_n}")
                set(_local "${CMAKE_MATCH_1}")
                string(REGEX MATCH "\"([^\"]+)\"$" _m "${_f}")
                string(REGEX REPLACE "\\.(glb|gltf)$" "" _stem "${CMAKE_MATCH_1}")
                if(_local AND _stem)
                    list(APPEND CNA_STREET_MODEL_NAMES "${_stem}=${_local}")
                endif()
            endforeach()
        endif()
    else()
        message(WARNING "cna-street: assets/external/manifest.json has ${_name_count} names "
                        "and ${_file_count} files; the model name map is skipped")
    endif()
endif()

if(TARGET cna_tool_source_to_cnb)
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
