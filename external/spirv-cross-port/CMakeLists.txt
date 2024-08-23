cmake_minimum_required(VERSION 3.30.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

project(spirv-cross-port VERSION 1.91.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_MODULE_STD 1)

set(SPV_DIR ${PROJECT_SOURCE_DIR}/SPIRV-Cross)

add_library(spirv-cross-port STATIC)

add_subdirectory("SPIRV-Cross")

target_include_directories(spirv-cross-port PRIVATE ${SPV_DIR})
target_link_libraries(spirv-cross-port PRIVATE spirv-cross-core)
target_sources(spirv-cross-port PUBLIC
	FILE_SET CXX_MODULES
	FILES mod.cppm)
