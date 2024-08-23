module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

export module lumina.vulkan:impl_wsi_glfw3;

import :wsi;

import lumina.platform;
import vulkan_hpp;

import std;

namespace lumina::vulkan
{

vk::SurfaceKHR create_surface(platform::Window* wnd, vk::Instance instance)
{
	VkSurfaceKHR c_api_surface_handle;
	if(glfwCreateWindowSurface(instance, wnd->native_handle(), nullptr, &c_api_surface_handle) != VK_SUCCESS)
		throw std::runtime_error("failed to create vk::SurfaceKHR");

	return vk::SurfaceKHR{c_api_surface_handle};
}

}
