# Script-mode half of the content pipeline: bake, then compile.
#
# A script rather than a per-file custom command because the file list does not
# exist until the bake has run, and a build system that has to be re-configured
# to notice a new texture is a build system that will be out of date.

if(NOT DEFINED SEED OR SEED STREQUAL "")
    set(SEED 20260903)
endif()

file(REMOVE_RECURSE "${STAGING}")
file(MAKE_DIRECTORY "${STAGING}")
file(MAKE_DIRECTORY "${OUTPUT}")

message(STATUS "cna-street: baking content source images")
execute_process(COMMAND "${BAKE}" --content "${STAGING}" --seed "${SEED}"
                RESULT_VARIABLE bake_result)
if(NOT bake_result EQUAL 0)
    message(FATAL_ERROR "cna-street: bake-assets failed (${bake_result})")
endif()

file(GLOB sources "${STAGING}/*.png")
list(LENGTH sources source_count)
message(STATUS "cna-street: compiling ${source_count} images to .cnb")

set(compiled 0)
foreach(source IN LISTS sources)
    get_filename_component(stem "${source}" NAME_WE)
    # NAME_WE stops at the first dot, and every asset here is
    # "<material>.<map>.png", so the logical name has to come from the full
    # filename with only the .png removed. Getting this wrong silently collapses
    # a material's three maps onto one asset.
    get_filename_component(filename "${source}" NAME)
    string(REGEX REPLACE "\\.png$" "" logical "${filename}")
    # Which colour space the mip chain is averaged in follows from what the map
    # is. An albedo or an emissive is sRGB-encoded, and averaging encoded values
    # as if they were light darkens every level; a normal, a roughness or a mask
    # is not encoded that way and must be averaged exactly as it is stored.
    if(logical MATCHES "\\.(albedo|emissive)$")
        set(space srgb)
    else()
        set(space linear)
    endif()
    execute_process(COMMAND "${COMPILER}" "${source}" "${OUTPUT}/${logical}.cnb"
                            --name "${logical}" --mipmaps --mip-color-space "${space}" --quiet
                    RESULT_VARIABLE compile_result)
    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR "cna-street: could not compile ${filename} (${compile_result})")
    endif()
    math(EXPR compiled "${compiled} + 1")
endforeach()

# --- imported glTF models ----------------------------------------------------
# The second half of the pipeline, and the more interesting one: CNA's own
# glTF importer, driven by cna_tool_gltf_to_cnb, which links the content
# library's shared glTF-to-CNJ orchestration -- so what this project imports is
# what the framework thinks the file means, not a second opinion.
#
# The models are absent from a tree that has not run scripts/fetch-assets.sh,
# and that is not an error here any more than it is at runtime.
set(models 0)
if(DEFINED GLTF_COMPILER AND NOT GLTF_COMPILER STREQUAL "" AND DEFINED GLTF_SOURCE)
    file(GLOB gltf_sources "${GLTF_SOURCE}/*.glb" "${GLTF_SOURCE}/*.gltf")
    list(LENGTH gltf_sources gltf_count)
    if(gltf_count GREATER 0)
        message(STATUS "cna-street: importing ${gltf_count} glTF model(s) through CNA")
        # The staging directory keeps the extracted images the model refers to;
        # they are copied beside the .cnb because a compiled Model carries its
        # textures as external references rather than absorbing them.
        set(gltf_staging "${STAGING}/gltf")
        file(MAKE_DIRECTORY "${gltf_staging}")
        foreach(source IN LISTS gltf_sources)
            get_filename_component(stem "${source}" NAME_WE)
            # The manifest's local name, not the upstream file's: the runtime
            # asks for "chair-damask", not "ChairDamaskPurplegold".
            set(logical "${stem}")
            if(DEFINED MODEL_NAMES)
                foreach(pair IN LISTS MODEL_NAMES)
                    string(REGEX MATCH "^([^=]+)=(.*)$" matched "${pair}")
                    if(matched AND CMAKE_MATCH_1 STREQUAL "${stem}")
                        set(logical "${CMAKE_MATCH_2}")
                    endif()
                endforeach()
            endif()
            file(REMOVE_RECURSE "${gltf_staging}/${logical}")
            file(MAKE_DIRECTORY "${gltf_staging}/${logical}")
            execute_process(COMMAND "${GLTF_COMPILER}" "${source}" "${OUTPUT}" "${logical}"
                                    --keep-cnj "${gltf_staging}/${logical}" --quiet
                            RESULT_VARIABLE import_result
                            ERROR_VARIABLE import_error)
            if(NOT import_result EQUAL 0)
                # A model CNA declines is reported and skipped rather than
                # failing the build: the scene has a fallback for every one of
                # them, and a single unsupported extension in one prop should
                # not cost the whole content build. What it must not do is pass
                # silently.
                message(WARNING "cna-street: could not import ${stem}: ${import_error}")
                continue()
            endif()
            # The images the model refers to, beside it. cnb_info lists them as
            # external references; they are ordinary JPEG/PNG files and
            # ContentManager resolves them by name.
            file(GLOB extracted "${gltf_staging}/${logical}/*.jpg"
                                "${gltf_staging}/${logical}/*.png")
            foreach(image IN LISTS extracted)
                get_filename_component(image_name "${image}" NAME)
                file(COPY_FILE "${image}" "${OUTPUT}/${image_name}")
            endforeach()
            math(EXPR models "${models} + 1")
        endforeach()
    endif()
endif()

message(STATUS "cna-street: content build complete -- ${compiled} surfaces and "
               "${models} model(s) in ${OUTPUT}")
