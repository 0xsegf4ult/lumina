add_library(lumina_physics STATIC "")

target_link_libraries(lumina_physics PRIVATE lumina_core)
target_sources(lumina_physics PUBLIC FILE_SET CXX_MODULES FILES
	broadphase/bvh4_node.cppm
	broadphase/bvh4_tree.cppm
	broadphase/broadphase_interface.cppm
	collision/mod.cppm
	collision/shape.cppm
	collision/shape/sphere.cppm
	collision/shape/capsule.cppm
	collision/shape/convex_hull.cppm
	collision/gjk.cppm
	collision/sat.cppm
	rigidbody_interface.cppm
	mod.cppm)
