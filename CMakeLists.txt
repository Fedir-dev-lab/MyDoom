cmake_minimum_required(VERSION 3.20)
project(MyDoomEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# --- SDL3 ---------------------------------------------------------------
# SDL_BUNDLED=ON  (default): SDL3 is downloaded and linked statically into the
#                 executable. Largest file (+~4 MB), but runs anywhere.
# SDL_BUNDLED=OFF: the executable uses the SDL3 already installed on the
#                 player's system (libSDL3.so). Smallest file; the player must
#                 have SDL3 (>= 3.2.0) installed.
option(SDL_BUNDLED "Link SDL3 statically into the executable" ON)

if(SDL_BUNDLED)
    include(FetchContent)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.0
        GIT_SHALLOW TRUE
    )
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
else()
    find_package(SDL3 3.2.0 REQUIRED)
endif()

# --- Sources --------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

add_executable(${PROJECT_NAME} ${ENGINE_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(SDL_BUNDLED AND TARGET SDL3::SDL3-static)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-static)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
endif()

# -march=native makes the binary run only on CPUs like the build machine.
# Keep it OFF for anything you distribute.
option(NATIVE_ARCH "Optimize for this CPU only (not portable)" OFF)

# DEV_MODE=ON compiles in the developer noclip/flight mode (F1 key in-game).
# Always OFF in release/distribution builds — set ON only during development.
option(DEV_MODE "Compile in developer noclip/flight mode" OFF)
if(DEV_MODE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEV_MODE)
    message(STATUS "DEV_MODE enabled — noclip/flight will be available (F1 in-game)")
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-O2>
)
if(NATIVE_ARCH)
    target_compile_options(${PROJECT_NAME} PRIVATE -march=native)
endif()

# ---------------------------------------------------------------------------
# Game pack (the "game file", like a .pk3/.wad).
# mdpack packs assets/ into one file: game.mdpk.  The engine extracts it
# into the user's cache dir at startup; nothing else touches assets/ at runtime.
# ---------------------------------------------------------------------------
add_executable(mdpack tools/mdpack.cpp src/game/GamePack.cpp)
target_include_directories(mdpack PRIVATE "${CMAKE_SOURCE_DIR}/src")

file(GLOB_RECURSE GAME_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/*")
set(GAME_PACK "${CMAKE_BINARY_DIR}/game.mdpk")
if(CMAKE_CROSSCOMPILING)
    # mdpack.exe cannot run on the build machine: reuse a pack from a native build.
    set(PREBUILT_GAME_PACK "" CACHE FILEPATH "game.mdpk from the native build")
    add_custom_command(
        OUTPUT "${GAME_PACK}"
        COMMAND ${CMAKE_COMMAND} -E copy "${PREBUILT_GAME_PACK}" "${GAME_PACK}"
        DEPENDS ${GAME_ASSET_FILES}
        COMMENT "Using prebuilt game.mdpk"
    )
else()
    add_custom_command(
        OUTPUT "${GAME_PACK}"
        COMMAND mdpack "${CMAKE_SOURCE_DIR}/assets" "${GAME_PACK}"
        DEPENDS mdpack ${GAME_ASSET_FILES}
        COMMENT "Packing game data -> game.mdpk"
    )
endif()
add_custom_target(game_pack ALL DEPENDS "${GAME_PACK}")
add_dependencies(${PROJECT_NAME} game_pack)

# --- Test suite -----------------------------------------------------------
add_executable(test_frustum tests/test_frustum.cpp)
target_include_directories(test_frustum PRIVATE "${CMAKE_SOURCE_DIR}/src")

add_executable(test_lod
    tests/test_lod.cpp
    src/renderer/Texture.cpp
    src/renderer/Framebuffer.cpp
    src/renderer/MeshRenderer.cpp
)
target_include_directories(test_lod PRIVATE "${CMAKE_SOURCE_DIR}/src")

add_executable(test_audio_formats
    tests/test_audio_formats.cpp
    src/audio/SoundtrackConverter.cpp
)
target_include_directories(test_audio_formats PRIVATE "${CMAKE_SOURCE_DIR}/src")

# --- Release packaging ----------------------------------------------------
# cmake --install build/release --prefix dist
# gives:  dist/MyDoomEngine   (the engine)
#         dist/game.mdpk      (the game: one file the engine unpacks itself)
option(INSTALL_LOOSE_ASSETS "Also install assets/ as a plain folder" OFF)
install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(FILES "${GAME_PACK}" DESTINATION .)
if(INSTALL_LOOSE_ASSETS)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets" DESTINATION .)
endif()
