cmake_minimum_required(VERSION 3.16)

project(M5CardputerZeroMusic VERSION 0.1.0 LANGUAGES C CXX ASM)

option(MUSIC_USE_SDL "Use LVGL's SDL backend for desktop testing" ON)
option(BUILD_TESTING "Build Music tests" OFF)
set(MUSIC_BIN_NAME "M5CardputerZero-Music" CACHE STRING "Output executable name")
if(MUSIC_USE_SDL)
    set(MUSIC_DEFAULT_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/dist/desktop")
else()
    set(MUSIC_DEFAULT_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/dist")
endif()
set(MUSIC_OUTPUT_DIR "${MUSIC_DEFAULT_OUTPUT_DIR}" CACHE PATH "Runtime output directory")

include(CTest)
include(ExternalProject)

if(APPLE AND NOT MUSIC_USE_SDL)
    message(FATAL_ERROR "macOS builds only support MUSIC_USE_SDL=ON.")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MUSIC_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(MUSIC_SRC_DIR "${MUSIC_ROOT}/src")
set(MUSIC_DEPS_DIR "${MUSIC_ROOT}/dependencies")
set(MUSIC_LVGL_DIR "${MUSIC_DEPS_DIR}/lvgl")
set(MUSIC_JPEG_DIR "${MUSIC_DEPS_DIR}/libjpeg-turbo")
set(MUSIC_SPDLOG_DIR "${MUSIC_DEPS_DIR}/spdlog")
set(MUSIC_SMOOTH_UI_DIR "${MUSIC_DEPS_DIR}/smooth_ui_toolkit")
set(MUSIC_MINIAUDIO_DIR "${MUSIC_DEPS_DIR}/miniaudio")
set(MUSIC_TAGLIB_DIR "${MUSIC_DEPS_DIR}/taglib")
set(MUSIC_SQLITECPP_DIR "${MUSIC_DEPS_DIR}/sqlitecpp")
set(MUSIC_SYSTEM_LIBS ${CMAKE_DL_LIBS})

if(UNIX AND NOT APPLE)
    list(APPEND MUSIC_SYSTEM_LIBS m)
endif()

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

foreach(required_dir IN LISTS
        MUSIC_LVGL_DIR
        MUSIC_JPEG_DIR
        MUSIC_SPDLOG_DIR
        MUSIC_SMOOTH_UI_DIR
        MUSIC_MINIAUDIO_DIR
        MUSIC_TAGLIB_DIR
        MUSIC_SQLITECPP_DIR
)
    if(NOT EXISTS "${required_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Missing dependency: ${required_dir}. Run ./bootstrap.sh first.")
    endif()
endforeach()

find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

# Debian multiarch runtime packages retain the SONAME link even when the target
# development package is not installed. It is sufficient for static TagLib to
# link against zlib during a local cross-build.
if(CMAKE_CROSSCOMPILING AND CMAKE_LIBRARY_ARCHITECTURE AND NOT DEFINED ZLIB_LIBRARY)
    find_library(MUSIC_ZLIB_LIBRARY
        NAMES z libz.so.1
        PATHS
            "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
            "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
        NO_DEFAULT_PATH
    )
    if(MUSIC_ZLIB_LIBRARY)
        set(ZLIB_LIBRARY "${MUSIC_ZLIB_LIBRARY}" CACHE FILEPATH "Target zlib library")
    endif()
endif()
find_package(ZLIB REQUIRED)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(LV_BUILD_CONF_PATH "${MUSIC_ROOT}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)

set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF CACHE BOOL "" FORCE)

set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB ON CACHE BOOL "" FORCE)
set(VISIBILITY_HIDDEN OFF CACHE BOOL "" FORCE)

set(SQLITECPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_DOXYGEN OFF CACHE BOOL "" FORCE)
set(SQLITECPP_INCLUDE_SCRIPT OFF CACHE BOOL "" FORCE)
set(SQLITECPP_INTERNAL_SQLITE ON CACHE BOOL "" FORCE)
set(SQLITE_ENABLE_COLUMN_METADATA OFF CACHE BOOL "" FORCE)
set(SQLITE_OMIT_LOAD_EXTENSION ON CACHE BOOL "" FORCE)

add_subdirectory("${MUSIC_LVGL_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${MUSIC_SPDLOG_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${MUSIC_SMOOTH_UI_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${MUSIC_TAGLIB_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/taglib" EXCLUDE_FROM_ALL)
if(NOT TARGET TagLib::tag)
    add_library(TagLib::tag ALIAS tag)
endif()
add_subdirectory("${MUSIC_SQLITECPP_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/sqlitecpp" EXCLUDE_FROM_ALL)

set(MUSIC_JPEG_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/dependencies/libjpeg-turbo")
file(MAKE_DIRECTORY "${MUSIC_JPEG_BUILD_DIR}")
set(MUSIC_JPEG_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_POLICY_DEFAULT_CMP0077=NEW
    -DENABLE_SHARED=OFF
    -DENABLE_STATIC=ON
    -DWITH_JAVA=OFF
    -DWITH_SIMD=OFF
    -DWITH_TURBOJPEG=OFF
)
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND MUSIC_JPEG_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()
ExternalProject_Add(music_libjpeg_build
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/dependencies/libjpeg-turbo-prefix"
    SOURCE_DIR "${MUSIC_JPEG_DIR}"
    BINARY_DIR "${MUSIC_JPEG_BUILD_DIR}"
    CMAKE_ARGS ${MUSIC_JPEG_CMAKE_ARGS}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target jpeg-static --parallel
    INSTALL_COMMAND ""
    UPDATE_COMMAND ""
    BUILD_BYPRODUCTS "${MUSIC_JPEG_BUILD_DIR}/libjpeg.a"
)
add_library(music_jpeg STATIC IMPORTED GLOBAL)
set_target_properties(music_jpeg PROPERTIES
    IMPORTED_LOCATION "${MUSIC_JPEG_BUILD_DIR}/libjpeg.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MUSIC_JPEG_BUILD_DIR};${MUSIC_JPEG_DIR}"
)
add_dependencies(music_jpeg music_libjpeg_build)

function(music_add_sdl2_parent_include target imported_target)
    if(NOT TARGET "${imported_target}")
        return()
    endif()

    get_target_property(sdl2_include_dirs "${imported_target}" INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT sdl2_include_dirs)
        return()
    endif()

    foreach(sdl2_include_dir IN LISTS sdl2_include_dirs)
        if(sdl2_include_dir MATCHES "/SDL2$")
            get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
            target_include_directories("${target}" SYSTEM PRIVATE "${sdl2_parent_include_dir}")
        endif()
    endforeach()
endfunction()

target_compile_definitions(lvgl PUBLIC
    MUSIC_LVGL_USE_SDL=$<IF:$<BOOL:${MUSIC_USE_SDL}>,1,0>
    MUSIC_LVGL_FS_POSIX_PATH=""
)

if(MUSIC_USE_SDL)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()

    if(SDL2_FOUND)
        target_link_libraries(lvgl PRIVATE PkgConfig::SDL2)
        music_add_sdl2_parent_include(lvgl PkgConfig::SDL2)
        set(MUSIC_SDL_LIBS PkgConfig::SDL2)
    else()
        find_package(SDL2 CONFIG QUIET)
        if(TARGET SDL2::SDL2)
            target_link_libraries(lvgl PRIVATE SDL2::SDL2)
            music_add_sdl2_parent_include(lvgl SDL2::SDL2)
            set(MUSIC_SDL_LIBS SDL2::SDL2)
        else()
            find_package(SDL2 REQUIRED)
            target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
            foreach(sdl2_include_dir IN LISTS SDL2_INCLUDE_DIRS)
                if(sdl2_include_dir MATCHES "/SDL2$")
                    get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
                    target_include_directories(lvgl SYSTEM PRIVATE "${sdl2_parent_include_dir}")
                endif()
            endforeach()
            set(MUSIC_SDL_LIBS ${SDL2_LIBRARIES})
        endif()
    endif()
endif()

set(MUSIC_TAGLIB_INCLUDE_DIRS
    "${MUSIC_TAGLIB_DIR}/taglib"
    "${MUSIC_TAGLIB_DIR}/taglib/toolkit"
    "${MUSIC_TAGLIB_DIR}/taglib/mpeg"
    "${MUSIC_TAGLIB_DIR}/taglib/mpeg/id3v2"
    "${MUSIC_TAGLIB_DIR}/taglib/mpeg/id3v2/frames"
    "${MUSIC_TAGLIB_DIR}/taglib/flac"
    "${MUSIC_TAGLIB_DIR}/taglib/mp4"
    "${MUSIC_TAGLIB_DIR}/taglib/riff"
    "${MUSIC_TAGLIB_DIR}/taglib/riff/wav"
    "${CMAKE_CURRENT_BINARY_DIR}/dependencies/taglib"
)

file(GLOB_RECURSE MUSIC_APP_CPP CONFIGURE_DEPENDS
    "${MUSIC_SRC_DIR}/*.cpp"
    "${MUSIC_SRC_DIR}/*.c"
)

add_executable(${MUSIC_BIN_NAME} ${MUSIC_APP_CPP})
target_include_directories(${MUSIC_BIN_NAME} PRIVATE
    "${MUSIC_SRC_DIR}"
    "${MUSIC_MINIAUDIO_DIR}"
    ${MUSIC_TAGLIB_INCLUDE_DIRS}
)
target_compile_definitions(${MUSIC_BIN_NAME} PRIVATE
    MUSIC_USE_SDL=$<IF:$<BOOL:${MUSIC_USE_SDL}>,1,0>
    MUSIC_ASSETS_PATH="$<IF:$<BOOL:${MUSIC_USE_SDL}>,${MUSIC_ROOT}/assets,/usr/share/Music>"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${MUSIC_BIN_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(${MUSIC_BIN_NAME} PRIVATE
    lvgl::lvgl
    music_jpeg
    smooth_ui_toolkit
    SQLiteCpp
    TagLib::tag
    spdlog::spdlog
    Threads::Threads
    ${MUSIC_SDL_LIBS}
    ${MUSIC_SYSTEM_LIBS}
)
add_dependencies(${MUSIC_BIN_NAME} music_libjpeg_build)
set_target_properties(${MUSIC_BIN_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${MUSIC_OUTPUT_DIR}"
)

if(BUILD_TESTING)
    add_executable(music_lyrics_tests
        tests/music_lyrics_test.cpp
        src/core/music_lyrics.cpp
    )
    target_include_directories(music_lyrics_tests PRIVATE
        "${MUSIC_SRC_DIR}"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(music_lyrics_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME music_lyrics COMMAND music_lyrics_tests)

    add_executable(music_cover_rasterizer_tests
        tests/cover_rasterizer_test.cpp
        src/rendering/cover_rasterizer.cpp
    )
    target_include_directories(music_cover_rasterizer_tests PRIVATE
        "${MUSIC_SRC_DIR}"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(music_cover_rasterizer_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(music_cover_rasterizer_tests PRIVATE
        ${MUSIC_SYSTEM_LIBS}
    )
    add_test(NAME music_cover_rasterizer COMMAND music_cover_rasterizer_tests)

    add_executable(music_cover_image_loader_tests
        tests/cover_image_loader_test.cpp
        src/rendering/artwork_palette.cpp
        src/rendering/cover_image_loader.cpp
    )
    target_include_directories(music_cover_image_loader_tests PRIVATE
        "${MUSIC_SRC_DIR}"
    )
    target_compile_definitions(music_cover_image_loader_tests PRIVATE
        MUSIC_BUNDLED_ARTWORK_TEST_ROOT="${MUSIC_ROOT}/assets/covers"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(music_cover_image_loader_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(music_cover_image_loader_tests PRIVATE
        lvgl::lvgl
        music_jpeg
        spdlog::spdlog
        Threads::Threads
        ${MUSIC_SDL_LIBS}
        ${MUSIC_SYSTEM_LIBS}
    )
    add_dependencies(music_cover_image_loader_tests music_libjpeg_build)
    add_test(NAME music_cover_image_loader COMMAND music_cover_image_loader_tests)

    add_executable(music_library_model_tests
        tests/music_library_model_test.cpp
        src/assets/runtime_assets.cpp
        src/core/music_guides.cpp
        src/core/music_types.cpp
        src/models/music_library_model.cpp
    )
    target_include_directories(music_library_model_tests PRIVATE
        "${MUSIC_SRC_DIR}"
        ${MUSIC_TAGLIB_INCLUDE_DIRS}
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(music_library_model_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_compile_definitions(music_library_model_tests PRIVATE
        MUSIC_ASSETS_PATH="${MUSIC_ROOT}/assets"
    )
    target_link_libraries(music_library_model_tests PRIVATE
        SQLiteCpp
        TagLib::tag
        spdlog::spdlog
        Threads::Threads
        ${MUSIC_SYSTEM_LIBS}
    )
    add_test(NAME music_library_model COMMAND music_library_model_tests)
endif()
