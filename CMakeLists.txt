cmake_minimum_required(VERSION 3.16)

project(Mate VERSION 0.1.9)

option(MATE_ENABLE_ONNX "Enable ML move support via ONNX Runtime" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Threads REQUIRED)

set(MATE_CORE_SOURCES
    src/chess.cpp
    src/config.cpp
    src/database.cpp
    src/network.cpp
    src/utils.cpp
)

if(MATE_ENABLE_ONNX)
    list(APPEND MATE_CORE_SOURCES src/chess_ML.cpp)
else()
    list(APPEND MATE_CORE_SOURCES src/chess_ML_stub.cpp)
endif()

add_library(mate_core ${MATE_CORE_SOURCES})
target_include_directories(mate_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/src"
)
target_compile_definitions(mate_core
    PRIVATE
        MATE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MATE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MATE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        MATE_VERSION="${PROJECT_VERSION}"
        MATE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mate_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(SQLite3 REQUIRED)
if(TARGET SQLite3::SQLite3)
    target_link_libraries(mate_core PUBLIC SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
    target_link_libraries(mate_core PUBLIC SQLite::SQLite3)
else()
    target_include_directories(mate_core PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(mate_core PUBLIC ${SQLite3_LIBRARIES})
endif()

if(MATE_ENABLE_ONNX)
    find_package(onnxruntime CONFIG REQUIRED)
    target_compile_definitions(mate_core PRIVATE MATE_ENABLE_ONNX=1)
    target_link_libraries(mate_core PRIVATE onnxruntime::onnxruntime)
endif()

add_executable(Mate src/main.cpp)
target_link_libraries(Mate PRIVATE mate_core Threads::Threads)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2_PC QUIET IMPORTED_TARGET sdl2)
    pkg_check_modules(FREETYPE2_PC QUIET IMPORTED_TARGET freetype2)
    pkg_check_modules(FONTCONFIG_PC QUIET IMPORTED_TARGET fontconfig)
endif()

if(SDL2_PC_FOUND AND FREETYPE2_PC_FOUND AND FONTCONFIG_PC_FOUND)
    target_sources(Mate PRIVATE src/gui.cpp)
    target_link_libraries(Mate PRIVATE PkgConfig::SDL2_PC PkgConfig::FREETYPE2_PC PkgConfig::FONTCONFIG_PC)
else()
    target_sources(Mate PRIVATE src/gui_stub.cpp)
endif()

if(BUILD_TESTING)
    add_executable(mate_core_tests tests/core_smoke.cpp)
    target_link_libraries(mate_core_tests PRIVATE mate_core)
    add_test(NAME mate_core_tests COMMAND mate_core_tests)
endif()
