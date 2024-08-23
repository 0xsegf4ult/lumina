add_library(lumina_ui STATIC "")

target_link_libraries(lumina_ui PRIVATE lumina_core lumina_platform lumina_vulkan PUBLIC imgui)
target_sources(lumina_ui PUBLIC FILE_SET CXX_MODULES FILES
	mod.cppm
	imgui.cppm
	device_overlay.cppm)
