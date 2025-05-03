add_library(lumina_vulkan STATIC "")

find_package(Vulkan REQUIRED)

if(${Vulkan_VERSION} VERSION_LESS "1.3.256")
	message( FATAL_ERROR "Minimum required Vulkan version is 1.3.256"
		 "Found ${Vulkan_VERSION}"
	)
endif()

add_library(VulkanHppModule)
target_sources(VulkanHppModule PUBLIC
	FILE_SET CXX_MODULES
	BASE_DIRS ${Vulkan_INCLUDE_DIR}
	FILES ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
)

target_compile_features(VulkanHppModule PUBLIC cxx_std_23)
target_compile_definitions(VulkanHppModule PUBLIC
	VULKAN_HPP_NO_CONSTRUCTORS
	VULKAN_HPP_NO_SMART_HANDLE
	VULKAN_HPP_NO_TO_STRING
)
target_compile_definitions(VulkanHppModule PUBLIC
	VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)
target_link_libraries(VulkanHppModule PUBLIC Vulkan::Headers)

target_link_libraries(lumina_vulkan PUBLIC VulkanHppModule PRIVATE lumina_core lumina_platform spirv-cross-port xxhash-port)

target_sources(lumina_vulkan PUBLIC FILE_SET CXX_MODULES 
	FILES
	mod.cppm
	buffer.cppm
	context.cppm
	command_buffer.cppm
	device.cppm
	descriptor.cppm
	image.cppm
	queues.cppm
	pipeline.cppm
	shader.cppm
	wsi.cppm
	PRIVATE
	impl/device.cpp
	impl/buffer.cpp
	impl/image.cpp
	impl/command_buffer.cpp
	impl/wsi_glfw3.cpp
	dispatch.cpp)
	

