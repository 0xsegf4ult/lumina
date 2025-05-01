module lumina.vulkan:impl_image;

import :device;
import :image;
import :queues;

import vulkan_hpp;
import std;

using std::uint32_t;

namespace lumina::vulkan
{

bool is_depth_format(vk::Format fmt)
{
	switch(fmt)
	{
	case vk::Format::eD32Sfloat:
	case vk::Format::eD16Unorm:
		return true;
	default:
		return false;
	}
}

vk::ImageType image_type_from_size([[maybe_unused]]uint32_t w, uint32_t h, uint32_t d)
{
	if(d == 1)
	{
	//	if(h == 1)
	//		return vk::ImageType::e1D;

		return vk::ImageType::e2D;
	}

	return vk::ImageType::e3D;
}

vk::ImageViewType get_imageview_type(vk::ImageType type)
{
	switch(type)
	{
	case vk::ImageType::e1D:
		return vk::ImageViewType::e1D;
	case vk::ImageType::e2D:
		return vk::ImageViewType::e2D;
	case vk::ImageType::e3D:
		return vk::ImageViewType::e3D;
	default:
		std::unreachable();
	}
}

uint32_t get_mip_levels(uint32_t w, uint32_t h)
{
	uint32_t result = 1;

	while(w > 16 || h > 16)
	{
		result++;
		w /= 2;
		h /= 2;
	}

	return result;
}

uint32_t format_blockdim(vk::Format fmt)
{
	switch(fmt)
	{
	case vk::Format::eBc5UnormBlock:
	case vk::Format::eBc6HSfloatBlock:
	case vk::Format::eBc7UnormBlock:
	case vk::Format::eBc7SrgbBlock:
		return 4u;
	default:
		return 1u;
	}
}

uint32_t format_blocksize(vk::Format fmt)
{
	switch(fmt)
	{
	case vk::Format::eBc5UnormBlock:
	case vk::Format::eBc6HSfloatBlock:
	case vk::Format::eBc7UnormBlock:
	case vk::Format::eBc7SrgbBlock:
	case vk::Format::eR32G32B32A32Sfloat:
		return 16u;
	case vk::Format::eR32G32Sfloat:
	case vk::Format::eR16G16B16A16Sfloat:
		return 8u;
	case vk::Format::eR16G16B16Sfloat:
		return 6u;
	case vk::Format::eR32Uint:
	case vk::Format::eR16G16Sfloat:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eB10G11R11UfloatPack32:
		return 4u;
	case vk::Format::eR8G8Unorm:
		return 2u;
	default:
		return 1u;
	}
}

uint32_t size_for_image(uint32_t w, uint32_t h, vk::Format fmt)
{
	return (w / format_blockdim(fmt)) * (h / format_blockdim(fmt)) * format_blocksize(fmt);
}

vk::ImageUsageFlags decode_image_usage(ImageUsage usage)
{
        // FIXME: make ImageUsage chainable
        switch(usage)
        {
        case ImageUsage::ShaderRead:
                return vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
        case ImageUsage::DepthShaderRead:
                return vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eDepthStencilAttachment;
        case ImageUsage::TransferSource:
                return vk::ImageUsageFlagBits::eTransferSrc;
        case ImageUsage::TransferDest:
                return vk::ImageUsageFlagBits::eTransferDst;
        case ImageUsage::ColorAttachment:
                return vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
        case ImageUsage::DepthAttachment:
                return vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
        case ImageUsage::Framebuffer:
        case ImageUsage::RWGraphics:
		return vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
	case ImageUsage::CubemapRead:
		return vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	case ImageUsage::Cubemap:
		return vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
        case ImageUsage::RWCompute:
                return vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
        case ImageUsage::RWGeneric:
                return vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
        case ImageUsage::PresentSource:
                return vk::ImageUsageFlagBits::eColorAttachment;
        case ImageUsage::Undefined:
        default:
                return vk::ImageUsageFlagBits();
        }
}


ImageView::~ImageView()
{
	device->release_resource(Queue::Graphics, {handle, 0});
}

Image::Image(Device* dev, const ImageKey& k, vk::Image img, vk::DeviceMemory m) : device{dev}, key{k}, handle{img}, memory{m}
{
	uint32_t w = key.width;
	uint32_t h = key.height;
	uint32_t off = 0u;
	
	mip_views.resize(key.levels);
	layer_views.resize(key.layers);
	vk::ImageType type = image_type_from_size(key.width, key.height, key.depth);

	for(uint32_t level = 0u; level < key.levels; level++)
	{
		mip_views[level] = device->create_image_view
		({
			.image = this,
			.view_type = ((key.usage == ImageUsage::Cubemap || key.usage == ImageUsage::CubemapRead) && key.layers == 6) ? vk::ImageViewType::eCube : get_imageview_type(type),
			.format = key.format,
			.level = level,
			.debug_name = key.debug_name + "::view-mip" + std::to_string(level)
		});
	
		for(uint32_t layer = 0u; layer < key.layers; layer++)
		{
			subresources.push_back({w, h, off});
			off += size_for_image(w, h, key.format);

			if(level != 0)
				continue;
			
			layer_views[layer] = device->create_image_view
			({
				.image = this,
				.view_type = get_imageview_type(type),
				.format = key.format,
				.layer = layer,
				.layers = 1,
				.debug_name = key.debug_name + "::view-layer" + std::to_string(layer)
			});
		}
			
		w /= 2;
		h /= 2;
	}
}

Image::~Image()
{
	if(owns_image)
		device->release_resource(Queue::Graphics, {handle, 0});

	if(owns_memory)
		device->release_resource(Queue::Graphics, {memory, 0});
}

void Image::disown()
{
	owns_image = false;
}

void Image::disown_memory()
{
	owns_memory = false;
}

}
