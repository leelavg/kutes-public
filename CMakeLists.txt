# --- notes
# 1. Native host and target system
# 2. Windows build on linux using mingw (changes made to support x64 only)
# 3. MacOSX aarch64 build on linux via podman
# 4. MacOSX amd64 build on linux via podman

cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(
  kutes
  VERSION 0.0.1
  DESCRIPTION "A cross platform micro gui for kubectl."
  HOMEPAGE_URL "https://github.com/leelavg/kutes"
  LANGUAGES C CXX
)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CPM_SOURCE_CACHE vendor/)
file(GLOB PATCHES "support/patches/*.patch")
file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/support/CPM.cmake
  EXPECTED_HASH SHA256=c8cdc32c03816538ce22781ed72964dc864b2a34a310d3b7104812a5ca2d835d
)
include(${CMAKE_CURRENT_SOURCE_DIR}/support/CPM.cmake)
CPMAddPackage(
  NAME yyjson
  # commit is on Sep 30, 2024
  URL https://github.com/ibireme/yyjson/archive/c3af82b.tar.gz
  URL_HASH SHA256=a4ba556d0478a805d4e2d9163700afba699a986d3f1805422dc535cbe0b05e50
  DOWNLOAD_ONLY TRUE
  CUSTOM_CACHE_KEY cache
)
CPMAddPackage(
  NAME nappgui_src
  # commit is on Nov 28, 2024
  URL https://github.com/frang75/nappgui_src/archive/50887ca.tar.gz
  URL_HASH SHA256=07b439b4aac034ec0912be38f51c637eacb61203bbea106b1704228d6ce5a74c
  PATCHES ${PATCHES}
  DOWNLOAD_ONLY TRUE
  CUSTOM_CACHE_KEY cache
)

# For fedora:
# dnf install cmake gtk3-devel ninja-build
# not used - libcurl-devel webkit2gtk4.0-devel mesa-libEGL-devel mesa-libGL-devel mesa-libGLU-devel
# For windows & Mac: cmake ninja-build
if (nappgui_src_ADDED)
  set(NAPPGUI_ROOT_PATH ${nappgui_src_SOURCE_DIR})
  set(NAPPGUI_DEMO FALSE)
  set(NAPPGUI_WEB FALSE)
  set(CMAKE_DISABLE_CRTDBG TRUE)
  add_subdirectory(${NAPPGUI_ROOT_PATH} EXCLUDE_FROM_ALL)
  # to properly link nappgui
  include(${NAPPGUI_ROOT_PATH}/prj/NAppCompilers.cmake)
  # to correctly detect platform
  nap_config_compiler()
endif()

if (yyjson_ADDED)
  add_subdirectory(${yyjson_SOURCE_DIR} EXCLUDE_FROM_ALL)
endif()

add_subdirectory(src)
