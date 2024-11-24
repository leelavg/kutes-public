cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(kutes)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=c8cdc32c03816538ce22781ed72964dc864b2a34a310d3b7104812a5ca2d835d
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

CPMAddPackage(
  NAME yyjson
  # commit is on Sep 30, 2024
  URL https://github.com/ibireme/yyjson/archive/c3af82b.tar.gz
  URL_HASH SHA256=a4ba556d0478a805d4e2d9163700afba699a986d3f1805422dc535cbe0b05e50
  DOWNLOAD_ONLY TRUE
)
CPMAddPackage(
  NAME nappgui_src
  # commit is on Nov 6, 2024
  URL https://github.com/frang75/nappgui_src/archive/40356b8.tar.gz
  URL_HASH SHA256=8057d424f3c71545453bb4ddabda9be3c468f1c19d5972d17b10ec15bbf90fef
  PATCHES patches/nappgui_src.patch
  DOWNLOAD_ONLY TRUE
)

if (nappgui_src_ADDED)
  set(NAPPGUI_ROOT_PATH ${nappgui_src_SOURCE_DIR})
  set(NAPPGUI_DEMO FALSE)
  set(NAPPGUI_WEB FALSE)
  set(CMAKE_DISABLE_CRTDBG TRUE)
  add_subdirectory(${NAPPGUI_ROOT_PATH} EXCLUDE_FROM_ALL)
endif()

if (yyjson_ADDED)
  set(YYJSON_DISABLE_WRITER TRUE)
  add_subdirectory(${yyjson_SOURCE_DIR} EXCLUDE_FROM_ALL)
endif()

add_subdirectory(src)
