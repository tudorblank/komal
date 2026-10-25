cmake_minimum_required(VERSION 3.24)
project(MyApp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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

FetchContent_MakeAvailable(SDL3 imgui absl stb)

# ---------------------------------------------------------------- wgpu-native
include(cmake/wgpu-native.cmake)   # defines target: wgpu_native
include(cmake/embed-file.cmake)    # defines: embed_file()

# ---------------------------------------------------------------- embedded assets
# UI font baked into the binary, so there is nothing to ship or locate at runtime.
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
embed_file(${GENERATED_DIR}/font-ui.cpp ${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf kFontUi)
# WGSL compiled into the binary: no shader files to ship or find relative to the working dir.
embed_file(${GENERATED_DIR}/shader-texture.cpp ${CMAKE_SOURCE_DIR}/src/gfx/shaders/texture.wgsl kShaderTexture)
embed_file(${GENERATED_DIR}/shader-color.cpp   ${CMAKE_SOURCE_DIR}/src/gfx/shaders/color.wgsl   kShaderColor)
embed_file(${GENERATED_DIR}/shader-preview-signal.cpp ${CMAKE_SOURCE_DIR}/src/gfx/shaders/preview-signal.wgsl kShaderPreviewSignal)
embed_file(${GENERATED_DIR}/shader-preview-blit.cpp   ${CMAKE_SOURCE_DIR}/src/gfx/shaders/preview-blit.wgsl   kShaderPreviewBlit)
set(GENERATED_SOURCES
    ${GENERATED_DIR}/font-ui.cpp
    ${GENERATED_DIR}/shader-texture.cpp
    ${GENERATED_DIR}/shader-color.cpp
    ${GENERATED_DIR}/shader-preview-signal.cpp
    ${GENERATED_DIR}/shader-preview-blit.cpp)

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

# ---------------------------------------------------------------- the app
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(${PROJECT_NAME} ${APP_SOURCES} ${GENERATED_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE imgui SDL3::SDL3 wgpu_native
                                              absl::flat_hash_map absl::flat_hash_set absl::hash)

if(UNIX AND NOT APPLE)
    # find libwgpu_native.so next to the executable
    set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

if(MINGW)
    # no libgcc / libstdc++ / winpthread DLLs to ship besides wgpu_native.dll
    target_link_options(${PROJECT_NAME} PRIVATE -static)
endif()

# Drop wgpu_native.dll / libwgpu_native.so next to the exe
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:wgpu_native> $<TARGET_FILE_DIR:${PROJECT_NAME}>)
