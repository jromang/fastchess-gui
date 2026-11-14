cmake_minimum_required(VERSION 3.24)

project(fastchess_gui VERSION 0.5.8 LANGUAGES C CXX)

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
    ${FC_APP}/src/core/events/events.cpp
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
# Tournament layer: schedules, journal, results, runner (no UI)
# ---------------------------------------------------------------------------

file(GLOB FCG_TOURNAMENT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tournament/src/tour/*.cpp)
add_library(fcg_tournament STATIC ${FCG_TOURNAMENT_SOURCES})
target_compile_features(fcg_tournament PUBLIC cxx_std_20)
target_include_directories(fcg_tournament PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tournament/src)
target_compile_options(fcg_tournament PRIVATE -Wall -Wextra)
target_link_libraries(fcg_tournament PUBLIC fastchess_core)

if(FCG_BUILD_TESTS)
    # fastchess' own mock engine, so the integration tests play real (random) games.
    add_executable(fcg_random_mover ${CMAKE_CURRENT_SOURCE_DIR}/tournament/tests/mock/random_mover.cpp)
    target_include_directories(fcg_random_mover PRIVATE ${FC_APP}/third_party)
    set_target_properties(fcg_random_mover PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(WIN32)
        target_link_options(fcg_random_mover PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    file(GLOB FCG_TOURNAMENT_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tournament/tests/*.cpp)
    add_executable(fcg-tournament-tests ${FCG_TOURNAMENT_TEST_SOURCES})
    target_link_libraries(fcg-tournament-tests PRIVATE fcg_tournament)
    add_dependencies(fcg-tournament-tests fcg_random_mover)
    target_compile_definitions(fcg-tournament-tests PRIVATE FCG_RANDOM_MOVER="$<TARGET_FILE_NAME:fcg_random_mover>")
    if(WIN32)
        target_link_options(fcg-tournament-tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
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

    # Fonts: Inter for the text (SIL OFL 1.1), Lucide for the icons (ISC).
    FetchContent_Declare(inter
        URL https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
        URL_HASH SHA256=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
        SOURCE_SUBDIR fcg-no-cmake)
    FetchContent_Declare(lucide
        URL https://registry.npmjs.org/lucide-static/-/lucide-static-1.59.0.tgz
        URL_HASH SHA256=39e2f835692c193233ac7bcdf34eb0cc2da0f839fd9f3674c34d2751bccf37f0
        SOURCE_SUBDIR fcg-no-cmake)

    # SOURCE_SUBDIR points nowhere for the header/source-only dependencies,
    # so MakeAvailable only downloads them.
    FetchContent_MakeAvailable(sdl3 imgui implot nanosvg stb inter lucide)

    # TLS for the lichess broadcasts: Mbed TLS (long term support branch), static, its libraries only.
    FetchContent_Declare(mbedtls
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.1/mbedtls-4.1.1.tar.bz2
        URL_HASH SHA256=3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c
        PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_mbedtls.cmake)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(TF_PSA_CRYPTO_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
    # Thread-safe: the lichess relay and the SSH tunnel use it at the same time.
    set(TF_PSA_CRYPTO_USER_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tf_psa_crypto_user_config.h
        CACHE FILEPATH "" FORCE)
    set(LINK_WITH_PTHREAD ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mbedtls)

    # SSH for the web page's tunnel: libssh2 (BSD-3-Clause), compiled here with a cryptography backend of our own on
    # Mbed TLS 4's PSA API (gui/libssh2-psa): its released versions do not build with Mbed TLS 4. A commit of its
    # master branch, the latest when this was written. Patched: a channel's data packets are appended to the one it
    # holds for the channel, so that what it keeps is about the bytes received, not a block of memory for each packet
    # however small; data for a channel closed on our side is dropped; a forwarded connection's window, which bounds
    # what each viewer makes it hold, is 128 KiB instead of 2 MiB (see cmake/patch_libssh2.cmake).
    FetchContent_Declare(libssh2
        URL https://github.com/libssh2/libssh2/archive/e75fa9a539d21e82d4920c126662e36984e97d61.tar.gz
        URL_HASH SHA256=3d4fd0016ec56945f911fd223a43d048622f80c4510cdf3e73de6e47a31c3b1b
        PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_libssh2.cmake
        SOURCE_SUBDIR fcg-no-cmake)
    FetchContent_MakeAvailable(libssh2)

    # QR codes of the web page's public address: Nayuki's QR Code generator (MIT), its C++ version compiled in.
    FetchContent_Declare(qrcodegen
        URL https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz
        URL_HASH SHA256=2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60
        SOURCE_SUBDIR fcg-no-cmake)
    FetchContent_MakeAvailable(qrcodegen)

    # The translations: .po files read by tinygettext, UTF-8 only (no iconv).
    FetchContent_Declare(tinygettext
        GIT_REPOSITORY https://github.com/tinygettext/tinygettext.git
        GIT_TAG 0ebef71ef7aa07ec6d026164fbe9472e6f82a583)
    set(TINYGETTEXT_UTF8_ONLY ON CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tinygettext)

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

    # libssh2's sources, but its own cryptography backends: ours instead of its mbedtls.c (see libssh2_config.h).
    # The other backends' files compile to nothing without their define.
    set(FCG_LIBSSH2_SOURCES)
    foreach(file agent bcrypt_pbkdf chacha channel cipher-chachapoly comp crypt global hostkey keepalive kex knownhost
                 mac misc packet pem poly1305 publickey scp session sftp transport userauth userauth_kbd_packet version)
        list(APPEND FCG_LIBSSH2_SOURCES ${libssh2_SOURCE_DIR}/src/${file}.c)
    endforeach()
    add_library(fcg_libssh2 STATIC ${FCG_LIBSSH2_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa/mbedtls_psa.c)
    target_include_directories(fcg_libssh2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa ${libssh2_SOURCE_DIR}/src)
    target_include_directories(fcg_libssh2 SYSTEM PUBLIC ${libssh2_SOURCE_DIR}/include)
    # LIBSSH2DEBUG for libssh2_trace(): the tunnel reads libssh2's errors as they come, the first one often the only
    # one to tell why a call failed (a host key's signature, say). Nothing is traced but the errors.
    target_compile_definitions(fcg_libssh2 PRIVATE HAVE_CONFIG_H LIBSSH2_MBEDTLS LIBSSH2_MBEDTLS_H LIBSSH2DEBUG)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa/mbedtls_psa.c PROPERTIES
                                COMPILE_OPTIONS "-Wall;-Wextra")
    target_link_libraries(fcg_libssh2 PRIVATE tfpsacrypto)
    if(WIN32)
        target_link_libraries(fcg_libssh2 PUBLIC ws2_32)
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

    # The fonts, embedded as byte arrays.
    set(FCG_FONT_CPPS)
    foreach(font IN ITEMS "inter_regular|${inter_SOURCE_DIR}/extras/ttf/Inter-Regular.ttf"
                          "inter_semibold|${inter_SOURCE_DIR}/extras/ttf/Inter-SemiBold.ttf"
                          "lucide_icons|${lucide_SOURCE_DIR}/font/lucide.ttf")
        string(REPLACE "|" ";" font "${font}")
        list(GET font 0 symbol)
        list(GET font 1 ttf)
        set(cpp ${CMAKE_CURRENT_BINARY_DIR}/generated/${symbol}.cpp)
        add_custom_command(
            OUTPUT ${cpp}
            COMMAND ${CMAKE_COMMAND} -DIN=${ttf} -DOUT=${cpp} -DSYMBOL=${symbol}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
            DEPENDS ${ttf} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
            COMMENT "Embedding ${symbol}")
        list(APPEND FCG_FONT_CPPS ${cpp})
    endforeach()

    # The translations, embedded with their language codes (gui/i18n/<code>.po).
    file(GLOB FCG_PO_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/i18n/*.po)
    set(FCG_PO_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/translations.cpp)
    string(REPLACE ";" "|" FCG_PO_LIST "${FCG_PO_FILES}")
    add_custom_command(
        OUTPUT ${FCG_PO_CPP}
        COMMAND ${CMAKE_COMMAND} -DOUT=${FCG_PO_CPP} "-DFILES=${FCG_PO_LIST}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_po.cmake
        DEPENDS ${FCG_PO_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_po.cmake
        COMMENT "Embedding translations"
        VERBATIM)

    file(GLOB FCG_GUI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/src/*.cpp)
    list(REMOVE_ITEM FCG_GUI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gui/src/main.cpp)

    # Everything except main(), so the unit tests can link it too.
    add_library(fcg_lib STATIC ${FCG_GUI_SOURCES} ${FCG_ASSET_CPP} ${FCG_FONT_CPPS} ${FCG_PO_CPP}
                ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
    target_compile_features(fcg_lib PUBLIC cxx_std_20)
    target_include_directories(fcg_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/gui/src)
    # Header-only libraries compiled in our files: their warnings are not ours to fix.
    target_include_directories(fcg_lib SYSTEM PUBLIC
        ${nanosvg_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
        ${qrcodegen_SOURCE_DIR}/cpp
    )
    target_compile_options(fcg_lib PRIVATE -Wall -Wextra)
    target_compile_definitions(fcg_lib PUBLIC FCG_VERSION="${PROJECT_VERSION}")
    target_link_libraries(fcg_lib PUBLIC fcg_tournament imgui_lib PRIVATE mbedtls fcg_libssh2 tinygettext)
    if(WIN32)
        target_link_libraries(fcg_lib PUBLIC ws2_32)
    endif()

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
        add_executable(fastchess-gui-tests ${FCG_TEST_SOURCES})
        # Mbed TLS too: the SSH tests play the server's part with its cryptography. And the libssh2 backend's
        # functions, which they call directly.
        target_link_libraries(fastchess-gui-tests PRIVATE fcg_lib mbedtls fcg_libssh2)
        target_include_directories(fastchess-gui-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui/libssh2-psa)
        target_include_directories(fastchess-gui-tests SYSTEM PRIVATE ${libssh2_SOURCE_DIR}/src)
        # A WinBoard engine for the tests of the adapter.
        add_executable(fcg_wb_mover ${CMAKE_CURRENT_SOURCE_DIR}/gui/tests/mock/wb_mover.cpp)
        set_target_properties(fcg_wb_mover PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
        if(WIN32)
            target_link_options(fcg_wb_mover PRIVATE -static -static-libgcc -static-libstdc++)
        endif()

        add_dependencies(fastchess-gui-tests fcg_random_mover fcg_wb_mover)
        target_compile_definitions(fastchess-gui-tests PRIVATE FCG_RANDOM_MOVER="$<TARGET_FILE_NAME:fcg_random_mover>"
                                                              FCG_WB_MOVER="$<TARGET_FILE_NAME:fcg_wb_mover>"
                                                              FCG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        if(WIN32)
            target_link_options(fastchess-gui-tests PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()
endif()
