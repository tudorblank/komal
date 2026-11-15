# Makes the extra themes beside the program match assets/themes/: each .toml copied (only when it
# changed), and a copy whose file is gone from assets/themes removed. Run by the build every time
# (the themes target in CMakeLists.txt), so adding, editing or removing a theme file shows on the
# next build.
#
#   cmake -DFROM=<assets/themes> -DTO=<the program's themes/> -P sync-themes.cmake

file(MAKE_DIRECTORY "${TO}")

file(GLOB themes "${FROM}/*.toml")
foreach(theme IN LISTS themes)
    get_filename_component(name "${theme}" NAME)
    file(COPY_FILE "${theme}" "${TO}/${name}" ONLY_IF_DIFFERENT)
endforeach()

file(GLOB copies "${TO}/*.toml")
foreach(copy IN LISTS copies)
    get_filename_component(name "${copy}" NAME)
    if(NOT EXISTS "${FROM}/${name}")
        file(REMOVE "${copy}")
        message(STATUS "Removed theme ${name}: it's gone from assets/themes")
    endif()
endforeach()
