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

# --- scanned surfaces -------------------------------------------------------
# After the bake, before the compile: where the manifest declares a scanned PBR
# set for a catalogue surface and it has been fetched, prepare-surfaces.py
# writes its three maps over the generated ones under the same name, and
# authored.txt beside them. The runtime treats those maps as measured values
# rather than as a generator's output. Pillow missing or nothing fetched is a
# message, not a failure: the generated surfaces stand.
if(DEFINED PYTHON AND NOT PYTHON STREQUAL "" AND DEFINED MANIFEST AND EXISTS "${MANIFEST}")
    get_filename_component(_downloads "${GLTF_SOURCE}" ABSOLUTE)
    message(STATUS "cna-street: preparing scanned surfaces")
    execute_process(COMMAND "${PYTHON}" "${PREPARE}"
                            --manifest "${MANIFEST}"
                            --downloads "${_downloads}"
                            --staging "${STAGING}"
                    RESULT_VARIABLE prepare_result)
    if(NOT prepare_result EQUAL 0)
        message(WARNING "cna-street: prepare-surfaces.py failed (${prepare_result}); "
                        "the generated surfaces stand")
    endif()
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

# The surface nominals, which the bake wrote beside its images. Not a texture
# and so not compiled: MaterialLibrary reads it directly to work out how far to
# scale each material's roughness and metalness factor so that PbrEffect's
# factor-times-map product averages the value the material declared. Without it
# a content-backed start-up keeps the squared factors, which is a whole city one
# stop too glossy, so the app warns when it is missing rather than guessing.
if(EXISTS "${STAGING}/surfaces.txt")
    file(COPY_FILE "${STAGING}/surfaces.txt" "${OUTPUT}/surfaces.txt")
else()
    message(WARNING "cna-street: the bake wrote no surfaces.txt; material factors "
                    "will not be normalised at runtime")
endif()

# The list of scanned surfaces, so a content-backed start-up knows which maps
# to take at face value. Removed when this build has none, or a stale list
# would mark generated surfaces as measured.
if(EXISTS "${STAGING}/authored.txt")
    file(COPY_FILE "${STAGING}/authored.txt" "${OUTPUT}/authored.txt")
else()
    file(REMOVE "${OUTPUT}/authored.txt")
endif()

# --- imported glTF models ----------------------------------------------------
# The second half of the pipeline, and the more interesting one: CNA's own
# glTF importer, driven by cna_tool_gltf_to_cnb, which links the content
# library's shared glTF-to-CNJ orchestration -- so what this project imports is
# what the framework thinks the file means, not a second opinion.
#
# The models are absent from a tree that has not run scripts/fetch-assets.sh,
# and that is not an error here any more than it is at runtime.
set(models 0)
if(DEFINED GLTF_COMPILER AND NOT GLTF_COMPILER STREQUAL "" AND DEFINED GLTF_SOURCE
   AND DEFINED MODEL_NAMES)
    # One `relative/path.gltf=logical-name` per manifest asset, from
    # manifest-tool.py at configure time. The path is relative to the
    # downloads directory; the logical name is what the runtime asks
    # ContentManager for ("chair-damask", not "ChairDamaskPurplegold").
    set(gltf_staging "${STAGING}/gltf")
    file(MAKE_DIRECTORY "${gltf_staging}")
    foreach(pair IN LISTS MODEL_NAMES)
        string(REGEX MATCH "^([^=]+)=(.*)$" matched "${pair}")
        if(NOT matched)
            continue()
        endif()
        set(source "${GLTF_SOURCE}/${CMAKE_MATCH_1}")
        set(logical "${CMAKE_MATCH_2}")
        if(NOT EXISTS "${source}")
            # Not fetched. Not an error here any more than at runtime.
            continue()
        endif()
        file(REMOVE_RECURSE "${gltf_staging}/${logical}")
        file(MAKE_DIRECTORY "${gltf_staging}/${logical}")
        execute_process(COMMAND "${GLTF_COMPILER}" "${source}" "${OUTPUT}" "${logical}"
                                --keep-cnj "${gltf_staging}/${logical}" --quiet
                        RESULT_VARIABLE import_result
                        ERROR_VARIABLE import_error)
        if(NOT import_result EQUAL 0)
            # A model CNA declines is reported and skipped rather than failing
            # the build: the scene has a fallback for every one of them, and a
            # single unsupported extension in one prop should not cost the
            # whole content build. What it must not do is pass silently.
            message(WARNING "cna-street: could not import ${logical}: ${import_error}")
            continue()
        endif()
        # The images the model refers to, beside it, for a compiled Model that
        # carries its textures as external references rather than absorbing
        # them. ContentManager resolves them by name -- and it looks for
        # "<name>.cnb" before it looks for the loose file, so each image is
        # also compiled under its own full name through the same mip-chain
        # compiler the catalogue's surfaces go through, in the colour space
        # scripts/model-textures.py reads off the model. The loose image
        # would arrive with one mip level (docs/cna-findings.md GLTF-206)
        # and shimmer from a few metres; the compiled one arrives with the
        # chain. The loose copy stays beside it for a runtime without the
        # compiled one.
        set(spaces "")
        if(DEFINED PYTHON AND NOT PYTHON STREQUAL "" AND DEFINED MODEL_TEXTURES
           AND EXISTS "${MODEL_TEXTURES}")
            execute_process(COMMAND "${PYTHON}" "${MODEL_TEXTURES}"
                                    "${gltf_staging}/${logical}/${logical}.cnj"
                            OUTPUT_VARIABLE spaces
                            RESULT_VARIABLE spaces_result
                            OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT spaces_result EQUAL 0)
                set(spaces "")
            endif()
            string(REPLACE "\n" ";" spaces "${spaces}")
        endif()
        file(GLOB extracted "${gltf_staging}/${logical}/*.jpg"
                            "${gltf_staging}/${logical}/*.png")
        foreach(image IN LISTS extracted)
            get_filename_component(image_name "${image}" NAME)
            file(COPY_FILE "${image}" "${OUTPUT}/${image_name}")
            set(space "")
            foreach(entry IN LISTS spaces)
                if(entry STREQUAL "${image_name}=srgb")
                    set(space srgb)
                elseif(entry STREQUAL "${image_name}=linear")
                    set(space linear)
                endif()
            endforeach()
            if(space STREQUAL "")
                continue()
            endif()
            execute_process(COMMAND "${COMPILER}" "${image}" "${OUTPUT}/${image_name}.cnb"
                                    --name "${image_name}" --mipmaps --mip-color-space "${space}"
                                    --quiet
                            RESULT_VARIABLE compile_result)
            if(NOT compile_result EQUAL 0)
                message(WARNING "cna-street: could not compile ${image_name} with a mip chain "
                                "(${compile_result}); the loose image stands")
                file(REMOVE "${OUTPUT}/${image_name}.cnb")
            endif()
        endforeach()
        math(EXPR models "${models} + 1")
    endforeach()
endif()

# --- imported people ------------------------------------------------------------
# The MakeHuman-derived pedestrians, in this project's own character format:
# a JSON header and two binaries per person, copied as they are, and their
# textures compiled exactly like the catalogue's surfaces -- with a mip chain,
# sRGB for an albedo and linear for a normal map -- which is what a texture
# absorbed into a model does not get (GLTF-206). Absent from a tree where
# Blender and MPFB have not run, and that is not an error.
set(people 0)
set(people_dir "${GLTF_SOURCE}/derived/people")
if(IS_DIRECTORY "${people_dir}")
    file(GLOB people_images "${people_dir}/*.png")
    foreach(image IN LISTS people_images)
        get_filename_component(filename "${image}" NAME)
        string(REGEX REPLACE "\\.png$" "" logical "${filename}")
        if(logical MATCHES "\\.albedo$")
            set(space srgb)
        else()
            set(space linear)
        endif()
        execute_process(COMMAND "${COMPILER}" "${image}" "${OUTPUT}/${logical}.cnb"
                                --name "${logical}" --mipmaps --mip-color-space "${space}" --quiet
                        RESULT_VARIABLE compile_result)
        if(NOT compile_result EQUAL 0)
            message(WARNING "cna-street: could not compile ${filename} (${compile_result})")
        endif()
    endforeach()
    file(GLOB people_files "${people_dir}/*.json" "${people_dir}/*.bin")
    foreach(source IN LISTS people_files)
        get_filename_component(filename "${source}" NAME)
        file(COPY_FILE "${source}" "${OUTPUT}/${filename}")
        if(filename MATCHES "\\.json$")
            math(EXPR people "${people} + 1")
        endif()
    endforeach()
endif()

message(STATUS "cna-street: content build complete -- ${compiled} surfaces, "
               "${models} model(s) and ${people} people in ${OUTPUT}")
