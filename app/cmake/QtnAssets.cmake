# Bundles the NME-300 assets of the original app.
#
# qtn_bundle_assets(<target> <assets_dir> <manifest_out>)
#   <assets_dir>   R6 output: names.csv (resourceName,originalFilename,offset,size,usedByNME300; data
#                  row i is BinaryData index i), nme300.txt (one originalFilename per line) and
#                  named/<originalFilename>.
#   <manifest_out> written at configure time: "index,filename,size" for every nme300.txt file.
# Each listed file and the manifest are added to <target> with MACOSX_PACKAGE_LOCATION, so the
# build copies them into Contents/Resources/assets/ (and re-copies when they change).
function (qtn_bundle_assets target assets_dir manifest_out)
    set (names_csv "${assets_dir}/names.csv")
    set (list_txt "${assets_dir}/nme300.txt")
    set (named_dir "${assets_dir}/named")
    if (NOT EXISTS "${names_csv}" OR NOT EXISTS "${list_txt}" OR NOT IS_DIRECTORY "${named_dir}")
        message (FATAL_ERROR
            "QTN_ASSETS_DIR='${assets_dir}' does not hold the extracted assets "
            "(names.csv, nme300.txt and named/). Run R6 (tools/name_assets.py) or pass "
            "-DQTN_ASSETS_DIR=<path to assets/extracted>.")
    endif ()
    set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${names_csv}" "${list_txt}")

    # names.csv: filename -> index and size.
    file (STRINGS "${names_csv}" rows)
    list (POP_FRONT rows header)
    if (NOT header STREQUAL "resourceName,originalFilename,offset,size,usedByNME300")
        message (FATAL_ERROR "${names_csv}: unexpected header '${header}'")
    endif ()
    set (index 0)
    foreach (row IN LISTS rows)
        if (NOT row MATCHES "^[^,]*,([^,]+),[^,]*,([0-9]+),[01]$")
            message (FATAL_ERROR "${names_csv}: malformed row ${index}: '${row}'")
        endif ()
        set ("qtn_index_${CMAKE_MATCH_1}" ${index})
        set ("qtn_size_${CMAKE_MATCH_1}" ${CMAKE_MATCH_2})
        math (EXPR index "${index} + 1")
    endforeach ()

    file (STRINGS "${list_txt}" filenames)
    set (manifest "index,filename,size\n")
    set (bundled "")
    foreach (filename IN LISTS filenames)
        if (NOT DEFINED "qtn_index_${filename}")
            message (FATAL_ERROR "${list_txt}: '${filename}' is not in names.csv")
        endif ()
        set (file "${named_dir}/${filename}")
        if (NOT EXISTS "${file}")
            message (FATAL_ERROR "Asset '${filename}' listed in nme300.txt is missing from ${named_dir}")
        endif ()
        file (SIZE "${file}" actual)
        if (NOT actual EQUAL "${qtn_size_${filename}}")
            message (FATAL_ERROR "Asset '${file}' is ${actual} bytes; names.csv says ${qtn_size_${filename}}")
        endif ()
        string (APPEND manifest "${qtn_index_${filename}},${filename},${actual}\n")
        list (APPEND bundled "${file}")
    endforeach ()

    # Rewrite only on change, so an unchanged configure does not trigger a re-copy.
    set (old "")
    if (EXISTS "${manifest_out}")
        file (READ "${manifest_out}" old)
    endif ()
    if (NOT old STREQUAL manifest)
        file (WRITE "${manifest_out}" "${manifest}")
    endif ()

    list (APPEND bundled "${manifest_out}")
    set_source_files_properties (${bundled} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/assets"
        HEADER_FILE_ONLY TRUE)
    target_sources (${target} PRIVATE ${bundled})

    list (LENGTH filenames count)
    message (STATUS "QuickTone assets: ${count} NME-300 files from ${assets_dir}")
endfunction ()
