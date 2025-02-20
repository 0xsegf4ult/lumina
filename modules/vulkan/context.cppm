module;

#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_core.h>

export module lumina.vulkan:context;

import vulkan_hpp;
import std;

import lumina.core.log;

import :queues;
import :device;

using std::uint32_t, std::size_t;

namespace lumina::vulkan
{

std::array<const char*, 1> default_instance_extensions =
{
	vk::KHRGetPhysicalDeviceProperties2ExtensionName
};

std::array<const char*, 2> default_device_extensions =
{
	vk::KHRSwapchainExtensionName,
	vk::KHRPushDescriptorExtensionName,
};

bool query_extension_support(vk::PhysicalDevice gpu, const std::vector<const char*>& required)
{
	std::vector<vk::ExtensionProperties> extensions = gpu.enumerateDeviceExtensionProperties();

	std::set<std::string> missing(required.begin(), required.end());
	for(const auto& ext : extensions)
		missing.erase(ext.extensionName);

	for(const auto& ext : missing)
		log::error("vulkan: unsupported extension enabled {}", ext);

	return missing.empty();
}

QueueFamilyIndices query_queue_families(vk::PhysicalDevice gpu)
{
	QueueFamilyIndices qf_indices;
	std::vector<vk::QueueFamilyProperties> queue_families = gpu.getQueueFamilyProperties();

	auto iter = std::find_if(queue_families.begin(), queue_families.end(),
	[](const vk::QueueFamilyProperties& qfp)
	{
		return qfp.queueFlags & vk::QueueFlagBits::eGraphics;
	});
	qf_indices.graphics = std::distance(queue_families.begin(), iter);

	iter = std::find_if(queue_families.begin(), queue_families.end(),
	[](const vk::QueueFamilyProperties& qfp)
	{
		return !(qfp.queueFlags & vk::QueueFlagBits::eGraphics) &&
			(qfp.queueFlags & vk::QueueFlagBits::eCompute);
	});
	qf_indices.compute = (iter != queue_families.end()) ? std::distance(queue_families.begin(), iter) : qf_indices.graphics;

	iter = std::find_if(queue_families.begin(), queue_families.end(),
	[](const vk::QueueFamilyProperties& qfp)
	{
		return !(qfp.queueFlags & vk::QueueFlagBits::eGraphics) &&
		       !(qfp.queueFlags & vk::QueueFlagBits::eCompute) &&
			(qfp.queueFlags & vk::QueueFlagBits::eTransfer);
	});
	qf_indices.transfer = (iter != queue_families.end()) ? std::distance(queue_families.begin(), iter) : qf_indices.graphics;

	return qf_indices;
}

int device_score(const GPUInfo& gpu)
{
	vk::PhysicalDeviceProperties props = gpu.handle.getProperties();

	int score = 0;
	
	switch(props.deviceType)
	{
	case vk::PhysicalDeviceType::eDiscreteGpu:
		score += 3;
		break;
	case vk::PhysicalDeviceType::eIntegratedGpu:
		score += 2;
		break;
	case vk::PhysicalDeviceType::eCpu:
		score += 1;
		break;
	default:
		break;
	}

	return score;

}

}

export namespace lumina::vulkan
{

class Context
{
public:
	Context(const std::vector<const char*>& layers, const std::vector<const char*>& extensions)
	{
		//vk::DynamicLoader dl;
		//PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
		VULKAN_HPP_DEFAULT_DISPATCHER.init();

		vk::ApplicationInfo app_info
		{
			.pApplicationName = "lumina::vulkan",
			.applicationVersion = 1,
			.pEngineName = "lumina::engine",
			.engineVersion = 1,
			.apiVersion = vk::makeApiVersion(1, 3, 0, 0)
		};
		
		std::vector<const char*> enabled_extensions;
		for(auto ext : default_instance_extensions)
			enabled_extensions.push_back(ext);

		for(auto ext : extensions)
			enabled_extensions.push_back(ext);

		vk::InstanceCreateInfo instance_info
		{
			.pApplicationInfo = &app_info,
			.enabledLayerCount = static_cast<uint32_t>(layers.size()),
			.ppEnabledLayerNames = layers.data(),
			.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
			.ppEnabledExtensionNames = enabled_extensions.data()
		};
		handle = vk::createInstance(instance_info);
		VULKAN_HPP_DEFAULT_DISPATCHER.init(handle);

		phys_devices = handle.enumeratePhysicalDevices();
		if(phys_devices.empty())
			log::error("vulkan: no compatible GPUs present");

		int ctr = 0;
		std::string devlist_msg;
		for(auto dev : phys_devices)
		{
			auto props = dev.getProperties();
			devlist_msg += std::format("\n{}: {}", ctr, std::string_view{props.deviceName});
			ctr++;	
		}
		log::info("vulkan: enumerated render devices: {}", devlist_msg); 
	}

	~Context()
	{
		handle.destroy();
	}

	Device create_device(const std::vector<const char*>& req_extensions, vk::SurfaceKHR surface)
	{
		std::vector<GPUInfo> eligible_devices;

		std::vector<const char*> enabled_extensions;
		for(auto ext : default_device_extensions)
			enabled_extensions.push_back(ext);

		for(auto ext : req_extensions)
			enabled_extensions.push_back(ext);

		for(auto phys : phys_devices)
		{
			if(!query_extension_support(phys, enabled_extensions))
				continue;

			QueueFamilyIndices qf_indices = query_queue_families(phys);
			if(!qf_indices.supported())
				continue;

			if(surface && !phys.getSurfaceSupportKHR(qf_indices.graphics.value(), surface))
				continue;

			eligible_devices.push_back(GPUInfo{phys, qf_indices, phys.getMemoryProperties(), phys.getProperties()});
		}

		if(eligible_devices.empty())
			throw std::runtime_error("no eligible GPUs found");

		auto device_sort_fn = [](const GPUInfo& lhs, const GPUInfo& rhs)
		{
			return device_score(lhs) < device_score(rhs);
		};

		std::sort(eligible_devices.begin(), eligible_devices.end(), device_sort_fn);
		GPUInfo& gpu = eligible_devices.back();

		log::info("vulkan: selected render device {}", std::string_view{gpu.props.deviceName});

		std::vector<vk::DeviceQueueCreateInfo> queue_ci;
		std::unordered_set<uint32_t> unique_queue_families = 
		{
			gpu.qf_indices.graphics.value(),
			gpu.qf_indices.compute.value(),
			gpu.qf_indices.transfer.value()
		};

		const float queue_priority = 0.0f;
		for(uint32_t index : unique_queue_families)
		{
			vk::DeviceQueueCreateInfo ci
			{
				.queueFamilyIndex = index,
				.queueCount = 1,
				.pQueuePriorities = &queue_priority
			};

			queue_ci.push_back(ci);
		}

		// FIXME: make configurable
		vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceRobustness2FeaturesEXT> chain =
		{
			{
				.queueCreateInfoCount = static_cast<uint32_t>(queue_ci.size()),
				.pQueueCreateInfos = queue_ci.data(),
				.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
				.ppEnabledExtensionNames = enabled_extensions.data()
			},
			{
				.features =
				{
					.independentBlend = true,
					.geometryShader = true,
					.tessellationShader = true,
					.logicOp = true,
					.multiDrawIndirect = true,
					.drawIndirectFirstInstance = true,
					.depthClamp = true,
					.fillModeNonSolid = true,
					.samplerAnisotropy = true,
					.textureCompressionBC = true
				}

			},
			{
				.drawIndirectCount = true,
				.shaderSampledImageArrayNonUniformIndexing = true,
				.descriptorBindingSampledImageUpdateAfterBind = true,
				.descriptorBindingPartiallyBound = true,
				.descriptorBindingVariableDescriptorCount = true,
				.runtimeDescriptorArray = true,
				.samplerFilterMinmax = true,
				.hostQueryReset = true,
				.timelineSemaphore = true
			},
			{
				.synchronization2 = true,
				.dynamicRendering = true,
			},
			{
				.nullDescriptor = true
			}
		};

		vk::Device dh = gpu.handle.createDevice(chain.get<vk::DeviceCreateInfo>());
		VULKAN_HPP_DEFAULT_DISPATCHER.init(dh);
		//FIXME: check for features
		DeviceFeatures feat{false};
		return {dh, handle, gpu, feat};
	}

	vk::Instance get_handle() const
	{
		return handle;
	}
private:
	std::vector<vk::PhysicalDevice> phys_devices;
	vk::Instance handle;
};

}

