add_library(lumina INTERFACE "")

add_subdirectory("core")
add_subdirectory("platform")
add_subdirectory("ui")
add_subdirectory("vulkan")

add_library(lumina::engine ALIAS lumina)

target_compile_features(lumina INTERFACE cxx_std_23)
target_compile_options(lumina INTERFACE -Wno-deprecated-declarations -stdlib=libc++)
target_link_libraries(lumina INTERFACE lumina_core lumina_platform lumina_ui lumina_vulkan)
