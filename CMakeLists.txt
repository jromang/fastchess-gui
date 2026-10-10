cmake_minimum_required(VERSION 3.24)

project(fastchess_gui VERSION 0.1.0 LANGUAGES C CXX)

option(FCG_BUILD_GUI "Build the fastchess-gui desktop application" ON)
option(FCG_BUILD_TESTS "Build the fastchess-gui unit tests" ON)
option(FCG_STATIC_RUNTIME "Link libstdc++ and libgcc statically on Linux (release binaries)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(FCG_STATIC_RUNTIME AND NOT WIN32)
    add_link_options(-static-libstdc++ -static-libgcc)
endif()

# Small release binaries: drop unused functions and strip symbols.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections -s)
endif()

# ---------------------------------------------------------------------------
# fastchess core (everything from app/Makefile except main.cpp)
# ---------------------------------------------------------------------------

set(FC_APP ${CMAKE_CURRENT_SOURCE_DIR}/app)

set(FC_CORE_SOURCES
    ${FC_APP}/src/cli/cli.cpp
    ${FC_APP}/src/cli/sanitize.cpp
    ${FC_APP}/src/core/globals/globals.cpp
    ${FC_APP}/src/core/logger/logger.cpp
    ${FC_APP}/src/core/printing/printing.cpp
    ${FC_APP}/src/core/time/time.cpp
    ${FC_APP}/src/engine/uci_engine.cpp
    ${FC_APP}/src/game/book/epd_reader.cpp
    ${FC_APP}/src/game/book/opening_book.cpp
    ${FC_APP}/src/game/book/pgn_reader.cpp
    ${FC_APP}/src/game/pgn/pgn_builder.cpp
    ${FC_APP}/src/game/pgn/openings_data.cpp
    ${FC_APP}/src/game/timecontrol/timecontrol.cpp
    ${FC_APP}/src/matchmaking/syzygy.cpp
    ${FC_APP}/src/matchmaking/elo/elo_pentanomial.cpp
    ${FC_APP}/src/matchmaking/elo/elo_wdl.cpp
    ${FC_APP}/src/matchmaking/match/match.cpp
    ${FC_APP}/src/matchmaking/sprt/sprt.cpp
    ${FC_APP}/src/matchmaking/tournament/base/tournament.cpp
    ${FC_APP}/src/matchmaking/tournament/roundrobin/roundrobin.cpp
    ${FC_APP}/src/matchmaking/tournament/tournament_manager.cpp
    ${FC_APP}/third_party/pyrrhic/tbprobe.cpp
    ${FC_APP}/third_party/fmt/src/format.cc
)

add_library(fastchess_core STATIC ${FC_CORE_SOURCES})
set_target_properties(fastchess_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(fastchess_core PUBLIC
    ${FC_APP}/src
    ${FC_APP}/third_party
    ${FC_APP}/third_party/fmt/include
)
target_compile_options(fastchess_core PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(fastchess_core PUBLIC Threads::Threads)

if(NOT WIN32)
    # Same probes as app/configuration/spawn-check.mk
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles([[
        #include <spawn.h>
        int main() { posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
                     return posix_spawn_file_actions_addchdir(&fa, "/"); }]] FC_HAVE_ADDCHDIR)
    check_cxx_source_compiles([[
        #include <spawn.h>
        int main() { posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
                     return posix_spawn_file_actions_addchdir_np(&fa, "/"); }]] FC_HAVE_ADDCHDIR_NP)
    if(FC_HAVE_ADDCHDIR)
        target_compile_definitions(fastchess_core PUBLIC HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
    elseif(FC_HAVE_ADDCHDIR_NP)
        target_compile_definitions(fastchess_core PUBLIC HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
    endif()
endif()

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE FC_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(FC_GIT_SHA)
        target_compile_definitions(fastchess_core PRIVATE GIT_SHA="${FC_GIT_SHA}")
    endif()
endif()

# The upstream command line tool, built from the same core library.
add_executable(fastchess ${FC_APP}/src/main.cpp)
target_link_libraries(fastchess PRIVATE fastchess_core)
set_target_properties(fastchess PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(WIN32)
    target_link_options(fastchess PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------

if(FCG_BUILD_GUI)
    include(FetchContent)

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    # Only windows, OpenGL, events and dialogs are used.
    foreach(sdl_off SDL_AUDIO SDL_GPU SDL_RENDER SDL_CAMERA SDL_HAPTIC SDL_SENSOR SDL_POWER SDL_VULKAN SDL_HIDAPI)
        set(${sdl_off} OFF CACHE BOOL "" FORCE)
    endforeach()

    FetchContent_Declare(sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9-docking
        SOURCE_SUBDIR fcg-no-cmake
        GIT_SHALLOW TRUE)
    FetchContent_Declare(implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG v1.0
        SOURCE_SUBDIR fcg-no-cmake
        GIT_SHALLOW TRUE)
    FetchContent_Declare(nanosvg
        GIT_REPOSITORY https://github.com/memononen/nanosvg.git
        GIT_TAG 239e102ec2c691f2902e20ace2ed36ee4a35cfe6
        SOURCE_SUBDIR fcg-no-cmake)
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
        SOURCE_SUBDIR fcg-no-cmake)

    # SOURCE_SUBDIR points nowhere for the header/source-only dependencies,
    # so MakeAvailable only downloads them.
    FetchContent_MakeAvailable(sdl3 imgui implot nanosvg stb)

    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
    )
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${implot_SOURCE_DIR}
    )
    set_target_properties(imgui_lib PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    # ImPlot instantiates every plot function for every numeric type unless told otherwise.
    target_compile_definitions(imgui_lib PUBLIC "IMPLOT_CUSTOM_NUMERIC_TYPES=(float)(double)" IMGUI_DISABLE_DEMO_WINDOWS)
    target_link_libraries(imgui_lib PUBLIC SDL3::SDL3-static)
    if(WIN32)
        target_link_libraries(imgui_lib PUBLIC opengl32)
    endif()

    # Piece artwork, embedded as string literals.
    set(FCG_ASSET_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/piece_svgs.cpp)
    file(GLOB FCG_PIECE_SVGS ${CMAKE_CURRENT_SOURCE_DIR}/gui/assets/pieces/*.svg)
    add_custom_command(
        OUTPUT ${FCG_ASSET_CPP}
        COMMAND ${CMAKE_COMMAND} -DOUT=${FCG_ASSET_CPP}
                -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/gui/assets/pieces
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_pieces.cmake
        DEPENDS ${FCG_PIECE_SVGS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_pieces.cmake
        COMMENT "Embedding piece SVGs")

    # Fallback UI font for systems without the usual ones (DroidSans, Apache 2.0, shipped with Dear ImGui).
    set(FCG_FONT_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/fallback_font.cpp)
    add_custom_command(
        OUTPUT ${FCG_FONT_CPP}
        COMMAND ${CMAKE_COMMAND} -DIN=${imgui_SOURCE_DIR}/misc/fonts/DroidSans.ttf -DOUT=${FCG_FONT_CPP}
                -DSYMBOL=fallback_font -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
        DEPENDS ${imgui_SOURCE_DIR}/misc/fonts/DroidSans.ttf ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
        COMMENT "Embedding the fallback font")

    file(GLOB FCG_GUI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/src/*.cpp)
    list(REMOVE_ITEM FCG_GUI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gui/src/main.cpp)

    # Everything except main(), so the unit tests can link it too.
    add_library(fcg_lib STATIC ${FCG_GUI_SOURCES} ${FCG_ASSET_CPP} ${FCG_FONT_CPP})
    target_compile_features(fcg_lib PUBLIC cxx_std_20)
    target_include_directories(fcg_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/src
        ${nanosvg_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
    )
    target_compile_options(fcg_lib PRIVATE -Wall -Wextra)
    target_compile_definitions(fcg_lib PUBLIC FCG_VERSION="${PROJECT_VERSION}")
    target_link_libraries(fcg_lib PUBLIC fastchess_core imgui_lib)

    set(FCG_WIN_RESOURCES "")
    if(WIN32)
        enable_language(RC)
        set(FCG_WIN_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gui/win/fastchess-gui.rc)
    endif()

    add_executable(fastchess-gui WIN32 ${CMAKE_CURRENT_SOURCE_DIR}/gui/src/main.cpp ${FCG_WIN_RESOURCES})
    target_link_libraries(fastchess-gui PRIVATE fcg_lib)
    if(WIN32)
        target_link_options(fastchess-gui PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    if(FCG_BUILD_TESTS)
        file(GLOB FCG_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/tests/*.cpp)
        # fastchess' own mock engine, so the integration test plays real (random) games.
        add_executable(fcg_random_mover ${FC_APP}/tests/mock/engine/random_mover.cpp)
        set_target_properties(fcg_random_mover PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
        if(WIN32)
            target_link_options(fcg_random_mover PRIVATE -static -static-libgcc -static-libstdc++)
        endif()

        add_executable(fastchess-gui-tests ${FCG_TEST_SOURCES})
        target_link_libraries(fastchess-gui-tests PRIVATE fcg_lib)
        add_dependencies(fastchess-gui-tests fcg_random_mover)
        target_compile_definitions(fastchess-gui-tests PRIVATE FCG_RANDOM_MOVER="$<TARGET_FILE_NAME:fcg_random_mover>"
                                                              FCG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        if(WIN32)
            target_link_options(fastchess-gui-tests PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()
endif()
