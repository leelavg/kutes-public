# ghcr.io/shepherdjerred/macos-cross-compiler:latest@sha256:a4ced303153cbbef65c7971fb742bad8423d33bc3ead276f11367b8e4ad580a2
# install cmake, ninja-build, patch in container
# ln -sr /sdk /MacOSX13.1.sdk (referred from /sdk/SDKSettings.json)
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_C_COMPILER aarch64-apple-darwin22-clang)
set(CMAKE_CXX_COMPILER aarch64-apple-darwin22-clang++)
set(CMAKE_ARCHITECTURE arm64)
set(CMAKE_FIND_ROOT_PATH /sdk)
set(CMAKE_OSX_SYSROOT /MacOSX13.1.sdk)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# usually <build-linux64>/tools/build/Debug/bin/nrc
set(NATIVE_NRC_PATH "" CACHE STRING "NRC path executable on host machine")
