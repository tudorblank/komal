cmake_minimum_required(VERSION 3.24)
project(Komal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # standard C++ only: MSVC has no GNU extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
# Uses an installed SDL3 (pacman -S sdl3 / apt install libsdl3-dev) if found,
# otherwise downloads and builds it.
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.20.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300
    FIND_PACKAGE_ARGS 3.2 CONFIG)

# ---------------------------------------------------------------- Dear ImGui (docking branch)
# Must speak the same webgpu.h dialect as your wgpu-native version.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.5-docking.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300)

# ---------------------------------------------------------------- abseil (hash containers for tiles)
# Uses an installed abseil (pacman -S abseil-cpp) if found, otherwise downloads and builds it.
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(absl
    URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20250814.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300
    FIND_PACKAGE_ARGS CONFIG)

# ---------------------------------------------------------------- stb (stb_image: image import)
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300)

# ---------------------------------------------------------------- doctest (the tests)
# One header: its own CMake files aren't used (SOURCE_SUBDIR holds none).
FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    INACTIVITY_TIMEOUT 30
    TIMEOUT 300
    SOURCE_SUBDIR doctest)

FetchContent_MakeAvailable(SDL3 imgui absl stb doctest)

# ---------------------------------------------------------------- wgpu-native
include(cmake/wgpu-native.cmake)   # defines target: wgpu_native
include(cmake/embed-file.cmake)    # defines: embed_file()

# ---------------------------------------------------------------- embedded assets
# Fonts and the window icon baked into the binary, so there is nothing to ship or locate at
# runtime: Manrope for the UI, JetBrains Mono for numbers you can type in (see theme.h).
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)
embed_file(${GENERATED_DIR}/font-ui.cpp      ${ASSETS_DIR}/Manrope-Regular.ttf     kFontUi)
embed_file(${GENERATED_DIR}/font-numbers.cpp ${ASSETS_DIR}/JetBrainsMono-Light.ttf kFontNumbers)
embed_file(${GENERATED_DIR}/app-icon.cpp     ${ASSETS_DIR}/komal-icon-256.png      kAppIcon)
# WGSL compiled into the binary: no shader files to ship or find relative to the working dir.
embed_file(${GENERATED_DIR}/shader-texture.cpp ${CMAKE_SOURCE_DIR}/src/canvas/shaders/texture.wgsl kShaderTexture)
embed_file(${GENERATED_DIR}/shader-color.cpp   ${CMAKE_SOURCE_DIR}/src/canvas/shaders/color.wgsl   kShaderColor)
embed_file(${GENERATED_DIR}/shader-preview-signal.cpp ${CMAKE_SOURCE_DIR}/src/canvas/shaders/preview-signal.wgsl kShaderPreviewSignal)
embed_file(${GENERATED_DIR}/shader-preview-blit.cpp   ${CMAKE_SOURCE_DIR}/src/canvas/shaders/preview-blit.wgsl   kShaderPreviewBlit)
embed_file(${GENERATED_DIR}/shader-checker.cpp        ${CMAKE_SOURCE_DIR}/src/canvas/shaders/checker.wgsl        kShaderChecker)
embed_file(${GENERATED_DIR}/shader-preview-stroke.cpp ${CMAKE_SOURCE_DIR}/src/canvas/shaders/preview-stroke.wgsl kShaderPreviewStroke)
set(GENERATED_SOURCES
    ${GENERATED_DIR}/font-ui.cpp
    ${GENERATED_DIR}/font-numbers.cpp
    ${GENERATED_DIR}/app-icon.cpp
    ${GENERATED_DIR}/shader-texture.cpp
    ${GENERATED_DIR}/shader-color.cpp
    ${GENERATED_DIR}/shader-preview-signal.cpp
    ${GENERATED_DIR}/shader-preview-blit.cpp
    ${GENERATED_DIR}/shader-checker.cpp
    ${GENERATED_DIR}/shader-preview-stroke.cpp)

# ---------------------------------------------------------------- imgui target
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)  # wgpu-native flavour
target_link_libraries(imgui PUBLIC SDL3::SDL3 wgpu_native)

# ---------------------------------------------------------------- the core
# The layers below the GPU and the UI (base, raster, node, command, document): the document and
# everything it's made of. The app and the tests both link it; it runs without a window or a GPU.
set(CORE_LAYERS base raster node command document)
set(CORE_SOURCES)
foreach(layer ${CORE_LAYERS})
    file(GLOB_RECURSE layer_sources CONFIGURE_DEPENDS src/${layer}/*.cpp src/${layer}/*.hpp)
    list(APPEND CORE_SOURCES ${layer_sources})
endforeach()
add_library(komal-core STATIC ${CORE_SOURCES})
target_include_directories(komal-core PUBLIC src)
target_include_directories(komal-core SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(komal-core PUBLIC SDL3::SDL3 absl::flat_hash_map absl::flat_hash_set absl::hash)

# ---------------------------------------------------------------- the app
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
list(FILTER APP_SOURCES EXCLUDE REGEX "/src/(base|raster|node|command|document)/")
add_executable(${PROJECT_NAME} ${APP_SOURCES} ${GENERATED_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${stb_SOURCE_DIR})   # the window icon
target_link_libraries(${PROJECT_NAME} PRIVATE komal-core imgui SDL3::SDL3 wgpu_native
                                              absl::flat_hash_map absl::flat_hash_set absl::hash)

# ---------------------------------------------------------------- the tests
# Built and run with every build: a failing test fails the build. The canvas's CPU-only blocks
# (levels of detail) are compiled in from their sources.
option(KOMAL_TESTS "Build and run the tests with every build" ON)
if(KOMAL_TESTS)
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp tests/*.hpp)
    add_executable(komal-tests ${TEST_SOURCES} src/canvas/tiles/downsample.cpp)
    target_include_directories(komal-tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
    target_link_libraries(komal-tests PRIVATE komal-core)
    enable_testing()
    add_test(NAME komal-tests COMMAND komal-tests)
    add_custom_command(TARGET komal-tests POST_BUILD COMMAND komal-tests --minimal COMMENT "Running the tests" VERBATIM)
endif()

# The source folders are layers: includes only go down (cmake/check-layers.py lists them). Checked
# before every build, so the structure can't quietly erode as the app grows.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(check-layers
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/check-layers.py ${CMAKE_SOURCE_DIR}/src
        COMMENT "Checking that includes only go down the layers"
        VERBATIM)
    add_dependencies(komal-core check-layers)
    add_dependencies(${PROJECT_NAME} check-layers)
endif()

# ---------------------------------------------------------------- app icon
# The source is assets/komal-icon.png; cmake/make-icons.py derives the files used here from it.
# At runtime the window gets assets/komal-icon-256.png (embedded above) on every platform.
if(WIN32)
    # The .exe's own icon (Explorer, taskbar pins, Alt+Tab before the window exists).
    file(WRITE ${GENERATED_DIR}/komal.rc "IDI_APP_ICON ICON \"${ASSETS_DIR}/komal-icon.ico\"\n")
    target_sources(${PROJECT_NAME} PRIVATE ${GENERATED_DIR}/komal.rc)
elseif(APPLE)
    # An app bundle (Komal.app), so Finder and the Dock show the icon.
    set(APP_ICNS ${ASSETS_DIR}/komal-icon.icns)
    set_source_files_properties(${APP_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(${PROJECT_NAME} PRIVATE ${APP_ICNS})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Komal"
        MACOSX_BUNDLE_GUI_IDENTIFIER "komal"
        MACOSX_BUNDLE_ICON_FILE komal-icon.icns)
elseif(UNIX)
    # Desktop entry + icon, for launchers and docks (Wayland compositors match the running
    # window to komal.desktop through its app id, set in Application::init).
    file(WRITE ${GENERATED_DIR}/komal.desktop
"[Desktop Entry]
Type=Application
Name=Komal
Exec=Komal %F
Icon=komal
Terminal=false
Categories=Graphics;2DGraphics;RasterGraphics;
MimeType=image/png;image/jpeg;image/bmp;image/gif;image/x-tga;image/vnd.adobe.photoshop;
")
    include(GNUInstallDirs)
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES $<TARGET_FILE:wgpu_native> DESTINATION ${CMAKE_INSTALL_BINDIR})   # found through $ORIGIN
    install(FILES ${GENERATED_DIR}/komal.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${ASSETS_DIR}/komal-icon-256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME komal.png)
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
        target_compile_options(komal-tests PRIVATE /utf-8 /Zc:__cplusplus /bigobj)
        target_compile_definitions(komal-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
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
