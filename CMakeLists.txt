cmake_minimum_required(VERSION 3.16)
project(Komal VERSION 0.1 LANGUAGES CXX)

if(MINGW)
    set(CMAKE_RC_FLAGS "-c 65001")
endif()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Widgets only now — OpenGL/OpenGLWidgets dropped
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
    src/main.cpp
    src/komal.cpp
    src/komal.h
    src/canvas.cpp
    src/canvas.h
    src/gfxdevice.cpp
    src/gfxdevice.h
    src/raster.cpp
    src/raster.h
)

qt_add_executable(Komal
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_link_libraries(Komal PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)

# --- wgpu-native ---
set(WGPU_NATIVE_DIR "${CMAKE_SOURCE_DIR}/include/webgpu" CACHE PATH "Path to wgpu-native headers/libs" FORCE)

target_include_directories(Komal PRIVATE
    "${WGPU_NATIVE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_directories(Komal PRIVATE
    "${WGPU_NATIVE_DIR}/lib"
)

target_link_libraries(Komal PRIVATE
    wgpu_native
)

if(WIN32)
    # transitive system libs wgpu-native needs on Windows (D3D12/surface creation)
    target_link_libraries(Komal PRIVATE
        d3dcompiler dxgi d3d12 ws2_32 userenv bcrypt ntdll
    )

    # wgpu_native.dll must sit next to the exe at runtime
    add_custom_command(TARGET Komal POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WGPU_NATIVE_DIR}/lib/wgpu_native.dll"
            "$<TARGET_FILE_DIR:Komal>"
        COMMENT "Copying wgpu_native.dll..."
    )
endif()

qt_finalize_executable(Komal)

add_custom_command(TARGET Komal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:Komal>/shaders"
    COMMENT "Copying shaders..."
)

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS
        "$ENV{Qt6_DIR}/bin"
        "$ENV{QTDIR}/bin"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Komal POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                "$<TARGET_FILE:Komal>"
                --no-translations
            COMMENT "Running windeployqt..."
        )
    endif()
endif()