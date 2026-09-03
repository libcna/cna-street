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

message(STATUS "cna-street: content build complete -- ${compiled} assets in ${OUTPUT}")
