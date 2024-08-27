add_library(lumina_vfs STATIC "")

target_compile_options(lumina_vfs PRIVATE ${LUMINA_COMPILE_OPTIONS})
target_link_libraries(lumina_vfs PRIVATE lumina_core)
target_sources(lumina_vfs PUBLIC FILE_SET CXX_MODULES FILES
	mod.cppm
)

