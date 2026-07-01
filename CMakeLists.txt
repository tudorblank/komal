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

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)

set(PROJECT_SOURCES
    src/main.cpp
    src/komal.cpp
    src/komal.h
    src/canvas.cpp
    src/canvas.h
    src/gldevice.cpp
    src/gldevice.h
    src/raster.cpp
    src/raster.h
    src/shader.cpp
    src/shader.h
)

qt_add_executable(Komal
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_link_libraries(Komal PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::OpenGL
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
)

if(WIN32)
    target_link_libraries(Komal PRIVATE opengl32)
endif()

qt_finalize_executable(Komal)

# Windows icon via windres — finds windres on PATH, no hardcoded paths
if(MINGW)
    find_program(WINDRES_EXECUTABLE windres REQUIRED)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/icon.o
        COMMAND ${WINDRES_EXECUTABLE} ${CMAKE_SOURCE_DIR}/icon.rc -O coff -o ${CMAKE_BINARY_DIR}/icon.o
        DEPENDS ${CMAKE_SOURCE_DIR}/icon.rc
    )
    target_sources(Komal PRIVATE ${CMAKE_BINARY_DIR}/icon.o)
endif()

# Copy shaders next to the binary
add_custom_command(TARGET Komal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:Komal>/shaders"
    COMMENT "Copying shaders..."
)

# windeployqt — runs automatically as part of the build
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