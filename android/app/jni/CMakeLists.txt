cmake_minimum_required(VERSION 3.20)

project(GAME)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL3 source in build/_deps/sdl3-src or build/debug/_deps/sdl3-src
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/_deps/sdl3-src/CMakeLists.txt")
    set(SDL3_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/_deps/sdl3-src")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../build-android/_deps/sdl3-src/CMakeLists.txt")
    set(SDL3_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build-android/_deps/sdl3-src")
else()
    set(SDL3_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/debug/_deps/sdl3-src")
endif()

add_subdirectory("${SDL3_SRC_DIR}" SDL)

# Your game and its CMakeLists.txt are in a subfolder named "src"
add_subdirectory(src)
