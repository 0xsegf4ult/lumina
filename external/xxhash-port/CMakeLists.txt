cmake_minimum_required(VERSION 3.30.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

project(xxhash-port VERSION 1.91.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_MODULE_STD 1)

set(XXHASH_DIR ${PROJECT_SOURCE_DIR}/xxHash)

add_library(xxhash-port STATIC)

target_include_directories(xxhash-port PRIVATE ${XXHASH_DIR})
target_sources(xxhash-port PUBLIC
	FILE_SET CXX_MODULES
	FILES mod.cppm)
