cmake_minimum_required(VERSION 3.25)   # FetchContent's SYSTEM
project(Komal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # standard C++ only: MSVC has no GNU extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(KOMAL_TESTS       "Build and run the tests with every build" ON)
option(KOMAL_WERROR      "Treat warnings in Komal's own code as errors" OFF)
option(KOMAL_SANITIZE    "Build everything with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(KOMAL_SYSTEM_DEPS "Use an installed SDL3 / abseil when found, instead of the pinned versions" OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
# Every dependency is the pinned version below, checked by its hash, unless KOMAL_SYSTEM_DEPS.
if(NOT KOMAL_SYSTEM_DEPS)
    set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
endif()

# Sanitizers go on everything, the libraries built here too: their containers' layout must match.
if(KOMAL_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        # The UBSan checks that work with abseil's constexpr hashing (GCC rejects the rest there).
        set(KOMAL_UBSAN signed-integer-overflow,shift,bounds,integer-divide-by-zero,float-cast-overflow,return,unreachable,vla-bound)
        add_compile_options(-fsanitize=address -fsanitize=${KOMAL_UBSAN} -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=address -fsanitize=${KOMAL_UBSAN})
        add_compile_definitions(_GLIBCXX_ASSERTIONS)
    endif()
endif()

# ---------------------------------------------------------------- SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.20.tar.gz
    URL_HASH SHA256=f0d9ea12f9e41a17995fec29cfb8dbfe28df75282493b489d3a2a91f30e74fa7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300
    SYSTEM
    FIND_PACKAGE_ARGS 3.2 CONFIG)

# ---------------------------------------------------------------- Dear ImGui (docking branch)
# Must speak the same webgpu.h dialect as your wgpu-native version.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.5-docking.tar.gz
    URL_HASH SHA256=c816c20e8c75f3e15ae867350e79925502d1a6a85938bb1a73b8927e5f31f9cb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300)

# ---------------------------------------------------------------- abseil (hash containers for tiles)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(absl
    URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20250814.1.tar.gz
    URL_HASH SHA256=1692f77d1739bacf3f94337188b78583cf09bab7e420d2dc6c5605a4f86785a1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300
    SYSTEM
    FIND_PACKAGE_ARGS CONFIG)

# ---------------------------------------------------------------- stb (stb_image: image import)
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5.tar.gz
    URL_HASH SHA256=a6c31950b3bdf05eb92258768ba2a11bd44449490574b1aac67028452288c40d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300)

# ---------------------------------------------------------------- doctest (the tests)
# One header: its own CMake files aren't used (SOURCE_SUBDIR holds none).
FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
    URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300
    SOURCE_SUBDIR doctest)

# ---------------------------------------------------------------- toml++ (theme files)
# One header: its own CMake files aren't used (SOURCE_SUBDIR holds none).
FetchContent_Declare(tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300
    SOURCE_SUBDIR include)

# ---------------------------------------------------------------- zstd (tile compression)
# Raster tiles as compact binary (raster/tile-codec: the one file that knows it). The static
# library alone: no programs, no threads, no old formats.
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300
    SOURCE_SUBDIR build/cmake)

FetchContent_MakeAvailable(SDL3 imgui absl stb doctest tomlplusplus zstd)

# ---------------------------------------------------------------- wgpu-native
include(cmake/wgpu-native.cmake)   # defines target: wgpu_native
include(cmake/embed-file.cmake)    # defines: embed_file()

# ---------------------------------------------------------------- embedded assets
# Fonts and the window icon baked into the binary, so there is nothing to ship or locate at
# runtime: Manrope for the UI, JetBrains Mono for numbers you can type in (see ui/style/fonts.hpp).
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)
embed_file(${GENERATED_DIR}/font-ui.cpp      ${ASSETS_DIR}/fonts/Manrope-Regular.ttf     kFontUi)
embed_file(${GENERATED_DIR}/font-numbers.cpp ${ASSETS_DIR}/fonts/JetBrainsMono-Light.ttf kFontNumbers)
embed_file(${GENERATED_DIR}/theme-main-dark.cpp  ${ASSETS_DIR}/themes/main-dark.toml  kThemeMainDark)
embed_file(${GENERATED_DIR}/theme-main-light.cpp ${ASSETS_DIR}/themes/main-light.toml kThemeMainLight)
embed_file(${GENERATED_DIR}/app-icon.cpp     ${ASSETS_DIR}/icons/komal-icon-256.png      kAppIcon)
# WGSL compiled into the binary: no shader files to ship or find relative to the working dir.
embed_file(${GENERATED_DIR}/shader-texture.cpp ${CMAKE_SOURCE_DIR}/src/ui/panels/canvas/shaders/texture.wgsl kShaderTexture)
embed_file(${GENERATED_DIR}/shader-color.cpp   ${CMAKE_SOURCE_DIR}/src/ui/panels/canvas/shaders/color.wgsl   kShaderColor)
embed_file(${GENERATED_DIR}/shader-preview-signal.cpp ${CMAKE_SOURCE_DIR}/src/ui/panels/canvas/shaders/preview-signal.wgsl kShaderPreviewSignal)
embed_file(${GENERATED_DIR}/shader-preview-blit.cpp   ${CMAKE_SOURCE_DIR}/src/ui/panels/canvas/shaders/preview-blit.wgsl   kShaderPreviewBlit)
embed_file(${GENERATED_DIR}/shader-checker.cpp        ${CMAKE_SOURCE_DIR}/src/ui/panels/canvas/shaders/checker.wgsl        kShaderChecker)
embed_file(${GENERATED_DIR}/shader-preview-stroke.cpp ${CMAKE_SOURCE_DIR}/src/ui/panels/canvas/shaders/preview-stroke.wgsl kShaderPreviewStroke)
embed_file(${GENERATED_DIR}/shader-fullscreen.cpp     ${CMAKE_SOURCE_DIR}/src/ui/panels/canvas/shaders/fullscreen.wgsl     kShaderFullscreen)
set(GENERATED_SOURCES
    ${GENERATED_DIR}/font-ui.cpp
    ${GENERATED_DIR}/font-numbers.cpp
    ${GENERATED_DIR}/app-icon.cpp
    ${GENERATED_DIR}/theme-main-dark.cpp
    ${GENERATED_DIR}/theme-main-light.cpp
    ${GENERATED_DIR}/shader-texture.cpp
    ${GENERATED_DIR}/shader-color.cpp
    ${GENERATED_DIR}/shader-preview-signal.cpp
    ${GENERATED_DIR}/shader-preview-blit.cpp
    ${GENERATED_DIR}/shader-checker.cpp
    ${GENERATED_DIR}/shader-preview-stroke.cpp
    ${GENERATED_DIR}/shader-fullscreen.cpp)

# ---------------------------------------------------------------- imgui target
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU   # wgpu-native flavour
                                         IMGUI_DISABLE_DEMO_WINDOWS)      # no demo window: imgui_demo.cpp leaves stubs
target_link_libraries(imgui PUBLIC SDL3::SDL3 wgpu_native)

# ---------------------------------------------------------------- warnings
# On Komal's own code only (the libraries' headers are system headers). Clean at this level;
# KOMAL_WERROR keeps it that way.
function(komal_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${KOMAL_WERROR}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
            -Wimplicit-fallthrough -Wcast-align -Wconversion -Wsign-conversion -Wold-style-cast -Wextra-semi
            $<$<CXX_COMPILER_ID:GNU>:-Wduplicated-cond -Wlogical-op -Wuseless-cast>
            $<$<BOOL:${KOMAL_WERROR}>:-Werror>)
    endif()
endfunction()

# ---------------------------------------------------------------- the core
# The layers below the GPU and the UI (base, input, raster, node, command, document): the document and
# everything it's made of. The app and the tests both link it; it runs without a window or a GPU
# (no SDL, no ImGui, no wgpu: cmake/check-layers.cmake holds every layer to what it may include).
set(CORE_LAYERS base input raster node command document)
set(CORE_SOURCES)
foreach(layer ${CORE_LAYERS})
    file(GLOB_RECURSE layer_sources CONFIGURE_DEPENDS src/${layer}/*.cpp src/${layer}/*.hpp)
    list(APPEND CORE_SOURCES ${layer_sources})
endforeach()
list(FILTER CORE_SOURCES EXCLUDE REGEX "/src/input/sdl-input\\.")   # SDL's: the app's, never the core's
add_library(komal-core STATIC ${CORE_SOURCES})
target_include_directories(komal-core PUBLIC src)
target_include_directories(komal-core SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(komal-core PUBLIC absl::flat_hash_map absl::flat_hash_set absl::hash)
target_link_libraries(komal-core PRIVATE libzstd_static)   # raster/tile-codec only (check-layers)
target_include_directories(komal-core SYSTEM PRIVATE ${zstd_SOURCE_DIR}/lib)
komal_warnings(komal-core)

# ---------------------------------------------------------------- the app
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
list(FILTER APP_SOURCES EXCLUDE REGEX "/src/(base|input|raster|node|command|document)/")
list(APPEND APP_SOURCES ${CMAKE_SOURCE_DIR}/src/input/sdl-input.cpp ${CMAKE_SOURCE_DIR}/src/input/sdl-input.hpp)
add_executable(${PROJECT_NAME} ${APP_SOURCES} ${GENERATED_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${stb_SOURCE_DIR}    # the window icon
                                                          ${tomlplusplus_SOURCE_DIR}/include)   # theme files
target_link_libraries(${PROJECT_NAME} PRIVATE komal-core imgui SDL3::SDL3 wgpu_native
                                              absl::flat_hash_map absl::flat_hash_set absl::hash)
komal_warnings(${PROJECT_NAME})

# ---------------------------------------------------------------- the tests
# The core's blocks, without a window or a GPU. Built and run with every build: a failing test
# fails the build.
if(KOMAL_TESTS)
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp tests/*.hpp)
    add_executable(komal-tests ${TEST_SOURCES})
    target_include_directories(komal-tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
    target_link_libraries(komal-tests PRIVATE komal-core)
    komal_warnings(komal-tests)
    enable_testing()
    add_test(NAME komal-tests COMMAND komal-tests)
    add_custom_command(TARGET komal-tests POST_BUILD COMMAND komal-tests --minimal COMMENT "Running the tests" VERBATIM)

    # The UI's blocks that need no window or GPU: its look (theme files, palette, styles), and how
    # the graph lays a node out and where a drop on a stack lands.
    file(GLOB UI_TEST_SOURCES CONFIGURE_DEPENDS tests/ui/*.cpp tests/ui/*.hpp)
    add_executable(komal-ui-tests ${UI_TEST_SOURCES}
        src/ui/style/theme.cpp src/ui/style/palette.cpp src/ui/style/ui-style.cpp src/ui/style/graph-style.cpp
        src/ui/panels/graph/internal/node-item.cpp src/ui/panels/graph/internal/stack-drop.cpp
        ${GENERATED_DIR}/theme-main-dark.cpp ${GENERATED_DIR}/theme-main-light.cpp)
    target_include_directories(komal-ui-tests PRIVATE src)
    target_include_directories(komal-ui-tests SYSTEM PRIVATE ${doctest_SOURCE_DIR} ${tomlplusplus_SOURCE_DIR}/include)
    target_link_libraries(komal-ui-tests PRIVATE komal-core imgui)
    komal_warnings(komal-ui-tests)
    add_test(NAME komal-ui-tests COMMAND komal-ui-tests)
    add_custom_command(TARGET komal-ui-tests POST_BUILD COMMAND komal-ui-tests --minimal COMMENT "Running the UI tests" VERBATIM)
endif()

# The source folders are layers: includes only go down, and each layer includes only the libraries
# it may use (cmake/check-layers.cmake lists both). Checked before every build, so the structure
# can't quietly erode as the app grows.
add_custom_target(check-layers
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/src -DTESTS=${CMAKE_SOURCE_DIR}/tests -P ${CMAKE_SOURCE_DIR}/cmake/check-layers.cmake
    COMMENT "Checking that includes only go down the layers"
    VERBATIM)
add_dependencies(komal-core check-layers)
add_dependencies(${PROJECT_NAME} check-layers)
if(KOMAL_TESTS)
    add_dependencies(komal-tests check-layers)
    add_dependencies(komal-ui-tests check-layers)
endif()

# ---------------------------------------------------------------- app and document icons
# The sources are assets/icons/komal-icon.png (the app) and assets/icons/doc-icon.png (a .komal
# document); cmake/make-icons.py derives the files used here from them. At runtime the window gets
# assets/icons/komal-icon-256.png (embedded above) on every platform. A .komal file shows the
# document icon in the OS's file manager once the app is installed: the type is registered with it.
if(WIN32)
    # The .exe's own icon (Explorer, taskbar pins, Alt+Tab before the window exists), and the
    # document's: a .komal association (an installer's registry entry) points at "Komal.exe,1".
    file(WRITE ${GENERATED_DIR}/komal.rc
"IDI_APP_ICON ICON \"${ASSETS_DIR}/icons/komal-icon.ico\"
IDI_DOC_ICON ICON \"${ASSETS_DIR}/icons/doc-icon.ico\"
")
    target_sources(${PROJECT_NAME} PRIVATE ${GENERATED_DIR}/komal.rc)
elseif(APPLE)
    # An app bundle (Komal.app), so Finder and the Dock show the icon; its Info.plist declares the
    # .komal type (cmake/Info.plist.in), so Finder shows .komal files with the document icon.
    set(APP_ICNS ${ASSETS_DIR}/icons/komal-icon.icns)
    set(DOC_ICNS ${ASSETS_DIR}/icons/doc-icon.icns)
    set_source_files_properties(${APP_ICNS} ${DOC_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(${PROJECT_NAME} PRIVATE ${APP_ICNS} ${DOC_ICNS})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "Komal"
        MACOSX_BUNDLE_GUI_IDENTIFIER "komal"
        MACOSX_BUNDLE_ICON_FILE komal-icon.icns)
elseif(UNIX)
    # Desktop entry + icon, for launchers and docks (Wayland compositors match the running
    # window to komal.desktop through its app id, set in Application::init). The .komal type is
    # a MIME type (shared-mime-info: by its name, or its first bytes), with the document icon.
    # The icon has a name of its own, komal-document, not the type's default (application-x-komal):
    # KDE takes a theme's generic document icon (application-x-generic) for application-x-* names
    # the theme doesn't have, before it looks in hicolor, so the default name never shows.
    file(WRITE ${GENERATED_DIR}/komal.desktop
"[Desktop Entry]
Type=Application
Name=Komal
Exec=Komal %F
Icon=komal
Terminal=false
Categories=Graphics;2DGraphics;RasterGraphics;
MimeType=application/x-komal;image/png;image/jpeg;image/bmp;image/gif;image/x-tga;image/vnd.adobe.photoshop;
")
    file(WRITE ${GENERATED_DIR}/komal-mime.xml
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">
  <mime-type type=\"application/x-komal\">
    <comment>Komal project</comment>
    <icon name=\"komal-document\"/>
    <glob pattern=\"*.komal\"/>
    <magic priority=\"80\">
      <match type=\"string\" value=\"KOMAL\" offset=\"0\"/>
    </magic>
  </mime-type>
</mime-info>
")
    include(GNUInstallDirs)
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES $<TARGET_FILE:wgpu_native> DESTINATION ${CMAKE_INSTALL_BINDIR})   # found through $ORIGIN
    install(FILES ${GENERATED_DIR}/komal.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${GENERATED_DIR}/komal-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME komal.xml)
    install(FILES ${ASSETS_DIR}/icons/komal-icon-256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME komal.png)
    foreach(size 16 32 48 64 128 256 512)
        install(FILES ${ASSETS_DIR}/icons/doc-icon-${size}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/mimetypes
                RENAME komal-document.png)
    endforeach()
    # The type and its icon for this user alone, without installing the app (a build of it, for
    # development): cmake --build <dir> --target register-file-type (unregister-file-type undoes it).
    foreach(action register unregister)
        add_custom_target(${action}-file-type
            COMMAND ${CMAKE_COMMAND} -DACTION=${action} -DMIME_XML=${GENERATED_DIR}/komal-mime.xml -DICONS=${ASSETS_DIR}/icons
                    -P ${CMAKE_SOURCE_DIR}/cmake/register-file-type.cmake
            COMMENT "The .komal file type, for this user: ${action}" VERBATIM)
    endforeach()
    # The desktop's caches learn the type and its icon (skipped when staging a package: DESTDIR).
    install(CODE "if(NOT DEFINED ENV{DESTDIR})
        execute_process(COMMAND update-mime-database \"${CMAKE_INSTALL_FULL_DATADIR}/mime\" ERROR_QUIET)
        execute_process(COMMAND gtk-update-icon-cache -f -t \"${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor\" ERROR_QUIET)
        execute_process(COMMAND update-desktop-database \"${CMAKE_INSTALL_FULL_DATADIR}/applications\" ERROR_QUIET)
    endif()")
endif()

# The wgpu-native library is copied next to the executable (below) and found there.
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "@executable_path" INSTALL_RPATH "@executable_path")
elseif(UNIX)
    set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

if(MSVC)
    # Sources are UTF-8 (box drawings in comments); keep windows.h's min/max macros away.
    foreach(target komal-core ${PROJECT_NAME})
        target_compile_options(${target} PRIVATE /utf-8 /Zc:__cplusplus /bigobj)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    endforeach()
    if(KOMAL_TESTS)
        foreach(target komal-tests komal-ui-tests)
            target_compile_options(${target} PRIVATE /utf-8 /Zc:__cplusplus /bigobj)
            target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
        endforeach()
    endif()
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
endif()

if(MINGW)
    # no libgcc / libstdc++ / winpthread DLLs to ship besides wgpu_native.dll
    target_link_options(${PROJECT_NAME} PRIVATE -static)
endif()

# Drop wgpu_native.dll / libwgpu_native.dylib / libwgpu_native.so next to the exe
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:wgpu_native> $<TARGET_FILE_DIR:${PROJECT_NAME}>)
