find_package(slang)
add_executable(shader_compiler)
target_link_libraries(shader_compiler lumina_vulkan slang::slang)
target_sources(shader_compiler PRIVATE main.cpp)

