add_library(lumina_platform STATIC "")

find_package(glfw3 CONFIG REQUIRED)

target_link_libraries(lumina_platform PRIVATE lumina_core glfw)
target_sources(lumina_platform PUBLIC FILE_SET CXX_MODULES FILES
	mod.cppm
	input.cppm
	window.cppm)
