# --- notes
# 1. Native host and target system
# 2. Windows build on linux using mingw (changes made to support x64 only)
# 3. MacOSX aarch64 build on linux via podman
# 4. MacOSX amd64 build on linux via podman

cmake_minimum_required(VERSION 3.22.0 FATAL_ERROR)
project(
  kutes
  VERSION 0.0.1
  DESCRIPTION "A cross platform micro gui for kubectl."
  HOMEPAGE_URL "https://github.com/leelavg/kutes"
  LANGUAGES C CXX
)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CPM_SOURCE_CACHE vendor/)
file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.5/CPM.cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/support/CPM.cmake
  EXPECTED_HASH SHA256=c46b876ae3b9f994b4f05a4c15553e0485636862064f1fcc9d8b4f832086bc5d
)
include(${CMAKE_CURRENT_SOURCE_DIR}/support/CPM.cmake)
CPMAddPackage(
  NAME yyjson
  # commit is on Jan 25, 2025
  URL https://github.com/ibireme/yyjson/archive/61c03f6.tar.gz
  URL_HASH SHA256=dd026202f927b1275f291cd8fedd02015588953e2642f43bd4eebf43f3951edd
  DOWNLOAD_ONLY TRUE
  CUSTOM_CACHE_KEY cache
)
# files ending and meaning
# - not used anymore
# + individual patches
# *-checkpoint-<commit>-patch clubbing of all individual patches which can be applied directly on that src commit
file(GLOB NAP_PATCHES "support/patches/nap-*.patch")
CPMAddPackage(
  NAME nappgui_src
  # commit is on Feb 9, 2025
  URL https://github.com/frang75/nappgui_src/archive/8639d19.tar.gz
  URL_HASH SHA256=c6aef21a5dedacb737a9579b23337eeaf5a6d24b953fb06fb69220697ba93eb1
  PATCHES ${NAP_PATCHES}
  DOWNLOAD_ONLY TRUE
  CUSTOM_CACHE_KEY cache
)
file(GLOB BRN_PATCHES "support/patches/brn-*.patch")
CPMAddPackage(
  NAME boron
  # commit is on Jan 23, 2025
  URL https://codeberg.org/wickedsmoke/boron/archive/64ca99397d.tar.gz
  URL_HASH SHA256=f01d6b94154a9613d2bac1e0562b9c2b78adf8320bf36789fa4200cc48cbc351
  PATCHES ${BRN_PATCHES}
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

if (boron_ADDED)
  # TODO: dynamic linking, deferring as it is harder
  # using SYSTEM here to suppress warnings from boron
  add_subdirectory(${boron_SOURCE_DIR} EXCLUDE_FROM_ALL SYSTEM)
endif()

add_subdirectory(src)
