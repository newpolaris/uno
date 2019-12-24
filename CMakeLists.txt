# CMake entry point
cmake_minimum_required (VERSION 3.0)
project(ubo)

set(ROOT_PATH "${PROJECT_SOURCE_DIR}")
set(DEPENDENCIES_PATH "${PROJECT_SOURCE_DIR}/externals")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(externals)

option(SUPPORT_OPENGL "Support legacy openGL" ON)
option(SUPPORT_METAL "Support apple metal" ON)

if(SUPPORT_OPENGL)
    set(BUILD_OPENGL TRUE)
endif()
if(APPLE)
    set(BUILD_METAL ${SUPPORT_METAL})
endif()

add_definitions(-D_CRT_SECURE_NO_WARNINGS)

add_subdirectory(sources)
