# Cross-compile for 64-bit Windows from Linux with mingw-w64.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_triple x86_64-w64-mingw32)

# prefer the "posix" thread model (needed for std::thread/mutex)
find_program(CMAKE_C_COMPILER   NAMES ${_triple}-gcc-posix ${_triple}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${_triple}-g++-posix ${_triple}-g++ REQUIRED)
find_program(CMAKE_RC_COMPILER  NAMES ${_triple}-windres REQUIRED)

set(CMAKE_FIND_ROOT_PATH /usr/${_triple})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
