# Specify the minimum version for CMake
cmake_minimum_required(VERSION 3.12)

# Project's name
# this generates the variable PROJECT_NAME
project(Mate VERSION 0.1.0.0)

# Require a modern C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable debug symbols - comment these 2 lines if you dont need the debugger!
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/lib/onnxruntime")
find_package(onnxruntime REQUIRED)

# Set the output folder where your program will be created
# CMAKE_SOURCE_DIR -> where to cmake was executed
# for in-source building: CMAKE_BINARY_DIR = CMAKE_SOURCE_DIR
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)

# EXECUTABLE_OUTPUT_PATH specify a common place where CMake should put all executable files
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

# specify a common place where CMake should put all libraries
#set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})
SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

# configure a header file to pass the version number to the source code:
# configure_file(TutorialConfig.h.in TutorialConfig.h)

# The following folder will be included
include_directories("${PROJECT_SOURCE_DIR}")

# Expose project version to C++ sources via preprocessor macros
add_compile_definitions(
	MATE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
	MATE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
	MATE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
	MATE_VERSION="${PROJECT_VERSION}"
)

add_executable(${PROJECT_NAME}
	${PROJECT_SOURCE_DIR}/src/main.cpp
	${PROJECT_SOURCE_DIR}/src/database.cpp
	${PROJECT_SOURCE_DIR}/src/utils.cpp
	${PROJECT_SOURCE_DIR}/src/chess.cpp
	${PROJECT_SOURCE_DIR}/src/chess_ML.cpp
	${PROJECT_SOURCE_DIR}/src/config.cpp
	${PROJECT_SOURCE_DIR}/src/network.cpp
)

find_package(SQLite3 REQUIRED)
include_directories(${SQLite3_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME}
    ${SQLite3_LIBRARIES}
	onnxruntime::onnxruntime
)
