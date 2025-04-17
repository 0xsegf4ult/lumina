add_library(lumina INTERFACE "")

add_subdirectory("core")
add_subdirectory("ecs")
add_subdirectory("platform")
add_subdirectory("vfs")
add_subdirectory("vulkan")
add_subdirectory("ui")
add_subdirectory("renderer")
add_subdirectory("physics")

# refactor this
add_subdirectory("fw")

add_library(lumina::engine ALIAS lumina)

target_compile_features(lumina INTERFACE cxx_std_23)
target_compile_options(lumina INTERFACE ${LUMINA_COMPILE_OPTIONS})
target_link_libraries(lumina INTERFACE lumina_core lumina_ecs lumina_platform lumina_ui lumina_vfs lumina_physics lumina_vulkan lumina_renderer lumina_fw)
