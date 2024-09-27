add_library(lumina_ecs STATIC "")

target_link_libraries(lumina_ecs PRIVATE lumina_core)
target_sources(lumina_ecs PUBLIC FILE_SET CXX_MODULES FILES
	entity.cppm
	pool.cppm
	realm.cppm
	sparse_set.cppm
	view.cppm
	mod.cppm)
