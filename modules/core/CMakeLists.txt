add_library(lumina_core STATIC "")

add_custom_command(
	OUTPUT
		${CMAKE_CURRENT_LIST_DIR}/config/mod.cppm
		ALL
	COMMAND
		${CMAKE_COMMAND} -D IN_FILE=${CMAKE_CURRENT_LIST_DIR}/config/mod.cppm.in -D OUT_FILE=${CMAKE_CURRENT_LIST_DIR}/config/mod.cppm -P ${PROJECT_SOURCE_DIR}/cmake/GenerateConfig.cmake
	WORKING_DIRECTORY
		${PROJECT_SOURCE_DIR}
)

add_custom_target(
	generate_config_module ALL DEPENDS ${CMAKE_CURRENT_LIST_DIR}/config/mod.cppm
)

add_dependencies(lumina_core generate_config_module)

target_compile_options(lumina_core PUBLIC ${LUMINA_COMPILE_OPTIONS})
set(LCORE_LINK_LIBRARIES spdlog::spdlog Tracy::TracyClient)

if(${LUMINA_PLATFORM} MATCHES "LINUX")
	set(LCORE_LINK_LIBRARIES ${LCORE_LINK_LIBRARIES} m pthread)
endif()

target_link_libraries(lumina_core PUBLIC ${LCORE_LINK_LIBRARIES})
target_sources(lumina_core PUBLIC FILE_SET CXX_MODULES FILES
	mod.cppm
	config/mod.cppm
	math/mod.cppm
	math/aabb.cppm
	math/atomic.cppm
	math/intersect_test.cppm
	math/matrix.cppm
	math/plane.cppm
	math/quaternion.cppm
	math/transform.cppm
	math/util.cppm
	math/vector.cppm
	array_proxy.cppm
	fixed_string.cppm
	hash.cppm
	handle.cppm
	iterable_proxy.cppm
	job.cppm
	log.cppm
	object_pool.cppm
	refcounted.cppm
	type_hash.cppm
	type_traits.cppm
	typesafe_flags.cppm)
