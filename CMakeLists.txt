cmake_minimum_required(VERSION 3.15)

project(StructuraSystems CXX C)

include(CMakeDependentOption)

option(RUN_TESTS "Build the tests" ON)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "~/Qt/6.7.3/macos/")
endif(APPLE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/debug)

add_subdirectory(./stucturasystems/)