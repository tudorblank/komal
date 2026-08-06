include_guard(GLOBAL)

set(WGPU_NATIVE_VERSION "v29.0.1.1" CACHE STRING "wgpu-native release tag to fetch (see https://github.com/gfx-rs/wgpu-native/releases)")

set(WGPU_NATIVE_ROOT "${CMAKE_SOURCE_DIR}/external/wgpu-native")

# --- platform / abi ---
if(WIN32)
    set(_wgpu_platform "windows")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_wgpu_abi "gnu")   # MinGW toolchain (e.g. Qt's bundled MinGW)
    else()
        set(_wgpu_abi "msvc")
    endif()
elseif(APPLE)
    set(_wgpu_platform "macos")
elseif(UNIX)
    set(_wgpu_platform "linux")
else()
    message(FATAL_ERROR "FetchWgpuNative: unsupported platform")
endif()

# --- architecture ---
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
        set(_wgpu_arch "aarch64")
    else()
        set(_wgpu_arch "x86_64")
    endif()
else()
    message(FATAL_ERROR "FetchWgpuNative: only 64-bit targets are supported")
endif()

# --- asset name (matches gfx-rs/wgpu-native release naming) ---
if(_wgpu_platform STREQUAL "windows")
    set(_wgpu_asset "wgpu-windows-${_wgpu_arch}-${_wgpu_abi}-release.zip")
else()
    set(_wgpu_asset "wgpu-${_wgpu_platform}-${_wgpu_arch}-release.zip")
endif()

set(_wgpu_platform_dir "${WGPU_NATIVE_ROOT}/${_wgpu_platform}")
set(_wgpu_stamp_file "${_wgpu_platform_dir}/.fetched-${WGPU_NATIVE_VERSION}")

if(NOT EXISTS "${_wgpu_stamp_file}")
    set(_wgpu_url "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_NATIVE_VERSION}/${_wgpu_asset}")
    set(_wgpu_zip "${CMAKE_BINARY_DIR}/_wgpu_native_download/${_wgpu_asset}")

    message(STATUS "wgpu-native: fetching ${_wgpu_asset} (${WGPU_NATIVE_VERSION})")
    message(STATUS "wgpu-native: ${_wgpu_url}")

    file(DOWNLOAD "${_wgpu_url}" "${_wgpu_zip}"
        SHOW_PROGRESS
        STATUS _wgpu_dl_status
    )
    list(GET _wgpu_dl_status 0 _wgpu_dl_code)
    if(NOT _wgpu_dl_code EQUAL 0)
        list(GET _wgpu_dl_status 1 _wgpu_dl_msg)
        file(REMOVE "${_wgpu_zip}")
        message(FATAL_ERROR "wgpu-native: download failed (${_wgpu_dl_msg})\n  URL: ${_wgpu_url}")
    endif()

    file(REMOVE_RECURSE "${_wgpu_platform_dir}")
    file(MAKE_DIRECTORY "${_wgpu_platform_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_wgpu_zip}" DESTINATION "${_wgpu_platform_dir}")

    file(WRITE "${_wgpu_stamp_file}" "${_wgpu_asset}\n")
    message(STATUS "wgpu-native: staged in ${_wgpu_platform_dir}")
else()
    message(STATUS "wgpu-native: using cached ${_wgpu_platform_dir} (${WGPU_NATIVE_VERSION})")
endif()

# The release archive layout is: include/webgpu/{webgpu.h,wgpu.h}, lib/...
set(WGPU_NATIVE_INCLUDE_DIR "${_wgpu_platform_dir}/include/webgpu" CACHE PATH "wgpu-native headers" FORCE)
set(WGPU_NATIVE_LIB_DIR "${_wgpu_platform_dir}/lib" CACHE PATH "wgpu-native libraries" FORCE)

if(NOT EXISTS "${WGPU_NATIVE_INCLUDE_DIR}/webgpu.h")
    message(FATAL_ERROR "wgpu-native: expected header not found at ${WGPU_NATIVE_INCLUDE_DIR}/webgpu.h - archive layout may have changed")
endif()