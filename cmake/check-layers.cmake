# The source folders are layers, bottom first. A file may include its own folder and the folders
# below it, never one above, and only the libraries its layer may use. The build runs this before
# every build (CMakeLists.txt) and stops on any wrong include, listing every one.
#
#   base       Signal, Deadline, Log, Perf, math, palette, files: the standard library only
#   input      what input is (InputFrame), a press followed to its release   the standard library
#              (PointerCapture); sdl-input makes it from SDL: built into the app, not the core
#   raster     pixels, tiles, grids, rasters, brush strokes                + abseil
#   node       the node graph: signals, masks, the render path             + abseil
#   command    the actions input and menus trigger (the one way in)        + abseil
#   document   the Project: nodes, rasters, history, the document's actions + abseil, stb_image
#   gpu        the GPU: device, objects, pages of slots                    + abseil, webgpu
#   ui         the interface: its look, widgets, panels (the canvas and the graph among them), tools
#                                                                          + webgpu, ImGui (and its internals), toml++
#   app        the platform: the window, the loop, frames; main            anything (SDL lives here)
#   tests      the core (base .. document) only                            + doctest
#   tests/ui   the UI's blocks that need no window or GPU (its look)       + doctest, the UI's libraries
#
#   cmake -DSRC=<src dir> -DTESTS=<tests dir> -P check-layers.cmake

set(LAYERS base input raster node command document gpu ui app)

# What each layer may include with <...>, as regexes; a standard header is a bare name.
set(STD "^[a-z_0-9]+$")
set(LIBS_base     "${STD}")
set(LIBS_input    "${STD}")
set(LIBS_raster   "${STD}" "^absl/")
set(LIBS_node     ${LIBS_raster})
set(LIBS_command  ${LIBS_raster})
set(LIBS_document ${LIBS_raster} "^stb_image\\.h$")
set(LIBS_gpu      ${LIBS_raster} "^webgpu/")
set(LIBS_ui       ${LIBS_gpu} "^imgui\\.h$" "^imgui_internal\\.h$" "^toml\\+\\+/")
set(LIBS_app      ".*")
set(LIBS_tests    ${LIBS_raster} "^doctest/")
set(LIBS_tests_ui ${LIBS_ui} "^doctest/")

# Files that may use a library their layer may not: "path|regex".
set(FILE_LIBS
    "input/sdl-input.|^SDL3/"         # the one file of input/ that knows SDL
    "raster/tile-codec.|^zstd\\.h$")  # ... and the one that knows zstd

# Headers private to a folder: only files inside it (and the header's own .cpp) may include them.
# "header|folder/" (a folder, or any path prefix).
#   the theme: only the palette reads it (it gives each colour a role)
#   the palette: only the styles read colour tokens; everything else borrows a style
#   the widgets' internals: panels use widgets.hpp
#   SDL's input: only the app sees it (the core never includes SDL)
set(PRIVATE
    "input/sdl-input.hpp|app/"
    "ui/style/palette.hpp|ui/style/"
    "ui/style/theme.hpp|ui/style/palette."
    "ui/widgets/interaction.hpp|ui/widgets/")

set(problems "")

# rank: a layer's place in LAYERS; tests rank as the top of the core (document).
function(check_file path rel layer rank)
    file(STRINGS "${path}" lines REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS lines)
        if(NOT line MATCHES "^[ \t]*#[ \t]*include[ \t]*([<\"])([^>\"]+)[>\"]")
            continue()
        endif()
        set(kind "${CMAKE_MATCH_1}")
        set(name "${CMAKE_MATCH_2}")
        if(kind STREQUAL "<")
            set(allowed FALSE)
            foreach(rule IN LISTS LIBS_${layer})
                if(name MATCHES "${rule}")
                    set(allowed TRUE)
                    break()
                endif()
            endforeach()
            foreach(entry IN LISTS FILE_LIBS)
                string(REPLACE "|" ";" pair "${entry}")
                list(GET pair 0 prefix)
                list(GET pair 1 rule)
                string(FIND "${rel}" "${prefix}" at)
                if(at EQUAL 0 AND name MATCHES "${rule}")
                    set(allowed TRUE)
                endif()
            endforeach()
            if(NOT allowed)
                list(APPEND problems "${rel}: includes <${name}> (not a library ${layer} may use)")
            endif()
            continue()
        endif()
        if(NOT name MATCHES "/")
            continue()   # "x.hpp": its own folder
        endif()
        foreach(entry IN LISTS PRIVATE)
            string(REPLACE "|" ";" pair "${entry}")
            list(GET pair 0 header)
            list(GET pair 1 folder)
            string(FIND "${rel}" "${folder}" at)
            string(REGEX REPLACE "\\.hpp$" ".cpp" own "${header}")   # its own source may
            string(FIND "${rel}" "tests/" test_at)   # tests may look inside: a parser's failures
            if(name STREQUAL header AND NOT at EQUAL 0 AND NOT rel STREQUAL own AND NOT test_at EQUAL 0)
                list(APPEND problems "${rel}: includes \"${name}\" (private to ${folder})")
            endif()
        endforeach()
        string(REGEX REPLACE "/.*" "" target "${name}")
        list(FIND LAYERS "${target}" target_rank)
        if(target_rank EQUAL -1)
            list(APPEND problems "${rel}: includes \"${name}\" ('${target}' isn't a layer)")
        elseif(target_rank GREATER rank)
            list(APPEND problems "${rel}: includes \"${name}\" (${target} is above ${layer})")
        endif()
    endforeach()
    set(problems "${problems}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE sources LIST_DIRECTORIES false "${SRC}/*.hpp" "${SRC}/*.cpp")
list(SORT sources)
foreach(path IN LISTS sources)
    file(RELATIVE_PATH rel "${SRC}" "${path}")
    string(REGEX REPLACE "/.*" "" layer "${rel}")
    list(FIND LAYERS "${layer}" rank)
    if(rank EQUAL -1)
        list(APPEND problems "${rel}: folder '${layer}' isn't a layer (add it to cmake/check-layers.cmake)")
        continue()
    endif()
    check_file("${path}" "${rel}" "${layer}" ${rank})
endforeach()

if(TESTS)
    list(FIND LAYERS document core_top)
    file(GLOB_RECURSE tests LIST_DIRECTORIES false "${TESTS}/*.hpp" "${TESTS}/*.cpp")
    list(SORT tests)
    list(FIND LAYERS ui ui_rank)
    foreach(path IN LISTS tests)
        file(RELATIVE_PATH rel "${TESTS}" "${path}")
        if(rel MATCHES "^ui/")
            check_file("${path}" "tests/${rel}" tests_ui ${ui_rank})
        else()
            check_file("${path}" "tests/${rel}" tests ${core_top})
        endif()
    endforeach()
endif()

if(problems)
    foreach(p IN LISTS problems)
        message("layer check: ${p}")
    endforeach()
    message(FATAL_ERROR "includes break the layers (see above)")
endif()
