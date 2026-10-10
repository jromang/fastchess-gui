# Cross-compile for 64-bit Windows from Linux with mingw-w64.
#   cmake -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
#   cmake --build build-win

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX x86_64-w64-mingw32)

# Debian and Ubuntu ship two thread models; pass -DMINGW_SUFFIX=-posix there to get std::thread.
if(NOT DEFINED MINGW_SUFFIX)
    set(MINGW_SUFFIX "")
endif()

set(CMAKE_C_COMPILER ${MINGW_PREFIX}-gcc${MINGW_SUFFIX})
set(CMAKE_CXX_COMPILER ${MINGW_PREFIX}-g++${MINGW_SUFFIX})
set(CMAKE_RC_COMPILER ${MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Prefer static archives so the .exe runs without mingw DLLs next to it.
set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
