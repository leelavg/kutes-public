cmake_minimum_required(VERSION 3.20)
project(boron VERSION 2.96.0 LANGUAGES C)

# picked some of the files from
#  ./configure --no-checksum --no-compress --no-execute --no-random --no-readline --no-socket --static
list(
  APPEND
  src
  urlan/env.c
  urlan/array.c
  urlan/binary.c
  urlan/block.c
  urlan/coord.c
  urlan/date.c
  urlan/path.c
  urlan/string.c
  urlan/context.c
  urlan/gc.c
  urlan/serialize.c
  urlan/tokenize.c
  urlan/vector.c
  urlan/parse_block.c
  urlan/parse_string.c
  support/str.c
  support/mem_util.c
  support/quickSortIndex.c
  support/fpconv.c
  eval/boron.c
  urlan/hashmap.c
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(-DCONFIG_HASHMAP=1 -DCONFIG_ISOLATE=1)
add_library(boron ${src})
target_link_libraries(boron PUBLIC m)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/urlan/unset.h" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/include/")
target_include_directories(boron
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/urlan>
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/eval>
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/support>
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
