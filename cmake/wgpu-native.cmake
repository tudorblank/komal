# Provides the imported target `wgpu_native`.
# 1) If third_party/wgpu-native/{include,lib} exists, that is used (vendored).
# 2) Otherwise the official prebuilt release is downloaded.
#
# Layout of the official zips:
#   include/webgpu/webgpu.h  include/webgpu/wgpu.h
#   lib/wgpu_native.dll + libwgpu_native.dll.a   (windows-*-gnu, used for mingw)
#   lib/wgpu_native.dll + wgpu_native.dll.lib    (windows-*-msvc)
#   lib/libwgpu_native.dylib                      (macos-*, install name @rpath/libwgpu_native.dylib)
#   lib/libwgpu_native.so                         (linux-*)

set(WGPU_NATIVE_VERSION "v27.0.2.0" CACHE STRING "wgpu-native release tag")

# Target CPU: what the compiler builds for (a cross or Visual Studio -A build may differ from
# the host). macOS universal builds aren't supported by the prebuilt zips: pick one arch.
set(_cpu "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _narch)
    if(_narch GREATER 1)
        message(FATAL_ERROR "wgpu-native: set CMAKE_OSX_ARCHITECTURES to one of arm64 / x86_64")
    endif()
    set(_cpu "${CMAKE_OSX_ARCHITECTURES}")
elseif(MSVC AND CMAKE_GENERATOR_PLATFORM)
    set(_cpu "${CMAKE_GENERATOR_PLATFORM}")
elseif(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
    set(_cpu "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
endif()
string(TOLOWER "${_cpu}" _cpu)
if(_cpu MATCHES "^(arm64|aarch64)$")
    set(_arch "aarch64")
elseif(_cpu MATCHES "^(x86_64|amd64|x64)$")
    set(_arch "x86_64")
else()
    message(FATAL_ERROR "wgpu-native: unsupported CPU '${_cpu}'")
endif()

if(WIN32)
    if(MSVC)
        set(_plat "windows-${_arch}-msvc")
    elseif(_arch STREQUAL "x86_64")
        set(_plat "windows-x86_64-gnu")
    else()
        message(FATAL_ERROR "wgpu-native: Windows on ARM needs MSVC (there is no aarch64 mingw build)")
    endif()
elseif(APPLE)
    set(_plat "macos-${_arch}")
elseif(UNIX)
    set(_plat "linux-${_arch}")
else()
    message(FATAL_ERROR "wgpu-native: platform not set up")
endif()

# vendored copies can be per-platform: third_party/wgpu-native-<plat>, or plain third_party/wgpu-native
set(WGPU_DIR "")
foreach(_cand "${CMAKE_SOURCE_DIR}/third_party/wgpu-native-${_plat}" "${CMAKE_SOURCE_DIR}/third_party/wgpu-native")
    if(NOT WGPU_DIR AND EXISTS "${_cand}/include/webgpu/webgpu.h")
        set(WGPU_DIR "${_cand}")
    endif()
endforeach()

if(WGPU_DIR)
    message(STATUS "wgpu-native: using vendored copy in ${WGPU_DIR}")
else()
    FetchContent_Declare(wgpu_native_dl
        URL https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_NATIVE_VERSION}/wgpu-${_plat}-release.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        INACTIVITY_TIMEOUT 30
        TIMEOUT 300)
    FetchContent_MakeAvailable(wgpu_native_dl)
    set(WGPU_DIR "${wgpu_native_dl_SOURCE_DIR}")
endif()

add_library(wgpu_native SHARED IMPORTED GLOBAL)
set_target_properties(wgpu_native PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${WGPU_DIR}/include")

if(WIN32)
    set(_implib "")
    foreach(_c "${WGPU_DIR}/lib/libwgpu_native.dll.a" "${WGPU_DIR}/lib/wgpu_native.dll.lib")
        if(NOT _implib AND EXISTS "${_c}")
            set(_implib "${_c}")
        endif()
    endforeach()
    if(NOT _implib)
        message(FATAL_ERROR "No wgpu_native import library found in ${WGPU_DIR}/lib")
    endif()
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/wgpu_native.dll"
        IMPORTED_IMPLIB   "${_implib}")
elseif(APPLE)
    # Its install name is @rpath/libwgpu_native.dylib: found next to the executable (see the
    # app's INSTALL_RPATH / BUILD_RPATH).
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/libwgpu_native.dylib")
else()
    # The Rust cdylib may have no SONAME; this makes CMake link with -lwgpu_native
    # so the NEEDED entry is just "libwgpu_native.so" (found via $ORIGIN rpath).
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/libwgpu_native.so"
        IMPORTED_NO_SONAME TRUE)
endif()
