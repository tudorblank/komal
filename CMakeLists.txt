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
    main.cpp
    komal.cpp
    komal.h
    shader.cpp
    canvas.cpp
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

if (WIN32)
    target_link_libraries(Komal PRIVATE opengl32)
endif()

qt_finalize_executable(Komal)

if(MINGW)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/icon.o
        COMMAND windres --preprocessor=C:/msys64/usr/bin/cat.exe ${CMAKE_SOURCE_DIR}/icon.rc -O coff -o ${CMAKE_BINARY_DIR}/icon.o
        DEPENDS ${CMAKE_SOURCE_DIR}/icon.rc
    )
    target_sources(Komal PRIVATE ${CMAKE_BINARY_DIR}/icon.o)
endif()

add_custom_command(TARGET Komal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:Komal>/shaders"
)