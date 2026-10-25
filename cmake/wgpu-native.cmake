# Provides the imported target `wgpu_native`.
# 1) If third_party/wgpu-native/{include,lib} exists, that is used (vendored).
# 2) Otherwise the official prebuilt release is downloaded.
#
# Layout of the official zips:
#   include/webgpu/webgpu.h  include/webgpu/wgpu.h
#   lib/wgpu_native.dll + libwgpu_native.dll.a   (windows-x86_64-gnu, used for mingw)
#   lib/wgpu_native.dll + wgpu_native.dll.lib    (windows-x86_64-msvc)
#   lib/libwgpu_native.so                         (linux-x86_64)

set(WGPU_NATIVE_VERSION "v27.0.2.0" CACHE STRING "wgpu-native release tag")

if(WIN32)
    if(MSVC)
        set(_plat "windows-x86_64-msvc")
    else()
        set(_plat "windows-x86_64-gnu")
    endif()
elseif(UNIX AND NOT APPLE)
    set(_plat "linux-x86_64")
else()
    message(FATAL_ERROR "Platform not set up for wgpu-native yet")
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
else()
    # The Rust cdylib may have no SONAME; this makes CMake link with -lwgpu_native
    # so the NEEDED entry is just "libwgpu_native.so" (found via $ORIGIN rpath).
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "${WGPU_DIR}/lib/libwgpu_native.so"
        IMPORTED_NO_SONAME TRUE)
endif()
