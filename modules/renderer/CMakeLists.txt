add_library(lumina_renderer STATIC "")

target_link_libraries(lumina_renderer PRIVATE lumina_core lumina_platform lumina_vfs lumina_vulkan lumina_ui)
target_sources(lumina_renderer PUBLIC FILE_SET CXX_MODULES FILES
	mod.cppm
	animation.cppm
	camera.cppm
	gpu_scene.cppm
	resource_format.cppm
	material_registry.cppm
	texture_registry.cppm
	mesh_registry.cppm)
