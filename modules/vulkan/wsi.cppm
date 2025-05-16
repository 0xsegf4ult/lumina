export module lumina.vulkan:wsi;

import :context;
import :device;
import :image;
import :queues;

import vulkan_hpp;

import lumina.platform;
import lumina.core.log;

import std;

namespace lumina::vulkan
{

vk::SurfaceFormatKHR choose_swapchain_format(const std::vector<vk::SurfaceFormatKHR>& formats)
{
	for(const auto& format : formats)
	{
		if(format.format == vk::Format::eB8G8R8A8Srgb &&
		   format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			return format;
	}

	log::warn("choose_swapchain_format: requested format B8G8R8A8_SRGB unsupported, using fallback format");
	return formats[0];
}

vk::PresentModeKHR choose_swapchain_present_mode(const std::vector<vk::PresentModeKHR>& modes)
{
	for(const auto& mode : modes)
	{
		if(mode == vk::PresentModeKHR::eMailbox)
			return mode;
		else if(mode == vk::PresentModeKHR::eImmediate)
			return mode;
	}
	
	return vk::PresentModeKHR::eFifo;
}

vk::Extent2D find_swapchain_extent(const vk::SurfaceCapabilitiesKHR& caps)
{
	vk::Extent2D real_extent = caps.currentExtent;

	real_extent.width = std::max
	(
		caps.minImageExtent.width,
		std::min(caps.maxImageExtent.width, real_extent.width)
	);

	real_extent.height = std::max
	(
		caps.minImageExtent.height,
		std::min(caps.maxImageExtent.height, real_extent.height)
	);

	return real_extent;
}

struct SurfaceProperties
{
	vk::SurfaceCapabilitiesKHR capabilities;
	std::vector<vk::SurfaceFormatKHR> formats;
	std::vector<vk::PresentModeKHR> present_modes;
};

vk::SurfaceKHR create_surface(platform::Window* wnd, vk::Instance instance);

}

export namespace lumina::vulkan
{

struct SwapchainTexture
{
	ImageHandle image;
	uint32_t index;
};

class WSI
{
public:
	WSI(platform::Window* wnd, Context* ctx, Device* dev) : window{wnd}, context{ctx}, device{dev}
	{
		surface = create_surface(window, context->get_handle());
		props.formats = device->get_gpu().getSurfaceFormatsKHR(surface);
		props.present_modes = device->get_gpu().getSurfacePresentModesKHR(surface);

		format = choose_swapchain_format(props.formats);
		current_present_mode = choose_swapchain_present_mode(props.present_modes);

		init_swapchain();
	}

	~WSI()
	{
		cleanup_swapchain();
		context->get_handle().destroySurfaceKHR(surface);
	}

	WSI(const WSI&) = delete;
	WSI(WSI&&) = delete;

	WSI& operator=(const WSI&) = delete;
	WSI& operator=(WSI&&) = delete;

	vk::Extent2D get_extent() const
	{
		return extent;
	}

	bool set_present_mode(vk::PresentModeKHR mode)
	{
		if(mode == current_present_mode)
			return true;

		if(std::find(props.present_modes.begin(), props.present_modes.end(), mode) == props.present_modes.end())
		{
			log::warn("vulkan::WSI: attempted to set unsupported present mode!");
			return false;
		}

		current_present_mode = mode;
		if(!device->get_features().has_swapchain_maintenance_ext)
		{
			needs_swapchain_rebuild = true;
		}

		return true;
	}

	SwapchainTexture* begin_frame()
	{
		device->begin_frame();

		do
		{
			vk::Semaphore acquire_sem = device->wsi_signal_acquire();
			try
			{
				vk::ResultValue<uint32_t> index = device->get_handle().acquireNextImageKHR(swapchain, 1000000, acquire_sem, nullptr);
				cur_index = index.value;
				needs_swapchain_rebuild = false;
			}
			catch(vk::OutOfDateKHRError ex)
			{
				window->await_wm_resize();
				needs_swapchain_rebuild = true;
			}

			if(needs_swapchain_rebuild)
			{
				device->wait_idle();
				cleanup_swapchain();
				init_swapchain();
				window->signal_wm_resize();
			}
		
		} while(needs_swapchain_rebuild);

		return &textures[cur_index];
	}

	void end_frame()
	{
		device->end_frame();
		vk::Semaphore present_sem = device->wsi_signal_present();

		vk::StructureChain<vk::PresentInfoKHR, vk::SwapchainPresentModeInfoEXT> present_chain
		{
			{
				.waitSemaphoreCount = 1u,
				.pWaitSemaphores = &present_sem,
				.swapchainCount = 1u,
				.pSwapchains = &swapchain,
				.pImageIndices = &cur_index
			},
			{
				.swapchainCount = 1u,
				.pPresentModes = &current_present_mode
			}
		};
		
		if(!device->get_features().has_swapchain_maintenance_ext)
			present_chain.unlink<vk::SwapchainPresentModeInfoEXT>();

		try
		{
			[[maybe_unused]] auto res = device->get_queue(Queue::Graphics).presentKHR(present_chain.get<vk::PresentInfoKHR>());
		}
		catch(vk::OutOfDateKHRError ex)
		{
			window->await_wm_resize();
			needs_swapchain_rebuild = true;
		}

		if(needs_swapchain_rebuild)
		{
			device->wait_idle();
			cleanup_swapchain();
			init_swapchain();
			window->signal_wm_resize();
			needs_swapchain_rebuild = false;
		}
	}
private:
	void init_swapchain()
	{
		props.capabilities = device->get_gpu().getSurfaceCapabilitiesKHR(surface);
		extent = find_swapchain_extent(props.capabilities);

		uint32_t img_count = props.capabilities.minImageCount + 1;
		if(props.capabilities.maxImageCount > 0 && img_count > props.capabilities.maxImageCount)
			img_count = props.capabilities.maxImageCount;

		vk::StructureChain<vk::SwapchainCreateInfoKHR, vk::SwapchainPresentModesCreateInfoEXT> chain
		{
			{
				.surface = surface,
				.minImageCount = img_count,
				.imageFormat = format.format,
				.imageColorSpace = format.colorSpace,
				.imageExtent = extent,
				.imageArrayLayers = 1,
				.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
				.imageSharingMode = vk::SharingMode::eExclusive,
				.preTransform = props.capabilities.currentTransform,
				.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
				.presentMode = current_present_mode,
				.clipped = true
			},
			{
				.presentModeCount = static_cast<uint32_t>(props.present_modes.size()),
				.pPresentModes = props.present_modes.data()
			}
		};
		
		if(!device->get_features().has_swapchain_maintenance_ext)
			chain.unlink<vk::SwapchainPresentModesCreateInfoEXT>();

		swapchain = device->get_handle().createSwapchainKHR(chain.get<vk::SwapchainCreateInfoKHR>());
		std::vector<vk::Image> img_handles = device->get_handle().getSwapchainImagesKHR(swapchain);
		textures.clear();

		for(uint32_t i = 0; i < img_handles.size(); i++)
		{
			SwapchainTexture tex;
			tex.image = device->proxy_image
			({
				.width = extent.width,
				.height = extent.height,
				.format = this->format.format,
				.usage = ImageUsage::ColorAttachment,
				.debug_name = "WSI::image" + std::to_string(i)
			}, img_handles[i]);

			tex.index = i;
			textures.emplace_back(std::move(tex));
		}
	}

	void cleanup_swapchain()
	{
		device->get_handle().destroySwapchainKHR(swapchain);
	}

	platform::Window* window;
	vulkan::Context* context;
	vulkan::Device* device;

	vk::PresentModeKHR current_present_mode;
	SurfaceProperties props;
	vk::Extent2D extent;
	vk::SurfaceFormatKHR format;

	vk::SurfaceKHR surface;
	vk::SwapchainKHR swapchain;

	uint32_t cur_index = 0;
	std::vector<SwapchainTexture> textures;

	bool needs_swapchain_rebuild = false;
};

}
