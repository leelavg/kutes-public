set(CMAKE_SYSTEM_NAME Windows)
# dnf install mingw64-gcc
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
# dnf install mingw64-g++
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# usually <build-linux64>/tools/build/Debug/bin/nrc
set(NATIVE_NRC_PATH "" CACHE STRING "NRC path executable on host machine")
add_link_options(-static -static-libgcc -static-libstdc++)
