cmake_minimum_required(VERSION 3.30.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

project(imgui VERSION 1.91.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_MODULE_STD 1)

set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/imgui)

add_library(imgui STATIC)
target_include_directories(imgui PRIVATE ${IMGUI_DIR})
target_sources(imgui PUBLIC
	FILE_SET CXX_MODULES
	FILES mod.cppm
	PRIVATE
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_demo.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
	${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp)
