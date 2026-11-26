cmake_minimum_required(VERSION 3.20)

project(my_app)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/*.cpp"
)

add_library(main SHARED
    ${ENGINE_SOURCES}
)

target_include_directories(main PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src"
)

target_compile_features(main PRIVATE cxx_std_20)

target_link_libraries(main PRIVATE SDL3::SDL3)
